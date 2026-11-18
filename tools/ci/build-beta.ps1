# Build + package a BFBC Recomp release (used by GitHub Actions and by hand).
#
# The recompiled sources are produced by `rexglue codegen` out of the retail
# `game/default.xex`, so neither `game/` nor `generated/` is in the repository.
# This script never needs them to be part of the checkout: it extracts the
# requested git ref into a staging tree and links the local `game/` and
# `generated/` directories into it (junctions, no copy, no upload).
#
# What it does:
#   1. resolve the MSVC + Windows SDK + clang + CMake + Ninja toolchain
#      (no vcvars shell needed; INCLUDE/LIB/PATH are set here)
#   2. `git archive <ref>` -> staging tree, junction game/ and generated/
#   3. CMake configure (Ninja, Release, clang-cl-style clang++, MultiThreadedDLL)
#   4. `bfbc_codegen` (rexglue codegen into generated/, junctioned)
#   5. build/fix_gen_branches.ps1 -Apply (idempotent branch repair)
#   6. Ninja build -> bfbc.exe
#   7. assemble the release folder, zip it, write SHA256, scan with Defender
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\ci\build-beta.ps1 -DryRun
#   powershell -ExecutionPolicy Bypass -File tools\ci\build-beta.ps1 -Version 2.1 -Ref beta-2.1
#   powershell -ExecutionPolicy Bypass -File tools\ci\build-beta.ps1 -Regenerate -Ref main
#
# In CI: set GITHUB_WORKSPACE/REXSDK, pass -Ref $env:GITHUB_REF_NAME and
# -OutDir dist. Progress lines are prefixed with `[ci]` so they are easy to grep.

