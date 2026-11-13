"""The slot machine's stool, casino style (2K): a red leather seat piped in gold, chrome legs, a brass foot ring and
black rubber feet. The stool is the Vault-Tec machine's own (its Stool shape: the pre-war house bar stool,
PlayerHouse_StoolBar01), its shape kept so the sitting animations still line up; only its textures are new, painted
here texel by texel from where each one is on the stool (its UVs, from WorkshopSlotMachine01.nif).

    python3 tools/make_slot_stool.py [<Data dir>] [--png]

Writes Textures\\fo4roulette\\SlotStool_d.dds / _n.dds (BC7) and _s.dds (BC1), and Materials\\fo4roulette\\SlotStool.BGSM
(the vanilla stool's material with our textures). tools/build_slot_machine.py points the model's stool at it.

The stool's parts (its own space: Z up, the seat at Z 50.7-54, centred at X 0, Y 4.8):
    seat top   the disc facing up at Z 54     seat underside  the disc facing down at Z 50.7
    seat side  two bands round it (Z 50.7-54) legs  four tapered strips (Z 2-51)
    foot ring  the tube at Z 19.6-21.2        feet  the caps at the bottom (Z 0-2.3)
"""
import os
import re
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from dds import save_dds  # noqa: E402
from nifheader import Nif, trishape  # noqa: E402

SRC = os.path.join(HERE, "source_slots")
N = 2048
CENTRE = np.array([0.0, 4.8])

# part ids
SEAT_TOP, SEAT_UNDER, SEAT_SIDE, LEG, RING, FOOT = range(6)

LEATHER = np.array([0.46, 0.035, 0.04])
GOLD = np.array([0.86, 0.64, 0.24])
CHROME = np.array([0.80, 0.80, 0.79])
RUBBER = np.array([0.06, 0.055, 0.055])
UNDER = np.array([0.09, 0.08, 0.08])


def stool_mesh():
    n = Nif(os.path.join(SRC, "WorkshopSlotMachine01.nif"))
    i = next(k for k in range(n.nblocks) if n.btype(k) == "BSTriShape"
             and n.s(struct.unpack_from("<i", n.block(k), 0)[0]) == "Stool:44")
    _, p, uv, tris, _ = trishape(n.block(i))
    return p.astype(np.float64), uv.astype(np.float64), np.array(tris)


def part_of(p, t):
    """which part a triangle is, from where it is and which way it faces"""
    P = p[t]
    nrm = np.cross(P[1] - P[0], P[2] - P[0])
    nrm /= np.linalg.norm(nrm) + 1e-9
    z = P[:, 2].mean()
    r = np.hypot(P[:, 0].mean() - CENTRE[0], P[:, 1].mean() - CENTRE[1])
    if z > 50.0:
        if nrm[2] > 0.9:
            return SEAT_TOP
        if nrm[2] < -0.9:
            return SEAT_UNDER
        return SEAT_SIDE
    if 19.0 < P[:, 2].min() and P[:, 2].max() < 22.0 and r > 13.0:
        return RING
    if P[:, 2].max() < 2.6:
        return FOOT
    return LEG


def rasterise(p, uv, tris):
    """per texel: its part (-1: none) and its place on the stool (3D)"""
    part = np.full((N, N), -1, np.int8)
    pos = np.zeros((N, N, 3))
    for t in tris:
        k = part_of(p, t)
        a, b, c = uv[t] * N
        x0, x1 = int(np.floor(min(a[0], b[0], c[0]))) - 1, int(np.ceil(max(a[0], b[0], c[0]))) + 1
        y0, y1 = int(np.floor(min(a[1], b[1], c[1]))) - 1, int(np.ceil(max(a[1], b[1], c[1]))) + 1
        x0, y0 = max(x0, 0), max(y0, 0)
        x1, y1 = min(x1, N - 1), min(y1, N - 1)
        if x1 < x0 or y1 < y0:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        det = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(det) < 1e-9:
            continue
        l1 = ((b[1] - c[1]) * (xs - c[0]) + (c[0] - b[0]) * (ys - c[1])) / det
        l2 = ((c[1] - a[1]) * (xs - c[0]) + (a[0] - c[0]) * (ys - c[1])) / det
        l3 = 1 - l1 - l2
        inside = (l1 >= -0.002) & (l2 >= -0.002) & (l3 >= -0.002)
        if not inside.any():
            continue
        P = p[t]
        q = l1[..., None] * P[0] + l2[..., None] * P[1] + l3[..., None] * P[2]
        sub = part[y0:y1 + 1, x0:x1 + 1]
        sub[inside] = k
        pos[y0:y1 + 1, x0:x1 + 1][inside] = q[inside]
    return part, pos


