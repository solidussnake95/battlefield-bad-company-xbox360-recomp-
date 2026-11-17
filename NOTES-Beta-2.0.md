# BFBC Recomp Beta 2.0 — Windows x64

Static recompilation of **Battlefield: Bad Company** (Xbox 360, Title ID
`454107F9`) into a native Windows application, built with **ReXGlue SDK 0.10.0**.

This package contains **no game files**. You must supply your own copy of the game.

## New in Beta 2.0

### Controllers
- `bfbc_pad_backend` defaults to `both`: XInput (Xbox pads) and SDL (PlayStation
  pads) are opened at the same time, so either pad works without restarting.
- Hot-plug works: the input system re-enumerates when a pad is connected or removed.
- With both backends open, devices are merged into guest user 0
  (`SharedAssignment`), so a pad seen by XInput and SDL does not create a phantom
  second player.
- `xinput` and `sdl` pin one backend if needed.

### Mouse
- Extra mouse buttons and the wheel can be mapped to pad actions:
  `bfbc_mouse_middle` (default `b`), `bfbc_mouse_x1` (default `x`),
  `bfbc_mouse_x2` (default `y`), `bfbc_mouse_wheel` (default `none`).
- Values are pad action names: `none`, `a`, `b`, `x`, `y`, `lb`, `rb`, `lt`, `rt`,
  `start`, `back`, `dpad_*`, `lstick_*`, `rstick_*`.
- F5's bindings still capture the next key or mouse button; side buttons and the
  wheel are handled by the mouse driver.
- The shipped aim defaults are gain 2.0, vertical 1.8, curve 1.0, deadzone 0.0,
  floor 7849, smoothing 0.2, dither off, frame-rate-independent aim on.
  `bfbc_mouse_smooth` is a 0..0.9 range, so an older `1.000000` in the file was
  rejected by the runtime: it kept the 0.2 default and logged nothing.

### Graphics
- F5 → Rendering has **Readback resolve** (`none`, `fast`, `some`, `full`).
  `fast` is the default; it is the mode that brightens the world on this driver.
- F5 → Rendering has **Render target path** (`RTV` / `ROV`).
  - `RTV` is the default: faster, but the Xbox 360's 10-bit HDR formats are
    approximated, so smoke and translucent particles can look hard-edged or too
    opaque.
  - `ROV` blends in the pixel shader: correct transparency, but each new
    blend/depth state compiles a pipeline (first-use hitches), and the ROV path
    can crash on AMD drivers with SDK 0.10.0. Choose `RTV` for a stable session.
  - Both are restart-only: the render target cache is built once at startup.
    F5 writes the choice and shows "restart required"; it does not change the
    running session.
- Startup defaults are now `readback_resolve=fast`,
  `async_shader_compilation=true`, and 256/1024/2048 MB texture-cache limits.
  The old `gamma_render_target_as_unorm16=false` override was removed; the plugin
  default is correct.
- The F4 cvar editor and F5 menu now release the mouse-look pointer while open
  (`OverlayCursorWatcher`), so the cursor is visible over both overlays.

### Input layout
- Default bindings: A=Space, B=C, X=R, Y=Q, LB=1, RB=2, L3=Shift,
  R3=Control, Start=`Plus` (`+`/`=`), Back=`Minus` (`-`), Guide=Tab, triggers on
  LMB/RMB, d-pad on the arrow keys.
- `keybind_rstick_*` are empty: the shaped mouse drives the right stick. Earlier
  builds put it on the arrow keys, so every arrow press moved the camera and the
  d-pad at once.
- Alt combinations are kept off the Guide button, so Alt+Tab cannot press it.
- Shift/Ctrl/Alt remain usable as action keys (`Shift+Shift` internally).

### Build
- `build.ps1` now uses the Visual Studio 2022 Build Tools toolchain
  (LLVM/Clang 19.1.5, MSVC 14.44, Windows SDK 10.0.26100) and CMake/Ninja from
  the host. This avoids the Clang 18 frontend crash seen on the generated sources.
- The release build is still a single
  `powershell -ExecutionPolicy Bypass -File build.ps1`.

