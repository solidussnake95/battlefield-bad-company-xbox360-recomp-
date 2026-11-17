# Engine notes — frame pacing, timestep, lighting

Working notes from the reverse-engineering sessions, kept verbatim (Arabic and
English as written, dates as written). They are the evidence behind the "30 fps
engine" and "no 60 fps mode" statements in `README.md`.

| File | Date | What it covers |
|---|---|---|
| `pacing-trace-2026-11-07.md` | 2026-11-07 | **The actual 30 Hz source**: `sub_82176408` (render thread entry) waits on an event with a 30 ms timeout (`-300000` in 100 ns units) — 30 ms wait + ~3 ms work = the measured 33.2-33.6 ms gaps. Also rules out `KeSetTimer`, `Sleep(33)` and the `KeQuerySystemTime` functions. Proposes 16.676 ms as the next test. |
| `NOTES_timestep.md` | 2026-11-07 | Where the timestep constants live (`0x8200D7D8` = 1/30, `0x820C1930` tunables, `0x820077E8` = 1/60), the functions that read them, the `bfbc_sim_step_60` experiment and its negative result (intro finishes ~10 s early, swap rate stays 30). |
| `session-2026-11-09.md` | 2026-11-09 | Short plan: the simulation advances a fixed 1/30 per accepted frame, so 60 fps at 1x speed needs `sub_8260C928` (frames -> seconds) and the pacing wait changed. |
| `session-2026-11-16.md` | 2026-11-16 | Shadow-profile safe limits (resolve surface pitch 1040, shadow map <= 1024), readback resolve, stutter analysis, and the profile commands. |
| `worklog-2026-11-06.md` | 2026-11-06 | Stutter, dark-lighting and 60-120 fps attempt, measured from `bin\logs` and window brightness captures. |
| `work-summary-2026-11-03.md` | 2026-11-03 | The long-form summary of the first week: build, packaging, settings, input, render path experiments. |

Tools these notes refer to: `tools/scan_timestep.py` (constant references ->
containing function), `tools/trace_pacing.py` (time/sleep/present imports ->
recompiled functions), `tools/call_graph.py` (call graph over the 53 954
generated functions).

## Known-stale statements (checked against the Beta 2.1 build)

These notes were written while the work was in progress; the following points no
longer match the code. Read them with this list in mind:

1. **`readback_resolve` vs `d3d12_readback_resolve`.** `session-2026-11-16.md`
   says `readback_resolve` does not exist and `d3d12_readback_resolve` is the real
   key. The opposite is true: `readback_resolve` (`none`/`fast`/`some`/`full`) is
   the string enum the build uses, and the log shows it being applied
   (`Graphics: readback_resolve = fast (before GPU init)`).
2. **"Pacing sleep in `sub_829253F8`"** (`NOTES_timestep.md`, next targets) is a
   dead end: `pacing-trace-2026-11-07.md` shows it only compares a one-second
   window. The wait is the event timeout in `sub_82176408`.
3. **The 30 ms -> 16.676 ms test is already shipped** as `bfbc_sim_wait_ms`
   (default 30, patches `0x82176428`); it was never run to a conclusion, so it is
   still an open experiment, not a solution.
4. Defaults that changed later: the shipped config is `rtv` + `2x` internal scale,
   and the port no longer writes `resolution_scale` / `bfbc_fps_limit`.
