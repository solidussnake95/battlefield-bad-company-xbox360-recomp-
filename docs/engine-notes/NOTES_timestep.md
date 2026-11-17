# Fixed timestep trace — where 60 fps would have to be patched

Goal: raise the swap rate to 60 without the game running at double speed. The
engine's own cap (`Game.MaxFps`) and the host pacer (`bfbc_pace_fps`) both change
*how often* the loop runs, so both double the game speed. This note records where
the timestep itself lives, so the next attempt patches the right thing.

## Method

`build/image_dump.bin` is the decompressed guest image (`15.7 MB`, base
`0x82000000`, code `0x82160000-0x82BD43BC`). It is **big-endian** PowerPC, so a
float literal appears byte-swapped — `1/30` is `3d088889`, not `8988083d`.

Function boundaries come from `generated/default/bfbc_funcs*.h`
(53 954 declared functions). A `lis`/`addi`/`ori` window scan decodes the code
range and maps each constant reference back to its containing function; rerun it
with `python tools/scan_timestep.py`.

## Findings

### The frame loop

`sub_82160818` (1034 lines) is the display loop: it calls `__imp__VdGetSystemCommandBuffer`,
then `sub_82168910`, then **`__imp__VdSwap` at `0x82160AD0`**, then stores the new
command buffer pointer (`+48(r31)`) and continues. It does not call any of the
timestep functions directly — the simulation advance is driven from elsewhere.

### The timestep constants

| Guest address | Value | Notes |
|---|---|---|
| `0x8200D7D8` | `1/30f` | followed in memory by a table of code pointers (`0x8219F0F0`, `0x82269F08`, `0x8226F1D8`, `0x82269F40`, ...) |
| `0x820C1930` | `1/30f` | inside a tunables block: `0.1, 200.0, 250, 1/30, 0.6, 4, 1, 0.524, 0.698, 0.01, 20` |
| `0x820077E8` | `1/60f` | standalone, followed by a table of code pointers |

12 functions read `1/30`, 9 read `1/60`. The three that show how it is used:

**`sub_82708DD0` — the delta-time helper.** Reads `obj+0x54` as the previous
timestamp, stores the new one, and writes `now - prev` to `obj+0x58`. When the
new timestamp is *behind* the previous one (first frame, or a time source that
does not move forward) it stores **`1/30`** instead. So `1/30` is the engine's
"one frame of time" default.

**`sub_824BD5C8` — generic fixed-step stepper.**
`sub_824BD5C8(obj, arg, count)`: for `count` iterations it loads the object's
vtable slot `+84` and calls `obj->vfunc(arg, dt = 1/30)`. Its caller
`sub_824CC8C0` uses `count = 10`.

**`sub_82269DB8` — settle/pre-sim.** Calls `sub_82269CD8(obj, dt = 1/30, 1)`
**50 times** before returning.

**`sub_8260C928` — frames -> seconds conversion.** Takes a timestamp from a
virtual call, subtracts the stored one (`sub_8260A970`), then computes
`(now - last) * 1/30`, negates it, `fsel`s it against 0 and clamps it with
another constant from the `0x820C0xxx` block. Multiplying a *difference* by
`1/30` is a frames-to-seconds conversion, which is consistent with the measured
behaviour: the engine's clock is quantised in 1/30 s units, so more swaps at the
same `1/30` per swap means more simulated time per real second.

## What this means for a 60 fps patch

The speed doubling is not a stray setting: the simulation advances by exactly
`1/30` of simulated time per accepted frame, so the only way to present 60 fps at
1x speed is to make it advance `1/60` per frame while the swap rate doubles.

Candidate patch (not implemented): override the 4 bytes at `0x8200D7D8` from
`0x3D088889` (`1/30`) to `0x3C888889` (`1/60`) at runtime, then run uncapped
(`Game.MaxFps 0`) with `bfbc_pace_fps = 60`. Every reader of that address would
step at `1/60`, and 60 swaps/s x `1/60` = 1x speed.

