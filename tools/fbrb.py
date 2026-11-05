"""Frostbite 1.0 (.fbrb) bundle reader/writer.

Format (as implemented by Frankelstner's fbrb.py for Battlefield: Bad Company 2,
which BFBC1 uses unchanged):

    "FbRB" | u32be part1_size | gzip(part1) | gzip(payload)

    part1 = u32be version(2) | u32be strings_size | strings
          | u32be entry_count | entries[entry_count]
          | u8 zipped | u32be payload_size

    entry = u32be name_offset | u32be frag_and_size | u32be payload_offset
          | u32be payload_size | u32be payload_size2 | u32be type_offset

name_offset/type_offset are relative to the start of `strings`; strings are
NUL-terminated. `payload_offset` is relative to the payload archive.
"""

from __future__ import annotations

import gzip
import io
import os
import struct
import sys

MAGIC = b"FbRB"
HEADER = struct.Struct(">I")
ENTRY = struct.Struct(">IIIIII")
ENTRY_COUNT = struct.Struct(">I")

# extension -> resource type name, from the original script.
TYPE_BY_EXT = {
    "swfmovie": "SwfMovie",
    "dx10pixelshader": "Dx10PixelShader",
    "havokphysicsdata": "HavokPhysicsData",
    "treemeshset": "TreeMeshSet",
    "terrainheightfield": "TerrainHeightfield",
    "itexture": "ITexture",
    "animtreeinfo": "AnimTreeInfo",
    "irradiancevolume": "IrradianceVolume",
    "visualterrain": "VisualTerrain",
    "skinnedmeshset": "SkinnedMeshSet",
    "dx10vertexshader": "Dx10VertexShader",
    "aimanimation": "AimAnimation",
    "occludermesh": "OccluderMesh",
    "dx9shaderdatabase": "Dx9ShaderDatabase",
    "wave": "Wave",
    "sootmesh": "SootMesh",
    "terrainmaterialmap": "TerrainMaterialMap",
    "rigidmeshset": "RigidMeshSet",
    "compositemeshset": "CompositeMeshSet",
    "watermesh": "WaterMesh",
    "visualwater": "VisualWater",
    "dx9vertexshader": "Dx9VertexShader",
    "dx9pixelshader": "Dx9PixelShader",
    "dx11shaderdatabase": "Dx11ShaderDatabase",
    "dx11pixelshader": "Dx11PixelShader",
    "grannymodel": "GrannyModel",
    "ragdollresource": "RagdollResource",
    "grannyanimation": "GrannyAnimation",
    "weathersystem": "WeatherSystem",
    "dx11vertexshader": "Dx11VertexShader",
    "terrain": "Terrain",
    "impulseresponse": "ImpulseResponse",
    "binkmemory": "BinkMemory",
    "deltaanimation": "DeltaAnimation",
    "dx10shaderdatabase": "Dx10ShaderDatabase",
    "meshdata": "MeshData",
    "xenonpixelshader": "XenonPixelShader",
    "xenonvertexshader": "XenonVertexShader",
    "xenonshaderdatabase": "XenonShaderDatabase",
    "xenontexture": "XenonTexture",
    "ps3pixelshader": "Ps3PixelShader",
    "ps3vertexshader": "Ps3VertexShader",
    "ps3shaderdatabase": "Ps3ShaderDatabase",
    "ps3texture": "Ps3Texture",
    "pathdatadefinition": "PathDataDefinition",
    "dbx": "<non-resource>",
    "bin": "<non-resource>",
    "dbmanifest": "<non-resource>",
}


class Entry:
    __slots__ = ("name", "type", "data", "deleted")

    def __init__(self, name: str, type_: str, data: bytes | None, deleted: bool):
        self.name = name
        self.type = type_
        self.data = data
        self.deleted = deleted

    def __repr__(self) -> str:
        return f"<Entry {self.name} [{self.type}] {len(self.data or b'')} bytes>"


