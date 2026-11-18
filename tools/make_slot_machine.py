"""The slot machine's textures (2K): the Vault-Tec Workshop machine's own (tools/source_slots, pulled from
DLCworkshop03 - Textures.ba2) repainted as a red and gold casino cabinet, its reel strip drawn again with our own
symbols, and its prize plates saying what pays.

    python3 tools/make_slot_machine.py <out dir (the texture folder: textures/WastelandWagers)>

Writes SlotMachine_d.dds / _n.dds (BC7) and _s.dds (BC1) at 2048, as the other tables' textures (and a _d.png to look at
with --png).

The reel strip: seven symbols, top to bottom, in the strip's seven slots (u 0.0114-0.0556, v 0.6113-0.9932); the reels
are cylinders round X with the strip twice round them (14 stops). SYMBOLS is the order: src/Slots.cpp's kStrip
must match it.
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dds import save_dds  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "source_slots")
N = 2048
SERIF = os.path.join(HERE, "DejaVuSerifCondensed-Bold.ttf")
SANS = os.path.join(HERE, "DejaVuSans-Bold.ttf")

# the strip, top to bottom (src/Slots.cpp kStrip: the same order)
SYMBOLS = ["cherry", "bell", "orange", "bar", "seven", "lemon", "plum"]
STRIP = (int(0.0114 * N), int(0.6113 * N), int(0.0556 * N) + 1, int(0.9932 * N) + 1)  # x0, y0, x1, y1

# the labels (the texture's pixels at 2048): the two prize plates either side of the reels, GRAND PRIZE, INSERT COIN
PLATE_L = (54, 408, 152, 572)
PLATE_R = (594, 408, 692, 570)
GRAND = (1462, 676, 1702, 750)
COIN = (1484, 408, 1608, 452)

RED = np.array([0.50, 0.035, 0.045])
GOLD = np.array([0.88, 0.66, 0.24])
BLACK = np.array([0.075, 0.07, 0.07])


def hsv(im):
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    mx = im.max(-1)
    mn = im.min(-1)
    d = mx - mn + 1e-6
    s = (mx - mn) / (mx + 1e-6)
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) * 60
    return h, s, mx


def smooth(x, a, b):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def recolour(im):
    """blue -> red lacquer, yellow / orange -> gold, the red-brown handle -> black; the metal a touch brighter. The
    scratches and grime kept: each colour's own lightness carried over"""
    h, s, v = hsv(im)
    lum = im @ np.array([0.299, 0.587, 0.114])
    sat = smooth(s, 0.12, 0.32)
    blue = sat * smooth(h, 165, 180) * (1 - smooth(h, 245, 260))
    gold = sat * smooth(h, 18, 24) * (1 - smooth(h, 50, 60))
    brown = sat * (1 - smooth(h, 14, 20)) + sat * smooth(h, 330, 345)
    out = im.copy()
    # metal: polished chrome under casino light (bright, a touch warm; the grime mostly polished out)
    metal = np.clip(1 - blue - gold - brown, 0, 1)[..., None]
    chrome = np.clip((lum[..., None] - 0.32) * 0.8 + 0.66, 0, 1) * np.array([1.0, 0.985, 0.95])
    out = out * (1 - metal) + chrome * metal
    # the colours: a clean lacquer (the scratches and wear kept faint: a cared-for machine, not a vault's)
    for mask, col, ref, lo, hi in ((blue, RED, 0.30, 0.78, 1.18), (gold, GOLD, 0.55, 0.7, 1.25), (brown, BLACK, 0.30, 0.6, 1.4)):
        m = mask[..., None]
        shade = np.clip(lum[..., None] / ref, lo, hi)
        target = np.clip(col * shade, 0, 1)
        out = out * (1 - m) + target * m
    return np.clip(out, 0, 1)


def font(path, size):
    return ImageFont.truetype(path, size)


