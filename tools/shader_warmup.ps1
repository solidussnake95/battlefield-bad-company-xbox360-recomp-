<#
.SYNOPSIS
  Warm up (pre-collect) the BFBC shader / pipeline cache before playing.

.DESCRIPTION
  The runtime caches what it has translated and every D3D12 pipeline it built in
  %USERPROFILE%\Documents\bfbc\cache\shaders\shareable\ :
    454107F9.xsh              translated 360 shaders
    454107F9.rtv.d3d12.xpso   pipeline descriptions, RTV path
    454107F9.rov.d3d12.xpso   pipeline descriptions, ROV path
  A warm cache is rebuilt in ~0.4 s at startup; states never seen still compile
  on first use and are appended when the process exits. This tool reports how
  warm the cache is, runs the game to grow it and measures the growth, and
  snapshots/restores a warm cache. It does not touch game code.

.PARAMETER Warm
  Launch windowed for -Minutes, close, print the delta, snapshot.

.PARAMETER Minutes
  Length of a -Warm run. Default 5.

.PARAMETER Backup
  Snapshot the cache to cache\shaders\snapshots\<Save>.

.PARAMETER Restore
  Restore a snapshot folder (listed by the default -View mode).

.PARAMETER Save
  Snapshot name. Default warm-<yyyyMMdd-HHmmss>.

.PARAMETER Keep
  With -Restore: keep the current cache as before-restore-<time>.

.EXAMPLE
  .\tools\shader_warmup.ps1
  .\tools\shader_warmup.ps1 -Warm -Minutes 10
  .\tools\shader_warmup.ps1 -Backup -Save warm-campaign
  .\tools\shader_warmup.ps1 -Restore warm-campaign -Keep

.NOTES
  RTV and ROV caches are separate: switching render_target_path needs its own
  warm-up runs.
#>
param(
    [switch]$Warm,
    [switch]$Backup,
    [string]$Restore,
    [string]$Save,
    [int]$Minutes = 5,
    [switch]$Keep,
    [string]$Bin,
    [string]$Cache
)

$ErrorActionPreference = 'Stop'

# Defaults: the repo layout (bin\bfbc.exe next to tools\) and the runtime's
# per-user cache, which is the same for every copy of the build.
if (-not $Bin) {
    $local = Join-Path (Split-Path $PSScriptRoot -Parent) 'bin\bfbc.exe'
    if (Test-Path $local) { $Bin = $local }
}
if (-not $Cache) { $Cache = Join-Path $env:USERPROFILE 'Documents\bfbc\cache\shaders' }
$Share = Join-Path $Cache 'shareable'
$Snaps = Join-Path $Cache 'snapshots'
New-Item -ItemType Directory -Force $Snaps | Out-Null

function Format-Size([long]$bytes) {
    if ($bytes -ge 1MB) { return '{0:N1} MB' -f ($bytes / 1MB) }
    return '{0:N0} KB' -f ($bytes / 1KB)
}

function Save-Snapshot([string]$name) {
    $dir = Join-Path $Snaps $name
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    New-Item -ItemType Directory -Force $dir | Out-Null
    if (Test-Path $Share) { Copy-Item (Join-Path $Share '*') $dir -Force }
    return $dir
}

function Get-NewestLog([string]$exe) {
    $logs = Join-Path (Split-Path $exe -Parent) 'logs'
    if (-not (Test-Path $logs)) { return $null }
    Get-ChildItem $logs -Filter 'bfbc_*.log' | Sort-Object Name | Select-Object -Last 1
}

function Get-CacheState {
    if (-not (Test-Path $Share)) { return @() }
    Get-ChildItem $Share -File | ForEach-Object {
        [pscustomobject]@{ Name = $_.Name; Bytes = $_.Length; When = $_.LastWriteTime }
    }
}

