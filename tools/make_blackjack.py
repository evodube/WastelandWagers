#!/usr/bin/env python3
"""The blackjack table's textures, 2048 each (Data/Textures/fo4roulette/):

- bjfelt_d / bjfelt_n: the felt, drawn here: baize with the seven betting circles and the card boxes in front of
  them, the insurance band, BLACKJACK PAYS 3 TO 2 and the dealer's rule, round the arc the players stand on
- bjwood_* / bjrail_*: the poker table's walnut and padded leather (make_poker_table.py), at 2048
- bjatlas_d: the model's own atlas (source_blackjack/atlas.png: the shoe, the chip rack, the odd flat colour), at 2048

    python tools/make_blackjack.py <out Data folder> [--preview]

Table space (build_blackjack.py): X across (the shoe at +X), Y towards the dealer (the chip rack at +Y), Z up, units
of 1.43 cm; the players' arc round C = (0, ARC_Y). The felt is mapped flat from above:
u = (x + FELT_X) / (2 * FELT_X), v = (FELT_Y1 - y) / (FELT_Y1 - FELT_Y0) (the dealer's side at the top).
Fonts: DejaVu Serif Condensed Bold, DejaVu Sans Bold (DejaVu's free licence).
"""

import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(__file__))
from dds import save_dds  # noqa: E402
from make_textures import felt_noise, felt_normal  # noqa: E402
import make_poker_table  # noqa: E402

HERE = os.path.dirname(__file__)
TITLE_FONT = os.path.join(HERE, "DejaVuSerifCondensed-Bold.ttf")
SANS_FONT = os.path.join(HERE, "DejaVuSans-Bold.ttf")

N = 2048
FELT_X = 74.0                 # the felt's extent (units): x +-FELT_X...
FELT_Y0, FELT_Y1 = -50.0, 54.0  # ...y FELT_Y0 .. FELT_Y1
S = 20                        # px a unit, drawing (then fitted to N x N)
ARC_Y = 24.08                 # the players' arc's middle (the circles and card boxes round it)
SPOT_R = 54.0                 # the betting circles...
CIRCLE_R = 3.35
CARD_R = 44.65                # ...and the card boxes
BOX_W, BOX_H = 5.4, 7.3
SPOT_ANGLES = None            # (worked out from SPOTS below)
# the seven places, left to right from the players' side (x, y of each circle; build_blackjack.py and the plugin's
# kBjSpots use the same)
SPOTS = [(-53.20, 14.76), (-43.34, -8.16), (-24.20, -24.20), (-0.05, -29.93), (24.24, -24.15), (43.38, -8.16), (53.24, 14.76)]

FELT = np.array((10, 92, 50), np.float32)   # the poker table's green
GOLD = (214, 178, 92, 255)
INK = (238, 234, 216, 255)


def xy(x, y):
    return ((x + FELT_X) * S, (FELT_Y1 - y) * S)


def arc_text(img, text, font, r, mid_deg, fill, spacing=1.0):
    """text round the arc's middle at radius r, centred on the angle mid_deg (table degrees: 0 +X, -90 towards the
    players), read from the players' side: left to right as x grows, the letters' tops towards the middle"""
    widths = [font.getlength(ch) for ch in text]
    total = sum(widths) * spacing
    a = math.radians(mid_deg) - (total / 2) / (r * S)
    for ch, w in zip(text, widths):
        step = w * spacing / (r * S)
        ang = a + step / 2
        a += step
        if ch == " ":
            continue
        x, y = r * math.cos(ang), ARC_Y + r * math.sin(ang)
        tile = Image.new("RGBA", (int(font.size * 2), int(font.size * 2)), (0, 0, 0, 0))
        ImageDraw.Draw(tile).text((tile.width / 2, tile.height / 2), ch, font=font, fill=fill, anchor="mm")
        # (upright for the players at -90; turned with the arc elsewhere)
        tile = tile.rotate(math.degrees(ang) + 90, resample=Image.BICUBIC)
        cx, cy = xy(x, y)
        img.alpha_composite(tile, (int(cx - tile.width / 2), int(cy - tile.height / 2)))


