#!/usr/bin/env python3
"""Build the recompiled call graph and answer reachability questions.

Usage:
    python tools/call_graph.py reach <root> [depth]   # what a function reaches
    python tools/call_graph.py callers <target> [depth]
    python tools/call_graph.py time                    # the KeQuerySystemTime sites
"""
import re
import sys
from collections import defaultdict, deque
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "generated" / "default"

DEFINE = re.compile(r"DEFINE_REX_FUNC\((\w+)\)")
CALL = re.compile(r"\b(sub_[0-9A-Fa-f]+)\(ctx, base\);")
IMPORT = re.compile(r"(__imp__\w+)\(ctx, base\);")

TIME_SITES = [
    "sub_8291F0F8",
    "sub_8292ACE0",
    "sub_8292AE50",
    "sub_8292BDA0",
    "sub_8292A938",
    "sub_82920CA8",
    "sub_82920DB8",
    "sub_829253F8",
]


def load():
    addresses, callees, callers, imports = {}, defaultdict(set), defaultdict(set), defaultdict(set)
    text = (GEN / "bfbc_init.cpp").read_text(errors="ignore")
    for m in re.finditer(r"\{\s*0x([0-9A-Fa-f]{8}),\s*(sub_[0-9A-Fa-f]+)\s*\}", text):
        addresses[m.group(2)] = int(m.group(1), 16)

    for path in sorted(GEN.glob("bfbc_recomp.*.cpp")):
        cur = None
        for line in path.read_text(errors="ignore").splitlines():
            m = DEFINE.match(line)
            if m:
                cur = m.group(1)
                continue
            if cur is None:
                continue
            c = CALL.search(line)
            if c and c.group(1) != cur:
                callees[cur].add(c.group(1))
                callers[c.group(1)].add(cur)
            i = IMPORT.search(line)
            if i:
                imports[cur].add(i.group(1))
    return addresses, callees, callers, imports


def walk(start, edges, depth):
    seen = {start: 0}
    q = deque([(start, 0)])
    while q:
        node, d = q.popleft()
        if d >= depth:
            continue
        for nxt in edges[node]:
            if nxt not in seen or seen[nxt] > d + 1:
                seen[nxt] = d + 1
                q.append((nxt, d + 1))
    return seen


def fmt(name, addresses):
    addr = addresses.get(name)
    return f"{name} @ 0x{addr:08X}" if addr else name


def main():
    addresses, callees, callers, imports = load()
    mode = sys.argv[1] if len(sys.argv) > 1 else "time"
    print(f"{len(addresses)} functions, {sum(len(v) for v in callees.values())} call edges\n")

    if mode == "reach":
        root = sys.argv[2]
        depth = int(sys.argv[3]) if len(sys.argv) > 3 else 3
        sub = walk(root, callees, depth)
        print(f"{fmt(root, addresses)} reaches {len(sub) - 1} functions (depth {depth}):")
        for name, d in sorted(sub.items(), key=lambda kv: (kv[1], kv[0])):
            if name == root:
                continue
            imps = ",".join(sorted(imports.get(name, ()))) if imports.get(name) else "-"
            print(f"  d{d} {fmt(name, addresses)}  imports: {imps}")
        hit = [n for n in sub if n in TIME_SITES]
        print(f"\n  time-reading functions inside: {[fmt(n, addresses) for n in hit] or 'none'}")
        return

    if mode == "callers":
        target = sys.argv[2]
        depth = int(sys.argv[3]) if len(sys.argv) > 3 else 3
        up = walk(target, callers, depth)
        print(f"callers of {fmt(target, addresses)} (depth {depth}):")
        for name, d in sorted(up.items(), key=lambda kv: (kv[1], kv[0])):
            if name == target:
                continue
            print(f"  d{d} {fmt(name, addresses)}  (calls {len(callees[name])} funcs)")
        return

    # default: the time sites and where they sit
    display_root = "sub_82160818"
    from_display = walk(display_root, callees, 4)
    for name in TIME_SITES:
        up = walk(name, callers, 3)
        in_display = name in from_display
        print(f"== {fmt(name, addresses)}  in display subtree: {in_display}")
        print(f"   imports: {', '.join(sorted(imports.get(name, ()))) or '-'}")
        print(f"   callers: {', '.join(fmt(n, addresses) for n in sorted(callers[name])) or '-'}")
        upper = [f"d{d}:{fmt(n, addresses)}" for n, d in sorted(up.items(), key=lambda kv: kv[1]) if n != name]
        print(f"   up to depth 3: {', '.join(upper) or '-'}\n")


if __name__ == "__main__":
    main()