def draw_symbol(name, w, h):
    """one reel symbol on the strip's cream, drawn at 4x and brought down"""
    S = 4
    W, H = w * S, h * S
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx, cy = W / 2, H / 2
    r = min(W, H) * 0.34

    def outline_ellipse(box, fill, edge=(30, 20, 20, 255), wd=None):
        d.ellipse(box, fill=fill, outline=edge, width=wd or int(S * 1.6))

    if name == "cherry":
        for dx, dy in ((-0.42, 0.28), (0.36, 0.36)):
            x, y = cx + dx * r, cy + dy * r
            outline_ellipse((x - r * 0.48, y - r * 0.48, x + r * 0.48, y + r * 0.48), (205, 18, 30, 255))
            d.ellipse((x - r * 0.28, y - r * 0.32, x - r * 0.08, y - r * 0.12), fill=(255, 150, 150, 255))
        d.line([(cx - 0.42 * r, cy - 0.15 * r), (cx + 0.15 * r, cy - 0.95 * r)], fill=(40, 110, 30, 255), width=int(S * 2.4))
        d.line([(cx + 0.36 * r, cy - 0.08 * r), (cx + 0.15 * r, cy - 0.95 * r)], fill=(40, 110, 30, 255), width=int(S * 2.4))
        d.polygon([(cx + 0.15 * r, cy - 0.95 * r), (cx + 0.75 * r, cy - 1.0 * r), (cx + 0.45 * r, cy - 0.7 * r)], fill=(70, 160, 50, 255))
    elif name == "lemon":
        outline_ellipse((cx - r * 1.0, cy - r * 0.68, cx + r * 1.0, cy + r * 0.68), (250, 222, 40, 255))
        d.ellipse((cx - r * 0.55, cy - r * 0.45, cx - r * 0.05, cy - r * 0.15), fill=(255, 248, 170, 255))
        d.polygon([(cx + 0.9 * r, cy - 0.2 * r), (cx + 1.18 * r, cy), (cx + 0.9 * r, cy + 0.2 * r)], fill=(230, 190, 20, 255))
    elif name == "orange":
        outline_ellipse((cx - r * 0.9, cy - r * 0.86, cx + r * 0.9, cy + r * 0.94), (250, 140, 20, 255))
        d.ellipse((cx - r * 0.5, cy - r * 0.5, cx - r * 0.05, cy - r * 0.1), fill=(255, 205, 140, 255))
        d.polygon([(cx, cy - 0.86 * r), (cx + 0.55 * r, cy - 1.15 * r), (cx + 0.3 * r, cy - 0.7 * r)], fill=(60, 150, 45, 255))
    elif name == "plum":
        outline_ellipse((cx - r * 0.78, cy - r * 0.92, cx + r * 0.78, cy + r * 0.92), (110, 30, 140, 255))
        d.ellipse((cx - r * 0.45, cy - r * 0.55, cx - r * 0.1, cy - r * 0.1), fill=(200, 140, 220, 255))
        d.line([(cx, cy - 0.9 * r), (cx + 0.15 * r, cy - 1.15 * r)], fill=(60, 40, 20, 255), width=int(S * 2))
    elif name == "bell":
        pts = []
        for i in range(41):
            t = i / 40
            y = cy - 0.9 * r + t * 1.6 * r
            half = r * (0.28 + 0.62 * t ** 1.6)
            pts.append((cx - half, y))
        pts += [(cx + x - cx, y) for (x, y) in reversed([(cx + (cx - px), py) for px, py in pts])]
        pts = pts[:41] + [(2 * cx - px, py) for px, py in reversed(pts[:41])]
        d.polygon(pts, fill=(235, 180, 40, 255), outline=(90, 60, 10, 255))
        d.rectangle((cx - r * 0.95, cy + 0.68 * r, cx + r * 0.95, cy + 0.82 * r), fill=(200, 140, 20, 255), outline=(90, 60, 10, 255))
        d.ellipse((cx - r * 0.2, cy + 0.78 * r, cx + r * 0.2, cy + 1.1 * r), fill=(150, 100, 15, 255))
        d.ellipse((cx - r * 0.12, cy - 1.08 * r, cx + r * 0.12, cy - 0.86 * r), fill=(200, 140, 20, 255))
        d.polygon([(cx - 0.45 * r, cy - 0.5 * r), (cx - 0.25 * r, cy - 0.55 * r), (cx - 0.55 * r, cy + 0.5 * r), (cx - 0.75 * r, cy + 0.5 * r)],
                  fill=(255, 235, 150, 255))
    elif name == "bar":
        bw, bh = W * 0.86, H * 0.36
        d.rounded_rectangle((cx - bw / 2, cy - bh / 2, cx + bw / 2, cy + bh / 2), radius=int(bh * 0.18), fill=(20, 20, 22, 255),
                            outline=(200, 160, 50, 255), width=int(S * 2))
        f = font(SANS, int(bh * 0.66))
        d.text((cx, cy + bh * 0.02), "BAR", font=f, fill=(245, 240, 230, 255), anchor="mm")
    elif name == "seven":
        f = font(SERIF, int(H * 0.86))
        for ox, oy in ((-2, 0), (2, 0), (0, -2), (0, 2), (-2, -2), (2, 2), (-2, 2), (2, -2)):
            d.text((cx + ox * S, cy + oy * S), "7", font=f, fill=(150, 110, 20, 255), anchor="mm")
        d.text((cx, cy), "7", font=f, fill=(215, 20, 30, 255), anchor="mm")
    return img.resize((w, h), Image.LANCZOS)


