#!/usr/bin/env python3
"""Writes fo4roulette's textures (Data/Textures/fo4roulette/*.dds) from the table model's maps.

    python tools/make_textures.py <model textures folder> <out Data folder>

- table:     the wood, brass and bowl (lambert1), converted as they are
- wheel_eu / wheel_us: the rotor (lambert2). Its pocket ring and number ring are drawn here, so the pockets sit
  exactly where the plugin expects them: pocket k of the wheel's sequence centred at 90 - k*360/N degrees in the
  rotor's own frame (k = 0 is the 0 pocket, at +Y), going clockwise seen from above
- layout_eu / layout_us: the felt (lambert3) with the betting layout drawn on it, at the positions in src/rl/Layout.h
- chip_1, chip_5, chip_25, chip_100, ball

Positions are in the model's units (the OBJ as imported in Blender, Z up), before the export scale.
"""

import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(__file__))
from dds import save_dds  # noqa: E402

FONT = os.path.join(os.path.dirname(__file__), "ChakraPetch-Bold.ttf")

EU = [0, 32, 15, 19, 4, 21, 2, 25, 17, 34, 6, 27, 13, 36, 11, 30, 8, 23, 10, 5, 24, 16, 33, 1, 20, 14, 31, 9, 22,
      18, 29, 7, 28, 12, 35, 3, 26]
US = [0, 28, 9, 26, 30, 11, 7, 20, 32, 17, 5, 22, 34, 15, 3, 24, 36, 13, 1, 37, 27, 10, 25, 29, 12, 8, 19, 31, 18, 6,
      21, 33, 16, 4, 23, 35, 14, 2]  # 37 = 00
RED = {1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36}

C_RED = (178, 22, 30)
C_BLACK = (22, 20, 20)
C_GREEN = (18, 112, 40)
C_WHITE = (238, 232, 218)
C_IVORY = (196, 186, 166)

# ---------------------------------------------------------------- wheel (rotor): planar UVs, from the model
WU = (-0.098188, 0.50677)   # u = a*x + b
WV = (-0.098444, 0.50515)   # v = a*y + b
POCKET_R = (2.83, 3.73)     # the sunken pocket ring
NUMBER_R = (3.78, 4.45)     # the sloped number ring outside it

# ---------------------------------------------------------------- layout (felt): planar UVs, from the model
LU = (-0.0246004, 0.795347)
LV = (-0.0246004, 0.390572)
# the betting layout, model units (src/rl/Layout.h has the same numbers x 6.6, from the table's origin)
LX0 = 10.2      # the zero column starts here (wheel side)
ZERO_W = 2.2
CELL_W = 1.45   # 12 number columns
COL_W = 1.8     # the 2-to-1 cells at the far end
LY0 = -3.0      # bottom of the row of 1, 4, 7... (the players' side is -Y)
ROW_H = 2.7
DOZ_H = 1.5
OUT_H = 1.5


def name(n: int) -> str:
    return "00" if n == 37 else str(n)


def colour(n: int):
    if n in (0, 37):
        return C_GREEN
    return C_RED if n in RED else C_BLACK


def font(size: float) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(FONT, max(6, int(size)))


def paste_text(img: Image.Image, text: str, centre, height_px: float, angle: float, fill, stretch: float = 1.0):
    """text centred at centre (pixels), its cap height ~height_px, rotated angle degrees counter-clockwise"""
    f = font(height_px * 1.35)
    l, t, r, b = f.getbbox(text)
    w, h = r - l + 8, b - t + 8
    tile = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(tile).text((4 - l, 4 - t), text, font=f, fill=fill)
    if stretch != 1.0:
        tile = tile.resize((max(1, int(w * stretch)), h), Image.LANCZOS)
    tile = tile.rotate(angle, resample=Image.BICUBIC, expand=True)
    img.alpha_composite(tile, (int(centre[0] - tile.width / 2), int(centre[1] - tile.height / 2)))


