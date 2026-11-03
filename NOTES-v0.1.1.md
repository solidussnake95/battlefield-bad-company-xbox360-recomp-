# BFBC Recomp v0.1.1 — Windows x64

Refreshed package of the static recompilation of **Battlefield: Bad Company**
(Xbox 360) into a native Windows application, built with ReXGlue SDK 0.10.0.

This release repackages the v0.1.0 build. The application code (`src/`,
`bfbc_manifest.toml`, `build.ps1`) is unchanged since v0.1.0; what changed is the
documentation and the repository hygiene around it. If you already run v0.1.0,
there is no behavioural reason to replace your copy.

## What changed since v0.1.0

- `README.md` and `RELEASE.md` rewritten in English and brought up to date with the
  current tree.
- Hard-coded machine-specific toolchain paths were removed from the documentation.
  The build instructions now refer to the `$Toolchain` / `$Sdk` variables at the top
  of `build.ps1`, and the codegen example in
  `build/NOTES_missing_functions.md` uses `$env:REXSDK`, which `build.ps1` sets.
- Packaging steps and the publication warning are documented in `RELEASE.md`, which now
  also lists what the packaging command actually produces.
- The repository page opens with a generated SVG banner (`docs/images/hero.svg`) instead
  of plain text. It is a graphic, **not** an in-game screenshot.
- `.gitignore` allows `docs/images/*.png` and `*.jpg` so real screenshots can be added
  later without loosening the rule that keeps dumps, logs and build output out of git.

Nothing in the runtime, the graphics path, the input mapping or the manifest changed.

## What is in the archive
- `bin\bfbc.exe` — the application (Release, x64). Double-click it; no arguments needed.
- `bin\rexruntime.dll`, `bin\rexgpu-xenos.dll` — runtime and the Xenos graphics plugin.
- `bin\bfbc.toml` — initial settings: fullscreen, 2× internal render scale, VSync on.
- `game\` — empty. Put your own copy of the game here (`default.xex` plus content).

**The archive contains no game files.** It cannot run without yours.

## Highlights (unchanged from v0.1.0)
- Starts directly — no command line, no batch file.
- The internal render scale (1×/2×/3×) applies before the graphics system is created.
- Mouse and keyboard emulate an Xbox 360 pad: WASD to move, mouse for the right stick,
  pad buttons spread over the keyboard, sensitivity adjustable in-game.
- `F5` overlay: display mode, output resolution, internal resolution, VSync, filtering,
  UI font size, mouse sensitivity and every keyboard binding — saved to `bfbc.toml`.
- `XeKeys*` licence-signature checks are stubbed so the title does not abort at boot.

## Requirements
- Windows 10/11 x64 with current GPU drivers.
- A copy of the game you own (Xbox 360 files).

## How to use
1. Extract the archive.
2. Put your game files in `game\` next to `bin\`.
3. Run `bin\bfbc.exe`.
4. `F5` for settings, `F3` for stats, `F4` for every cvar, `` ` `` for the console.

## Known limitations
- No frame limiter in SDK 0.10.0; VSync is the only pacing control.
- Internal resolution changes require a restart to take effect.
- The mouse emulates the right stick (the game has no native mouse input).
- `NtCreateFile` warnings for assets missing from the disc image are harmless.

## Verification checklist

Fill this in from the machine that packages the archive, then delete the checkboxes
that do not apply before publishing:

- [ ] `powershell -ExecutionPolicy Bypass -File build.ps1` completes in `Release`.
- [ ] `bin\bfbc.exe` in the package is byte-identical to the build output.
- [ ] The package starts from its final layout: window stays responsive,
      log shows `bfbc [rexglue-v0.10.0-Release]`.
- [ ] `draw_resolution_scale_x/y = 2` and `resolution_scale = 2` are applied before
      GPU init.
- [ ] `Input: keyboard/mouse driver registered` appears in the log.
- [ ] `game\` in the archive is empty and no `generated/`, `.pdb` or `.map` files
      are included.

## Warning

`bfbc.exe` is compiled from code generated out of `default.xex`, so it contains code
derived from the game. Publishing it publicly means distributing game code; the safe
alternative is to publish the source only (`build.ps1` + `bfbc_manifest.toml`) and let
each user build against their own copy.