def draw_strip(im):
    x0, y0, x1, y1 = STRIP
    w = x1 - x0
    slot = (y1 - y0) / 7.0
    cream = Image.new("RGB", (w + 8, y1 - y0 + 4), (238, 230, 210))
    # a little grain, so it's not flat plastic
    grain = (np.random.default_rng(7).normal(0, 4, (cream.size[1], cream.size[0], 1))).astype(np.int16)
    ca = np.clip(np.asarray(cream).astype(np.int16) + grain, 0, 255).astype(np.uint8)
    cream = Image.fromarray(ca)
    im.paste(cream, (x0 - 4, y0 - 2))
    d = ImageDraw.Draw(im)
    for k, name in enumerate(SYMBOLS):
        top = y0 + k * slot
        # the divider between symbols (a thin red line)
        d.line([(x0 + 2, int(top)), (x1 - 2, int(top))], fill=(170, 30, 30), width=2)
        sym = draw_symbol(name, w - 6, int(slot) - 10)
        im.paste(sym, (x0 + 3, int(top + 5)), sym)
    d.line([(x0 + 2, y1 - 1), (x1 - 2, y1 - 1)], fill=(170, 30, 30), width=2)


def plate(im, box, lines):
    """a prize plate: black, a gold edge, its lines of text (text, size fraction, colour)"""
    x0, y0, x1, y1 = box
    d = ImageDraw.Draw(im)
    d.rectangle(box, fill=(18, 16, 16), outline=(214, 165, 60), width=4)
    d.rectangle((x0 + 7, y0 + 7, x1 - 7, y1 - 7), outline=(120, 90, 30), width=1)
    w, h = x1 - x0, y1 - y0
    y = y0 + h * 0.08
    total = sum(sz for _, sz, _ in lines)
    for text, sz, col in lines:
        px = int(h * sz / total * 0.84)
        f = font(SERIF, px)
        while f.getlength(text) > w * 0.86 and px > 8:
            px -= 1
            f = font(SERIF, px)
        d.text((x0 + w / 2, y + h * sz / total * 0.42), text, font=f, fill=col, anchor="mm")
        y += h * sz / total * 0.86


def relabel(im, box, lines, col, bg=None, serif=True):
    """a label's old lettering painted out (its own background colour) and new lettering put in, a line or more"""
    if isinstance(lines, str):
        lines = [lines]
    x0, y0, x1, y1 = box
    painted = plain(im, [box])  # (the old lettering gone, its metal's grain kept)
    im.paste(painted.crop(box), box[:2])
    d = ImageDraw.Draw(im)
    w, h = x1 - x0, y1 - y0
    lh = h / len(lines)
    px = int(lh * 0.8)
    f = font(SERIF if serif else SANS, px)
    while max(f.getlength(t) for t in lines) > w * 0.94 and px > 6:
        px -= 1
        f = font(SERIF if serif else SANS, px)
    for k, text in enumerate(lines):
        d.text((x0 + w / 2, y0 + lh * (k + 0.5)), text, font=f, fill=col, anchor="mm")


