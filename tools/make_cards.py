#!/usr/bin/env python3
"""The poker textures. The cards': jdastine's "Playing Cards" (Sketchfab, CC BY 4.0) has each face as its own 2048
texture, the card itself in one part of it (the faces' UVs cover u 0.489-0.976, v 0-0.779; the back's u 0-0.487 of its
texture): a 5:8 card. This crops each to just the card and packs the 52 faces and the back into one atlas, 10 across
and 6 down (cells 204.8 x 341.3 at 2048: the card's 5:8, give or take; cell k = column k % 10, row k // 10; cards in the
order of CARDS, the back in cell 52), the faces un-mirrored (the model's UVs flip them).

    python make_cards.py playing_cards.glb <Data dir> [--size N] [--table <poker table textures dir>]

Writes Textures\\WastelandWagers\\cards_d.dds and card_s.dds (and a preview png next to this script with --preview);
with --table, the table's too (Pieter Ferreira's "Poker Table", CC BY 4.0): poker_d / poker_n / poker_s, 1024, BC7.
"""

import io
import json
import os
import struct
import sys

from PIL import Image, ImageOps

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dds import save_dds  # noqa: E402

# the model's materials by card: rank (2..9, T, J, Q, K, A) and suit (c, d, h, s); its 'cardback' is material 2
MATERIAL = {
    "2d": 1, "Qd": 3, "Jd": 4, "6h": 5, "Td": 6, "9d": 7, "8d": 8, "7d": 9, "6d": 10, "5d": 11, "4d": 12, "3d": 13,
    "Ad": 14, "Kh": 15, "Ks": 16, "Kc": 17, "Qh": 18, "Qs": 19, "Qc": 20, "Jh": 21, "Js": 22, "Jc": 23, "Th": 24,
    "Ts": 25, "Kd": 26, "9s": 27, "9c": 28, "7c": 29, "8h": 30, "6c": 31, "2s": 32, "Tc": 33, "6s": 34, "As": 35,
    "3h": 36, "2c": 37, "5c": 38, "Ah": 39, "4h": 40, "3c": 41, "4c": 42, "7h": 43, "Ac": 44, "5s": 45, "5h": 46,
    "2h": 47, "7s": 48, "8c": 49, "9h": 50, "4s": 51, "8s": 52, "3s": 53,
}
BACK = 2
RANKS = "23456789TJQKA"
SUITS = "cdhs"
CARDS = [r + s for s in SUITS for r in RANKS]  # card index = suit * 13 + rank (the plugin's numbering)
COLS, ROWS = 10, 6
FACE_UV = (0.489, 0.0, 0.976, 0.779)  # the card in a face's texture (u0, v0, u1, v1; v from the top)
BACK_UV = (0.0, 0.0, 0.487, 0.779)


def load(path):
    with open(path, "rb") as f:
        f.read(12)
        n, _ = struct.unpack("<I4s", f.read(8))
        j = json.loads(f.read(n))
        n, _ = struct.unpack("<I4s", f.read(8))
        buf = f.read(n)

    def texture(mat):
        ti = j["materials"][mat]["pbrMetallicRoughness"]["baseColorTexture"]["index"]
        bv = j["bufferViews"][j["images"][j["textures"][ti]["source"]]["bufferView"]]
        o = bv.get("byteOffset", 0)
        return Image.open(io.BytesIO(buf[o:o + bv["byteLength"]])).convert("RGB")

    return texture


def atlas(glb, size):
    texture = load(glb)
    out = Image.new("RGB", (size, size), (255, 255, 255))
    for k, card in enumerate(CARDS + ["back"]):
        mat = BACK if card == "back" else MATERIAL[card]
        tex = texture(mat)
        u0, v0, u1, v1 = BACK_UV if card == "back" else FACE_UV
        face = tex.crop((round(u0 * tex.width), round(v0 * tex.height), round(u1 * tex.width), round(v1 * tex.height)))
        if card != "back":
            face = ImageOps.mirror(face)
        # (cell edges rounded to whole pixels; build_poker.py maps the same fractions)
        x0, x1 = round((k % COLS) * size / COLS), round((k % COLS + 1) * size / COLS)
        y0, y1 = round((k // COLS) * size / ROWS), round((k // COLS + 1) * size / ROWS)
        out.paste(face.resize((x1 - x0, y1 - y0), Image.LANCZOS), (x0, y0))
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--table" in sys.argv:
        args = [a for a in args if a != sys.argv[sys.argv.index("--table") + 1]]
    size = 2048
    if "--size" in sys.argv:
        size = int(sys.argv[sys.argv.index("--size") + 1])
        args = [a for a in args if a != str(size)]
    glb, data = args[0], args[1]
    img = atlas(glb, size)
    out = os.path.join(data, "Textures", "WastelandWagers")
    os.makedirs(out, exist_ok=True)
    save_dds(img, os.path.join(out, "cards_d.dds"), "BC7")
    if "--preview" in sys.argv:
        img.resize((size // 2, size // 2)).save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "cards_preview.png"))
    save_dds(Image.new("RGB", (16, 16), (60, 90, 0)), os.path.join(out, "card_s.dds"), "DXT1")  # a little sheen
    print("cards_d.dds", img.size)
    if "--table" in sys.argv:
        table(sys.argv[sys.argv.index("--table") + 1], out)


def table(src, out, size=1024):
    import numpy as np
    def tex(name):
        return Image.open(os.path.join(src, f"LOW_POLY_POKER_TABLE_{name}.png"))
    save_dds(tex("BaseColor").convert("RGB").resize((size, size), Image.LANCZOS), os.path.join(out, "poker_d.dds"), "BC7")
    save_dds(tex("Normal").convert("RGB").resize((size, size), Image.LANCZOS), os.path.join(out, "poker_n.dds"), "BC7")
    r = np.array(tex("Roughness").convert("L").resize((size, size), Image.LANCZOS), np.float32) / 255
    m = np.array(tex("Metallic").convert("L").resize((size, size), Image.LANCZOS), np.float32) / 255
    spec = np.clip(0.35 * (1 - r) + m * 0.8, 0, 1)
    gloss = np.clip(1 - r, 0, 1)
    s = np.stack([spec, gloss, np.zeros_like(spec)], -1)
    save_dds(Image.fromarray((s * 255).astype(np.uint8), "RGB"), os.path.join(out, "poker_s.dds"), "DXT1")
    print("poker_d/n/s.dds", size)


if __name__ == "__main__":
    main()
