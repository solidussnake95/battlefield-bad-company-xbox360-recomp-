# Fix rexglue's unresolved-branch emissions in the generated C++ AFTER codegen.
#
# Why: FunctionGraph::classifyTarget() resolves the caller with
# getFunctionContaining(callerAddr), which only inspects the *nearest preceding*
# function entry (functionsByBase_.upper_bound then --). When a discovered or
# absorbed entry sits just below the branch site and does not itself cover it,
# the lookup yields nullptr, so a branch into a label that IS present in the
# emitted function body is classified Unknown and codegen emits
#
#     if (cond) REX_FATAL("Unresolved branch from 0xSITE to 0xTARGET");
#
# instead of the correct
#
#     if (cond) goto loc_TARGET;
#
# This pass rewrites exactly those REX_FATAL calls, deterministically:
#   * label present in the same emitted function -> `goto loc_XXXXXXXX;`
#   * otherwise (target lives outside this function) -> tail call
#     `sub_XXXXXXXX(ctx, base); return;`
#
# It is idempotent: after the rewrite no matching REX_FATAL text remains, so
# re-running is a no-op. Wire it between `rexglue codegen` and the C++ build.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File build\fix_gen_branches.ps1            # dry run
#   powershell -ExecutionPolicy Bypass -File build\fix_gen_branches.ps1 -Apply
param(
  [switch]$Apply,
  [string]$Dir = (Join-Path $PSScriptRoot '..\generated\default')
)
$ErrorActionPreference = 'Stop'

$marker  = 'REX_FATAL("Unresolved'
$rxFatal = [regex]'REX_FATAL\("Unresolved (?:branch|call) from 0x([0-9A-Fa-f]{8}) to 0x([0-9A-Fa-f]{8})"\);?'
$rxDef   = [regex]'^\s*DEFINE_REX_FUNC\('
$rxLabel = [regex]'^\s*loc_([0-9A-Fa-f]{8}):'

$totalGoto = 0; $totalCall = 0
$report = New-Object System.Collections.Generic.List[string]

foreach ($f in Get-ChildItem (Join-Path $Dir '*.cpp')) {
  $text = [System.IO.File]::ReadAllText($f.FullName)
  # Fast path: the vast majority of translation units have nothing to fix.
  if ($text.IndexOf($marker, [StringComparison]::Ordinal) -lt 0) { continue }

  $lines = $text -split "`r?`n"

  # Function start indices.
  $defs = New-Object System.Collections.Generic.List[int]
  for ($i = 0; $i -lt $lines.Count; $i++) { if ($rxDef.IsMatch($lines[$i])) { $defs.Add($i) } }
  if ($defs.Count -eq 0) { continue }

  # label -> line indices
  $labels = @{}
  for ($i = 0; $i -lt $lines.Count; $i++) {
    $m = $rxLabel.Match($lines[$i])
    if ($m.Success) {
      $a = $m.Groups[1].Value.ToUpperInvariant()
      if (-not $labels.ContainsKey($a)) { $labels[$a] = New-Object System.Collections.Generic.List[int] }
      $labels[$a].Add($i)
    }
  }

  $changed = $false
  for ($i = 0; $i -lt $lines.Count; $i++) {
    $m = $rxFatal.Match($lines[$i])
    if (-not $m.Success) { continue }
    $site = $m.Groups[1].Value.ToUpperInvariant()
    $target = $m.Groups[2].Value.ToUpperInvariant()

    # Enclosing emitted function: last DEFINE before $i, first DEFINE after.
    $start = -1
    foreach ($d in $defs) { if ($d -lt $i) { $start = $d } else { break } }
    $end = $lines.Count
    foreach ($d in $defs) { if ($d -gt $i) { $end = $d; break } }

    $hasLabel = $false
    if ($labels.ContainsKey($target)) {
      foreach ($l in $labels[$target]) { if ($l -gt $start -and $l -lt $end) { $hasLabel = $true; break } }
    }

    if ($hasLabel) {
      $lines[$i] = $rxFatal.Replace($lines[$i], "goto loc_$target;")
      $totalGoto++
      $report.Add(("{0}:{1}  0x{2} -> goto loc_{3}" -f $f.Name, ($i + 1), $site, $target))
    } else {
      $lines[$i] = $rxFatal.Replace($lines[$i], "{ sub_$target(ctx, base); return; }")
      $totalCall++
      $report.Add(("{0}:{1}  0x{2} -> tail call sub_{3}" -f $f.Name, ($i + 1), $site, $target))
    }
    $changed = $true
  }

  if ($changed -and $Apply) {
    [System.IO.File]::WriteAllText($f.FullName, ($lines -join "`r`n"))
  }
}

Write-Host "unresolved-branch rewrites: goto=$totalGoto tailcall=$totalCall"
$report | ForEach-Object { Write-Host "  $_" }
if (-not $Apply) { Write-Host "(dry run - pass -Apply to write)" }
