# Beta 2.1 — config writer and validation fixes

Bugfix release on top of **Beta 2.0** (see `NOTES-Beta-2.0.md` for the feature set).
No gameplay, rendering or input changes.

## `bfbc.toml`

- **Saving from F5 no longer rebuilds the file.** Values are replaced where they
  stand, comment lines are kept, only keys the file did not have are appended, and
  the file is written through a temp file + rename so an interrupted save cannot
  truncate it. Previously every save deleted the owned keys, dropped all comments
  and appended everything again at the bottom, so the file was reordered each time.
- **Dead keys are deleted instead of carried forward**: `bfbc_fps_limit`,
  `resolution_scale`, `d3d12_readback_resolve`, `fullscreen_exclusive`. They still
  parse, so older configs keep working; the next save removes them.
- **The config is validated at startup.** Unknown keys, obsolete keys and values
  outside a flag's range or allowed set are now logged. The runtime returned `false`
  for those and logged nothing, which is how `bfbc_mouse_smooth = 1.0` (range
  0..0.9) sat in a config applying nothing while the log showed the 0.2 default.
- `bfbc.toml.example` documents every key (reference only; the game does not read it).

## F5 menu

- **"Frame rate limit" removed.** `bfbc_fps_limit` has not driven anything since the
  engine limiter was removed; the slider only kept writing the key.
- **"Exclusive fullscreen" removed.** SDK 0.10.0 registers no `fullscreen_exclusive`
  flag, so the option did nothing and the menu always reopened as Borderless.
  Display mode is now Windowed / Fullscreen.

## Shipped config

`bin\bfbc.toml`: `readback_resolve = "fast"`, RTV render path, 2x internal scale,
dual pad backend, `keybind_rstick_*` empty (the shaped mouse drives the right stick,
the arrow keys belong to the d-pad), Guide without `Alt+Tab`, side mouse buttons on
B/X/Y, `bfbc_mouse_smooth = 0.2`.

## Upgrading from Beta 2.0

Replace `bin\bfbc.exe`, or keep your `bfbc.toml` — the first F5 save cleans it. A
`bfbc.toml` saved by Beta 2.0 can carry `resolution_scale`/`bfbc_fps_limit`; they
are ignored now.
