# For each unresolved branch target, show the nearest registered function starts around it.
$ErrorActionPreference = 'Stop'
$sb = [System.Text.StringBuilder]::new()
Get-ChildItem 'D:\recomps\bfbc-recomp\generated\default\*.cpp' | ForEach-Object { [void]$sb.Append([System.IO.File]::ReadAllText($_.FullName)) }
$text = $sb.ToString()
$starts = [System.Collections.Generic.HashSet[uint32]]::new()
foreach ($m in [regex]::Matches($text, 'DEFINE_REX_FUNC\(sub_([0-9A-Fa-f]{8})\)')) { [void]$starts.Add([Convert]::ToUInt32($m.Groups[1].Value,16)) }
foreach ($m in [regex]::Matches($text, 'DEFINE_REX_FUNC\("sub_([0-9A-Fa-f]{8})"\)')) { [void]$starts.Add([Convert]::ToUInt32($m.Groups[1].Value,16)) }
"registered sub_ labels = $($starts.Count)"

$tableStarts = [System.Collections.Generic.HashSet[uint32]]::new()
$init = [System.IO.File]::ReadAllText('D:\recomps\bfbc-recomp\generated\default\bfbc_init.cpp')
foreach ($m in [regex]::Matches($init, '\{\s*0x([0-9A-Fa-f]{8}),\s*[A-Za-z_]')) { [void]$tableStarts.Add([Convert]::ToUInt32($m.Groups[1].Value,16)) }
"init-table entries = $($tableStarts.Count)"

$sorted = @($starts) | Sort-Object
$targets = @(Get-Content 'D:\recomps\bfbc-recomp\build\unresolved_targets.txt')
foreach ($h in $targets) {
  $t = [Convert]::ToUInt32($h,16)
  $prev = $null; $next = $null; $exact = $false
  foreach ($a in $sorted) {
    if ($a -eq $t) { $exact = $true }
    if ($a -lt $t) { $prev = $a } elseif ($a -gt $t) { $next = $a; break }
  }
  $nearBefore = @($sorted | Where-Object { $_ -ge ($t - 0x60) -and $_ -lt $t })
  $nearAfter  = @($sorted | Where-Object { $_ -gt $t -and $_ -le ($t + 0x60) })
  "target 0x{0:X8} exactEntry={1} prev={2} next={3}" -f $t, $exact, $(if($prev){"0x{0:X8}" -f $prev}else{'-'}), $(if($next){"0x{0:X8}" -f $next}else{'-'})
  "    before(0x60): " + (($nearBefore | ForEach-Object { "0x{0:X8}(-0x{1:X})" -f $_, ($t - $_) }) -join ' ')
  "    after (0x60): " + (($nearAfter  | ForEach-Object { "0x{0:X8}(+0x{1:X})" -f $_, ($_ - $t) }) -join ' ')
}