param(
    # Git ref to build. Defaults to the tag/branch GitHub checked out.
    [string]$Ref = $(if ($env:GITHUB_REF_NAME) { $env:GITHUB_REF_NAME } else { 'HEAD' }),

    # Repository whose objects are used for `git archive` (the checkout itself).
    [string]$SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,

    # Retail game files (default.xex + content). Not modified, only linked.
    [string]$GameDir = '',
    # Codegen output, refreshed in place by `rexglue codegen` on every build.
    [string]$GeneratedDir = '',
    # ReXGlue SDK 0.10.0 win-amd64 (rexglue.exe, rexruntime.dll, rexgpu-xenos.dll).
    [string]$SdkDir = '',

    [string]$Version = '2.1',
    [string]$StageDir = '',
    [string]$OutDir = '',
    [int]$Jobs = 0,

    [switch]$SkipZip,
    [switch]$SkipScan,
    [switch]$NoStage,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Step($msg) { Write-Host "[ci] $msg" }
function Fail($msg) { throw "[ci] $msg" }

# CMake writes -D values into generated .cmake files verbatim, so a Windows
# path with backslashes ("C:\Program Files\...") fails to parse there.
function CM([string]$Path) { $Path -replace '\\', '/' }

# Links are removed with rmdir: Remove-Item -Recurse on a junction follows it
# and deletes the target's contents.
function Remove-Link([string]$Path) {
    if (-not (Test-Path $Path)) { return }
    $item = Get-Item $Path -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        & cmd /c rmdir "`"$Path`"" | Out-Null
    }
}

# --- paths -------------------------------------------------------------------
if (-not $GameDir) { $GameDir = Join-Path $SourceRoot 'game' }
if (-not $GeneratedDir) { $GeneratedDir = Join-Path $SourceRoot 'generated' }
if (-not $SdkDir) {
    $SdkDir = if ($env:REXSDK) { $env:REXSDK } else { 'D:\recomps\.sdk\rexglue-sdk-0.10.0\win-amd64' }
}
if (-not $OutDir) { $OutDir = Join-Path $SourceRoot 'release' }
if (-not $StageDir) {
    $tag = ($Ref -replace '[^A-Za-z0-9._-]', '_')
    $StageDir = Join-Path $env:LOCALAPPDATA "bfbc-ci\src-$tag"
}
$Name = "BFBC-Recomp-Beta-$Version-win64"
$Zip = Join-Path $OutDir "$Name.zip"

Step "ref=$Ref version=$Version"
Step "source=$SourceRoot"
Step "game=$GameDir"
Step "generated=$GeneratedDir"
Step "sdk=$SdkDir"
Step "stage=$StageDir"
Step "out=$OutDir"

# --- toolchain ---------------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { Fail "vswhere.exe not found: $vswhere" }
$VS = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $VS) { Fail 'Visual Studio with the C++ toolset was not found (vswhere)' }

$MsvcRoot = Join-Path $VS 'VC\Tools\MSVC'
if (-not (Test-Path $MsvcRoot)) { Fail "MSVC toolset directory missing: $MsvcRoot" }
$Msvc = (Get-ChildItem $MsvcRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName

$KitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$KitsVer = (Get-ChildItem (Join-Path $KitsRoot 'Include') -Directory |
            Sort-Object { [version]($_.Name -replace '[^0-9.]', '') } -Descending |
            Select-Object -First 1).Name
if (-not $KitsVer) { Fail "Windows SDK headers not found under $KitsRoot\Include" }
$KitsInc = Join-Path $KitsRoot "Include\$KitsVer"
$KitsLib = Join-Path $KitsRoot "Lib\$KitsVer"

$Llvm = Join-Path $VS 'VC\Tools\Llvm\x64'
if (-not (Test-Path (Join-Path $Llvm 'bin\clang++.exe'))) { $Llvm = 'C:\Program Files\LLVM' }
$Clang = Join-Path $Llvm 'bin\clang++.exe'
if (-not (Test-Path $Clang)) { Fail 'clang++ was not found (VS LLVM component or C:\Program Files\LLVM)' }

$CMake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if (-not $CMake) {
    $CMake = (Get-ChildItem (Join-Path $VS 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin') -Filter cmake.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
}
if (-not $CMake) { Fail 'cmake.exe was not found (PATH or the VS CMake component)' }

$Ninja = (Get-Command ninja.exe -ErrorAction SilentlyContinue).Source
if (-not $Ninja) {
    $Ninja = (Get-ChildItem (Join-Path $VS 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja') -Filter ninja.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
}
if (-not $Ninja) { Fail 'ninja.exe was not found (PATH, winget Ninja, or the VS Ninja component)' }

Step "vs=$VS"
Step "msvc=$Msvc"
Step "windows-sdk=$KitsVer"
Step "clang=$Clang"
Step "cmake=$CMake"
Step "ninja=$Ninja"

foreach ($p in @(
    (Join-Path $SdkDir 'bin\rexglue.exe'),
    (Join-Path $SdkDir 'bin\rexruntime.dll'),
    (Join-Path $SdkDir 'lib\cmake\rexglue')
)) { if (-not (Test-Path $p)) { Fail "SDK is incomplete, missing: $p" } }

if (-not (Test-Path (Join-Path $GameDir 'default.xex'))) {
    Fail "retail game files missing: $(Join-Path $GameDir 'default.xex') (set -GameDir)"
}
if (-not (Test-Path $GeneratedDir)) { New-Item -ItemType Directory -Force $GeneratedDir | Out-Null }

if ($DryRun) { Step 'dry run - toolchain resolved, nothing built'; exit 0 }

# --- staging tree ------------------------------------------------------------
if ($NoStage) {
    $BuildRoot = $SourceRoot
} else {
    Remove-Link (Join-Path $StageDir 'game')
    Remove-Link (Join-Path $StageDir 'generated')
    if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
    New-Item -ItemType Directory -Force $StageDir | Out-Null

    $archive = Join-Path $env:TEMP "bfbc-$Version-src.zip"
    if (Test-Path $archive) { Remove-Item -Force $archive }
    Step "archiving $Ref"
    & git -C $SourceRoot archive --format=zip --output=$archive $Ref
    if ($LASTEXITCODE -ne 0) { Fail "git archive $Ref failed" }
    Expand-Archive -Path $archive -DestinationPath $StageDir -Force
    Remove-Item -Force $archive

    # Expand-Archive stamps everything with the current time. The codegen rule
    # treats bfbc_manifest.toml as an input, so a manifest newer than
    # codegen.build.stamp makes ninja regenerate the sources on every build -
    # after the branch repair, undoing it. Pin it to the commit time instead.
    $commitDate = (& git -C $SourceRoot log -1 --format=%cI $Ref)
    if ($LASTEXITCODE -eq 0 -and $commitDate) {
        $manifest = Join-Path $StageDir 'bfbc_manifest.toml'
        (Get-Item $manifest).LastWriteTime = [DateTimeOffset]::Parse($commitDate).LocalDateTime
    }

    & cmd /c mklink /J "`"$(Join-Path $StageDir 'game')`"" "`"$GameDir`"" | Out-Null
    if ($LASTEXITCODE -ne 0) { Fail "cannot link $GameDir" }
    & cmd /c mklink /J "`"$(Join-Path $StageDir 'generated')`"" "`"$GeneratedDir`"" | Out-Null
    if ($LASTEXITCODE -ne 0) { Fail "cannot link $GeneratedDir" }
    $BuildRoot = $StageDir
}

$Build = Join-Path $BuildRoot 'build'
# `build/` is also the CMake binary dir, and it holds the tracked helper scripts
# (fix_gen_branches.ps1 & co). Wipe the cache, never the directory.
if (-not (Test-Path $Build)) { New-Item -ItemType Directory -Force $Build | Out-Null }
foreach ($stale in 'CMakeCache.txt', 'CMakeFiles', 'build.ninja', '.ninja_deps', '.ninja_log') {
    $p = Join-Path $Build $stale
    if (Test-Path $p) { Remove-Item -Recurse -Force $p }
}

# --- environment (what vcvars64.bat would set) -------------------------------
$env:INCLUDE = (@(
    "$Msvc\include",
    "$KitsInc\ucrt", "$KitsInc\shared", "$KitsInc\um", "$KitsInc\winrt", "$KitsInc\cppwinrt"
) -join ';')
$env:LIB = (@(
    "$Msvc\lib\x64",
    "$KitsLib\ucrt\x64", "$KitsLib\um\x64"
) -join ';')
$env:PATH = (@(
    (Join-Path $Llvm 'bin'), (Split-Path $CMake), (Split-Path $Ninja), (Join-Path $SdkDir 'bin')
) -join ';') + ';' + $env:PATH
$env:REXSDK = $SdkDir

if (-not $Jobs -or $Jobs -le 0) {
    $LogicalCpus = [Environment]::ProcessorCount
    $RamGB = [math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
    $Jobs = [math]::Max(1, [math]::Min($LogicalCpus, [math]::Floor($RamGB / 2)))
}
Step "jobs=$Jobs"

# --- codegen ---------------------------------------------------------------
Step 'configure'
& $CMake -S $BuildRoot -B $Build -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$(CM $Ninja)" `
    '-DCMAKE_BUILD_TYPE=Release' `
    "-DCMAKE_C_COMPILER=$(CM (Join-Path $Llvm 'bin\clang.exe'))" `
    "-DCMAKE_CXX_COMPILER=$(CM $Clang)" `
    "-DCMAKE_RC_COMPILER=$(CM (Join-Path $Llvm 'bin\llvm-rc.exe'))" `
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL' `
    "-DREXSDK=$(CM $SdkDir)"
if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed ($LASTEXITCODE)" }

# Codegen writes into generated/ and is also a dependency of the main target, so
# it has to be built and repaired *before* the compile step: if the main build
# runs it, it regenerates the `REX_FATAL("Unresolved branch ...")` sites that the
# repair pass below fixes, and the compiler then sees the unfixed sources.
Step 'codegen'
& $CMake --build $Build --target bfbc_codegen
if ($LASTEXITCODE -ne 0) { Fail "codegen failed ($LASTEXITCODE)" }
if (-not (Test-Path (Join-Path $BuildRoot 'generated\default\sources.cmake'))) {
    Fail 'codegen did not produce generated/default/sources.cmake'
}

Step 'branch repair'
try {
    & (Join-Path $BuildRoot 'build\fix_gen_branches.ps1') -Apply -Dir (Join-Path $BuildRoot 'generated\default')
} catch {
    Fail "fix_gen_branches failed: $_"
}

# The repair edits codegen outputs, and ninja treats the codegen edge as dirty
# while ANY of its many outputs is older than the manifest - which would rerun
# codegen right before compiling and replace the repaired branches with
# REX_FATAL() again. Refresh the generated sources (but not the stamp: touching
# it invalidates ninja's stored depfile info and forces codegen to re-run), then
# put the manifest behind the oldest output so the edge is clean.
$now = Get-Date
$genDir = Join-Path $BuildRoot 'generated\default'
Get-ChildItem $genDir -Recurse -File |
    Where-Object { $_.Name -ne 'codegen.build.stamp' } |
    ForEach-Object { $_.LastWriteTime = $now }
$oldest = (Get-ChildItem $genDir -Recurse -File | Measure-Object LastWriteTime -Minimum).Minimum
(Get-Item (Join-Path $BuildRoot 'bfbc_manifest.toml')).LastWriteTime = $oldest.AddMinutes(-1)

Step 'build'
& $CMake --build $Build -j $Jobs
if ($LASTEXITCODE -ne 0) { Fail "build failed ($LASTEXITCODE)" }

$Exe = Join-Path $Build 'bfbc.exe'
if (-not (Test-Path $Exe)) { Fail "expected binary missing: $Exe" }
Step ("bfbc.exe {0:N0} bytes" -f (Get-Item $Exe).Length)

if ($SkipZip) { Step 'skip zip - done'; exit 0 }

# --- package -----------------------------------------------------------------
$Rel = Join-Path $OutDir $Name
if (Test-Path $Rel) { Remove-Item -Recurse -Force $Rel }
if (Test-Path $Zip) { Remove-Item -Force $Zip }
New-Item -ItemType Directory -Force "$Rel\bin", "$Rel\game", "$Rel\licenses" | Out-Null

foreach ($f in 'bfbc.exe', 'rexgpu-xenos.dll', 'rexruntime.dll', 'TracyClient.dll') {
    $from = Join-Path $Build $f
    if (-not (Test-Path $from)) { $from = Join-Path $SdkDir "bin\$f" }   # runtime DLLs live in the SDK
    if (-not (Test-Path $from)) { Fail "package needs $f, not found in build\ or the SDK bin\" }
    Copy-Item $from "$Rel\bin" -ErrorAction Stop
}
Copy-Item (Join-Path $BuildRoot 'release\bfbc.shipped.toml') "$Rel\bin\bfbc.toml"

$Docs = @('README.md', 'README.txt', "NOTES-Beta-$Version.md", 'bfbc.toml.example') |
    ForEach-Object { Join-Path $BuildRoot $_ } | Where-Object { Test-Path $_ }
Copy-Item $Docs $Rel

$SdlLicense = Join-Path $SdkDir 'licenses\SDL3\LICENSE.txt'
if (Test-Path $SdlLicense) { Copy-Item $SdlLicense "$Rel\licenses\SDL3-LICENSE.txt" }

'Put your own copy of the game in this folder: default.xex plus the content folders.' |
    Set-Content -Encoding ASCII "$Rel\game\PUT-YOUR-GAME-FILES-HERE.txt"

# The package must never ship game data or generated sources.
$leak = Get-ChildItem $Rel -Recurse -File |
    Where-Object { $_.Name -match '^default\.xex$|\.pdb$|\.map$|\.log$' -or $_.FullName -match '\\generated\\' }
if ($leak) { Fail "release folder contains forbidden files: $($leak.FullName -join ', ')" }

Step "zipping -> $Zip"
Compress-Archive -Path $Rel -DestinationPath $Zip -CompressionLevel Optimal

$Hash = (Get-FileHash $Zip -Algorithm SHA256).Hash
"$Hash  $Name.zip" | Set-Content -Encoding ASCII (Join-Path $OutDir "$Name.zip.sha256")
Step "zip=$Zip"
Step "size=$((Get-Item $Zip).Length) sha256=$Hash"

# --- Defender scan (local convenience; CI runners usually have no AV) --------
if (-not $SkipScan) {
    $Mp = Join-Path $env:ProgramFiles 'Windows Defender\MpCmdRun.exe'
    if (Test-Path $Mp) {
        # MpCmdRun only accepts an absolute path here; a relative one fails with
        # hr=0x80508023 and exit code 2, which looks like a detection.
        $scanPath = (Resolve-Path $Zip).Path
        Step "defender scan: $scanPath"
        & $Mp -Scan -ScanType 3 -File $scanPath -DisableRemediation | Out-Host
        switch ($LASTEXITCODE) {
            0 { Step 'defender: no threats found' }
            2 { Step 'defender: THREAT FOUND - inspect %TEMP%\MpCmdRun.log before shipping' }
            default { Step "defender: scan could not run (exit $LASTEXITCODE) - see %TEMP%\MpCmdRun.log" }
        }
    } else {
        Step 'defender not installed - scan skipped'
    }
}

Step 'PACK OK'
