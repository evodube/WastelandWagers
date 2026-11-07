#!/usr/bin/env python3
"""Writes fo4roulette's textures (Data/Textures/fo4roulette/*.dds) from the table model's maps.

    python tools/make_textures.py <model textures folder> <out Data folder>     (the model's .obj next to it, in ../model)

- table:     the wood and bowl (lambert1): a sharp solid wood grain over the model's own colours (wood.py); its normal map
- wheel_eu / wheel_us: the rotor's disc (build_meshes.py rebuilds the rotor, with planar UVs): pocket floors and
  the number ring, pocket k of the wheel's sequence centred at 90 - k*360/N degrees in the rotor's own frame (k = 0
  is the 0 pocket, at +Y), going clockwise seen from above; metal_*: the cone, frets and turret (nickel)
- layout_eu / layout_us: new felt with the betting layout printed on it, at the positions in src/rl/Layout.h
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
C_METAL = (150, 152, 158)   # the metal lines painted on the disc
C_STEEL = (70, 67, 61)      # polished nickel, darkened with age: warm, dark; the shine is its reflections and highlights

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


# ---------------------------------------------------------------- the rotor, built by build_meshes.py
# Its disc (pocket floors, the lip, the number ring) has planar UVs from above: u = 0.5 + x / (2 * ROTOR_UV),
# v = 0.5 + y / (2 * ROTOR_UV), model units, the wheel's centre at 0. The cone, the frets and the turret are metal.
ROTOR_UV = 4.5
POCKET_FLOOR = (2.83, 3.725)
LIP = (3.725, 3.784)
RING = (3.784, 4.45)


def rotor_disc(seq: list[int], size: int, ss: int = 2) -> Image.Image:
    """the disc's texture: drawn ss times bigger and scaled down, so every edge is smooth"""
    S = size * ss
    n = len(seq)
    step = 360.0 / n
    c = (np.arange(S) + 0.5) / S * 2 * ROTOR_UV - ROTOR_UV
    x, y = np.meshgrid(c, -c)  # row 0 is the image's top: v = 1, +y
    r = np.hypot(x, y)
    ang = np.degrees(np.arctan2(y, x))
    rel = ((90.0 - ang) / step + 0.5) % n          # pocket index + where in it (clockwise from above)
    k = np.floor(rel).astype(int) % n
    within = rel % 1.0
    cols = np.array([colour(m) for m in seq], dtype=np.float32)
    slot = cols[k]
    out = np.zeros((S, S, 3), np.float32) + np.array(C_METAL, np.float32)

    # pocket floors: the pocket's colour, a little darker towards the walls and the frets (soft, the same in every pocket)
    fl = (r >= POCKET_FLOOR[0]) & (r < POCKET_FLOOR[1])
    across = np.clip((r - POCKET_FLOOR[0]) / (POCKET_FLOOR[1] - POCKET_FLOOR[0]), 0, 1)
    edge = np.minimum(np.minimum(across, 1 - across) * 6.0, 1.0) * np.minimum(np.minimum(within, 1 - within) * 8.0, 1.0)
    shade = 0.72 + 0.28 * np.sqrt(edge)
    out[fl] = slot[fl] * shade[fl, None]
    # the number ring: the pocket's colour, thin metal lines between and a metal edge each side
    ring = (r >= RING[0]) & (r < RING[1])
    out[ring] = slot[ring]
    line_w = 0.025 / (2 * math.pi * r / n)                 # 0.025 model units, as a fraction of a pocket
    sep = ring & ((within < line_w) | (within > 1 - line_w))
    out[sep] = C_METAL
    rim = ring & ((r < RING[0] + 0.035) | (r > RING[1] - 0.04))
    out[rim] = C_METAL

    # the numbers on a strip that's the ring unrolled (across = round the wheel, clockwise seen from above; down =
    # inwards: the tops of the numbers face out), picked up by each pixel of the ring
    W, H = 16384, 512
    r0, r1 = RING[0] + 0.06, RING[1] - 0.07
    strip = Image.new("L", (W, H), 0)
    rm = (r0 + r1) / 2
    along = W / (2 * math.pi * rm)
    down = H / (r1 - r0)
    f = font(H * 0.72 * 1.35)
    for i, m in enumerate(seq):
        txt = name(m)
        l, tt, rr, b = f.getbbox(txt)
        tile = Image.new("L", (rr - l + 8, b - tt + 8), 0)
        ImageDraw.Draw(tile).text((4 - l, 4 - tt), txt, font=f, fill=255)
        want_w = min(tile.width * along / down, 2 * math.pi * rm / n * along * 0.72)
        tile = tile.resize((max(1, int(want_w)), int(H * 0.78)), Image.LANCZOS)
        cxp = ((-(90.0 - i * step)) % 360.0) / 360.0 * W
        for dx in (0, -W, W):
            strip.paste(tile, (int(cxp - tile.width / 2 + dx), int(H * 0.11)), tile)
    sa = np.array(strip, dtype=np.float32) / 255.0
    sx = (((-ang) % 360.0) / 360.0 * W).astype(int) % W
    sy = np.clip(((r1 - r) * down).astype(int), 0, H - 1)
    a = sa[sy, sx]
    a[~(ring & (r >= r0) & (r <= r1))] = 0.0
    out = out * (1 - a[..., None]) + np.array(C_WHITE, np.float32) * a[..., None]
    img = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "RGB")
    return img.resize((size, size), Image.LANCZOS)


