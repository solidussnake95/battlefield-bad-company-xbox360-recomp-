"""Frostbite 1.0 .dbx <-> XML converter (Python 3 port of Frankelstner's dbx.py).

Only the parts needed for inspection/editing are implemented:
    python dbx.py toxml  file.dbx [file.xml]
    python dbx.py todbx  file.xml [file.dbx]

The XML is the same shape the BFBC2 Toolkit uses: nested tags, "name"
attributes, numbers separated by '/', booleans as true/false.
"""

from __future__ import annotations

import sys
from struct import pack, unpack

HEADER = b"{binary}"
XMLHEADER = b'<?xml version="1.0"?>\r\n'
TAB = "\t"

HALVES = ("SphereKeyW", "SphereKeyY", "SphereKeyZ", "TargetId", "SourceId", "SphereKeyX")
DOUBLES = ("AwareForgetTime", "LineOfSightTestTime", "SensingTimeSpan", "FireKeepTime",
           "LostForgetTime", "TimeUntilUnseenIsLost", "AttackerTrackTime")
HASHES = ("OriginalHashedWaveName", "HashedName", "HashedWaveName", "OnRoadMaterialNameHashes",
          "Hash", "Id", "CompositeMeshPartNames")
TYPE2 = ("Name", "TextureFile", "LocationName")
EMPTYNUMS = ('NeighbourLinks', 'LeftCurve', 'ForwardGearSpeeds', 'DownCurve',
             'CompositeMeshPartNames', 'RandomEventWeight', 'RightCurve',
             'ShCoefficientsLightDelta', 'ShCoefficientsLight',
             'FirstPartHealthStateNetworkIds', 'ReverseGearSpeeds', 'ZOcclusionLookup',
             'ForwardGearRatios', 'DisallowedIndices', 'SkinnedMeshTransforms', 'UpCurve',
             'FirstPartHealthStateIndices', 'ShCoefficientsShadow', 'ReverseGearRatios')


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0
        self.strings: list[bytes] = []

    def read(self, n: int) -> bytes:
        out = self.data[self.pos:self.pos + n]
        self.pos += n
        return out

    def skip(self, n: int) -> None:
        self.pos += n

    def u128(self) -> int:
        result = 0
        shift = 0
        while True:
            byte = self.data[self.pos]
            self.pos += 1
            result |= (byte & 127) << shift
            shift += 7
            if not byte & 128:
                return result

    def string(self, index: int) -> str:
        return self.strings[index].decode("latin-1")


def intfloat(raw: bytes, name: str) -> str:
    intnum = unpack(">i", raw)[0]
    if name in HASHES:
        return str(intnum)
    if intnum >> 24 == 0 or intnum >> 23 in (255, -1):
        return str(intnum)
    value = unpack(">f", raw)[0]
    return repr(value)


def toxml(data: bytes) -> str:
    r = Reader(data)
    if r.read(8) != HEADER:
        raise ValueError("not a dbx file (missing {binary} magic)")
    totoffset, zero, reloffset, numstrings = unpack(">IIII", r.read(16))
    offsets = unpack(">" + "I" * numstrings, r.read(4 * numstrings))
    lengths = [offsets[i + 1] - offsets[i] for i in range(numstrings - 1)]
    lengths.append(reloffset - 4 * numstrings - offsets[-1])
    r.strings = [r.read(n)[:-1] for n in lengths]

    out: list[str] = []
    level = 0
    open_tags: list[str] = []
    while r.pos < len(data):
        prefix_number = r.u128()
        if prefix_number == 0:
            level -= 1
            out.append(level * TAB + "</" + open_tags.pop() + ">")
            continue
        prefix = r.string(prefix_number)
        typ = data[r.pos]
        r.pos += 1
        # the original script hexlifies the byte: high nibble = kind ('a','2','7','6'),
        # low nibble = number of attributes.
        typ_char = "%x" % (typ >> 4)
        numattrs = typ & 0x0F
        attrs = []
        for _ in range(numattrs):
            attrs.append((r.string(r.u128()), r.string(r.u128())))
        attr_text = " ".join(f'{a}="{v}"' for a, v in attrs)
        tag = level * TAB + "<" + prefix + ((" " + attr_text) if attrs else "")

        if typ_char == "a":
            r.skip(1)
            level += 1
            open_tags.append(prefix)
            out.append(tag + ">")
        elif typ_char == "2":
            content = r.string(r.u128())
            if content:
                out.append(tag + ">" + content + "</" + prefix + ">")
            else:
                out.append(tag + " />")
        elif typ_char == "7":
            numofnums = r.u128()
            numlength = r.u128()
            name = attrs[0][1] if attrs else ""
            if numlength == 4:
                if numofnums % 4 == 0 and numofnums:
                    rawnums = [r.read(4) for _ in range(numofnums)]
                    numtype = 0
                    for i in range(3, numofnums, 4):
                        if not numtype:
                            if rawnums[i] == b"\0\0\0\0":
                                numtype = 1
                            elif rawnums[i] == b"\xcd\xcd\xcd\xcd":
                                numtype = 2
                        elif numtype == 1 and rawnums[i] != b"\0\0\0\0":
                            numtype = 666
                            break
                        elif numtype == 2 and rawnums[i] != b"\xcd\xcd\xcd\xcd":
                            numtype = 666
                            break
                    if numtype == 1:
                        content = "/".join("*zero*" if i % 4 == 3 else intfloat(raw, name)
                                           for i, raw in enumerate(rawnums))
                    elif numtype == 2:
                        content = "/".join("*nonzero*" if i % 4 == 3 else intfloat(raw, name)
                                           for i, raw in enumerate(rawnums))
                    else:
                        content = "/".join(intfloat(raw, name) for raw in rawnums)
                else:
                    content = "/".join(intfloat(r.read(4), name) for _ in range(numofnums))
            elif numlength == 8:
                content = "/".join(str(x) for x in unpack(">" + "d" * numofnums,
                                                          r.read(8 * numofnums)))
            else:
                content = "/".join(str(x) for x in unpack(">" + "H" * numofnums,
                                                          r.read(2 * numofnums)))
            out.append(tag + ">" + content + "</" + prefix + ">")
        else:  # type 6: bool / small int
            r.skip(1)
            bol = r.read(1)
            if bol == b"\x01":
                content = "true"
            elif bol == b"\x00":
                content = "false"
            else:
                content = str(bol[0])
            out.append(tag + ">" + content + "</" + prefix + ">")
    return "\n".join(out) + "\n"


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd = argv[1]
    if cmd == "toxml":
        with open(argv[2], "rb") as f:
            xml = toxml(f.read())
        target = argv[3] if len(argv) > 3 else argv[2][:-4] + ".xml"
        with open(target, "w", encoding="latin-1", newline="\r\n") as f:
            f.write(xml)
        print(f"wrote {target}")
    else:
        print(f"unknown command {cmd}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
