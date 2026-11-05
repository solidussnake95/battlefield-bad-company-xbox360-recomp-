# Build script for the Battlefield: Bad Company ReXGlue recompilation.
#
# Uses the portable toolchain left by the "Saints Reborn" project (LLVM/clang,
# CMake, Ninja and Microsoft's C++ headers/libraries via xwin). Nothing here
# needs Administrator rights or a Visual Studio install.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File build.ps1            # configure + build
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Configure # configure only
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Clean

param(
    [switch]$Configure,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$Root = $PSScriptRoot
$Toolchain = "D:\SAINTS ROW\build\toolchain"
$Sysroot = Join-Path $Toolchain "winsysroot-14.44.17.14-10.0.26100"
$Llvm = Join-Path $Toolchain "llvm-19.1.5"
$CMake = Join-Path $Toolchain "cmake-3.31.6\bin\cmake.exe"
$Ninja = Join-Path $Toolchain "ninja-1.13.2\ninja.exe"
$Sdk = "D:\recomps\.sdk\rexglue-sdk-0.10.0\win-amd64"
$Build = Join-Path $Root "build"

$Msvc = Join-Path $Sysroot "VC\Tools\MSVC\14.44.17.14"
$KitsInc = Join-Path $Sysroot "Windows Kits\10\Include\10.0.26100"
$KitsLib = Join-Path $Sysroot "Windows Kits\10\Lib\10.0.26100"

foreach ($p in @($CMake, $Ninja, (Join-Path $Llvm "bin\clang++.exe"), $Sysroot, $Sdk)) {
    if (-not (Test-Path $p)) { throw "Missing build dependency: $p" }
}

# --- Load the MSVC-ish environment (what vcvars64.bat would set) -------------
$env:INCLUDE = (@(
    "$Msvc\include",
    "$KitsInc\ucrt", "$KitsInc\shared", "$KitsInc\um", "$KitsInc\winrt", "$KitsInc\cppwinrt"
) -join ";")
$env:LIB = (@(
    "$Msvc\lib\x64",
    "$KitsLib\ucrt\x64", "$KitsLib\um\x64"
) -join ";")
$env:PATH = (@(
    "$Llvm\bin", (Join-Path $Toolchain "cmake-3.31.6\bin"), (Join-Path $Toolchain "ninja-1.13.2"),
    (Join-Path $Sdk "bin")
) -join ";") + ";" + $env:PATH
$env:REXSDK = $Sdk

$LogicalCpus = [Environment]::ProcessorCount
$RamGB = [math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
$Jobs = [math]::Max(1, [math]::Min($LogicalCpus, [math]::Floor($RamGB / 2)))
Write-Host "Build: $Jobs parallel jobs ($LogicalCpus CPU threads, $RamGB GB RAM)"

if ($Clean -and (Test-Path $Build)) { Remove-Item -Recurse -Force $Build }

$cfgArgs = @(
    "-S", $Root, "-B", $Build, "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=$Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_C_COMPILER=$(Join-Path $Llvm 'bin\clang.exe')",
    "-DCMAKE_CXX_COMPILER=$(Join-Path $Llvm 'bin\clang++.exe')",
    "-DCMAKE_RC_COMPILER=$(Join-Path $Llvm 'bin\llvm-rc.exe')",
    # The xwin sysroot only ships the release CRT (no msvcrtd.lib), so pin every
    # configuration to the release runtime; this is what the runtime expects.
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
    "-DREXSDK=$Sdk"
)
& $CMake @cfgArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

if ($Configure) { Write-Host "Configure only - done."; exit 0 }

# --- Codegen + post-codegen repair -------------------------------------------
# Codegen is itself a build step (custom target `bfbc_codegen`, see
# generated/rexglue.cmake). It must run BEFORE the repair pass, otherwise the
# build regenerates the sources and silently discards the fixes. Running it here
# also refreshes codegen.build.stamp, so the compiler-side step below is a no-op.
& (Join-Path $Sdk 'bin\rexglue.exe') codegen (Join-Path $Root 'bfbc_manifest.toml')
if ($LASTEXITCODE -ne 0) { throw "Codegen failed ($LASTEXITCODE)" }

# rexglue classifies a branch's caller with getFunctionContaining(siteAddr),
# which only inspects the nearest preceding function entry. Where a discovered
# entry sits just below the branch site without covering it, a branch into a
# label that is present in the emitted body is classified Unknown and becomes
# `if (cond) REX_FATAL(...)` instead of `if (cond) goto loc_XXXX;`.
# This pass rewrites exactly those sites; it is idempotent.
& (Join-Path $Root 'build\fix_gen_branches.ps1') -Apply

& $CMake --build $Build -j $Jobs
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }

Write-Host "`nBuilt: $Build\bfbc.exe" -ForegroundColor Green
