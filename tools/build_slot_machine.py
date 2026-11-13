#!/usr/bin/env python3
"""The slot machine's model and material: the Vault-Tec Workshop's WorkshopSlotMachine01.nif (tools/source_slots,
from DLCworkshop03 - Main.ba2) with
  - its material our own (Materials\\fo4roulette\\SlotMachine.BGSM: the DLC's, its textures ours: make_slot_machine.py),
  - its behaviour graph off (the BGED extra data renamed: the plugin turns the reels and the lever itself, so they land
    where it wants them; the DLC's graph would play its own canned spins),
  - its workshop power connection points off (the CPA extra data renamed: nothing to wire),
  - the reels' and lever's NiTransformControllers not active (their flags' active bit cleared).
Only header strings and those flags change: every block's size is as it was.

    python3 tools/build_slot_machine.py <Data dir>     (writes Meshes\\fo4roulette\\SlotMachine.nif and the BGSM)

The reels' strip starts (src/Slots.cpp kStripStart) come from the reel shapes' UVs: printed here as a check.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from nifheader import Nif, trishape  # noqa: E402

SRC = os.path.join(HERE, "source_slots")
MATERIAL = "Materials\\fo4roulette\\SlotMachine.BGSM"
TEXTURES = {
    "DLC06/SetDressing/SlotMachine/SlotMachine01_d.dds": "fo4roulette/SlotMachine_d.dds",
    "DLC06/SetDressing/SlotMachine/SlotMachine01_n.dds": "fo4roulette/SlotMachine_n.dds",
    "DLC06/SetDressing/SlotMachine/SlotMachine01_s.dds": "fo4roulette/SlotMachine_s.dds",
}


def bgsm(src: bytes) -> bytes:
    """the DLC's material with our textures: its strings are length-prefixed (u32, the null counted)"""
    out = bytearray()
    pos = 0
    for m in re.finditer(rb"(.{4})([A-Za-z][\x20-\x7e]+\.dds\x00)", src, re.S):
        n = struct.unpack("<I", m.group(1))[0]
        text = m.group(2)
        if n != len(text):
            continue
        name = text[:-1].decode()
        new = TEXTURES.get(name)
        if new is None:
            continue
        out += src[pos:m.start()]
        enc = new.encode() + b"\0"
        out += struct.pack("<I", len(enc)) + enc
        pos = m.end()
    out += src[pos:]
    return bytes(out)


def strip_starts(n: Nif) -> list[float]:
    import math
    starts = []
    for name in ("WorkshopSlotMachine01ReelA:0", "WorkshopSlotMachine01ReelB:0", "WorkshopSlotMachine01ReelC:0"):
        i = next(k for k in range(n.nblocks) if n.btype(k) == "BSTriShape" and n.s(struct.unpack_from("<i", n.block(k), 0)[0]) == name)
        _, p, uv, _, _ = trishape(n.block(i))
        top = uv[:, 1].min()
        angs = sorted({round(math.degrees(math.atan2(p[k, 2], p[k, 1])) % 360, 1) for k in range(len(p)) if abs(uv[k, 1] - top) < 0.002})
        starts.append(angs[0])
    return starts


def main(data: str) -> None:
    n = Nif(os.path.join(SRC, "WorkshopSlotMachine01.nif"))
    strings = list(n.strings)
    for k, s in enumerate(strings):
        if s == "Materials\\DLC06\\SetDressing\\SlotMachine\\SlotMachine01.BGSM":
            strings[k] = MATERIAL
        elif s == "BGED":
            strings[k] = "BGEX"  # (the behaviour graph: off)
        elif s == "CPA":
            strings[k] = "CPX"   # (the power connection points: off)
    assert MATERIAL in strings and "BGEX" in strings
    # the reels' and the lever's own controllers (the DLC's whole animation timeline, played by its graph): not active,
    # or they'd set the bones every frame over ours
    d = bytearray(n.d)
    for i in range(n.nblocks):
        if n.btype(i) == "NiTransformController":
            at = n.offsets[i] + 4
            flags = struct.unpack_from("<H", d, at)[0]
            struct.pack_into("<H", d, at, flags & ~0x8)
    n.d = bytes(d)
    os.makedirs(os.path.join(data, "Meshes", "fo4roulette"), exist_ok=True)
    os.makedirs(os.path.join(data, "Materials", "fo4roulette"), exist_ok=True)
    n.write(os.path.join(data, "Meshes", "fo4roulette", "SlotMachine.nif"), strings)
    with open(os.path.join(SRC, "SlotMachine01.BGSM"), "rb") as f:
        mat = bgsm(f.read())
    assert b"fo4roulette/SlotMachine_d.dds" in mat
    with open(os.path.join(data, "Materials", "fo4roulette", "SlotMachine.BGSM"), "wb") as f:
        f.write(mat)
    print("reel strip starts (degrees):", strip_starts(n))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "dist", "Data"))
