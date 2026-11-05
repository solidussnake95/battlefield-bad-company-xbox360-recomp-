"""Patch the Frostbite config bundle of the recomp (game/dist/xenon/config.zz).

config.zz is a plain ZIP holding Game.cfg and the Frost engine Lua scripts.
Game.cfg contains an `applySettings [=[ ... ]=]` block (a key/value list of
engine settings); the project appends its own block at the end of the file so
game patches stay in one place.

Usage:
    python config_patch.py show   <config.zz>
    python config_patch.py apply  <config.zz> <profile>
    python config_patch.py fov    <config.zz> <degrees|off>
    python config_patch.py restore <config.zz>

`fov` writes the camera field of view into the same override block, so it
survives profile changes; `apply` keeps whatever `fov` stored.

Profiles:
    none              remove every override (shipped Game.cfg)
    shadow-far        extend the sun shadow map reach without touching the
                      cascade scheme (single 200 m map)
    shadow-wide       same single map, but 2048 px, 300 m and a deeper
                      extrusion (sharper far shadows, cut line pushed back)
    shadow-cascades   the real fix for the cut line: three cascaded boxes out
                      to 350 m, each with its own resolution
"""

from __future__ import annotations

import os
import shutil
import sys
import zipfile

BEGIN = "-- bfbc port overrides begin"
END = "-- bfbc port overrides end"

PROFILES = {
    # Remove every override (back to the shipped Game.cfg). Used as the
    # control run when comparing against a shadow profile.
    "none": [],
    "shadow-far": [
        "# Extend the sun shadow map: the shipped value is 100 m with a 5%",
        "# screen-area cull, so tree shadows stop being drawn well before the",
        "# tree itself disappears.",
        "# Render.ForceFov 30   (previous experiment, left disabled)",
        "WorldRender.ShadowmapsEnable true",
        "WorldRender.ShadowmapViewDistance 200",
        "WorldRender.ShadowmapMinScreenArea 0.005",
    ],
    # Only try this when shadow-far reaches farther but looks blocky: a single
    # 100 m map at 200 m loses texel density, three cascades give it back.
    "shadow-wide": [
        "# Same single shadow map, but bigger and deeper: 2048 px so far",
        "# shadows stay sharp, 300 m reach, and a 100 m extrusion so tall",
        "# trees/hills are not clipped out of the shadow volume.",
        "WorldRender.ShadowmapsEnable true",
        "WorldRender.ShadowmapViewDistance 300",
        "WorldRender.ShadowmapMinScreenArea 0.002",
        "WorldRender.ShadowmapWidth 2048",
        "WorldRender.ShadowmapHeight 2048",
        "WorldRender.ShadowmapExtrusionLength 100",
    ],
    # Diagnostic only: push the single map far past anything you can see, so
    # the "shadow stops here" line leaves the screen. If the line survives,
    # it is a real cast shadow, not the map edge.
    "shadow-diag": [
        "WorldRender.ShadowmapsEnable true",
        "WorldRender.ShadowmapViewDistance 1000",
        "WorldRender.ShadowmapMinScreenArea 0.002",
        "WorldRender.ShadowmapWidth 2048",
        "WorldRender.ShadowmapHeight 2048",
        "WorldRender.ShadowmapExtrusionLength 30",
    ],
    # Verified best so far: the map edge sits past the fog and 2048 px keep
    # texels usable. 4k was rejected (flat dark frame in the 13.04 shot).
    "shadow-final": [
        "WorldRender.ShadowmapsEnable true",
        "WorldRender.ShadowmapViewDistance 1000",
        "WorldRender.ShadowmapMinScreenArea 0.002",
        "WorldRender.ShadowmapWidth 2048",
        "WorldRender.ShadowmapHeight 2048",
        "WorldRender.ShadowmapExtrusionLength 30",
    ],
    # Console-look experiment: cover the whole visible world so distant hills
    # are lit like on the 360 (outside the map they are treated as shadowed,
    # which is what makes our far terrain dark). Costs near shadow sharpness.
    "shadow-long": [
        "WorldRender.ShadowmapsEnable true",
        "WorldRender.ShadowmapViewDistance 2500",
        "WorldRender.ShadowmapMinScreenArea 0.002",
        "WorldRender.ShadowmapWidth 2048",
        "WorldRender.ShadowmapHeight 2048",
        "WorldRender.ShadowmapExtrusionLength 30",
    ],
    # Diagnosis: draw the shadow map frustum and the raw shadow map on screen
    # so we can see exactly where the map ends instead of guessing.
    "shadow-debug": [
        "WorldRender.ShadowmapsEnable true",
        "WorldRender.ShadowmapViewDistance 1000",
        "WorldRender.ShadowmapMinScreenArea 0.002",
        "WorldRender.ShadowmapWidth 2048",
        "WorldRender.ShadowmapHeight 2048",
        "WorldRender.ShadowmapExtrusionLength 30",
        "WorldRender.DrawShadowFrustums true",
        "WorldRender.DrawDebugShadowmaps true",
        "WorldRender.DrawLightSources true",
    ],
    # The only safe shape: the render target is allocated 1024 wide (pitch
    # 1040), so anything above 1024 makes the resolve read past the surface
    # ("Resolve region 0 <= x < 2048 is outside the surface pitch 1040") and
    # the runtime eventually dies. Range is the only free lever.
    "shadow-safe": [
        "WorldRender.ShadowmapsEnable true",
        "WorldRender.ShadowmapViewDistance 300",
        "WorldRender.ShadowmapMinScreenArea 0.005",
    ],
    # shadow-safe plus film grain off (the grain is what makes the picture look
    # dim and noisy: the shipped FilmGrainColorScale is 0.065 and it is added on
    # top of an already dark dusk scene).
    "shadow-clear": [
        "WorldRender.ShadowmapsEnable true",
        "WorldRender.ShadowmapViewDistance 300",
        "WorldRender.ShadowmapMinScreenArea 0.005",
    ],
    "shadow-cascades": [
        "# single map is replaced by three overlapping boxes, so resolution",
        "# follows the viewer instead of being spread over one huge map.",
        "WorldRender.ShadowmapsEnable true",
        "WorldRender.CascadedBoxShadowmapEnable true",
        "WorldRender.CascadedBoxShadowmapClipMethodEnable true",
        "WorldRender.ShadowmapSliceCount 3",
        "WorldRender.ShadowmapSliceSchemeWeight 0.85",
        "WorldRender.ShadowmapSliceOverlapFactor 0.02",
        "WorldRender.ShadowmapViewDistance 350",
        "WorldRender.ShadowmapMinScreenArea 0.002",
        "WorldRender.ShadowmapWidth 2048",
        "WorldRender.ShadowmapHeight 2048",
        "WorldRender.ShadowmapExtrusionLength 100",
    ],
}


