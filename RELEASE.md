# Windows release package — Beta 2.0

The package is the `Release` build of `bfbc.exe` plus the runtime libraries, laid out
so that a user only has to drop their own game files next to it, then zipped.
**No game files are included.**

## What it contains

```
BFBC-Recomp-Beta-2.0-win64/
  bin/
    bfbc.exe            62 MB   - the recompiled application (Release, x64)
    rexgpu-xenos.dll     2.7 MB - Xenos graphics plugin (from the SDK)
    rexruntime.dll       9.9 MB - ReXGlue runtime
    TracyClient.dll    231 KB   - profiler client loaded by the runtime
    bfbc.toml                   - initial settings (fullscreen, 2x scale, RTV)
  game/
    PUT-YOUR-GAME-FILES-HERE.txt
  licenses/
    SDL3-LICENSE.txt
  README.md
  README.txt
  NOTES-Beta-2.0.md
  bfbc.toml.example
```

`game/` is empty on purpose apart from the placeholder: the user supplies
`default.xex` and the content folders. The package must never contain
`game/default.xex`, `game/Output/`, `game/dist/`, `generated/`, `.map`, `.pdb`
or logs.

`bfbc.toml` ships with:

- `render_target_path_d3d12 = "rtv"` — the stable path. ROV can be selected in F5,
  but it needs a restart and can crash on AMD with SDK 0.10.0.
- `draw_resolution_scale_x/y = 2` (the plugin's `resolution_scale` alias is not
  written: it maps onto the same two flags and only invited them to disagree).
- `readback_resolve = "fast"` and `async_shader_compilation = true`.
- `bfbc_pad_backend = "both"`.
- `bfbc_pace_fps = 0` — no limiter; `bfbc_fps_limit` is gone (the key was
  registered only so old files keep parsing, and saving drops it).
- The mouse defaults: gain 2.0, vertical 1.8, curve 1.0, deadzone 0.0,
  floor 7849, smoothing 0.2 (the cvar range is 0..0.9, so the old 1.0 was
  rejected and silently fell back to 0.2), dither off, frame-rate-independent
  aim on, side buttons on B/X/Y.
- `keybind_rstick_*` empty: the shaped mouse drives the right stick, the arrow
  keys belong to the d-pad alone.
- `bfbc.toml.example` documents every key; it is not read by the game.

## Reproducing the package

```powershell
# 1) Release build (writes build\bfbc.exe and stages the runtime DLLs)
powershell -ExecutionPolicy Bypass -File build.ps1

# 2) Assemble a clean folder (delete any previous one first)
$rel = "release\BFBC-Recomp-Beta-2.0-win64"
if (Test-Path $rel) { Remove-Item -Recurse -Force $rel }
New-Item -ItemType Directory -Force "$rel\bin", "$rel\game", "$rel\licenses" | Out-Null
Copy-Item build\bfbc.exe, build\rexgpu-xenos.dll, build\rexruntime.dll,
           build\TracyClient.dll "$rel\bin"
Copy-Item README.md, README.txt, NOTES-Beta-2.0.md "$rel"
Copy-Item (Join-Path $env:REXSDK "licenses\SDL3\LICENSE.txt") "$rel\licenses\SDL3-LICENSE.txt"
"Put your own copy of the game here: default.xex plus the content folders." |
  Set-Content "$rel\game\PUT-YOUR-GAME-FILES-HERE.txt"

# 3) Zip
Compress-Archive -Path $rel -DestinationPath "release\BFBC-Recomp-Beta-2.0-win64.zip" -Force
```

`release/` is ignored by git: the archive is published as a release asset, never
committed.

### `bfbc.toml`

The package's `bfbc.toml` is curated, not copied blindly from `build\bfbc.toml`
(that file contains whatever the developer last changed in F5). It keeps the
shipped mouse defaults above and uses RTV + 2x internal resolution, and it does
not contain test-only keys such as `mrt_edram_used_range_clamp_to_min`.

## Packaging test (run before publishing)

- [ ] `build.ps1` completes in `Release`; `bin\bfbc.exe` is byte-identical to
      `build\bfbc.exe`.
- [ ] The package starts from its final layout: window responds and the log shows
      `bfbc [rexglue-v0.10.0-Release]`.
- [ ] Log shows `render_target_path_d3d12 = rtv (before GPU init)`.
- [ ] Log shows `Input: custom input system built (pads=xinput+sdl, merged into user 0...)`
      with both a PlayStation and an Xbox pad connected (or at least the backends
      opened).
- [ ] `F5 -> Rendering` shows **Readback resolve** and **Render target path**.
- [ ] `F5 -> Display mode` offers two entries (Windowed / Fullscreen); there is no
      "Exclusive fullscreen" — SDK 0.10.0 has no such flag.
- [ ] Saving in F5 keeps the comments in `bfbc.toml`, leaves the key order alone, and
      does not re-add `bfbc_fps_limit`, `resolution_scale`, `d3d12_readback_resolve`
      or `fullscreen_exclusive`. Put a `# test` line in the file and a
      `bfbc_fps_limit = 60` line, save, and check both are handled.
- [ ] The log of a clean launch has no `Config:` warnings; adding `bfbc_typo = 1` or
      `bfbc_mouse_smooth = 1.0` produces one.
- [ ] `game\` contains only `PUT-YOUR-GAME-FILES-HERE.txt`; no `default.xex`,
      content folders, `generated/`, `.map`, `.pdb` or `logs/`.
- [ ] The zip contains no game files.

## Publishing on GitHub

```powershell
git tag -f -a beta-2.0 -m "Release Beta 2.0"
git push origin main
git push origin beta-2.0

gh release create beta-2.0 "release\BFBC-Recomp-Beta-2.0-win64.zip" `
  --title "BFBC Recomp Beta 2.0 win64" `
  --notes-file NOTES-Beta-2.0.md
```

The repository's previous release is Beta 1.0:
https://github.com/solidussnake95/battlefield-bad-company-xbox360-recomp-/releases/tag/beta-1.0

## Warning

`bfbc.exe` is compiled from code generated out of `default.xex`, so it contains
code derived from the game. Publishing it publicly means distributing game code;
the safe alternative is to publish the source only (`build.ps1` +
`bfbc_manifest.toml`) and let each user build against their own copy.
