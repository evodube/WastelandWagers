#!/usr/bin/env python3
"""Writes the poker table's felt (Data/Textures/fo4roulette/pokerfelt_d.dds, pokerfelt_n.dds): green baize with a
line round it and TEXAS HOLD'EM on it, once facing the players' side and once the dealer's, a card suit where the
line turns round each end.

    python tools/make_poker_felt.py <out Data folder> [--preview]

The felt (build_poker_felt.py splits it off the table into its own shape) is mapped flat from above, table space
(X across, the dealer at +X; Y along): u = (HALF_L - y) / (2 * HALF_L), v = (x + HALF_W) / (2 * HALF_W). Fonts:
DejaVu Serif Condensed Bold (the title) and DejaVu Sans Bold (the suits), DejaVu's free licence.
"""

import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(__file__))
from dds import save_dds  # noqa: E402
from make_textures import felt_noise, felt_normal  # noqa: E402

HERE = os.path.dirname(__file__)
TITLE_FONT = os.path.join(HERE, "DejaVuSerifCondensed-Bold.ttf")
SUIT_FONT = os.path.join(HERE, "DejaVuSans-Bold.ttf")

HALF_W, HALF_L = 20.25, 63.5   # the felt's extent (table units), its straight sides to +-STRAIGHT
STRAIGHT = 43.0
INSET = 3.4                    # the line, in from the edge
LINE_W = 0.3
PLAYERS_TITLE_Y = 21.0       # the title on the players' side, along from the middle seat (at y 0)
W, H = 1024, 512               # u along the table's length, v across it
S = 32                         # px a unit, drawing (then shrunk to W x H)

FELT = np.array((10, 92, 50), np.float32)   # casino green
INK = (234, 230, 212, 255)                   # the print: cream


def canvas_xy(x, y):
    """table space to the drawing as it's seen from above, from the players' side (-X): the dealer's side (+X) at the
    top, +Y to the left"""
    return ((HALF_L - y) * S, (HALF_W - x) * S)


def felt() -> Image.Image:
    cw, ch = int(2 * HALF_L * S), int(2 * HALF_W * S)
    n = felt_noise(1024, 5)
    n = np.array(Image.fromarray(((n + 4) * 32).clip(0, 255).astype(np.uint8)).resize((cw, ch), Image.BICUBIC), np.float32) / 32 - 4
    base = FELT * (1 + 0.07 * n[..., None])
    # a little darker towards the rail, and paler where hands and chips go
    yy, xx = np.mgrid[0:ch, 0:cw].astype(np.float32)
    x = HALF_W - yy / S
    y = HALF_L - xx / S
    inside = np.where(np.abs(y) <= STRAIGHT, HALF_W - np.abs(x), HALF_W - np.hypot(x, np.abs(y) - STRAIGHT))
    base *= (1 - 0.14 * np.exp(-np.clip(inside, 0, None) / 2.5))[..., None]
    base *= (1 + 0.05 * np.exp(-((x + 8) / 9) ** 2 - (y / 40) ** 2))[..., None]
    img = Image.fromarray(base.clip(0, 255).astype(np.uint8), "RGB").convert("RGBA")
    d = ImageDraw.Draw(img, "RGBA")

    # the line: the stadium, INSET in
    r = HALF_W - INSET
    lw = max(2, int(LINE_W * S))
    gap = 2.0  # (the line stops short of each suit)
    for sx in (-1, 1):
        d.line([canvas_xy(sx * r, -STRAIGHT + gap), canvas_xy(sx * r, STRAIGHT - gap)], fill=INK, width=lw)
    trim = math.degrees(math.asin(gap / r))
    for sy in (-1, 1):
        cx, cy = canvas_xy(0, sy * STRAIGHT)
        box = [cx - r * S, cy - r * S, cx + r * S, cy + r * S]
        # (canvas angles, clockwise from +y of the table)
        if cx < HALF_L * S:  # (the end on the left)
            d.arc(box, 90 + trim, 270 - trim, fill=INK, width=lw)
        else:
            d.arc(box, -90 + trim, 90 - trim, fill=INK, width=lw)

    # the suits where the line turns, in the gaps left in it
    sf = ImageFont.truetype(SUIT_FONT, int(2.6 * S))
    for (sx, sy, suit, col) in ((-1, -1, "♠", INK), (-1, 1, "♥", INK), (1, 1, "♦", INK), (1, -1, "♣", INK)):
        cx, cy = canvas_xy(sx * r, sy * STRAIGHT)
        tile = Image.new("RGBA", (int(4 * S), int(4 * S)), (0, 0, 0, 0))
        td = ImageDraw.Draw(tile)
        td.text((tile.width / 2, tile.height / 2), suit, font=sf, fill=col, anchor="mm")
        if sx > 0:
            tile = tile.rotate(180)
        img.alpha_composite(tile, (int(cx - tile.width / 2), int(cy - tile.height / 2)))

    # TEXAS HOLD'EM on the line on each long side: a cream plaque, the letters in the felt's green; upright for whoever
    # sits on that side
    tf = ImageFont.truetype(TITLE_FONT, int(2.3 * S))
    text = "TEXAS HOLD'EM"
    l, t, rr, b = tf.getbbox(text)
    tw, th = rr - l, b - t
    pw, ph = tw + int(1.6 * S), th + int(1.0 * S)
    plaque = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
    pd = ImageDraw.Draw(plaque)
    pd.rectangle([0, 0, pw - 1, ph - 1], fill=INK)
    pd.text((pw / 2, ph / 2), text, font=tf, fill=tuple(int(c) for c in FELT * 0.9) + (255,), anchor="mm")
    # (the dealer's side: in the middle, between the deck and the muck. The players' side: off to one side, clear of
    # the middle seat's cards and chips)
    for sx, y in ((-1, PLAYERS_TITLE_Y), (1, 0.0)):
        cx, cy = canvas_xy(sx * r, y)
        tile = plaque if sx < 0 else plaque.rotate(180)
        img.alpha_composite(tile, (int(cx - tile.width / 2), int(cy - tile.height / 2)))
    return img.convert("RGB")


def main() -> int:
    out = os.path.join(sys.argv[1], "Textures", "fo4roulette")
    os.makedirs(out, exist_ok=True)
    big = felt()
    d = big.resize((W, H), Image.LANCZOS)
    save_dds(d, os.path.join(out, "pokerfelt_d.dds"), "BC7")
    nm = felt_normal(1024).resize((W, H), Image.LANCZOS)
    save_dds(nm, os.path.join(out, "pokerfelt_n.dds"), "BC7")
    if "--preview" in sys.argv:
        big.resize((2032, 648), Image.LANCZOS).save(os.path.join(out, "pokerfelt_preview.png"))
    print(f"pokerfelt_d {d.size}, pokerfelt_n {nm.size} ({tw_info()})")
    return 0


def tw_info():
    return f"{2 * HALF_L} x {2 * HALF_W} units"


if __name__ == "__main__":
    sys.exit(main())
