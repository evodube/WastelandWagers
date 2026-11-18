"""fo4roulette.esl, retired (the mod's old name, now WastelandWagers.esl): an empty light plugin in its place, so
installing the new version over the old one leaves none of the old plugin's records loaded and nothing to delete by
hand. (Its tables are gone from saves either way: the new plugin's records are new ones.)

    python3 tools/make_retired.py <path to fo4roulette.esl>
"""
import os
import struct
import sys


def subrecord(tag: bytes, data: bytes) -> bytes:
    return tag + struct.pack("<H", len(data)) + data


def main(out: str) -> None:
    data = (subrecord(b"HEDR", struct.pack("<fiI", 1.0, 0, 0x800))
            + subrecord(b"CNAM", b"evodube\0")
            + subrecord(b"SNAM", b"Retired: Wasteland Wagers is WastelandWagers.esl now. This empty plugin stands in for "
                                 b"the old one; it can be unticked or removed.\0"))
    header = b"TES4" + struct.pack("<IIIIHH", len(data), 0x201, 0, 0, 131, 0) + data  # (ESM + ESL, as WastelandWagers.esl)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "wb") as f:
        f.write(header)


if __name__ == "__main__":
    main(sys.argv[1])
