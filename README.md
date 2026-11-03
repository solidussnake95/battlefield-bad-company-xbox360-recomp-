# Battlefield: Bad Company — Xbox 360 static recompilation for Windows

A **static recompilation** of *Battlefield: Bad Company* (Xbox 360, Title ID `454107F9`)
into a native Windows x64 application, built with **ReXGlue SDK 0.10.0**.

This repository contains **project code only** — no retail game content and no code
generated from it (`game/` and `generated/` are ignored; see below).

---

## Contents

| Path | Description |
|---|---|
| `CMakeLists.txt` | Builds `bfbc.exe` (WIN32) and stages the GPU plugin plus a linker map |
| `bfbc_manifest.toml` | Recompilation manifest: entry point, `setjmp/longjmp`, hand-discovered function entries |
| `src/main.cpp` | Registers the application (`REX_DEFINE_APP`) |
| `src/bfbc_app.h` | App hooks: launch defaults, GPU plugin preload, mouse/keyboard driver |
| `src/bfbc_graphics.h` | F5 overlay: display, resolution, VSync, filtering, mouse sensitivity, key bindings |
| `src/bfbc_patches.cpp` | Stubs for the SDK's `XeKeys*` licence-signature functions |
| `build.ps1` | Full build: configure → codegen → branch repair → Ninja |
| `build/fix_gen_branches.ps1` | Rewrites codegen branches that become `REX_FATAL` although their label exists |
| `build/add_funcs.ps1`, `build/analyze_image.ps1` | Tooling for adding missing function entries to the manifest |
| `build/NOTES_missing_functions.md` | How missing functions were found without splitting existing ones |
| `build/run_bfbc.bat` | Short launcher (no longer required) |
| `RELEASE.md` | How the Windows package is assembled and published |

---

## Requirements

- Windows x64.
- **ReXGlue SDK 0.10.0** (`win-amd64`) — provides `rexglue.exe`, `rexruntime.dll` and `rexgpu-xenos.dll`.
- **LLVM/Clang 19.1.5**, **CMake 3.31.6**, **Ninja 1.13.2**.
- An `xwin` sysroot with MSVC 14.44 and Windows SDK 10.0.26100 (no Visual Studio install needed).

The toolchain paths are set at the top of `build.ps1`; point them at your machine.

---

## Game files (not included)

Place a copy **you own** in `game/` so that it looks like this:

```
game/default.xex          # the title's executable image
game/Output/...           # content data
game/dist/...
game/dvd-prb/...
```

Nothing can be generated or run without `default.xex`.

---

## Build and run

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
build\bfbc.exe
```

`build.ps1` performs, in order:

1. CMake configure with Clang and the dynamic runtime.
2. `rexglue codegen bfbc_manifest.toml` — emits `generated/default/*` from `game/default.xex`.
3. `build/fix_gen_branches.ps1` — repairs codegen branch output before it is compiled.
4. Ninja build → `build/bfbc.exe`.

Running `bfbc.exe` on its own (double-click) is enough: it locates `..\game` (or `game\`
next to the executable), loads the `xenos` plugin, allows the title to write to `GAME:\`,
and then creates the graphics system.

A prebuilt Windows package is published under
[Releases](https://github.com/solidussnake95/battlefield-bad-company-xbox360-recomp-/releases).

---

## What this project adds on top of the SDK

1. **Launch defaults** (`OnConfigurePaths`): game data root, `gpu_plugin=xenos`,
   and `allow_game_relative_writes` — applied only when neither the command line
   nor `bfbc.toml` decides, so explicit settings always win.
2. **Internal resolution actually works** (`OnPreSetup`): the GPU plugin registers its
   cvars when its DLL is loaded, but the SDK loads that DLL during presentation setup,
   after `bfbc.toml` has been read. Saved `draw_resolution_scale_x/y` values were
   therefore dropped and the render-target cache was always built at 1×. The DLL is now
   preloaded so the cvars exist, the saved values are pushed in, and only then is the
   graphics system created.
3. **Mouse and keyboard** (`OnPostSetup`): registers the SDK's `MnkInputDriver`
   (WASD movement, mouse as the right stick, pad buttons on the keyboard).
4. **F5 overlay** (`bfbc_graphics.h`): display mode, output resolution, internal
   resolution, VSync, anisotropy, shader compilation, mouse sensitivity and every
   `keybind_*` — all saved to `bfbc.toml`.
5. **`bfbc_patches.cpp`**: `XeKeysConsolePrivateKeySign` and
   `XeKeysConsoleSignatureVerification` report success instead of 0, otherwise the
   title aborts on its disc check.
6. **`fix_gen_branches.ps1`**: turns `if (cond) REX_FATAL(...)` back into
   `if (cond) goto loc_XXXX;` where the emitter failed to classify a branch whose
   target label is present in the emitted body.

---

## Known limitations

- **No frame limiter**: SDK 0.10.0 exposes none, and `video_mode_refresh_rate` was
  measured to have no effect on pacing. VSync (display rate) or nothing.
- **Internal resolution changes** are saved to `bfbc.toml` and applied on the next
  launch, because the render-target cache is created once at startup.
- **The mouse is not raw input**: the title only reads an analog stick, so mouse look
  emulates the right stick and follows the game's own curve. Sensitivity is tunable in F5.
- Some optional assets produce `NtCreateFile` warnings (files absent from the disc
  image); they do not prevent startup.

---

## Legal

This repository contains **no** game content and **no** generated code derived from it.
`game/` and `generated/` are listed in `.gitignore`. To use this project you must own
a copy of the game.