class Bundle:
    def __init__(self, entries: list[Entry], zipped: bool = True):
        self.entries = entries
        self.zipped = zipped

    # -- reading ---------------------------------------------------------
    @classmethod
    def load(cls, path: str) -> "Bundle":
        with open(path, "rb") as f:
            if f.read(4) != MAGIC:
                raise ValueError(f"{path}: not an FbRB archive")
            (part1_size,) = HEADER.unpack(f.read(4))
            part1 = gzip.decompress(f.read(part1_size))
            payload = gzip.decompress(f.read())
        return cls._parse(part1, payload)

    @classmethod
    def _parse(cls, part1: bytes, payload: bytes) -> "Bundle":
        version = HEADER.unpack_from(part1, 0)[0]
        strings_size = HEADER.unpack_from(part1, 4)[0]
        strings = part1[8:8 + strings_size]
        pos = 8 + strings_size
        (count,) = ENTRY_COUNT.unpack_from(part1, pos)
        pos += 4
        zipped = part1[len(part1) - 5] != 0

        def cstr(offset: int) -> str:
            end = strings.find(b"\0", offset)
            return strings[offset:end].decode("latin-1")

        entries = []
        for i in range(count):
            name_off, flags, data_off, size, _size2, type_off = ENTRY.unpack_from(
                part1, pos + i * ENTRY.size)
            name = cstr(name_off)
            type_ = cstr(type_off)
            deleted = (flags & 0x00010000) == 0
            data = None
            if not deleted:
                data = payload[data_off:data_off + size]
            entries.append(Entry(name, type_, data, deleted))
        return cls(entries, zipped)

    # -- writing ---------------------------------------------------------
    def save(self, path: str, compression: int = 6) -> None:
        strings = bytearray()
        type_offsets: dict[str, int] = {}
        table = bytearray()
        payload = bytearray()

        for e in self.entries:
            name_off = len(strings)
            strings += e.name.encode("latin-1") + b"\0"
            data = e.data or b""
            # type string lives once per distinct type
            if e.type not in type_offsets:
                type_offsets[e.type] = len(strings)
                strings += e.type.encode("latin-1") + b"\0"
            flags = 0x00010000 if data else 0x00000000
            table += ENTRY.pack(name_off, flags, len(payload), len(data),
                                len(data), type_offsets[e.type])
            payload += data

        part1 = bytearray()
        part1 += HEADER.pack(2)
        part1 += HEADER.pack(len(strings))
        part1 += strings
        part1 += ENTRY_COUNT.pack(len(self.entries))
        part1 += table
        part1 += b"\x01" if self.zipped else b"\x00"
        part1 += HEADER.pack(len(payload))

        buf = io.BytesIO()
        with gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=1) as gz:
            gz.write(bytes(part1))
        part1_gz = buf.getvalue()

        buf = io.BytesIO()
        with gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=compression) as gz:
            gz.write(bytes(payload))
        payload_gz = buf.getvalue()

        with open(path, "wb") as f:
            f.write(MAGIC)
            f.write(HEADER.pack(len(part1_gz)))
            f.write(part1_gz)
            f.write(payload_gz)


def suffix_for(entry: Entry) -> str:
    """File extension the original unpacker would use for this entry."""
    if entry.type == "*deleted*":
        return ".dbxdeleted" if entry.name.lower().endswith(".dbx") else ".resdeleted"
    if entry.type == "<non-resource>":
        return ".nonres" if entry.name.lower().endswith(".res") else os.path.splitext(entry.name)[1]
    return "." + entry.type.lower()


def type_for_suffix(suffix: str) -> str:
    s = suffix.lower().lstrip(".")
    if s in TYPE_BY_EXT:
        return TYPE_BY_EXT[s]
    for ext, name in TYPE_BY_EXT.items():
        if ext == s:
            return name
    raise KeyError(f"unknown resource suffix .{s}")


def cmd_list(path: str) -> None:
    b = Bundle.load(path)
    for e in b.entries:
        print(f"{e.type}\t{len(e.data or b'')}\t{e.name}")


def cmd_extract(path: str, outdir: str) -> None:
    b = Bundle.load(path)
    for e in b.entries:
        if e.deleted:
            continue
        dest = os.path.join(outdir, e.name.replace("/", os.sep))
        if not os.path.splitext(dest)[1]:
            dest += suffix_for(e)
        os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
        with open(dest, "wb") as f:
            f.write(e.data or b"")
    print(f"extracted {len(b.entries)} entries to {outdir}")


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd = argv[1]
    if cmd == "list":
        cmd_list(argv[2])
    elif cmd == "extract":
        cmd_extract(argv[2], argv[3])
    elif cmd == "find":
        needle = argv[3].encode("latin-1")
        b = Bundle.load(argv[2])
        for e in b.entries:
            data = e.data or b""
            pos = data.find(needle)
            if pos >= 0:
                print(f"{e.name} [{e.type}] size={len(data)} at +0x{pos:x}")
    else:
        print(f"unknown command {cmd}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