GRAIN_OFF = ("PostProcess.FilmGrainEnable = true", "PostProcess.FilmGrainEnable = false")
GRAIN_ON = ("PostProcess.FilmGrainEnable = false", "PostProcess.FilmGrainEnable = true")

# Plain Lua assignments the override block cannot reach. Profiles not listed
# here put the shipped value back, so switching profiles never leaves a stale
# edit behind.
RAW = {
    "shadow-clear": [GRAIN_OFF],
}
RAW_DEFAULT = [GRAIN_ON]

# Camera field of view. The engine reads its settings at boot, so this is a
# next-launch option rather than something the F5 menu can change live. Vertical
# degrees: the shipped camera is 60, and the range below only guards typos.
FOV_KEY = "Render.ForceFov"
FOV_MIN = 45.0
FOV_MAX = 120.0


def block_bounds(text: str) -> tuple[int, int] | None:
    start = text.find(BEGIN)
    if start < 0:
        return None
    stop = text.find(END, start)
    if stop < 0:
        raise SystemExit(f"marker not found: {END}")
    return start, stop


def block_lines(text: str) -> list[str]:
    """Every setting line currently inside the bfbc override block."""
    bounds = block_bounds(text)
    if not bounds:
        return []
    out = []
    for line in text[bounds[0]:bounds[1]].splitlines():
        key = line.strip()
        if not key or key in (BEGIN, END) or key.startswith("applySettings") or key.startswith("]="):
            continue
        out.append(line.rstrip())
    return out


def with_fov(lines: list[str], fov_line: str | None) -> list[str]:
    kept = [line for line in lines if not line.strip().startswith(FOV_KEY)]
    if fov_line:
        kept.append(fov_line)
    return kept


def current_fov_line(text: str) -> str | None:
    for line in block_lines(text):
        if line.strip().startswith(FOV_KEY):
            return line
    return None