### Diagnostic
- `bfbc_sim_step_60` (off by default) halves the engine's 1/30 s frame-step
  constant, and `bfbc_sim_wait_ms` changes the render-thread wait timeout. These
  are investigative switches for 60 fps work; they do not by themselves make the
  game run at 60 fps at normal speed.

## Fixes
- **The config writer no longer rewrites the whole file.** Saving from F5 edits the
  value where it stands, keeps comment lines, and only appends keys the file did not
  have yet, so the file no longer gets reordered on every save. It is written to a
  temp file and renamed over the old one, so an interrupted save cannot truncate it.
- **Saving drops the keys this build removed** (`bfbc_fps_limit`, `resolution_scale`,
  `d3d12_readback_resolve`) instead of carrying them forward forever. They still
  parse, for older files.
- **The dead "Frame rate limit" slider is gone from F5.** `bfbc_fps_limit` has not
  driven anything since the limiter was removed; the menu kept writing it.
- **`bfbc.toml` is validated at startup.** Unknown keys, and values outside a flag's
  range or allowed set, are reported as warnings; before, the runtime skipped them
  silently and the setting stayed on its default.
- `readback_resolve` is now the string cvar the plugin actually exposes
  (`none`/`fast`/`some`/`full`). The old boolean `d3d12_readback_resolve` was the
  wrong name and never changed the mode.
- `bfbc_pad_backend` exists because the SDK's `input_backend` only accepts
  `xinput` or `sdl`; any other value (including `auto`) is silently replaced with
  `sdl`, so it cannot express "both".
- The F5 render-target selection is save-only; applying it to a live session could
  leave the render target cache on the old path and crash on the next draw.
- `gamma_render_target_as_unorm16` is no longer forced off. The old override
  changed lighting; the correct default is left in place.

## What is in the archive
- `bin\bfbc.exe` — application (Release, x64).
- `bin\rexruntime.dll`, `bin\rexgpu-xenos.dll` — runtime and Xenos GPU plugin.
- `bin\TracyClient.dll` — profiler client loaded by the runtime.
- `bin\bfbc.toml` — initial settings: fullscreen, 2× internal scale, VSync on,
  RTV render target path, readback `fast`, dual pad backend, the mouse defaults
  above.
- `bfbc.toml.example` — every key, documented. Reference only; the game does not
  read it.
- `game\` — empty. Put your own copy of the game here.
- `licenses\SDL3-LICENSE.txt`, `README.md`, `README.txt`, this file.

**No game files, no generated code and no build artefacts are included.**
The archive cannot run without your own copy of the game.

## Requirements
- Windows 10/11 x64 with current GPU drivers.
- A copy of the game you own (Xbox 360 files: `default.xex` plus content).
- DirectX 12 capable GPU.

## How to use
1. Extract the archive.
2. Put your game files in `game\` next to `bin\`.
3. Run `bin\bfbc.exe`.
4. `F5` settings, `F3` stats, `F4` every cvar, `` ` `` console.

## Known issues
- **RTV smoke**: the default RTV path approximates the 360's HDR formats, so smoke
  and some translucent effects can look hard-edged/too opaque. ROV fixes them but
  may hitch and can crash on AMD with SDK 0.10.0.
- **ROV crash**: on this SDK the ROV path can hit a null event in
  `rexruntime.dll` (`XEvent::Set`) after a short session. Use RTV until a newer
  runtime is available.
- **Restart required**: internal resolution, readback mode and render target path
  are applied at startup only.
- **30 fps engine**: the game's simulation is fixed at ~30 Hz; there is no true
  60 fps mode in this release.
- In-game button prompts remain Xbox 360 icons; the game has no native mouse input
  path.
- `NtCreateFile` warnings for optional assets missing from the disc image are
  harmless.

## Warning
`bfbc.exe` is compiled from code generated out of `default.xex`, so it contains
code derived from the game. Publishing it publicly means distributing game code;
the safe alternative is to publish the source only (`build.ps1` +
`bfbc_manifest.toml`) and let each user build against their own copy.
