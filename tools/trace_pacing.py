#!/usr/bin/env python3
"""Map kernel time/sleep/present imports back to the recompiled functions using them.

The generated code is split across many .cpp files; each function body starts at
`DEFINE_REX_FUNC(sub_XXXXXXXX)`. bfbc_init.cpp holds the { 0xADDR, sub_XXXXXXXX }
table, which gives every function its guest address.

Usage:
    python tools/trace_pacing.py                 # report all patterns
    python tools/trace_pacing.py KeDelay         # only patterns containing text
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "generated" / "default"

PATTERNS = [
    "__imp__KeDelayExecutionThread",
    "__imp__KeQuerySystemTime",
    "__imp__KeQueryPerformanceCounter",
    "__imp__KeQueryInterruptTime",
    "__imp__KeWaitForSingleObject",
    "__imp__NtWaitForSingleObject",
    "__imp__NtDelayExecution",
    "__imp__KeSetEvent",
    "__imp__KeResetEvent",
    "__imp__NtYieldExecution",
    "__imp__VdSwap",
    "__imp__VdGetCurrentDisplayInformation",
    "__imp__VdQueryVideoMode",
]


def load_addresses():
    table = {}
    text = (GEN / "bfbc_init.cpp").read_text(errors="ignore")
    for m in re.finditer(r"\{\s*0x([0-9A-Fa-f]{8}),\s*(sub_[0-9A-Fa-f]+)\s*\}", text):
        table[m.group(2)] = int(m.group(1), 16)
    return table


def callers(name, sources):
    """Functions that call `name(ctx, base)`."""
    needle = f"{name}(ctx, base);"
    out = []
    for path, lines in sources:
        cur = None
        for i, line in enumerate(lines):
            m = re.match(r"DEFINE_REX_FUNC\((\w+)\)", line)
            if m:
                cur = m.group(1)
            if needle in line and cur and cur != name:
                out.append((cur, path.name, i + 1))
    return out


def main():
    flt = sys.argv[1] if len(sys.argv) > 1 else None
    patterns = [p for p in PATTERNS if not flt or flt.lower() in p.lower()]
    addresses = load_addresses()

    sources = []
    for path in sorted(GEN.glob("bfbc_recomp.*.cpp")):
        sources.append((path, path.read_text(errors="ignore").splitlines()))

    print(f"{len(addresses)} functions in the address table\n")
    for pattern in patterns:
        hits = []
        for path, lines in sources:
            cur = None
            for i, line in enumerate(lines):
                m = re.match(r"DEFINE_REX_FUNC\((\w+)\)", line)
                if m:
                    cur = m.group(1)
                if pattern in line and cur:
                    comment = ""
                    for back in range(i - 1, max(i - 6, -1), -1):
                        c = lines[back].strip()
                        if c.startswith("//"):
                            comment = c
                            break
                    hits.append((cur, path.name, i + 1, comment))
        print(f"== {pattern}: {len(hits)} site(s)")
        for name, fname, line, comment in hits:
            addr = addresses.get(name)
            addr_s = f"0x{addr:08X}" if addr else "?"
            n = len(callers(name, sources))
            print(f"   {name} @ {addr_s}  ({fname}:{line}) callers={n}  {comment}")
        print()


if __name__ == "__main__":
    main()