function Show-Status([string]$exe) {
    $state = Get-CacheState
    if (-not $state) { Write-Host "No cache yet at $Share (run the game once)."; return }

    Write-Host "Cache: $Share"
    $state | Sort-Object Name | ForEach-Object {
        Write-Host ('  {0,-28} {1,10}  {2}' -f $_.Name, (Format-Size $_.Bytes), $_.When)
    }
    Write-Host ('  {0,-28} {1,10}' -f 'total', (Format-Size (($state | Measure-Object Bytes -Sum).Sum)))

    $snaps = Get-ChildItem $Snaps -Directory -ErrorAction SilentlyContinue | Sort-Object Name
    if ($snaps) {
        Write-Host "`nSnapshots:"
        $snaps | ForEach-Object {
            $sum = (Get-ChildItem $_.FullName -File | Measure-Object Length -Sum).Sum
            Write-Host ('  {0,-32} {1,10}' -f $_.Name, (Format-Size $sum))
        }
    }

    $log = Get-NewestLog $exe
    if (-not $log) { return }
    $lines = Get-Content $log.FullName
    $tr = $lines | Select-String 'Translated (\d+) shaders' | Select-Object -Last 1
    $pl = $lines | Select-String 'Created (\d+) graphics pipelines' | Select-Object -Last 1
    Write-Host ("`nNewest log: {0}" -f $log.Name)
    if ($tr) { Write-Host ('  translated shaders : ' + [regex]::Match($tr.Line,'(\d+) shaders').Groups[1].Value) }
    if ($pl) { Write-Host ('  cached pipelines   : ' + [regex]::Match($pl.Line,'Created (\d+)').Groups[1].Value) }

    $gaps = @($lines | Select-String 'gap [\d.]+-([\d.]+) ms' | ForEach-Object {
        [double][regex]::Match($_.Line,'gap [\d.]+-([\d.]+)').Groups[1].Value
    })
    if ($gaps.Count -gt 0) {
        Write-Host ('  frames over 50 ms  : ' + @($gaps | Where-Object { $_ -gt 50 }).Count)
        Write-Host ('  worst gap          : ' + ('{0:N1} ms' -f ($gaps | Measure-Object -Maximum).Maximum))
    }
}

# --- dispatch -------------------------------------------------------------
if (-not $Bin) { Write-Host 'bfbc.exe not found; pass -Bin <path to bfbc.exe>.'; exit 1 }

if ($Restore) {
    $src = Join-Path $Snaps $Restore
    if (-not (Test-Path $src)) { Write-Host "No snapshot named '$Restore'."; exit 1 }
    if ($Keep) { Save-Snapshot ('before-restore-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) | Out-Null }
    Copy-Item (Join-Path $src '*') $Share -Force
    Write-Host "Restored '$Restore' into $Share"
    Show-Status $Bin
    exit 0
}

if ($Backup) {
    if (-not $Save) { $Save = 'warm-' + (Get-Date -Format 'yyyyMMdd-HHmmss') }
    Write-Host ('Saved ' + (Save-Snapshot $Save))
    exit 0
}

if (-not $Warm) { Show-Status $Bin; exit 0 }

# --- warm-up run ----------------------------------------------------------
$before = @{}
Get-CacheState | ForEach-Object { $before[$_.Name] = $_.Bytes }

Write-Host ("Warming for {0} min: {1}" -f $Minutes, $Bin)
Write-Host 'Play the map or menu you care about; the run is closed automatically.'
$proc = Start-Process -FilePath $Bin -ArgumentList '--fullscreen=false' `
    -WorkingDirectory (Split-Path $Bin -Parent) -PassThru

$deadline = (Get-Date).AddMinutes($Minutes)
while ((Get-Date) -lt $deadline -and -not $proc.HasExited) { Start-Sleep -Seconds 2 }
if (-not $proc.HasExited) {
    $proc.CloseMainWindow() | Out-Null
    if (-not $proc.WaitForExit(30000)) { $proc.Kill(); Write-Host 'Killed (no graceful close).' }
}
Write-Host 'Closed.'

Start-Sleep -Seconds 2
$after = Get-CacheState
$grew = 0
$after | Sort-Object Name | ForEach-Object {
    $was = if ($before.ContainsKey($_.Name)) { $before[$_.Name] } else { 0 }
    $delta = $_.Bytes - $was
    $grew += $delta
    $sign = if ($delta -ge 0) { '+' } else { '' }
    Write-Host ('  {0,-28} {1,10}  {2}{3}' -f $_.Name, (Format-Size $_.Bytes), $sign, (Format-Size $delta))
}
if ($grew -le 0) {
    Write-Host "`nNo growth: this state is already warm."
} else {
    Write-Host ("`nGrew by {0}. Run again in other areas to collect more." -f (Format-Size $grew))
}
if (-not $Save) { $Save = 'warm-' + (Get-Date -Format 'yyyyMMdd-HHmmss') }
Write-Host ('Snapshot: ' + (Save-Snapshot $Save))
Show-Status $Bin