Expected to need follow-up work, in the order the risk shows up:

1. Systems that treat `1/30` as a **rate** rather than a step (timers, animation
   lengths, network tick, particles) would need their own constant — the second
   copy at `0x820C1930` and the `1/60` at `0x820077E8` exist for exactly this
   reason and should not be touched blindly.
2. Anything that counts frames instead of accumulating dt (HUD animation, weapon
   timers, mouse look through `bfbc_mouse` which shapes one stick sample per
   frame) will run at double rate until patched individually.
3. Physics: `1/30` -> `1/60` halves the step, which is normally safer, but if any
   subsystem applies `1/30` twice the effective step changes differently.

Safe test order: patch the constant only, load the first campaign mission, and
compare the `Frame:` line in `bin/logs/bfbc_NNN.log` plus in-game movement speed
against the shipped build before touching anything else.

---

# Experiment 2026-11-07: the 1/60 patch, implemented and measured

Implementation: `bfbc_sim_step_60` (bool, default **false**, init-only) in
`src/bfbc_app.h`. When enabled, `OnPostLoadXexImage` rewrites the four bytes at
`0x8200D7D8` from `3D088889` (1/30) to `3C888889` (1/60). The write has to go
through the guest heap: a bare store faulted with
`Unhandled guest access violation: write of guest 0x8200D7D8`, so the patch
queries the heap with `LookupHeap`, calls `Protect(..., kReadWrite)`, writes, and
restores the old protection.

## Runs

| Run | Config | Log |
|---|---|---|
| A, reference | engine pacing (`Game.MaxFps` unset), `bfbc_pace_fps = 0` | `build/logs/bfbc_006.log` |
| B, patched | `Game.MaxFps 0` (config.zz) + `bfbc_pace_fps = 60` + patch on | `build/logs/bfbc_007.log` |

Both: ROV, 2x internal resolution, VSync on.

A: `Frame: 30.0 fps (gap 33.2-33.6 ms)` — the engine's clean 30 Hz.
B: `Timestep: frame step 1/30 -> 1/60 at 0x8200d7d8` in the log, then
`Frame: 29.8-30.1 fps (cap 60, gap 16.5-53.0 ms)`.

Screenshots at fixed wall-clock offsets (`tools/ab/base_t*.png`, `patch_t*.png`):

| t | A (reference) | B (patched) |
|---|---|---|
| 15 s | EA handshake | near-white flash (already past EA) |
| 25 s | Dolby Digital | Bad Company splash |
| 35 s | Bad Company splash | Bad Company splash |
| 45 s | Bad Company splash | black |

## What it means

The intro reached the splash ~10 s earlier in B, i.e. engine time ran **faster**,
not at half speed, while the swap rate stayed at 30 fps. So this constant feeds
the engine's own pacing/tick (fixed holds, timed sequence waits) rather than the
per-frame simulation delta: shortening the tick makes the engine believe less time
passed between frames, and its timed holds finish sooner.

It is therefore **not** the knob that turns a higher swap rate into correct-speed
60 fps. The flag stays in the build as a diagnostic switch, off by default, and
the shipped configs are back to their previous state.

## Next targets

1. The pacing sleep itself: `KeQuerySystemTime` is called from 8 functions and
   `KeDelayExecutionThread` from exactly one (`sub_829253F8`, `bfbc_recomp.94.cpp`).
   That single delay call is the best lead for "why the engine stops at 30".
2. `sub_8260C928` — `(now - last) * 1/30` clamped — is where a frame count
   becomes seconds; the delta that the simulation actually consumes is what has
   to be scaled.

## Unresolved during this experiment

With `draw_resolution_scale_x/y = 1` and `vsync = false` the process stopped after
`Translated 1879 shaders from the storage` and never logged a frame, twice, with
no error line (`build/logs/bfbc_009.log`, `bfbc_010.log`). Not investigated; the
runs at scale 2 worked normally.