def wheel(base: Image.Image, seq: list[int], size: int) -> Image.Image:
    img = base.convert("RGBA").resize((size, size), Image.LANCZOS)
    n = len(seq)
    step = 360.0 / n
    # per pixel: model x, y, radius and angle
    px = (np.arange(size) + 0.5) / size
    u, v = np.meshgrid(px, px)
    v = 1.0 - v
    x = (u - WU[1]) / WU[0]
    y = (v - WV[1]) / WV[0]
    r = np.hypot(x, y)
    ang = np.degrees(np.arctan2(y, x))
    # sequence slot: pocket k spans 90 - (k + 0.5) step .. 90 - (k - 0.5) step
    k = np.floor(((90.0 - ang) / step + 0.5) % n).astype(int)
    within = ((90.0 - ang) / step + 0.5) % 1.0   # 0..1 across the slot
    arr = np.array(img).astype(np.float32)
    cols = np.array([colour(m) for m in seq], dtype=np.float32)
    slot_col = cols[k]

    # number ring: wedges in the pocket colour, a thin brass line between them
    ring = (r >= NUMBER_R[0] - 0.05) & (r <= NUMBER_R[1] + 0.08)
    edge = (within < 0.035) | (within > 0.965)
    arr[ring, :3] = slot_col[ring]
    arr[ring & edge, :3] = (176, 140, 74)
    # pocket ring: ivory base, the pocket floor coloured, frets (ivory) between
    pr = (r >= POCKET_R[0] - 0.04) & (r <= POCKET_R[1] + 0.04)
    arr[pr, :3] = C_IVORY
    floor = pr & (within > 0.11) & (within < 0.89) & (r > POCKET_R[0] + 0.07) & (r < POCKET_R[1] - 0.07)
    shade = 0.82 + 0.18 * np.clip((r - POCKET_R[0]) / (POCKET_R[1] - POCKET_R[0]), 0, 1)
    arr[floor, :3] = slot_col[floor] * shade[floor, None]
    # a dark line where the rings meet
    seam = (r > POCKET_R[1] + 0.04) & (r < NUMBER_R[0] - 0.05)
    arr[seam, :3] = (60, 46, 30)
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")

    ppu = abs(WU[0]) * size  # pixels per model unit
    rt = (NUMBER_R[0] + NUMBER_R[1]) / 2
    for i, m in enumerate(seq):
        phi = 90.0 - i * step
        cx = (WU[0] * rt * math.cos(math.radians(phi)) + WU[1]) * size
        cy = (1.0 - (WV[0] * rt * math.sin(math.radians(phi)) + WV[1])) * size
        txt = name(m)
        width = (2 * math.pi * rt / n) * ppu * 0.78
        hgt = (NUMBER_R[1] - NUMBER_R[0]) * ppu * 0.55
        f = font(hgt * 1.35)
        l, t, rr, b = f.getbbox(txt)
        stretch = min(1.0, width / max(1, rr - l))
        paste_text(img, txt, (cx, cy), hgt, phi + 90.0, C_WHITE + (255,), stretch)
    return img.convert("RGB")