def read_entries(path: str) -> list[tuple[zipfile.ZipInfo, bytes]]:
    with zipfile.ZipFile(path) as z:
        return [(info, z.read(info.filename)) for info in z.infolist()]


def write_entries(path: str, entries: list[tuple[zipfile.ZipInfo, bytes]]) -> None:
    tmp = path + ".tmp"
    with zipfile.ZipFile(tmp, "w") as z:
        for info, data in entries:
            z.writestr(info, data)
    os.replace(tmp, path)


def replace_block(text: str, lines: list[str]) -> str:
    block = BEGIN + "\napplySettings [=[\n" + "\n".join(lines) + "\n]=]\n" + END
    start = text.find(BEGIN)
    if start < 0:
        if not lines:
            return text
        # shipped Game.cfg has no override block yet: append one
        return text.rstrip("\r\n") + "\n\n" + block + "\n"
    stop = text.find(END, start)
    if stop < 0:
        raise SystemExit(f"marker not found: {END}")
    if not lines:
        # drop the whole override block, leaving the shipped file untouched
        return text[:start] + text[stop + len(END):].lstrip("\r\n")
    return text[:start] + block + text[stop + len(END):]


def apply_raw(text: str, edits: list[tuple[str, str]]) -> str:
    """Literal replacements inside Game.cfg (outside the override block).

    Some settings are plain Lua assignments (`PostProcess.FilmGrainEnable =
    true`) and are not part of the applySettings key/value list, so they have
    to be edited in place. Applying the same edit twice is a no-op.
    """
    for old, new in edits:
        if new in text:
            continue
        if old not in text:
            raise SystemExit(f"literal not found in Game.cfg: {old}")
        text = text.replace(old, new)
    return text


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd, path = argv[1], argv[2]
    entries = read_entries(path)

    if cmd == "show":
        for info, data in entries:
            if info.filename == "Game.cfg":
                text = data.decode("latin-1")
                start = text.find(BEGIN)
                print(text[start:] if start >= 0 else "(no override block)")
        return 0

    if cmd == "apply":
        profile = argv[3] if len(argv) > 3 else ""
        if profile not in PROFILES:
            raise SystemExit(f"unknown profile {profile!r}; try {', '.join(PROFILES)}")
        backup = path + ".bfbc-backup"
        if not os.path.exists(backup):
            shutil.copy2(path, backup)
        changed = False
        for i, (info, data) in enumerate(entries):
            if info.filename == "Game.cfg":
                text = data.decode("latin-1")
                # Keep a field of view set through `fov` across profile changes.
                text = replace_block(text, with_fov(PROFILES[profile], current_fov_line(text)))
                text = apply_raw(text, RAW.get(profile, RAW_DEFAULT))
                entries[i] = (info, text.encode("latin-1"))
                changed = True
        if not changed:
            raise SystemExit("Game.cfg not found in the archive")
        write_entries(path, entries)
        print(f"applied profile {profile!r} to {path} (backup: {backup})")
        return 0

    if cmd == "fov":
        value = argv[3] if len(argv) > 3 else ""
        fov_line = None
        if value and value.lower() != "off":
            try:
                degrees = float(value)
            except ValueError:
                raise SystemExit("fov takes a number of degrees or 'off'")
            clamped = min(max(degrees, FOV_MIN), FOV_MAX)
            if clamped != degrees:
                print(f"fov {degrees:g} is outside {FOV_MIN:g}..{FOV_MAX:g}, using {clamped:g}")
            fov_line = f"{FOV_KEY} {clamped:g}"

        backup = path + ".bfbc-backup"
        if not os.path.exists(backup):
            shutil.copy2(path, backup)
        changed = False
        for i, (info, data) in enumerate(entries):
            if info.filename == "Game.cfg":
                text = data.decode("latin-1")
                text = replace_block(text, with_fov(block_lines(text), fov_line))
                entries[i] = (info, text.encode("latin-1"))
                changed = True
        if not changed:
            raise SystemExit("Game.cfg not found in the archive")
        write_entries(path, entries)
        print(f"field of view {'cleared' if not fov_line else 'set to ' + value} in {path} "
              f"(applies on the next launch, backup: {backup})")
        return 0

    if cmd == "restore":
        backup = path + ".bfbc-backup"
        if not os.path.exists(backup):
            raise SystemExit(f"no backup at {backup}")
        shutil.copy2(backup, path)
        print(f"restored {path} from {backup}")
        return 0

    print(f"unknown command {cmd}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
