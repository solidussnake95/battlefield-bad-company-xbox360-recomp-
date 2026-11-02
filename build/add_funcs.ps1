# Rewrite the [entrypoint.functions.*] declarations in bfbc_manifest.toml.
#
# Usage:
#   ./add_funcs.ps1 -ListFile missing_refined.txt            # replace with the given set
#   ./add_funcs.ps1 -ListFile extra.txt -Append              # add to the existing set
#   ./add_funcs.ps1 -Clear                                   # drop all function declarations
#
# The list file holds one 8-hex-digit guest address per line (0x prefix optional).
param(
  [string]$ListFile,
  [switch]$Append,
  [switch]$Clear
)
$ErrorActionPreference = 'Stop'
$manifest = 'D:\recomps\bfbc-recomp\bfbc_manifest.toml'
$lines = Get-Content $manifest

# Keep everything before the first function-declaration comment/entry.
$cut = $lines.Count
for ($i = 0; $i -lt $lines.Count; $i++) {
  if ($lines[$i] -match '^\s*#.*Function entry points' -or
      $lines[$i] -match '^\s*#.*Tail-call targets' -or
      $lines[$i] -match '^\s*\[entrypoint\.functions\.') { $cut = $i; break }
}
$head = $lines[0..($cut - 1)]

$addrs = New-Object System.Collections.Generic.List[string]
if ($Append) {
  foreach ($m in [regex]::Matches(($lines -join "`n"), '\[entrypoint\.functions\."0x([0-9A-Fa-f]{8})"\]')) {
    $addrs.Add($m.Groups[1].Value.ToUpper())
  }
}
if ($ListFile) {
  foreach ($ln in Get-Content $ListFile) {
    $t = ($ln -replace '^0x', '').Trim()
    if ($t -match '^[0-9A-Fa-f]{8}$') { $addrs.Add($t.ToUpper()) }
  }
}
$addrs = $addrs | Sort-Object -Unique

$out = New-Object System.Collections.Generic.List[string]
$head | ForEach-Object { $out.Add($_) }
if (-not $Clear -and $addrs.Count -gt 0) {
  $out.Add('')
  $out.Add('# Function entry points the analyzer misses.')
  $out.Add('# Found by scanning decompressed image data for runs of in-code-range pointers')
  $out.Add('# that are neither compiled functions nor internal labels, with the byte before')
  $out.Add('# each candidate required to be a code terminator (blr / b* / bc* / padding).')
  $out.Add('# These are real entry points reached only through indirect calls or data')
  $out.Add('# tables, so no direct branch and no PDATA record covers them.')
  foreach ($a in $addrs) { $out.Add("[entrypoint.functions.`"0x$a`"]") }
}
Set-Content -Path $manifest -Value ($out -join "`r`n") -Encoding ASCII
Write-Host "manifest rewritten: $($addrs.Count) function declaration(s)"