def layout(base: Image.Image, american: bool, size: int) -> Image.Image:
    # work in a view from above (x right, y up), which is the texture turned 180 degrees
    img = base.convert("RGB").resize((size, size), Image.LANCZOS).rotate(180)
    k = abs(LU[0]) * size                      # pixels per model unit
    xmin = (1.0 - LU[1]) / LU[0]               # u = 1
    ymax = (0.0 - LV[1]) / LV[0]               # v = 0

    def P(x, y):
        return ((x - xmin) * k, (ymax - y) * k)

    # clear the original layout: felt copied from an empty stretch of the same texture
    x1 = LX0 + ZERO_W + 12 * CELL_W + COL_W
    y_bottom = LY0 - DOZ_H - OUT_H
    y_top = LY0 + 3 * ROW_H
    a = P(LX0 - 0.6, y_top + 0.8)
    b = P(x1 + 0.6, y_bottom - 0.8)
    box = (int(a[0]), int(a[1]), int(b[0]), int(b[1]))
    src_y = int(P(0, -12.0)[1])
    patch = img.crop((box[0], src_y, box[2], src_y + box[3] - box[1]))
    img.paste(patch, box[:2])

    d = ImageDraw.Draw(img, "RGBA")
    line = (232, 226, 205)
    lw = max(2, int(0.06 * k))

    def rect(x0, y0, x1_, y1, fill=None):
        p0, p1 = P(x0, y1), P(x1_, y0)
        d.rectangle([p0, p1], fill=fill, outline=line, width=lw)

    def label(text, x, y, h, angle=0.0, fill=(240, 234, 214, 255), width=None):
        c = P(x, y)
        stretch = 1.0
        if width:
            f = font(h * k * 1.35)
            l, t, r, b2 = f.getbbox(text)
            stretch = min(1.0, width * k / max(1, r - l))
        paste_text(img_rgba, text, c, h * k, angle, fill, stretch)

    # zero(s)
    zx = LX0
    if american:
        rect(zx, LY0, zx + ZERO_W, LY0 + 1.5 * ROW_H)
        rect(zx, LY0 + 1.5 * ROW_H, zx + ZERO_W, y_top)
    else:
        rect(zx, LY0, zx + ZERO_W, y_top)
    nx0 = LX0 + ZERO_W
    for c in range(12):
        for rrow in range(3):
            rect(nx0 + c * CELL_W, LY0 + rrow * ROW_H, nx0 + (c + 1) * CELL_W, LY0 + (rrow + 1) * ROW_H)
    for rrow in range(3):
        rect(nx0 + 12 * CELL_W, LY0 + rrow * ROW_H, x1, LY0 + (rrow + 1) * ROW_H)
    for dz in range(3):
        rect(nx0 + dz * 4 * CELL_W, LY0 - DOZ_H, nx0 + (dz + 1) * 4 * CELL_W, LY0)
    for o in range(6):
        rect(nx0 + o * 2 * CELL_W, y_bottom, nx0 + (o + 1) * 2 * CELL_W, LY0 - DOZ_H)

    img_rgba = img.convert("RGBA")
    d = ImageDraw.Draw(img_rgba, "RGBA")
    # numbers: an oval in the number's colour, the number on it, upright for the players (-Y side)
    for c in range(12):
        for rrow in range(3):
            num = 3 * c + rrow + 1
            cx = nx0 + (c + 0.5) * CELL_W
            cy = LY0 + (rrow + 0.5) * ROW_H
            e0 = P(cx - CELL_W * 0.36, cy + ROW_H * 0.36)
            e1 = P(cx + CELL_W * 0.36, cy - ROW_H * 0.36)
            d.ellipse([e0, e1], fill=colour(num) + (255,))
            label(str(num), cx, cy, 0.55, 0.0, width=CELL_W * 0.62)
    if american:
        label("0", zx + ZERO_W / 2, LY0 + 0.75 * ROW_H, 0.9, 0.0)
        label("00", zx + ZERO_W / 2, LY0 + 2.25 * ROW_H, 0.9, 0.0)
    else:
        label("0", zx + ZERO_W / 2, LY0 + 1.5 * ROW_H, 1.0, 0.0)
    for rrow in range(3):
        label("2 to 1", nx0 + 12 * CELL_W + COL_W / 2, LY0 + (rrow + 0.5) * ROW_H, 0.42, 90.0, width=ROW_H * 0.8)
    for dz, t in enumerate(("1st 12", "2nd 12", "3rd 12")):
        label(t, nx0 + (dz * 4 + 2) * CELL_W, LY0 - DOZ_H / 2, 0.6, 0.0)
    outs = ("1-18", "EVEN", None, None, "ODD", "19-36")
    for o, t in enumerate(outs):
        cx = nx0 + (o * 2 + 1) * CELL_W
        cy = y_bottom + OUT_H / 2
        if t:
            label(t, cx, cy, 0.6, 0.0)
        else:
            col = C_RED if o == 2 else C_BLACK
            pts = [P(cx - 0.9, cy), P(cx, cy + 0.45), P(cx + 0.9, cy), P(cx, cy - 0.45)]
            d.polygon(pts, fill=col + (255,), outline=line, width=max(1, lw // 2))
    return img_rgba.convert("RGB").rotate(180)


def chip(value: int, col, size: int = 512) -> Image.Image:
    """the chip's face over the whole square; the side wall's UVs sit on the outer rim"""
    img = Image.new("RGB", (size, size), col)
    d = ImageDraw.Draw(img)
    c = size / 2
    rim = size * 0.5
    # edge spots: 6 white blocks round the rim
    for i in range(6):
        a0 = i * 60 - 12
        d.pieslice([c - rim, c - rim, c + rim, c + rim], a0, a0 + 24, fill=(236, 232, 222))
    d.ellipse([c - rim * 0.80, c - rim * 0.80, c + rim * 0.80, c + rim * 0.80], fill=col)
    d.ellipse([c - rim * 0.70, c - rim * 0.70, c + rim * 0.70, c + rim * 0.70], outline=(236, 232, 222),
              width=int(size * 0.012))
    ink = (20, 20, 20) if col[0] > 200 and col[1] > 200 else (240, 236, 226)
    f = font(size * 0.30)
    t = str(value)
    l, tt, r, b = f.getbbox(t)
    d.text((c - (l + r) / 2, c - (tt + b) / 2), t, font=f, fill=ink)
    return img.filter(ImageFilter.SMOOTH)


CHIPS = {1: (226, 222, 210), 5: (172, 28, 34), 25: (28, 120, 52), 100: (24, 24, 26)}


def spec_from(rough: Image.Image, metal: Image.Image, size: int, base: float = 0.35) -> Image.Image:
    r = np.array(rough.convert("L").resize((size, size), Image.LANCZOS), dtype=np.float32) / 255
    m = np.array(metal.convert("L").resize((size, size), Image.LANCZOS), dtype=np.float32) / 255
    spec = np.clip(base * (1 - r) + m * 0.8, 0, 1)
    gloss = np.clip(1 - r, 0, 1)
    out = np.stack([spec, gloss, np.zeros_like(spec)], axis=-1)
    return Image.fromarray((out * 255).astype(np.uint8), "RGB")


def flat(size=64, col=(128, 128, 255)) -> Image.Image:
    return Image.new("RGB", (size, size), col)


def main() -> int:
    src, data = sys.argv[1], sys.argv[2]
    out = os.path.join(data, "Textures", "fo4roulette")
    os.makedirs(out, exist_ok=True)

    def load(n):
        for ext in (".jpeg", ".jpg", ".png"):
            p = os.path.join(src, n + ext)
            if os.path.exists(p):
                return Image.open(p)
        raise FileNotFoundError(n)

    def put(img, n, fmt="DXT1"):
        save_dds(img, os.path.join(out, n + ".dds"), fmt)
        img.convert("RGB").resize((min(img.width, 1024),) * 2).save(os.path.join(out, n + "_preview.png")) if PREVIEW else None
        print("wrote", n)

    # table wood, brass and bowl
    put(load("lambert1SG_albedo").resize((2048, 2048), Image.LANCZOS), "table_d")
    put(load("lambert1SG_normal").resize((2048, 2048), Image.LANCZOS), "table_n")
    put(spec_from(load("lambert1SG_roughness"), load("lambert1SG_metallic"), 1024), "table_s")
    # wheels
    wbase = load("lambert2SG_albedo")
    put(wheel(wbase, EU, 2048), "wheel_eu_d")
    put(wheel(wbase, US, 2048), "wheel_us_d")
    put(flat(), "flat_n")
    put(spec_from(load("lambert2SG_roughness"), load("lambert2SG_metallic"), 512, 0.5), "wheel_s")
    # layouts
    lbase = load("lambert3SG_albedo")
    put(layout(lbase, False, 2048), "layout_eu_d")
    put(layout(lbase, True, 2048), "layout_us_d")
    put(flat(64, (10, 40, 0)), "felt_s")
    # chips and ball
    for v, col in CHIPS.items():
        put(chip(v, col), f"chip_{v}_d")
    put(flat(64, (90, 200, 0)), "chip_s")
    put(flat(64, (244, 240, 230)), "ball_d")
    put(flat(64, (220, 230, 0)), "ball_s")
    return 0


PREVIEW = "--preview" in sys.argv
if __name__ == "__main__":
    sys.argv = [a for a in sys.argv if a != "--preview"]
    sys.exit(main())