def felt() -> Image.Image:
    cw, ch = int(2 * FELT_X * S), int((FELT_Y1 - FELT_Y0) * S)
    n = felt_noise(1024, 7)
    n = np.array(Image.fromarray(((n + 4) * 32).clip(0, 255).astype(np.uint8)).resize((cw, ch), Image.BICUBIC), np.float32) / 32 - 4
    base = FELT * (1 + 0.07 * n[..., None])
    yy, xx = np.mgrid[0:ch, 0:cw].astype(np.float32)
    x = xx / S - FELT_X
    y = FELT_Y1 - yy / S
    rr = np.hypot(x, y - ARC_Y)
    base *= (1 - 0.12 * np.clip((rr - 60) / 14, 0, 1))[..., None]          # darker towards the rail
    base *= (1 + 0.05 * np.exp(-((rr - 48) / 10) ** 2))[..., None]          # paler where the hands are played
    img = Image.fromarray(base.clip(0, 255).astype(np.uint8), "RGB").convert("RGBA")
    d = ImageDraw.Draw(img, "RGBA")

    def arc(r, w, fill, a0=180, a1=360):
        cx, cy = xy(0, ARC_Y)
        # (PIL's angles go clockwise on the canvas, table angles anticlockwise: -a)
        d.arc([cx - r * S, cy - r * S, cx + r * S, cy + r * S], 360 - a1, 360 - a0, fill=fill, width=max(2, int(w * S)))

    arc(59.0, 0.28, GOLD, 188, 352)      # round the outside of the places
    arc(39.0, 0.22, GOLD, 196, 344)      # the insurance band
    arc(33.0, 0.22, GOLD, 196, 344)
    # the places: a betting circle (two rings, the inside darker) and the card box in front of it, towards the dealer
    for sx, sy in SPOTS:
        ang = math.atan2(sy - ARC_Y, sx)
        cx, cy = xy(sx, sy)
        for rad, col, wid in ((CIRCLE_R, INK, 0.22), (CIRCLE_R - 0.5, GOLD, 0.12)):
            d.ellipse([cx - rad * S, cy - rad * S, cx + rad * S, cy + rad * S], outline=col, width=max(2, int(wid * S)))
        inner = (CIRCLE_R - 0.7) * S
        d.ellipse([cx - inner, cy - inner, cx + inner, cy + inner], fill=tuple(int(c * 0.82) for c in FELT) + (255,))
        bx, by = CARD_R * math.cos(ang), ARC_Y + CARD_R * math.sin(ang)
        tile = Image.new("RGBA", (int(BOX_H * S * 1.6), int(BOX_H * S * 1.6)), (0, 0, 0, 0))
        td = ImageDraw.Draw(tile)
        tw, th = BOX_W * S, BOX_H * S
        ox, oy = (tile.width - tw) / 2, (tile.height - th) / 2
        td.rounded_rectangle([ox, oy, ox + tw, oy + th], radius=int(0.5 * S), outline=INK, width=max(2, int(0.2 * S)))
        tile = tile.rotate(math.degrees(ang) + 90, resample=Image.BICUBIC)
        px, py = xy(bx, by)
        img.alpha_composite(tile, (int(px - tile.width / 2), int(py - tile.height / 2)))
    # the words
    title = ImageFont.truetype(TITLE_FONT, int(2.9 * S))
    small = ImageFont.truetype(SANS_FONT, int(1.35 * S))
    band = ImageFont.truetype(SANS_FONT, int(2.0 * S))
    arc_text(img, "INSURANCE PAYS 2 TO 1", band, 35.0, -90, INK, 1.08)
    arc_text(img, "BLACKJACK PAYS 3 TO 2", title, 26.0, -90, GOLD, 1.04)
    arc_text(img, "DEALER MUST DRAW TO 16 AND STAND ON ALL 17S", small, 20.5, -90, INK, 1.06)
    return img.convert("RGB")


def atlas() -> Image.Image:
    a = Image.open(os.path.join(HERE, "source_blackjack", "atlas.png")).convert("RGB")
    return a.resize((N, N), Image.LANCZOS)


def main() -> int:
    out = os.path.join(sys.argv[1], "Textures", "fo4roulette")
    os.makedirs(out, exist_ok=True)
    big = felt()
    d = big.resize((N, N), Image.LANCZOS)
    save_dds(d, os.path.join(out, "bjfelt_d.dds"), "BC7")
    save_dds(felt_normal(1024).resize((N, N), Image.LANCZOS), os.path.join(out, "bjfelt_n.dds"), "BC7")
    make_poker_table.N = N
    for name, (dd, nn, ss) in (("bjwood", make_poker_table.wood()), ("bjrail", make_poker_table.leather())):
        save_dds(dd, os.path.join(out, f"{name}_d.dds"), "BC7")
        save_dds(nn, os.path.join(out, f"{name}_n.dds"), "BC7")
        save_dds(ss, os.path.join(out, f"{name}_s.dds"), "DXT1")
    save_dds(atlas(), os.path.join(out, "bjatlas_d.dds"), "BC7")
    if "--preview" in sys.argv:
        big.resize((big.width // 2, big.height // 2), Image.LANCZOS).save(os.path.join(out, "bjfelt_preview.png"))
    print("bjfelt_d", d.size)
    return 0


if __name__ == "__main__":
    sys.exit(main())