def rotor_spec(size: int = 1024) -> Image.Image:
    """the disc's specular (R) and gloss (G): a lacquered wheel, the metal lines and edges shinier"""
    c = (np.arange(size) + 0.5) / size * 2 * ROTOR_UV - ROTOR_UV
    x, y = np.meshgrid(c, -c)
    r = np.hypot(x, y)
    spec = np.full((size, size), 0.45, np.float32)
    gloss = np.full((size, size), 0.70, np.float32)
    metal = (r < POCKET_FLOOR[0]) | ((r >= LIP[0]) & (r < LIP[1])) | (r > RING[1] - 0.04)
    spec[metal], gloss[metal] = 0.9, 0.85
    out = np.stack([spec, gloss, np.zeros_like(spec)], -1)
    return Image.fromarray((out * 255).astype(np.uint8), "RGB")


def metal(size: int = 512, seed: int = 7) -> tuple[Image.Image, Image.Image]:
    """dark polished nickel (the cone, frets, turret, diamonds): faint brushing, fingerprints and smudges, fine
    scratches; its specular map (the smudges duller)"""
    rng = np.random.default_rng(seed)
    streaks = rng.normal(0, 1, (size, 1)) * 0.8 + rng.normal(0, 1, (size, size)) * 0.3
    streaks = np.array(Image.fromarray(((streaks + 3) * 40).clip(0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6)),
                       np.float32) / 40 - 3
    smudge = np.array(Image.fromarray((rng.random((size // 16, size // 16)) * 255).astype(np.uint8)).resize((size, size), Image.BICUBIC)
                      .filter(ImageFilter.GaussianBlur(size / 64)), np.float32) / 255.0
    smudge = np.clip((smudge - 0.45) * 2.5, 0, 1)
    lines = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(lines)
    for _ in range(size // 3):
        x0, y0 = rng.random() * size, rng.random() * size
        a = rng.random() * np.pi
        n = rng.random() * size / 12 + 4
        d.line([x0, y0, x0 + np.cos(a) * n, y0 + np.sin(a) * n], fill=int(40 + rng.random() * 110), width=1)
    scratch = np.array(lines.filter(ImageFilter.GaussianBlur(0.4)), np.float32) / 255.0
    base = np.array(C_STEEL, np.float32) * (1 + 0.05 * streaks[..., None])
    base = base * (1 - 0.18 * smudge[..., None]) + np.array([150, 146, 138], np.float32) * 0.35 * scratch[..., None]
    dimg = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8), "RGB")
    spec = 210 * (1 - 0.35 * smudge) + 30 * scratch
    gloss = 200 * (1 - 0.45 * smudge) - 40 * scratch
    s = Image.fromarray(np.dstack([spec.clip(0, 255).astype(np.uint8), gloss.clip(0, 255).astype(np.uint8),
                                   np.zeros((size, size), np.uint8)]), "RGB")
    return dimg, s


FELT = (17, 66, 22)   # baize green


def felt_noise(size: int, seed: int) -> np.ndarray:
    """fine fuzz plus broad, flowing changes of shade, about 0 +- 1"""
    rng = np.random.default_rng(seed)
    fine = rng.normal(0, 1, (size, size)).astype(np.float32)
    fine = np.array(Image.fromarray(((fine + 4) * 32).clip(0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.7)),
                    np.float32) / 32 - 4
    low = rng.normal(0, 1, (6, 6)).astype(np.float32)
    low = np.array(Image.fromarray(((low + 4) * 32).clip(0, 255).astype(np.uint8)).resize((size, size), Image.BICUBIC)
                   .filter(ImageFilter.GaussianBlur(size / 24)), np.float32) / 32 - 4
    return fine * 0.9 + low * 1.2  # fuzz, and broad, smooth changes of shade (no blotches)


def felt_normal(size: int, seed: int = 11) -> Image.Image:
    """the cloth's weave and fuzz as a normal map"""
    rng = np.random.default_rng(seed)
    h = rng.normal(0, 1, (size, size)).astype(np.float32)
    h = np.array(Image.fromarray(((h + 4) * 32).clip(0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.8)), np.float32) / 32
    yy, xx = np.mgrid[0:size, 0:size]
    h += 0.35 * np.sin(xx * 2.1) * np.sin(yy * 2.1)  # a faint weave
    gx = np.roll(h, -1, 1) - np.roll(h, 1, 1)
    gy = np.roll(h, -1, 0) - np.roll(h, 1, 0)
    k = 0.9
    nx, ny, nz = -gx * k, gy * k, np.ones_like(h)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    rgb = np.stack([nx / ln, ny / ln, nz / ln], -1) * 127.5 + 127.5
    return Image.fromarray(rgb.clip(0, 255).astype(np.uint8), "RGB")


def layout(american: bool, size: int, ss: int = 3) -> Image.Image:
    """the felt (all of it new: the model's own has stretched streaks) with the layout printed on it"""
    noise = felt_noise(size, 3)
    felt = np.array(FELT, np.float32) * (1 + 0.06 * noise[..., None])
    # worn: the nap rubbed paler and a little greyer where hands and chips go (round the layout), the edges grubbier
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32) / size
    kk = abs(LU[0])
    lx0 = ((LX0 - (1.0 - LU[1]) / LU[0]) * kk, (((0.0 - LV[1]) / LV[0]) - (LY0 + 1.5 * ROW_H)) * kk)
    cx, cy = lx0[0] + (ZERO_W + 6 * CELL_W) * kk, lx0[1]
    rub = np.exp(-(((xx - cx) / 0.22) ** 2 + ((yy - cy) / 0.12) ** 2))
    grey = felt.mean(-1, keepdims=True)
    felt = felt * (1 + 0.10 * rub[..., None]) * (1 - 0.25 * rub[..., None]) + grey * 0.25 * rub[..., None]
    edge = np.minimum(np.minimum(xx, 1 - xx), np.minimum(yy, 1 - yy))
    felt *= (1 - 0.10 * np.exp(-edge / 0.06))[..., None]  # grubbier towards the rail
    img = Image.fromarray(felt.clip(0, 255).astype(np.uint8), "RGB")  # in the view from above (x right, y up)

    k = abs(LU[0]) * size                      # pixels per model unit
    xmin = (1.0 - LU[1]) / LU[0]               # u = 1
    ymax = (0.0 - LV[1]) / LV[0]               # v = 0

    def P(x, y):
        return ((x - xmin) * k, (ymax - y) * k)

    x1 = LX0 + ZERO_W + 12 * CELL_W + COL_W
    y_bottom = LY0 - DOZ_H - OUT_H
    y_top = LY0 + 3 * ROW_H
    a, b = P(LX0 - 0.5, y_top + 0.5), P(x1 + 0.5, y_bottom - 0.5)
    box = (int(a[0]), int(a[1]), int(b[0]) + 1, int(b[1]) + 1)
    bw, bh = box[2] - box[0], box[3] - box[1]
    ink = Image.new("RGBA", (bw * ss, bh * ss), (0, 0, 0, 0))   # the print, ss times bigger
    d = ImageDraw.Draw(ink, "RGBA")
    K = k * ss

    def Q(x, y):
        px, py = P(x, y)
        return ((px - box[0]) * ss, (py - box[1]) * ss)

    line = (226, 220, 198, 255)
    lw = max(2, int(round(0.055 * K)))

    def rect(x0, y0, x1_, y1):
        p0, p1 = Q(x0, y1), Q(x1_, y0)
        d.rectangle([p0, p1], outline=line, width=lw)

    def label(text, x, y, h, angle=0.0, fill=(236, 230, 210, 255), width=None):
        stretch = 1.0
        if width:
            f = font(h * K * 1.35)
            l, tt, r, b2 = f.getbbox(text)
            stretch = min(1.0, width * K / max(1, r - l))
        paste_text(ink, text, Q(x, y), h * K, angle, fill, stretch)

    zx = LX0
    nx0 = LX0 + ZERO_W
    # numbers: an oval in the number's colour, the number on it
    for c in range(12):
        for rrow in range(3):
            num = 3 * c + rrow + 1
            cx = nx0 + (c + 0.5) * CELL_W
            cy = LY0 + (rrow + 0.5) * ROW_H
            e0 = Q(cx - CELL_W * 0.34, cy + ROW_H * 0.36)
            e1 = Q(cx + CELL_W * 0.34, cy - ROW_H * 0.36)
            d.ellipse([e0, e1], fill=colour(num) + (255,))
            label(str(num), cx, cy, 0.55, 0.0, width=CELL_W * 0.6)
    if american:
        rect(zx, LY0, zx + ZERO_W, LY0 + 1.5 * ROW_H)
        rect(zx, LY0 + 1.5 * ROW_H, zx + ZERO_W, y_top)
        label("0", zx + ZERO_W / 2, LY0 + 0.75 * ROW_H, 0.9, 0.0)
        label("00", zx + ZERO_W / 2, LY0 + 2.25 * ROW_H, 0.9, 0.0)
    else:
        rect(zx, LY0, zx + ZERO_W, y_top)
        label("0", zx + ZERO_W / 2, LY0 + 1.5 * ROW_H, 1.0, 0.0)
    for c in range(12):
        for rrow in range(3):
            rect(nx0 + c * CELL_W, LY0 + rrow * ROW_H, nx0 + (c + 1) * CELL_W, LY0 + (rrow + 1) * ROW_H)
    for rrow in range(3):
        rect(nx0 + 12 * CELL_W, LY0 + rrow * ROW_H, x1, LY0 + (rrow + 1) * ROW_H)
        label("2 to 1", nx0 + 12 * CELL_W + COL_W / 2, LY0 + (rrow + 0.5) * ROW_H, 0.42, 90.0, width=ROW_H * 0.8)
    for dz, tx in enumerate(("1st 12", "2nd 12", "3rd 12")):
        rect(nx0 + dz * 4 * CELL_W, LY0 - DOZ_H, nx0 + (dz + 1) * 4 * CELL_W, LY0)
        label(tx, nx0 + (dz * 4 + 2) * CELL_W, LY0 - DOZ_H / 2, 0.6, 0.0)
    outs = ("1-18", "EVEN", None, None, "ODD", "19-36")
    for o, tx in enumerate(outs):
        rect(nx0 + o * 2 * CELL_W, y_bottom, nx0 + (o + 1) * 2 * CELL_W, LY0 - DOZ_H)
        cx = nx0 + (o * 2 + 1) * CELL_W
        cy = y_bottom + OUT_H / 2
        if tx:
            label(tx, cx, cy, 0.6, 0.0)
        else:
            col = C_RED if o == 2 else C_BLACK
            pts = [Q(cx - 0.9, cy), Q(cx, cy + 0.45), Q(cx + 0.9, cy), Q(cx, cy - 0.45)]
            d.polygon(pts, fill=col + (255,), outline=line, width=max(1, lw // 2))

    # down to size, and printed onto the cloth: the ink takes the felt's fuzz too
    ink = ink.resize((bw, bh), Image.LANCZOS)
    ia = np.array(ink, np.float32) / 255.0
    region = np.array(img.crop(box), np.float32)
    fz = 1 + 0.06 * noise[box[1]:box[3], box[0]:box[2], None]
    alpha = ia[..., 3:4] * 0.95
    region = region * (1 - alpha) + ia[..., :3] * 255.0 * fz * alpha
    img.paste(Image.fromarray(region.clip(0, 255).astype(np.uint8), "RGB"), box[:2])
    return img.rotate(180)


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
CHIP_COLOURS = ["00794e", "030303", "060606", "e7e7e7", "ff463c"]  # build_chips.py's result


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
    # the wood: a sharp solid grain over the model's own colours (wood.py), at full size and in BC7 (DXT1 at 2048
    # made it blotchy)
    import glob
    from wood import wood
    obj = (glob.glob(os.path.join(src, "..", "model", "*.obj")) + glob.glob(os.path.join(src, "*.obj")))[0]
    put(wood(load("lambert1SG_albedo"), obj, SIZE, load("lambert1SG_normal")), "table_d", "BC7")
    put(load("lambert1SG_normal").convert("RGB").resize((SIZE, SIZE), Image.LANCZOS), "table_n", "BC7")
    put(spec_from(load("lambert1SG_roughness"), load("lambert1SG_metallic"), 1024), "table_s")
    # the rotor (build_meshes.py makes it, with planar UVs on the disc) and its metal
    put(rotor_disc(EU, SIZE, 4), "wheel_eu_d", "BC7")
    put(rotor_disc(US, SIZE, 4), "wheel_us_d", "BC7")
    put(rotor_spec(min(SIZE, 1024)), "wheel_s")
    put(flat(), "flat_n")
    md, ms = metal(min(SIZE, 1024))
    put(md, "metal_d", "BC7")
    put(ms, "metal_s")
    put(flat(16, (255, 255, 255)), "metal_m")   # the env map's mask: all of it shines
    # the felt with the layouts printed on it
    put(layout(False, SIZE, 4), "layout_eu_d", "BC7")
    put(layout(True, SIZE, 4), "layout_us_d", "BC7")
    put(flat(64, (14, 46, 0)), "felt_s")
    # chips: one colour per material of the chip set (build_chips.py lists them)
    for hexcol in CHIP_COLOURS:
        put(flat(8, tuple(int(hexcol[i:i + 2], 16) for i in (0, 2, 4))), f"chipcol_{hexcol}")
    put(flat(64, (90, 200, 0)), "chip_s")
    # the ball
    put(flat(64, (244, 240, 230)), "ball_d")
    put(flat(64, (220, 230, 0)), "ball_s")
    return 0


PREVIEW = "--preview" in sys.argv
SIZE = 4096 if "--4k" in sys.argv else 2048 if "--2k" in sys.argv else 1024  # every big texture: 1024 (BC7) by default
if __name__ == "__main__":
    sys.argv = [a for a in sys.argv if a not in ("--preview", "--4k", "--2k")]
    sys.exit(main())
