# Register this machine as a self-hosted GitHub Actions runner for the BFBC
# recomp build.
#
# Why self-hosted: the workflow compiles sources generated out of the retail
# `game/default.xex`, and that tree (~190 MB) plus the game files cannot be
# uploaded to a public repository. The runner uses the copies already on this
# machine; `tools/ci/build-beta.ps1` links them into a staging tree at build
# time.
#
# Security note: this repository is public. The workflow only triggers on a
# `beta-2.1` tag push or a manual dispatch, so fork pull requests never execute
# on this machine. Do not add `pull_request` triggers to it.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\ci\install-runner.ps1 -Start
#   powershell -ExecutionPolicy Bypass -File tools\ci\install-runner.ps1 -Service   (elevated shell)
#   powershell -ExecutionPolicy Bypass -File tools\ci\install-runner.ps1 -Status
#   powershell -ExecutionPolicy Bypass -File tools\ci\install-runner.ps1 -Uninstall

param(
    [string]$Repo = 'solidussnake95/battlefield-bad-company-xbox360-recomp-',
    [string]$RunnerDir = 'D:\recomps\.ci\runner',
    [string]$Name = "$env:COMPUTERNAME-bfbc",
    [string]$Labels = 'bfbc',
    [string]$TaskName = 'BFBC GitHub Actions Runner',
    [switch]$Start,
    [switch]$Service,
    [switch]$Status,
    [switch]$Uninstall,
    [switch]$Reconfigure
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Step($m) { Write-Host "[runner] $m" }
function Fail($m) { throw "[runner] $m" }
function IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal $id).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { Fail 'gh CLI is required' }
& gh auth status *> $null
if ($LASTEXITCODE -ne 0) { Fail 'gh is not authenticated (run: gh auth login)' }

# The workflow runs its steps with `shell: pwsh`; without PowerShell 7 the job
# dies at the first step with "pwsh: command not found".
if (-not (Get-Command pwsh -ErrorAction SilentlyContinue)) {
    Step 'pwsh (PowerShell 7) is not installed - the workflow needs it'
    Step 'install with: winget install --id Microsoft.PowerShell -e --source winget'
}

function Api([string]$Method, [string]$Path) {
    & gh api -X $Method $Path
    if ($LASTEXITCODE -ne 0) { Fail "gh api $Method $Path failed" }
}

if ($Status) {
    Step "runners registered on $Repo"
    $runners = (Api GET "repos/$Repo/actions/runners") | ConvertFrom-Json
    $runners.runners | ForEach-Object { "{0}  {1}  {2}" -f $_.name, $_.status, (($_.labels.name) -join ',') }
    if (Test-Path (Join-Path $RunnerDir '.runner')) { Step "local runner configured in $RunnerDir" }
    Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue |
        Select-Object TaskName, State | Format-Table -AutoSize
    exit 0
}

if ($Uninstall) {
    $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($task) { Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false; Step "removed task '$TaskName'" }
    if (Test-Path (Join-Path $RunnerDir 'config.cmd')) {
        $token = (Api POST "repos/$Repo/actions/runners/remove-token" | ConvertFrom-Json).token
        Push-Location $RunnerDir
        try { & .\config.cmd remove --unattended --token $token } finally { Pop-Location }
    }
    Step 'uninstalled'
    exit 0
}

# --- download the runner -----------------------------------------------------
$Version = (& gh api repos/actions/runner/releases/latest --jq .tag_name).TrimStart('v')
if (-not $Version) { Fail 'cannot resolve the latest runner version' }
Step "runner v$Version -> $RunnerDir"

New-Item -ItemType Directory -Force $RunnerDir | Out-Null
$zip = Join-Path $env:TEMP "actions-runner-win-x64-$Version.zip"
$url = "https://github.com/actions/runner/releases/download/v$Version/actions-runner-win-x64-$Version.zip"

if (-not (Test-Path (Join-Path $RunnerDir 'config.cmd'))) {
    Step "downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    Expand-Archive -Path $zip -DestinationPath $RunnerDir -Force
    Remove-Item -Force $zip
}

# --- register ----------------------------------------------------------------
$configured = Test-Path (Join-Path $RunnerDir '.runner')
if (-not $configured -or $Reconfigure) {
    $token = (Api POST "repos/$Repo/actions/runners/registration-token" | ConvertFrom-Json).token
    Step "registering '$Name' with labels: self-hosted,windows,x64,$Labels"
    Push-Location $RunnerDir
    try {
        & .\config.cmd --unattended --replace `
            --url "https://github.com/$Repo" `
            --token $token `
            --name $Name `
            --labels $Labels `
            --work '_work'
        if ($LASTEXITCODE -ne 0) { Fail "config.cmd failed ($LASTEXITCODE)" }
    } finally { Pop-Location }
} else {
    Step "already registered as $(Get-Content (Join-Path $RunnerDir '.runner') | ConvertFrom-Json | ForEach-Object agentName)"
}

# --- run ---------------------------------------------------------------------
if ($Service) {
    if (-not (IsAdmin)) { Fail 'service install needs an elevated PowerShell (Run as administrator)' }
    Push-Location $RunnerDir
    try {
        & .\svc.cmd install
        if ($LASTEXITCODE -ne 0) { Fail "svc.cmd install failed ($LASTEXITCODE)" }
        & .\svc.cmd start
    } finally { Pop-Location }
    Step 'installed as a Windows service'
} elseif ($Start) {
    # No elevation here: keep it as a per-user logon task.
    $action = New-ScheduledTaskAction -Execute (Join-Path $RunnerDir 'run.cmd') -WorkingDirectory $RunnerDir
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
        -Settings $settings -Description 'Self-hosted GitHub Actions runner for BFBC recomp' -Force | Out-Null
    Start-ScheduledTask -TaskName $TaskName
    Step "started scheduled task '$TaskName' (runs at logon)"
}

# --- verify ------------------------------------------------------------------
Start-Sleep -Seconds 5
Step "runners registered on $Repo"
$runners = (Api GET "repos/$Repo/actions/runners") | ConvertFrom-Json
$runners.runners | ForEach-Object { "{0}  {1}  {2}" -f $_.name, $_.status, (($_.labels.name) -join ',') }
Step 'done'