def noise(shape, scale, seed):
    """smooth value noise, about -1..1"""
    rng = np.random.default_rng(seed)
    small = rng.normal(0, 1, (max(2, shape[0] // scale), max(2, shape[1] // scale))).astype(np.float32)
    im = Image.fromarray(small).resize((shape[1], shape[0]), Image.BICUBIC)
    a = np.asarray(im, np.float64)
    return a / (a.std() + 1e-9)


def smooth(x, a, b):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def paint(part, pos):
    """colour (0-1), height (for the normal map), specular, gloss"""
    col = np.zeros((N, N, 3))
    height = np.zeros((N, N))
    spec = np.zeros((N, N))
    gloss = np.zeros((N, N))
    z = pos[..., 2]
    r = np.hypot(pos[..., 0] - CENTRE[0], pos[..., 1] - CENTRE[1])
    ang = np.arctan2(pos[..., 1] - CENTRE[1], pos[..., 0] - CENTRE[0])
    grain = 0.6 * noise((N, N), 3, 1) + 0.4 * noise((N, N), 9, 2)
    fine = noise((N, N), 1, 3)

    # the seat: red leather, gold piping round its top edge and its bottom edge
    leather = LEATHER * (1.0 + 0.06 * grain[..., None] + 0.03 * fine[..., None])
    top = part == SEAT_TOP
    side = part == SEAT_SIDE
    # the top: a stitched ring a third of the way in, the cushion's crown a touch lighter in the middle
    crown = 1.0 + 0.10 * (1 - smooth(r, 0, 9.5))
    seam = np.exp(-((r - 8.2) / 0.16) ** 2)
    stitch = seam * (0.5 + 0.5 * np.sign(np.sin(ang * 70)))
    pipe_top = smooth(r, 11.55, 11.8)
    c_top = leather * crown[..., None] * (1 - 0.35 * seam[..., None])
    c_top = c_top * (1 - stitch[..., None] * 0.5) + GOLD * 0.75 * stitch[..., None] * 0.5
    c_top = c_top * (1 - pipe_top[..., None]) + GOLD * pipe_top[..., None]
    col[top] = c_top[top]
    height[top] = (-0.6 * seam + 0.5 * pipe_top + 0.05 * grain)[top]
    spec[top] = (0.22 * (1 - pipe_top) + 0.75 * pipe_top)[top]
    gloss[top] = (0.42 * (1 - pipe_top) + 0.7 * pipe_top)[top]
    # the side: piping top and bottom, plain leather between, a stitched line just inside each
    pipe_side = np.maximum(smooth(z, 53.7, 53.85), 1 - smooth(z, 50.85, 51.0))
    sline = np.exp(-((z - 53.4) / 0.05) ** 2) + np.exp(-((z - 51.3) / 0.05) ** 2)
    sline = sline * (0.5 + 0.5 * np.sign(np.sin(ang * 70)))
    c_side = leather * (1 - 0.3 * sline[..., None])
    c_side = c_side * (1 - pipe_side[..., None]) + GOLD * pipe_side[..., None]
    col[side] = c_side[side]
    height[side] = (0.5 * pipe_side - 0.3 * sline + 0.05 * grain)[side]
    spec[side] = (0.22 * (1 - pipe_side) + 0.75 * pipe_side)[side]
    gloss[side] = (0.42 * (1 - pipe_side) + 0.7 * pipe_side)[side]
    # underneath: black
    under = part == SEAT_UNDER
    col[under] = (UNDER * (1 + 0.05 * grain[..., None]))[under]
    spec[under], gloss[under] = 0.1, 0.25
    # the legs: polished chrome, faint lengthwise streaks
    leg = part == LEG
    streak = noise((N, N), 4, 4)
    col[leg] = (CHROME * (1 + 0.04 * streak[..., None]))[leg]
    spec[leg], gloss[leg] = 0.95, 0.88
    height[leg] = 0.02 * streak[leg]
    # the foot ring: brass
    ring = part == RING
    col[ring] = (GOLD * (0.92 + 0.05 * streak[..., None]))[ring]
    spec[ring], gloss[ring] = 0.85, 0.78
    # the feet: black rubber
    foot = part == FOOT
    col[foot] = (RUBBER * (1 + 0.1 * grain[..., None]))[foot]
    spec[foot], gloss[foot] = 0.05, 0.15
    return np.clip(col, 0, 1), height, np.clip(spec, 0, 1), np.clip(gloss, 0, 1)


def bleed(a, mask, steps=12):
    """the painted texels carried out past their edges (so mip maps don't pull in black)"""
    a = a.copy()
    m = mask.copy()
    for _ in range(steps):
        acc = np.zeros_like(a)
        cnt = np.zeros(m.shape)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sm = np.roll(m, (dy, dx), (0, 1))
            sa = np.roll(a, (dy, dx), (0, 1))
            acc[sm] += sa[sm]
            cnt[sm] += 1
        grow = (~m) & (cnt > 0)
        a[grow] = acc[grow] / cnt[grow][..., None] if a.ndim == 3 else acc[grow] / cnt[grow]
        m = m | grow
    return a


def normal_map(height, mask):
    # (a light blur: 3x3, by hand)
    h = height.copy()
    h = (np.roll(h, 1, 0) + 2 * h + np.roll(h, -1, 0)) / 4
    h = (np.roll(h, 1, 1) + 2 * h + np.roll(h, -1, 1)) / 4
    gy, gx = np.gradient(h)
    k = 2.5
    nx, ny, nz = -gx * k, -gy * k, np.ones_like(h)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    out = np.stack([nx / ln, ny / ln, nz / ln], -1) * 0.5 + 0.5
    out[~mask] = (0.5, 0.5, 1.0)
    return out


def bgsm(src: bytes) -> bytes:
    names = {
        "setdressing/playerhouse/playerhouse_stoolbar01_d.dds": "fo4roulette/SlotStool_d.dds",
        "setdressing/playerhouse/playerhouse_stoolbar01_n.dds": "fo4roulette/SlotStool_n.dds",
        "setdressing/playerhouse/playerhouse_stoolbar01_s.dds": "fo4roulette/SlotStool_s.dds",
    }
    out = bytearray()
    pos = 0
    for m in re.finditer(rb"(.{4})([A-Za-z][\x20-\x7e]+\.dds\x00)", src, re.S):
        n = struct.unpack("<I", m.group(1))[0]
        text = m.group(2)
        new = names.get(text[:-1].decode().lower())
        if n != len(text) or new is None:
            continue
        out += src[pos:m.start()]
        enc = new.encode() + b"\0"
        out += struct.pack("<I", len(enc)) + enc
        pos = m.end()
    out += src[pos:]
    return bytes(out)


def main(data):
    p, uv, tris = stool_mesh()
    part, pos = rasterise(p, uv, tris)
    mask = part >= 0
    col, height, spec, gloss = paint(part, pos)
    col = bleed(col, mask)
    spec = bleed(spec, mask)
    gloss = bleed(gloss, mask)
    tex = os.path.join(data, "Textures", "fo4roulette")
    os.makedirs(tex, exist_ok=True)
    d = Image.fromarray((col * 255).astype(np.uint8))
    nm = Image.fromarray((normal_map(height, mask) * 255).astype(np.uint8))
    s = Image.fromarray((np.stack([spec, gloss, np.zeros_like(spec)], -1) * 255).astype(np.uint8))
    if "--png" in sys.argv:
        d.save(os.path.join(tex, "SlotStool_d.png"))
        s.save(os.path.join(tex, "SlotStool_s.png"))
    save_dds(d.convert("RGBA"), os.path.join(tex, "SlotStool_d.dds"), "BC7")
    save_dds(nm.convert("RGBA"), os.path.join(tex, "SlotStool_n.dds"), "BC7")
    save_dds(s, os.path.join(tex, "SlotStool_s.dds"), "DXT1")
    mats = os.path.join(data, "Materials", "fo4roulette")
    os.makedirs(mats, exist_ok=True)
    with open(os.path.join(SRC, "PlayerHouse_StoolBar01.BGSM"), "rb") as f:
        mat = bgsm(f.read())
    assert b"fo4roulette/SlotStool_d.dds" in mat and b"fo4roulette/SlotStool_s.dds" in mat
    with open(os.path.join(mats, "SlotStool.BGSM"), "wb") as f:
        f.write(mat)
    print("stool: parts", {k: int((part == k).sum()) for k in range(6)})


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    main(args[0] if args else os.path.join(HERE, "..", "dist", "Data"))
