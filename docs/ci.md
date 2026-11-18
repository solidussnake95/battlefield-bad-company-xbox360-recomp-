# CI — building a release in GitHub Actions

The workflow is `.github/workflows/beta-2.1.yml`. It builds `bfbc.exe` and
publishes `BFBC-Recomp-Beta-<version>-win64.zip` as a workflow artifact, and as
a GitHub release when it is started from a `beta-2.1` tag (or when
"publish release" is ticked by hand).

## Why the runner is self-hosted

`bfbc.exe` is compiled from C++ that `rexglue codegen` generates out of the
retail `game/default.xex`:

| needed to build | size | in the repo? |
|---|---|---|
| `game/` (retail XEX + content) | several GB | no, copyright |
| `generated/default/` (codegen output) | 367 files, ~190 MB | no, `.gitignore` |
| ReXGlue SDK 0.10.0 `win-amd64` | ~40 MB | no, external package |

A GitHub-hosted runner would have to download all of that first. Pushing it into
a public repository (or its artifacts) publishes game-derived code. So the build
runs on a runner on the machine that already owns those files, and sends only
the finished zip to GitHub.

`tools/ci/build-beta.ps1` never touches the game files destructively: it runs
`git archive <ref>` into `%LOCALAPPDATA%\bfbc-ci\src-<ref>`, then creates
**junctions** for `game/` and `generated/` inside that staging tree. Git clean
never runs there, so the retail files cannot be deleted by a checkout.

## One-time runner setup

```powershell
# from the repository root; needs an authenticated `gh` CLI
powershell -ExecutionPolicy Bypass -File tools\ci\install-runner.ps1 -Start
```

The workflow steps use `shell: pwsh`, so the machine needs PowerShell 7:

```powershell
winget install --id Microsoft.PowerShell -e --source winget
```

If `pwsh` is missing the job fails immediately with `pwsh: command not found`;
`install-runner.ps1` warns about it up front.

That downloads the current `actions-runner-win-x64`, registers it against this
repository with the labels `self-hosted, windows, x64, bfbc`, and starts it as a
per-user *logon task* (`BFBC GitHub Actions Runner`). No Administrator rights are
needed.

If you prefer a Windows service, run the same script from an elevated shell with
`-Service`. Useful switches:

```powershell
... -Status        # is the runner online? what labels?
... -Uninstall     # deregister and remove the logon task
... -Reconfigure   # re-register (e.g. after moving the runner folder)
```

The script only registers the runner; the workflow has no `pull_request`
trigger, so pull requests (including forks) never execute code on this machine.
Keep it that way: on a public repository, a self-hosted runner plus a fork PR
trigger is remote code execution.

## Building

Actions → **Build Beta 2.1** → *Run workflow*:

| input | default | meaning |
|---|---|---|
| `ref` | `beta-2.1` | git tag/branch to build; must exist in the local repo |
| `publish_release` | `true` | create/update the GitHub release for `ref` |

A tag push (`git push origin beta-2.1`) with the workflow present in the tagged
commit does the same with no input. The workflow itself always checks out the
default branch for its tooling, then compiles the chosen ref from `git archive`
in the local repository — so a tag created before the CI files existed is still
buildable.

Path overrides, if the machine layout changes (repository → Settings → Variables):
`BFBC_REPO_DIR`, `BFBC_GAME_DIR`, `BFBC_GENERATED_DIR`, `BFBC_SDK_DIR`.

## Running the same build by hand

The workflow is a thin wrapper; the script does the work and prints the zip path
and hash:

```powershell
powershell -ExecutionPolicy Bypass -File tools\ci\build-beta.ps1 -DryRun            # toolchain check
powershell -ExecutionPolicy Bypass -File tools\ci\build-beta.ps1 -Ref beta-2.1        # full build
```

The script builds the `bfbc_codegen` target first, applies
`build/fix_gen_branches.ps1`, then refreshes the generated tree's timestamps so
ninja does not re-run codegen during the compile. Order matters: codegen emits
`REX_FATAL("Unresolved branch ...")` for a few sites that the repair rewrites to
`goto`/tail calls, and a codegen run after the repair would silently undo them.

Output: `release\BFBC-Recomp-Beta-2.1-win64.zip` and `.zip.sha256`. The script
refuses to package if `default.xex`, `.pdb`, `.map`, logs or a `generated/`
folder end up inside the release tree.

## Checking the download

The build script scans the finished zip with Windows Defender when
`MpCmdRun.exe` is present (exit code 0 = clean, 2 = threat). That only proves the
machine's local AV engine is happy; for a second opinion upload the zip to
<https://www.virustotal.com/> and compare its SHA-256 with the one printed in
the run summary and in `*.zip.sha256`.

```powershell
Get-FileHash .\BFBC-Recomp-Beta-2.1-win64.zip -Algorithm SHA256
```

Expect a handful of generic detections on a self-built game recompilation
("Wacatac"/"ML.Attribute.HighConfidence" style heuristics) — the exe is an
unsigned 60 MB binary containing recompiled guest code. Anything that names a
specific payload (miner, stealer, RAT) is worth investigating, and the build is
reproducible from the tag if you want to diff.