def plain(im, boxes, ring=14):
    """a label's old lettering gone (in the colour map and the specular map both, or the old words show through the
    new ones as a sheen): the colour of the surface round it (the commonest in a ring just outside), with its grain (a
    pixel's difference from that colour, taken at random from the ring), so it's no flat patch"""
    a = np.asarray(im).astype(np.float64)
    rng = np.random.default_rng(11)
    H, W = a.shape[:2]
    for x0, y0, x1, y1 in boxes:
        X0, Y0, X1, Y1 = max(0, x0 - ring), max(0, y0 - ring), min(W, x1 + ring), min(H, y1 + ring)
        outer = a[Y0:Y1, X0:X1].copy()
        mask = np.ones(outer.shape[:2], bool)
        mask[y0 - Y0:y1 - Y0, x0 - X0:x1 - X0] = False
        around = outer[mask]
        base = np.median(around, axis=0)
        dev = around - base
        dev = np.clip(dev, -40, 40)
        pick = dev[rng.integers(0, len(dev), (y1 - y0) * (x1 - x0))].reshape(y1 - y0, x1 - x0, a.shape[2])
        a[y0:y1, x0:x1] = base + pick * 0.6
    out = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    soft = out.filter(ImageFilter.GaussianBlur(0.7))
    for box in boxes:
        out.paste(soft.crop(box), box[:2])
    return out


def flatten_normal(n, boxes):
    a = np.asarray(n).copy()
    s = a.shape[0] / N
    for x0, y0, x1, y1 in boxes:
        a[int(y0 * s):int(y1 * s), int(x0 * s):int(x1 * s), :3] = (128, 128, 255)
    return Image.fromarray(a)


def main(out):
    src = np.asarray(Image.open(os.path.join(SRC, "SlotMachine01_d.png")).convert("RGB")).astype(np.float64) / 255.0
    im = Image.fromarray((recolour(src) * 255).astype(np.uint8))
    draw_strip(im)
    # the side plates (were SPECIAL AWARD 7-7-7 $50): what pays
    gold = (236, 190, 80)
    red = (230, 40, 45)
    white = (240, 232, 220)
    plate(im, PLATE_L, [("JACKPOT", 1.0, gold), ("7 7 7", 1.6, red), ("WINS IT", 1.0, gold)])
    plate(im, PLATE_R, [("BAR BAR", 1.0, white), ("BAR", 1.0, white), ("PAYS 50", 1.3, gold)])
    # GRAND PRIZE -> JACKPOT; INSERT COIN -> INSERT CAPS (two lines, as it was)
    relabel(im, GRAND, "JACKPOT", (236, 190, 80))
    relabel(im, COIN, ["INSERT", "CAPS"], (236, 190, 80), serif=False)
    if "--png" in sys.argv:
        im.save(os.path.join(out, "SlotMachine_d.png"))
    save_dds(im.convert("RGBA"), os.path.join(out, "SlotMachine_d.dds"), "BC7")
    n = Image.open(os.path.join(SRC, "SlotMachine01_n.png")).convert("RGBA").resize((N, N), Image.LANCZOS)
    n = flatten_normal(n, [STRIP, PLATE_L, PLATE_R, GRAND, COIN])
    save_dds(n, os.path.join(out, "SlotMachine_n.dds"), "BC7")
    sp = Image.open(os.path.join(SRC, "SlotMachine01_s.png")).convert("RGB").resize((N, N), Image.LANCZOS)
    sp = plain(sp, [PLATE_L, PLATE_R, GRAND, COIN])
    if "--png" in sys.argv:
        sp.save(os.path.join(out, "SlotMachine_s.png"))
    save_dds(sp, os.path.join(out, "SlotMachine_s.dds"), "DXT1")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    main(args[0] if args else os.path.join(HERE, "..", "dist", "Data", "Textures", "WastelandWagers"))
