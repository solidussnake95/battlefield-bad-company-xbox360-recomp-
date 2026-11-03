# Windows release package — v0.1.1

The package is the `Release` build of `bfbc.exe` plus the runtime libraries, laid out
so that a user only has to drop their own game files next to it, then zipped.

## What it contains

```
BFBC-Recomp-v0.1.1-win64/
  bin/
    bfbc.exe            62 MB   - the recompiled application (Release, x64)
    rexgpu-xenos.dll     2.7 MB - Xenos graphics plugin (from the SDK)
    rexruntime.dll       9.9 MB - ReXGlue runtime
    TracyClient.dll    231 KB   - profiler client loaded by the runtime
    bfbc.toml                   - initial settings (fullscreen, 2x internal scale)
  game/                         - empty: the user supplies default.xex and content
  licenses/SDL3-LICENSE.txt
  README.txt                    - player-facing instructions
  README.md                     - project and build documentation
  PUT-YOUR-GAME-FILES-HERE.txt
```

`v0.1.1` repackages the `v0.1.0` build: the application code is unchanged and only the
documentation and repository hygiene were updated. See `NOTES-v0.1.1.md`.

No game files, no `generated/` sources and no debug artefacts (`.map`, `.pdb`) are included.

## Reproducing the package

```powershell
# 1) Release build (writes build\bfbc.exe and stages the runtime DLLs)
powershell -ExecutionPolicy Bypass -File build.ps1

# 2) Assemble the folder
$rel = "release\BFBC-Recomp-v0.1.1-win64"
New-Item -ItemType Directory -Force "$rel\bin", "$rel\game", "$rel\licenses" | Out-Null
Copy-Item build\bfbc.exe, build\rexgpu-xenos.dll, build\rexruntime.dll,
           build\TracyClient.dll, build\bfbc.toml "$rel\bin"
Copy-Item README.md, NOTES-v0.1.1.md "$rel"
Copy-Item (Join-Path $env:REXSDK "licenses\SDL3\LICENSE.txt") "$rel\licenses\SDL3-LICENSE.txt"

# 3) Zip
Compress-Archive -Path $rel -DestinationPath "release\BFBC-Recomp-v0.1.1-win64.zip" -Force
```

`release/` is ignored by git: the archive is published as a release asset, never committed.

## Packaging test (run before publishing)

For `v0.1.0` the package was started from its final layout (`bin\bfbc.exe` with `game\`
beside it, the folder temporarily junctioned to the local game data) and the checks
below passed. `v0.1.1` repackages the same binary, so repeat them after building and
tick the checklist in `NOTES-v0.1.1.md`:

- window came up and stayed responsive: `bfbc [rexglue-v0.10.0-Release]`
- `draw_resolution_scale_x/y = 2` and `resolution_scale = 2` applied before GPU init
- `Input: keyboard/mouse driver registered`
- `bin\bfbc.exe` hash identical to the build output

## Publishing on GitHub

```powershell
git tag -a v0.1.1 -m "BFBC Recomp v0.1.1 (Windows x64)"
git push origin v0.1.1

git tag -l                 # v0.1.0, v0.1.1

git log --oneline v0.1.0..v0.1.1   # what the release notes must describe

gh release create v0.1.1 "release\BFBC-Recomp-v0.1.1-win64.zip" `
  --title "BFBC Recomp v0.1.1 (Windows x64)" `
  --notes-file NOTES-v0.1.1.md
```

`v0.1.0` is published here:
https://github.com/solidussnake95/battlefield-bad-company-xbox360-recomp-/releases/tag/v0.1.0

`v0.1.1` is published here once the command above runs:
https://github.com/solidussnake95/battlefield-bad-company-xbox360-recomp-/releases/tag/v0.1.1

## Warning

`bfbc.exe` is compiled from code generated out of `default.xex`, so it contains code
derived from the game. Publishing it publicly means distributing game code; the safe
alternative is to publish the source only (`build.ps1` + `bfbc_manifest.toml`) and let
each user build against their own copy.
