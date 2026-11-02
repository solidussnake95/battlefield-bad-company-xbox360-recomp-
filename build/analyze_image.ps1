# Offline analysis of the guest image dump.
#
# Inputs (produced by BfbcApp::OnPostLoadXexImage):
#   build/image_dump.bin      full image 0x82000000..0x82F00000, big-endian dwords
#   build/code_range_dump.bin code range 0x82160000..0x82BD43BC, big-endian dwords
#
# Outputs:
#   build/candidates.txt      pointer-referenced, terminator-preceded, unregistered
#   build/candidates_stub.txt subset whose body is a short stub ending in blr/bctr
#   build/candidates_stats.txt
#
# A candidate is kept only when ALL of these hold:
#   * some dword in the image equals the address (so the game itself holds it as
#     a function/void pointer - the strongest available signal),
#   * the address is inside the code range,
#   * the address is not an existing function or emitted label,
#   * the dword right before it is a code terminator (blr / bctr / b* / bc* /
#     nop / zero / mr r8,r8 padding).
param(
  [string]$ImageDump = 'D:\recomps\bfbc-recomp\build\image_dump.bin',
  [string]$CodeDump  = 'D:\recomps\bfbc-recomp\build\code_range_dump.bin',
  [string]$Generated = 'D:\recomps\bfbc-recomp\generated\default'
)
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Text;
public static class ImageScan {
  public const uint ImageBase = 0x82000000u;
  public const uint CodeBase  = 0x82160000u;

  static uint W(byte[] b, int o) {
    return ((uint)b[o] << 24) | ((uint)b[o+1] << 16) | ((uint)b[o+2] << 8) | (uint)b[o+3];
  }

  public static bool IsTerminator(uint w) {
    if (w == 0x4E800020u) return true;   // blr
    if (w == 0x4E800420u) return true;   // bctr
    if (w == 0x4E800421u) return true;   // bctrl
    if (w == 0x60000000u) return true;   // nop
    if (w == 0x00000000u) return true;   // padding
    if (w == 0x7D084378u) return true;   // mr r8,r8 padding
    uint op = w >> 26;
    return op == 18 || op == 16 || op == 19;  // b*/bl*, bc*, bclr/bcctr
  }

  // Short stub: at most 12 instructions, no b/bl/bc, ends in blr or bctr.
  // These are vtable thunks / accessors reached only through pointers.
  public static bool IsStub(byte[] code, int off) {
    int n = code.Length;
    for (int i = 0; i < 12; i++) {
      int k = off + i * 4;
      if (k + 4 > n) return false;
      uint w = W(code, k);
      if (w == 0x4E800020u || w == 0x4E800420u) return i > 0;
      uint op = w >> 26;
      if (op == 18 || op == 16) return false;   // b / bc inside -> not a stub
      if (op == 19 && w != 0x4E800421u) return false;
    }
    return false;
  }

  // Returns every distinct in-code-range dword found anywhere in the image,
  // together with the first image address that holds it.
  public static Dictionary<uint, uint> Pointers(byte[] img, byte[] code, uint codeEnd) {
    var map = new Dictionary<uint, uint>(65536);
    for (int o = 0; o + 4 <= img.Length; o += 4) {
      uint v = W(img, o);
      if (v < CodeBase || v >= codeEnd) continue;
      if ((v & 3) != 0) continue;
      if (!map.ContainsKey(v)) map[v] = ImageBase + (uint)o;
    }
    return map;
  }

  // code[symbol] -> whether the address can start a function body
  public static string BuildReport(Dictionary<uint, uint> ptrs, byte[] code, uint codeEnd,
                                   HashSet<uint> known) {
    var cand = new List<uint>();
    var stub = new List<uint>();
    foreach (var kv in ptrs) {
      uint v = kv.Key;
      if (known.Contains(v)) continue;
      int off = (int)(v - CodeBase);
      if (off < 0 || off + 4 > code.Length) continue;
      if (off >= 4) {
        uint prev = W(code, off - 4);
        if (!IsTerminator(prev)) continue;
      }
      cand.Add(v);
      if (IsStub(code, off)) stub.Add(v);
    }
    cand.Sort();
    stub.Sort();
    var sb = new StringBuilder();
    sb.Append("pointers_in_code=").Append(ptrs.Count)
      .Append(" candidates=").Append(cand.Count)
      .Append(" stubs=").Append(stub.Count).Append('\n');
    sb.Append("CANDIDATES\n");
    foreach (var v in cand) sb.Append(v.ToString("X8")).Append('\n');
    sb.Append("STUBS\n");
    foreach (var v in stub) sb.Append(v.ToString("X8")).Append('\n');
    return sb.ToString();
  }
}
"@

$img  = [System.IO.File]::ReadAllBytes($ImageDump)
$code = [System.IO.File]::ReadAllBytes($CodeDump)
$codeEnd = [uint32]([uint64][ImageScan]::CodeBase + [uint64]$code.Length)
"image=$($img.Length) code=$($code.Length) codeEnd=0x$('{0:X8}' -f $codeEnd)"

# Existing function and label symbols from the emitted code.
$known = [System.Collections.Generic.HashSet[uint32]]::new()
$sb = [System.Text.StringBuilder]::new()
foreach ($file in Get-ChildItem $Generated -Filter *.cpp) { [void]$sb.Append([System.IO.File]::ReadAllText($file.FullName)) }
$all = $sb.ToString()
foreach ($m in [regex]::Matches($all, '(?:DEFINE_REX_FUNC\(sub_|loc_)([0-9A-Fa-f]{8})')) {
  [void]$known.Add([Convert]::ToUInt32($m.Groups[1].Value, 16))
}
"known symbols = $($known.Count)"

$ptrs = [ImageScan]::Pointers($img, $code, $codeEnd)
$report = [ImageScan]::BuildReport($ptrs, $code, $codeEnd, $known)
$lines = $report -split "`n"
$candIdx = [Array]::IndexOf($lines, 'CANDIDATES')
$stubIdx = [Array]::IndexOf($lines, 'STUBS')
$head = $lines[0]
$cands = $lines[($candIdx + 1)..($stubIdx - 1)] | Where-Object { $_ }
$stubs = $lines[($stubIdx + 1)..($lines.Count - 1)] | Where-Object { $_ }

$head | Set-Content 'D:\recomps\bfbc-recomp\build\candidates_stats.txt'
$cands | Set-Content 'D:\recomps\bfbc-recomp\build\candidates.txt'
$stubs | Set-Content 'D:\recomps\bfbc-recomp\build\candidates_stub.txt'
$head
"candidates -> build\candidates.txt ($($cands.Count))"
"stubs      -> build\candidates_stub.txt ($($stubs.Count))"
