# Decode context around unresolved branch sources/targets from the offline code dump.
$ErrorActionPreference = 'Stop'
$b  = [System.IO.File]::ReadAllBytes('D:\recomps\bfbc-recomp\build\code_range_dump.bin')
$cb = [Convert]::ToUInt32('82160000',16)
$ce = [uint32]($cb + $b.Length)

function W([int]$o) {
  if ($o -lt 0 -or $o + 4 -gt $b.Length) { return [uint32]0 }
  return ([uint32]$b[$o] -shl 24) -bor ([uint32]$b[$o+1] -shl 16) -bor ([uint32]$b[$o+2] -shl 8) -bor [uint32]$b[$o+3]
}
function Off([uint64]$a) { [int]($a - $cb) }

# Minimal PPC disassembly for the opcodes that matter in this investigation.
function Dis([uint32]$w, [uint64]$addr) {
  $op = $w -shr 26
  $xo = $w -band 0x7FF
  switch ($op) {
    18 { # b / bl / ba / bla
      $li = $w -band 0x03FFFFFC
      if ($li -band 0x02000000) { $li = $li - 0x04000000 }
      if ($w -band 0x2) { $t = [uint64]($li -band 0xFFFFFFFF) } else { $t = [uint64]($addr + $li) }
      $lk = if ($w -band 1) { 'l' } else { '' }
      $aa = if ($w -band 2) { 'a' } else { '' }
      return "b$($lk)$($aa) 0x{0:X8}" -f $t
    }
    16 { # bc / bcl
      $bo = ($w -shr 21) -band 0x1F
      $bi = ($w -shr 16) -band 0x1F
      $bd = $w -band 0xFFFC
      if ($bd -band 0x8000) { $bd = $bd - 0x10000 }
      if ($w -band 2) { $t = [uint64]($bd -band 0xFFFFFFFF) } else { $t = [uint64]($addr + $bd) }
      $lk = if ($w -band 1) { 'l' } else { '' }
      $aa = if ($w -band 2) { 'a' } else { '' }
      return "bc$($lk)$($aa) $bo,$bi,0x{0:X8}" -f $t
    }
    19 {
      switch ($xo) {
        16   { return 'bclr' }
        528  { return 'bcctr' }
        50   { return 'rfi' }
        default { return ('op19 xo={0}' -f $xo) }
      }
    }
    17 { return 'sc' }
    32 { return ('lwz r{0},{1}(r{2})' -f (($w -shr 21) -band 0x1F), ($w -band 0xFFFF), (($w -shr 16) -band 0x1F)) }
    36 { return ('stw r{0},{1}(r{2})' -f (($w -shr 21) -band 0x1F), ($w -band 0xFFFF), (($w -shr 16) -band 0x1F)) }
    37 { return ('stwux r{0},r{1},r{2}' -f (($w -shr 21) -band 0x1F), (($w -shr 16) -band 0x1F), (($w -shr 11) -band 0x1F)) }
    14 { return ('addi r{0},r{1},{2}' -f (($w -shr 21) -band 0x1F), (($w -shr 16) -band 0x1F), ($w -band 0xFFFF)) }
    15 { return ('addis r{0},r{1},0x{2:X4}' -f (($w -shr 21) -band 0x1F), (($w -shr 16) -band 0x1F), ($w -band 0xFFFF)) }
    24 { return ('ori r{0},r{1},0x{2:X4}' -f (($w -shr 21) -band 0x1F), (($w -shr 16) -band 0x1F), ($w -band 0xFFFF)) }
    31 { return ('op31 xo={0}' -f $xo) }
  }
  return ('0x{0:X8} (op {1})' -f $w, $op)
}

$pairs = @(
  @('82909E6C','82909EA8'), @('8290BA90','8290BB44'), @('8290C290','8290C2D0'),
  @('82918A08','82918AAC'), @('8291BCA4','8291BDB4'), @('8291BCC0','8291BD78'),
  @('82AF278C','82AF2814'), @('82210070','822100E4'), @('82214544','82214C84'),
  @('822EFEDC','822F0048'), @('8249FF70','824A00DC'), @('824A012C','824A0124'),
  @('82500020','8250008C'), @('825F00C8','825F0254'), @('82630000','8263014C'),
  @('826EFFD8','826F0024'), @('826EFFF4','826F0010'), @('8274FF90','82750098'),
  @('827AF9B0','827B00DC'), @('827AFFEC','827B0030'), @('827EFDF8','827F0100'),
  @('8286FFE4','828700B4'), @('8289F648','828A0044'), @('8289FFE4','828A0058'),
  @('828AE548','828AE588'), @('8290BB68','8290BB60'), @('82918AD0','82918AC8'),
  @('82A4DA68','82A4DBE8'), @('82A6FFF0','82A7004C'), @('82AB0000','82AB0018')
)

foreach ($p in $pairs) {
  $t = [Convert]::ToUInt32($p[0],16)
  $s = [Convert]::ToUInt32($p[1],16)
  $to = Off $t; $so = Off $s
  "=================== target 0x{0:X8}  <- source 0x{1:X8} ===================" -f $t,$s
  "  [before target]"
  for ($i = -6; $i -lt 0; $i++) {
    $a = [uint64]($t + $i*4); $w = W ($to + $i*4)
    "    0x{0:X8}: {1:X8}  {2}" -f $a, $w, (Dis $w $a)
  }
  "  [at target]"
  for ($i = 0; $i -lt 10; $i++) {
    $a = [uint64]($t + $i*4); $w = W ($to + $i*4)
    "    0x{0:X8}: {1:X8}  {2}" -f $a, $w, (Dis $w $a)
  }
  "  [around source]"
  for ($i = -4; $i -le 2; $i++) {
    $a = [uint64]($s + $i*4); $w = W ($so + $i*4)
    "    0x{0:X8}: {1:X8}  {2}" -f $a, $w, (Dis $w $a)
  }
  ""
}
