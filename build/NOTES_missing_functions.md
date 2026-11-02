# BFBC recomp â€” missing-function workflow

How we find and safely add guest functions that the rexglue analyzer misses.
Everything here is offline/deterministic; no guessing at runtime.

## Failure signatures

| log line | meaning |
|---|---|
| `[FATAL] Call to invalid or unregistered function at guest address 0xâ€¦` | indirect call (`bctr`/vtable slot) hit an address with no registered function |
| `[FATAL] Unresolved branch from 0xâ€¦ to 0xâ€¦` | a *conditional* branch whose target is neither inside the caller function nor an entry point (`TargetKind::Unknown` in `function_graph.cpp`) |

Both are fixed by declaring `[entrypoint.functions."0xâ€¦"]` in `bfbc_manifest.toml`
and re-running codegen. Never patch generated code: codegen rewrites it.

## Rule that matters: never split a function

`classifyTarget()` resolves an internal branch only if the target is *inside* the
caller function (`containsAddress`). Declaring an address that lives **inside** a
function as a function entry creates two overlapping ranges; the branch then
falls outside the caller and becomes `REX_FATAL`. This was the `826F9AD4` bug:
a vtable-thunk pattern in the middle of `sub_826F9AB8` (inside an `if` branch)
was added by a naive scan and broke `826F9B10 â†’ 826F9B18`.

**Accept a candidate only if the dword right before it is a code terminator.**

## Data sources

| file | produced by | contents |
|---|---|---|
| `build/code_range_dump.bin` | `BfbcApp::OnPostLoadXexImage` | guest code `0x82160000..0x82BD43BC`, big-endian dwords |
| `build/image_dump.bin` | same hook | whole image `0x82000000..0x82F00000` |
| `build/vtable_scan.txt` | same hook | runs of adjacent code pointers with `*` = unregistered |
| `generated/default/bfbc_init.cpp` | codegen | authoritative `{ 0xADDR, name }` table of registered functions |

All guest memory is big-endian; `TranslateVirtual` already accounts for the
guestâ†’host bias.

## Candidate classes (in order of confidence)

1. **Pointer-referenced** â€” the address appears as a big-endian dword anywhere in
   the image. This is the game itself holding a function pointer (vtable slot,
   callback table, exception record). 53,088 such addresses exist; only a
   handful are unregistered at any time.
2. **Structural stub** â€” no pointer exists (address is computed at runtime, e.g.
   MSVC adjustor thunks). Accept only if:
   * preceded by `blr` (`0x4E800020`) or `bctr` (`0x4E800420`) â€” i.e. right after
     a real function end, not after a plain `b` inside a function;
   * body is â‰¤12 instructions and ends in `blr`/`bctr` with no `b`/`bc` inside;
   * every instruction has a plausible primary opcode (rejects data: opcode
     0â€“6/9/22 or `0x0101xxxx`);
   * the first two words are **not** both code addresses (that is a jump table,
     i.e. data, not a function).
3. **Jump tables** â€” runs of code pointers. Never add as functions. They are
   handled correctly already when the dispatch site is registered; the pointer
   targets that need registration are added individually by class 1.

## Tools

* `build/analyze_image.ps1` â€” loads both dumps, extracts every in-code pointer,
  applies the terminator filter, and writes `candidates.txt` /
  `candidates_stub.txt` / `candidates_stats.txt`.
* `build/add_funcs.ps1` â€” rewrites the `[entrypoint.functions.*]` block.
  `-ListFile x.txt` replaces, `-Append` adds, `-Clear` drops all.
* Scratch lists: `missing_338.txt`, `add_next_final.txt`, `missing_390.txt`, â€¦
  Keep the *union* in `missing_390.txt`-style files; the manifest must always be
  rewritten from one accumulated list, never edited by hand.

## Cycle

```powershell
# 1. update the accumulated list, rewrite the manifest
Get-Content build\missing_prev.txt, build\add_next_final.txt |
  ForEach-Object { ($_ -replace '^0x','').Trim().ToUpper() } |
  Sort-Object -Unique | Set-Content build\missing_next.txt
powershell -ExecutionPolicy Bypass -File build\add_funcs.ps1 -ListFile build\missing_next.txt

# 2. codegen, then confirm the addresses became real functions
& D:\recomps\.sdk\rexglue-sdk-0.10.0\win-amd64\bin\rexglue.exe codegen bfbc_manifest.toml
Select-String generated\default\*.cpp -Pattern 'DEFINE_REX_FUNC\(sub_XXXX\)'

# 3. build and run
powershell -ExecutionPolicy Bypass -File build.ps1
cd build; .\bfbc.exe --game_data_root=..\game --log_file=runN.log --log_level=debug --allow_game_relative_writes --gpu_plugin=xenos
```

`--allow_game_relative_writes` is mandatory; without `--gpu_plugin=xenos` no
graphics plugin is loaded.


## Unresolved branches: real cause (codegen-side, not manifest-side)

Symptom: `codegen` prints

```
Unresolved conditional branch to 0x8249FF70 from 0x824A00DC (no CallTarget)
Unresolved function 0x8290BA90 from 0x8290BB44 (no CallTarget in FunctionNode)
```

and the emitted body contains `if (cond) REX_FATAL("Unresolved branch from ...")`
even though the target label **is** present in that very same function body:

```cpp
loc_824A00D4:
    ...
    // b 0x824a00d4
    goto loc_824A00D4;
loc_8249FF70:          // <-- the branch target, same function
```

Cause: `FunctionGraph::classifyTarget(target, callerAddr, isCall)` resolves the
caller via `getFunctionContaining(callerAddr)`, and that helper only inspects the
single nearest preceding entry:

```cpp
auto it = functionsByBase_.upper_bound(addr);
if (it != functionsByBase_.begin()) { --it; if (containsAddress(addr)) ...; }
return nullptr;            // never walks further back
```

Discovery regularly registers a tiny or absorbed entry a few bytes below a
branch site. That entry does not cover the site, the backward walk stops,
`callerFn` becomes null, and a branch to a label in the *emitted* function falls
through to `TargetKind::Unknown` - or, after the target address is declared in
the manifest, to `Function` with no recorded call edge, which then fails
`findCallTarget`. Declaring more addresses cannot fix this:
`[functions."0x..."] end` is only trusted inside the declared `[base, base+size)`
range, so it cannot cover a loop head that sits *below* the entry discovery
created.

Fix: a deterministic post-codegen pass, `build/fix_gen_branches.ps1`, wired into
`build.ps1` before the C++ build. It rewrites exactly those `REX_FATAL` calls:

* `loc_TARGET` present in the same emitted function -> `goto loc_TARGET;`
* otherwise -> tail call `sub_TARGET(ctx, base); return;`

It is idempotent (no marker text survives) and logs every edit. Current state:
10 sites, all `goto`, none needing a tail call, no `REX_FATAL("Unresolved ...")`
left in the generated tree.

This is a workaround for an upstream emitter bug. The proper fix is to make
`emit_conditional_branch` / `emit_function_call` consult the emitting node
(`fn.containsAddress(target)` / `fn.base()`) instead of re-deriving the caller
from the graph by site address.
