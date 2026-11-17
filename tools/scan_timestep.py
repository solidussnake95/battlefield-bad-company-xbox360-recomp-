"""
Finds code that references a given guest address inside the decompressed image.

The Xbox 360 image is big-endian PowerPC, and the recompiler emits the same
instructions as C++ in generated/default/bfbc_recomp.*.cpp (each one carries the
PPC instruction in a comment), so addresses are formed at runtime with
lis/addi/ori pairs instead of appearing literally in the generated source. This
script decodes the code range directly and reports the containing function.

Usage:
    python tools/scan_timestep.py                 # the 1/30, 1/60 and MaxFps strings
    python tools/scan_timestep.py 0x8200D7D8      # any guest address
"""

import bisect
import glob
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGE = os.path.join(ROOT, "build", "image_dump.bin")
FUNCS = os.path.join(ROOT, "generated", "default", "bfbc_funcs*.h")
IMAGE_BASE = 0x82000000
CODE_START = 0x82160000
CODE_END = 0x82BD43BC

DEFAULT_TARGETS = {
    0x8200D7D8: "1/30 (float 0x3D088889, followed by a table of code pointers)",
    0x820C1930: "1/30 (float, inside a tunables block: 0.1, 200, 250, 0.6, 4, 1)",
    0x820077E8: "1/60 (float 0x3C888889)",
    0x8200E954: "string 'Game.MaxFps'",
}


def load_function_starts():
    starts = set()
    for path in glob.glob(FUNCS):
        with open(path, errors="ignore") as handle:
            for match in re.finditer(r"DECLARE_REX_FUNC\(sub_([0-9A-Fa-f]{8})\)", handle.read()):
                starts.add(int(match.group(1), 16))
    return sorted(starts)


def sext16(value):
    return value - 0x10000 if value & 0x8000 else value


def scan(image, targets, starts):
    hits = []
    end = min(CODE_END, IMAGE_BASE + len(image))
    for addr in range(CODE_START, end, 4):
        insn = struct.unpack_from(">I", image, addr - IMAGE_BASE)[0]
        if insn >> 26 != 15:  # addis / lis
            continue
        rt = (insn >> 21) & 0x1F
        if (insn >> 16) & 0x1F:  # ra must be 0 for a constant materialization
            continue
        upper = sext16(insn & 0xFFFF) << 16
        for step in range(1, 12):
            at = addr + 4 * step
            if at >= end:
                break
            insn2 = struct.unpack_from(">I", image, at - IMAGE_BASE)[0]
            op2 = insn2 >> 26
            rt2 = (insn2 >> 21) & 0x1F
            value = None
            if op2 == 14 and rt2 == rt:  # addi
                value = (upper + sext16(insn2 & 0xFFFF)) & 0xFFFFFFFF
            elif op2 == 24 and rt2 == rt:  # ori
                value = (upper | (insn2 & 0xFFFF)) & 0xFFFFFFFF
            elif op2 in (48, 32) and ((insn2 >> 16) & 0x1F) == rt:  # lfs / lwz
                value = (upper + sext16(insn2 & 0xFFFF)) & 0xFFFFFFFF
            if value in targets:
                index = bisect.bisect_right(starts, addr) - 1
                func = starts[index] if index >= 0 else 0
                hits.append((targets[value], addr, at, func))
    return hits


def main():
    targets = DEFAULT_TARGETS
    if len(sys.argv) > 1:
        targets = {int(sys.argv[1], 16): sys.argv[1]}
    with open(IMAGE, "rb") as handle:
        image = handle.read()
    starts = load_function_starts()
    print(f"image {len(image)} bytes, {len(starts)} known functions")
    for name, lis_at, use_at, func in scan(image, targets, starts):
        print(f"{name[:46]:48} lis@{lis_at:#010x} use@{use_at:#010x} sub_{func:08X}")


if __name__ == "__main__":
    main()
