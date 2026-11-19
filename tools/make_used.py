#!/usr/bin/env python3
"""The "Used" tables and slot machine: the same models as the clean ones, with post-war textures made from theirs.

    python3 tools/make_used.py <Data dir> [materials dir]

Reads the clean textures in <Data>\\Textures\\WastelandWagers and writes, beside them:
  - <name>_used_d.dds (2K, BC1): faded, sun-bleached and grimy; felt stained, burnt and worn bald, wood scratched with its
    varnish worn through and water rings, the padded rails cracked and patched with tape, metal rusted and chipped
  - <name>_used_n.dds (BC7) for the ones with their own normal map: the scratches, cracks and dents in it too
  - <name>_used_s.dds (BC1): duller, blotchy
  - used_n.dds (2K, BC7): a scratched, dented normal for the parts that had none (flat_n)
...and in <Data>\\Meshes\\WastelandWagers the Used models (RouletteTableUsed.nif, PokerTableUsed.nif,
BlackjackTableUsed.nif, SlotMachineUsed.nif): the clean ones with those textures. The slot machine's go through its
materials: <Data>\\Materials\\WastelandWagers\\SlotMachineUsed.BGSM, SlotStoolUsed.BGSM and SlotStandUsed.BGSM (its
stand: the Institute counter's material made over in rusty metal; the vanilla BGSM it copies is read from
[materials dir]\\SetDressing\\Institute\\InstituteCafeCounter01a.BGSM, or tools/source_slots if it's there).

Every pattern is tileable (made in frequency space, or drawn wrapped round), so tiled UVs show no seams, and fixed by
seed: the same textures each time.
"""
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dds import save_dds  # noqa: E402
from nifheader import Nif  # noqa: E402

S = 2048  # every Used diffuse is 2K

# ---------------------------------------------------------------- patterns (all tileable, values 0..1)


def fbm(size, scale, octaves=5, seed=0, aspect=1.0):
    """Fractal noise with features about `scale` pixels across, tileable (filtered white noise, in frequency space)."""
    h, w = size
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w))
    amp = 1.0
    total = 0.0
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.rfftfreq(w)[None, :] * aspect
    f = np.sqrt(fx * fx + fy * fy)
    for o in range(octaves):
        s = scale / (2 ** o)
        spec = np.fft.rfft2(rng.standard_normal((h, w)))
        spec *= np.exp(-(f * s) ** 2 * 2.0)
        n = np.fft.irfft2(spec, s=(h, w))
        n /= n.std() + 1e-9
        out += amp * n
        total += amp
        amp *= 0.5
    out /= total
    return np.clip(0.5 + out * 0.22, 0, 1)


def smooth(x, a, b):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def wrapped_dist(size, cx, cy):
    h, w = size
    y = np.arange(h)[:, None]
    x = np.arange(w)[None, :]
    dy = np.abs(y - cy)
    dx = np.abs(x - cx)
    dy = np.minimum(dy, h - dy)
    dx = np.minimum(dx, w - dx)
    return np.sqrt(dx * dx + dy * dy)


def rings(size, count, rmin, rmax, seed):
    """Rings left by wet glasses and bottles: a dark rim, a faint fill, broken here and there."""
    rng = np.random.default_rng(seed)
    m = np.zeros(size)
    wobble = fbm(size, 40, 3, seed + 1)
    for _ in range(count):
        cx, cy = rng.uniform(0, size[1]), rng.uniform(0, size[0])
        r = rng.uniform(rmin, rmax)
        d = wrapped_dist(size, cx, cy) + (wobble - 0.5) * r * 0.15
        rim = np.exp(-((d - r) / (r * 0.05 + 1.5)) ** 2)
        fill = (d < r) * 0.25
        gaps = smooth(fbm(size, r * 0.6, 2, int(rng.integers(1 << 30))), 0.35, 0.55)
        m = np.maximum(m, (rim + fill) * gaps * rng.uniform(0.5, 1.0))
    return np.clip(m, 0, 1)


def spots(size, count, rmin, rmax, seed, halo=2.2):
    """Burns (cigarettes left on the felt): a black core, a scorched brown halo. Returns (core, halo)."""
    rng = np.random.default_rng(seed)
    core = np.zeros(size)
    ring = np.zeros(size)
    rough = fbm(size, 8, 3, seed + 7)
    for _ in range(count):
        cx, cy = rng.uniform(0, size[1]), rng.uniform(0, size[0])
        r = rng.uniform(rmin, rmax)
        stretch = rng.uniform(1.0, 2.5)  # long ones: a cigarette lying where it burnt
        ang = rng.uniform(0, np.pi)
        h, w = size
        y = np.arange(h)[:, None] - cy
        x = np.arange(w)[None, :] - cx
        y = (y + h / 2) % h - h / 2
        x = (x + w / 2) % w - w / 2
        u = x * np.cos(ang) + y * np.sin(ang)
        v = -x * np.sin(ang) + y * np.cos(ang)
        d = np.sqrt((u / stretch) ** 2 + v * v) + (rough - 0.5) * r * 0.8
        core = np.maximum(core, smooth(-d, -r, -r * 0.5))
        ring = np.maximum(ring, smooth(-d, -r * halo, -r) * 0.85)
    return core, ring


def lines(size, count, length, width, seed, curl=0.0, alpha=(90, 255)):
    """Scratches: thin lines, mostly one way (things slid across the table) with strays, drawn wrapped round."""
    rng = np.random.default_rng(seed)
    h, w = size
    img = Image.new("L", (w, h), 0)
    dr = ImageDraw.Draw(img)
    main = rng.uniform(0, np.pi)
    for _ in range(count):
        x0, y0 = rng.uniform(0, w), rng.uniform(0, h)
        ang = main + rng.normal(0, 0.35) if rng.random() < 0.7 else rng.uniform(0, np.pi)
        L = rng.uniform(0.2, 1.0) * length
        n = max(2, int(L / 12))
        pts = []
        for k in range(n):
            t = k / (n - 1)
            a = ang + curl * np.sin(t * 3.0 + x0)
            pts.append((x0 + np.cos(a) * L * t, y0 + np.sin(a) * L * t))
        wd = max(1, int(round(rng.uniform(0.5, 1.0) * width)))
        a = int(rng.uniform(*alpha))
        for ox in (-w, 0, w):
            for oy in (-h, 0, h):
                dr.line([(px + ox, py + oy) for px, py in pts], fill=a, width=wd)
    return np.asarray(img, dtype=np.float64) / 255.0


def cracks(size, scale, seed, width=0.018):
    """Cracked vinyl and leather: the zero lines of two noises, a crazed network."""
    a = fbm(size, scale, 4, seed)
    b = fbm(size, scale * 0.6, 4, seed + 5)
    net = np.exp(-((a - 0.5) / width) ** 2) + 0.6 * np.exp(-((b - 0.5) / (width * 0.8)) ** 2)
    where = smooth(fbm(size, scale * 2.5, 3, seed + 9), 0.42, 0.62)  # only in places
    return np.clip(net * where, 0, 1)


def tape(size, count, seed):
    """Strips of grey duct tape over the worst of it: (mask, its own shading)."""
    rng = np.random.default_rng(seed)
    h, w = size
    img = Image.new("L", (w, h), 0)
    dr = ImageDraw.Draw(img)
    for _ in range(count):
        cx, cy = rng.uniform(0, w), rng.uniform(0, h)
        L, T = rng.uniform(120, 360), rng.uniform(38, 60)
        a = rng.uniform(0, np.pi)
        c, s = np.cos(a), np.sin(a)
        corners = [(-L / 2, -T / 2), (L / 2, -T / 2 + rng.uniform(-6, 6)), (L / 2, T / 2), (-L / 2, T / 2 + rng.uniform(-6, 6))]
        pts = [(cx + x * c - y * s, cy + x * s + y * c) for x, y in corners]
        for ox in (-w, 0, w):
            for oy in (-h, 0, h):
                dr.polygon([(x + ox, y + oy) for x, y in pts], fill=255)
    m = np.asarray(img.filter(ImageFilter.GaussianBlur(0.8)), dtype=np.float64) / 255.0
    return m


def speckle(size, density, seed, blur=0.7):
    rng = np.random.default_rng(seed)
    m = (rng.random(size) < density).astype(np.float64) * rng.uniform(0.4, 1.0, size)
    img = Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(blur))
    return np.asarray(img, dtype=np.float64) / 255.0 * 3.0


# ---------------------------------------------------------------- colour helpers


def lum(c):
    return c[..., 0] * 0.299 + c[..., 1] * 0.587 + c[..., 2] * 0.114


def desat(c, k):
    g = lum(c)[..., None]
    return c + (g - c) * k


def mix(a, b, t):
    t = np.asarray(t)
    if t.ndim == 2:
        t = t[..., None]
    return a + (b - a) * t


def col(r, g, b):
    return np.array([r, g, b], dtype=np.float64) / 255.0


# ---------------------------------------------------------------- the weathering, by kind of surface



# ---------------------------------------------------------------- wood and leather, made to look real

def gblur(a, sigma):
    """A Gaussian blur that wraps round (tileable)."""
    import cv2
    pad = int(sigma * 3) + 1
    big = np.pad(a, ((pad, pad), (pad, pad)) + ((0, 0),) * (a.ndim - 2), mode="wrap")
    out = cv2.GaussianBlur(big.astype(np.float32), (0, 0), sigma, borderType=cv2.BORDER_REFLECT)
    return out[pad:-pad, pad:-pad].astype(np.float64)


def voronoi(size, cell, seed, warp=0.0):
    """Cells about `cell` pixels across, tileable: (distance to the nearest point, to the second nearest, its index,
    how many points). warp: the cells' edges wobbled by that much of a cell."""
    from scipy.spatial import cKDTree
    h, w = size
    rng = np.random.default_rng(seed)
    n = max(4, int(h * w / (cell * cell)))
    pts = rng.uniform(0, 1, (n, 2)) * np.array([h, w])
    tree = cKDTree(pts, boxsize=[h, w])
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    if warp:
        yy += (fbm(size, cell * 1.5, 3, seed + 1) - 0.5) * cell * warp * 4
        xx += (fbm(size, cell * 1.5, 3, seed + 2) - 0.5) * cell * warp * 4
    q = np.stack([np.mod(yy, h).ravel(), np.mod(xx, w).ravel()], 1)
    d, i = tree.query(q, k=2, workers=-1)
    return d[:, 0].reshape(size), d[:, 1].reshape(size), i[:, 0].reshape(size), n


def grain_angle(lum_img):
    """The way the grain runs (radians, image x right / y down): across the strongest gradients."""
    gy, gx = np.gradient(gblur(lum_img, 1.5))
    jxx, jyy, jxy = (gx * gx).mean(), (gy * gy).mean(), (gx * gy).mean()
    return 0.5 * np.arctan2(2 * jxy, jxx - jyy) + np.pi / 2


def streaks(size, scale, seed, angle, octaves=4):
    """Noise drawn out along the grain (horizontal or vertical, whichever's nearer: tileable that way)."""
    along_x = abs(np.cos(angle)) >= abs(np.sin(angle))
    if along_x:
        return fbm(size, scale, octaves, seed, aspect=0.08)
    t = fbm(size[::-1], scale, octaves, seed, aspect=0.08)
    return t.T


def grain_lines(size, count, length, width, seed, angle, spread=0.03, alpha=(150, 255)):
    """Drying checks and splits: thin cracks along the grain, wavering a little, drawn wrapped round."""
    rng = np.random.default_rng(seed)
    h, w = size
    img = Image.new("L", (w, h), 0)
    dr = ImageDraw.Draw(img)
    for _ in range(count):
        x0, y0 = rng.uniform(0, w), rng.uniform(0, h)
        a0 = angle + rng.normal(0, spread)
        L = rng.uniform(0.3, 1.0) * length
        n = max(3, int(L / 8))
        pts = []
        off = 0.0
        for k in range(n):
            t = k / (n - 1)
            off += rng.normal(0, 0.6)
            px = x0 + np.cos(a0) * L * t - np.sin(a0) * off
            py = y0 + np.sin(a0) * L * t + np.cos(a0) * off
            pts.append((px, py))
        a = int(rng.uniform(*alpha))
        # tapering: thick in the middle, a hair at the ends
        wd = rng.uniform(0.6, 1.0) * width
        seg = max(1, len(pts) // 3)
        for ox in (-w, 0, w):
            for oy in (-h, 0, h):
                for j in range(len(pts) - 1):
                    t = j / max(1, len(pts) - 2)
                    ww = max(1, int(round(wd * (1 - abs(2 * t - 1) ** 2))))
                    dr.line([(pts[j][0] + ox, pts[j][1] + oy), (pts[j + 1][0] + ox, pts[j + 1][1] + oy)], fill=a, width=ww)
    del seg
    return np.asarray(img, dtype=np.float64) / 255.0


def wood_dry(c, seed):
    """Wood left out in the sun and rain: the finish gone, faded to a silvery grey-brown, the grain standing up
    (the soft wood between worn down), split along it in drying checks, a few dark water stains and grime in the grain."""
    size = c.shape[:2]
    L = lum(c)
    ang = grain_angle(L)
    hp = L - gblur(L, 10)                       # the grain's detail
    hp2 = L - gblur(L, 2.5)                     # ...and its finest lines
    # faded: the colour mostly gone, lighter, the grain's contrast kept (more: the soft wood weathers away)
    Lf = 0.47 + (L - L.mean()) * 0.9 + hp * 1.6 + hp2 * 0.8
    sun = fbm(size, 600, 3, seed + 1)
    Lf = Lf * (0.9 + 0.22 * sun)
    st = streaks(size, 90, seed + 2, ang)       # grey weathering streaks along the grain
    Lf = Lf * (0.86 + 0.28 * st)
    silver = col(196, 186, 168) / 0.75
    brown = col(178, 150, 116) / 0.66
    tone = mix(silver, brown, smooth(fbm(size, 300, 3, seed + 3), 0.35, 0.75) * 0.55 + 0.15)
    out = Lf[..., None] * tone * 1.02
    out = mix(out, desat(c, 0.4) * 1.5 + 0.05, 0.12)   # a little of the old stain left in it
    # drying checks along the grain, and a few long splits
    checks = grain_lines(size, 260, 260, 2, seed + 4, ang, 0.025, (110, 230))
    splits = grain_lines(size, 16, 900, 4, seed + 5, ang, 0.012, (200, 255))
    crack = np.clip(checks * 0.8 + splits, 0, 1)
    shadow = np.clip(gblur(crack, 1.2) * 1.5, 0, 1)       # the cracks' dark insides and a soft edge
    out = out * (1 - shadow[..., None] * 0.35) * (1 - crack[..., None] * 0.6)
    # grime settled in the grain and the cracks, water stains, a few nail holes
    grime = smooth(-hp, 0.0, 0.06)
    out = out * (1 - grime[..., None] * 0.25)
    r = rings(size, 4, 30, 70, seed + 6)
    out = out * (1 - r[..., None] * 0.14)
    stain = smooth(fbm(size, 260, 4, seed + 7), 0.7, 0.82)   # old rain run-off, faint
    out = mix(out, out * col(170, 150, 125), stain * 0.35)
    holes = speckle(size, 0.00004, seed + 8, 1.6)
    out = out * (1 - np.clip(holes, 0, 1)[..., None] * 0.7)
    dust = fbm(size, 30, 4, seed + 9)
    out = mix(out, out * 0.85 + col(140, 128, 108) * 0.25, smooth(dust, 0.6, 0.85) * 0.3)
    H = hp * 10 + hp2 * 4 - crack * 2.2 - shadow * 0.5 - holes * 1.2 + (st - 0.5) * 0.6
    dull = np.clip(0.8 + crack * 0.2, 0, 1)
    return np.clip(out, 0, 1), H, dull


def leather(c, seed, crack_col=None):
    """Old leather and vinyl: dried out and faded, crazed all over into little cells, split through in a network of
    cracks showing the paler fibres, the cells' edges curled and rubbed lighter, flaked off here and there."""
    size = c.shape[:2]
    rng = np.random.default_rng(seed)
    out = mix(c, desat(c, 0.35) * 0.85 + col(38, 32, 26), 0.55)    # faded, dried out
    out = out * (0.9 + 0.2 * fbm(size, 400, 3, seed + 1))[..., None]
    # the big cells and their cracks
    f1, f2, idx, n = voronoi(size, 44, seed + 2, warp=0.3)
    edge = f2 - f1
    worn = smooth(fbm(size, 280, 4, seed + 3), 0.3, 0.55)           # where it's crazed (most of it)
    gap = 1.4 + 2.6 * smooth(fbm(size, 90, 3, seed + 10), 0.45, 0.75)   # some cracks opened wider than others
    crack = smooth(-edge, -gap, -0.3) * worn
    rim = smooth(-edge, -gap - 3.0, -gap) * (1 - crack) * worn
    shade = rng.uniform(0.86, 1.14, n)[idx]
    out = out * (1 + (shade - 1) * worn)[..., None]
    # fine crazing inside the cells, where it's worst
    g1, g2, _, _ = voronoi(size, 13, seed + 4, warp=0.45)
    fine = smooth(-(g2 - g1), -1.3, -0.2) * smooth(fbm(size, 160, 4, seed + 5), 0.48, 0.66) * worn
    # flaked off: a few cells' top coat gone to the rough backing
    flaked = (rng.random(n) < 0.04)[idx] * smooth(edge, 1.0, 4.0) * worn * smooth(fbm(size, 18, 3, seed + 11), 0.42, 0.55)
    backing = col(98, 78, 58) * (0.7 + 0.6 * fbm(size, 5, 3, seed + 6))[..., None]
    fibre = col(92, 78, 62) if crack_col is None else crack_col
    # rubbed: the top coat worn grey-brown where hands and arms go, cell by cell (a crack stops the wear)
    rub = smooth(fbm(size, 200, 4, seed + 12) + (rng.random(n)[idx] - 0.5) * 0.25, 0.55, 0.68) * smooth(edge, 0.5, 3.0)
    out = mix(out, desat(out, 0.6) * 1.55 + col(30, 26, 21), rub * 0.4)
    out = mix(out, out * 1.15 + 0.012, rim * 0.35)
    out = mix(out, backing, flaked * 0.85)
    # the split: the pale fibres showing in it, a dark shadow along one side (lit from above, as the game's sun mostly is)
    inside = fibre * (0.75 + 0.5 * fbm(size, 4, 2, seed + 7))[..., None]
    shadow = np.roll(np.roll(crack, 1, 0), 1, 1) * (1 - crack)
    out = mix(out, out * 0.45, shadow * 0.8)
    out = mix(out, inside, crack * 0.9)
    out = mix(out, out * 0.62 + fibre * 0.15, fine * 0.6)
    # scuffs and dust
    sc = lines(size, 120, 140, 2, seed + 8, 0.3, (40, 140))
    out = mix(out, out * 1.4 + 0.04, sc * 0.4)
    dust = fbm(size, 30, 4, seed + 9)
    out = mix(out, out * 0.85 + col(130, 120, 102) * 0.2, smooth(dust, 0.6, 0.85) * 0.3)
    dome = np.clip(edge / 22.0, 0, 1) ** 0.5
    H = dome * 1.2 * worn - crack * 1.6 - fine * 0.5 - flaked * 0.5 + sc * 0.2
    dull = np.clip(0.55 + crack * 0.4 + flaked * 0.4 - dome * 0.2, 0, 1)
    return np.clip(out, 0, 1), H, dull


def masks(c):
    """For the atlases: where it's wood (warm browns), black leather (dark and grey), red leather."""
    mx, mn = c.max(-1), c.min(-1)
    sat = (mx - mn) / (mx + 1e-6)
    L = lum(c)
    r, g, b = c[..., 0], c[..., 1], c[..., 2]
    wood = smooth(sat, 0.25, 0.4) * (r > g) * (g > b) * smooth(L, 0.05, 0.1) * (1 - smooth(L, 0.55, 0.7)) * (r - b > 0.08)
    dark = (1 - smooth(L, 0.3, 0.42)) * (1 - smooth(sat, 0.35, 0.55))
    red = smooth(sat, 0.45, 0.6) * (r > 1.6 * g) * (r > 1.6 * b) * smooth(L, 0.05, 0.1)
    soft = lambda m: np.clip(gblur(m.astype(np.float64), 1.0), 0, 1)
    return soft(wood), soft(dark), soft(red)

def weather(c, kind, seed, atlas=True):
    """c: HxWx3 in 0..1, at 2K. Returns (colour, height (dents and scratches, 0 = untouched), dull (0..1, more =
    less shine))."""
    if kind == "wood":
        return wood_dry(c, seed)
    if kind == "rail":
        return leather(c, seed)
    if atlas and kind in ("dark", "props", "stool"):
        # atlases: their wood made dry wood, their leather cracked leather, the rest weathered as before
        base, H0, d0 = weather(c, "metal" if kind == "stool" else kind, seed, atlas=False)
        wm, dm, rm = masks(c)
        if kind == "stool":
            lm = rm  # the red seat (its black bits are the stool's metal)
            wm = wm * 0
        else:
            lm = dm
            rm = rm * 0
        if wm.max() > 0.01:
            wc, wH, wd = wood_dry(c, seed + 50)
            base, H0, d0 = mix(base, wc, wm), H0 + (wH - H0) * wm, d0 + (wd - d0) * wm
        if lm.max() > 0.01:
            lc, lH, ld = leather(c, seed + 60)
            base, H0, d0 = mix(base, lc, lm), H0 + (lH - H0) * lm, d0 + (ld - d0) * lm
        return base, H0, d0
    size = c.shape[:2]
    H = np.zeros(size)
    dull = np.zeros(size)
    grime = fbm(size, 260, 5, seed + 1)
    blotch = smooth(grime, 0.48, 0.75)
    dust = fbm(size, 30, 4, seed + 2)
    sun = fbm(size, 700, 3, seed + 3)

    if kind in ("felt", "layout"):
        print_ = 0.55 if kind == "layout" else 1.0  # the printed layouts kept readable
        c = desat(c, 0.38)
        c = mix(c, col(118, 112, 78), 0.22)                          # faded to an olive drab
        c = mix(c, c * 1.35 + 0.04, smooth(sun, 0.5, 0.8) * 0.55)    # sun-bleached in patches
        bald = smooth(fbm(size, 140, 4, seed + 4), 0.58, 0.72)        # worn bald where the arms rest
        c = mix(c, desat(c, 0.6) * 1.25 + 0.05, bald * 0.55)
        stain = smooth(fbm(size, 180, 5, seed + 5), 0.62, 0.70)       # spilt drink, dried
        rim = stain - smooth(fbm(size, 180, 5, seed + 5), 0.66, 0.74)
        c = mix(c, c * col(150, 110, 70), stain * 0.65 * print_)
        c = c * (1 - rim[..., None] * 0.35 * print_)
        r = rings(size, 9, 28, 60, seed + 6)
        c = c * (1 - r[..., None] * 0.45 * print_)
        core, halo = spots(size, 14, 6, 14, seed + 7)
        c = mix(c, c * col(120, 80, 45), halo * print_)
        c = mix(c, col(12, 10, 8), core)
        H -= core * 1.5 + bald * 0.2
        c = c * (1 - blotch[..., None] * 0.35)
        c = c * (1 - speckle(size, 0.003, seed + 8)[..., None] * 0.3)
        nap = lines(size, 260, 220, 2, seed + 9, 0.2, (40, 120))       # the nap brushed the wrong way, snags
        c = mix(c, c * 1.25 + 0.03, nap * 0.4)
        H -= nap * 0.3
        dull += 0.3

    elif kind in ("metal", "slot", "stool", "wheel", "dark", "props", "print"):
        heavy = {"metal": 1.0, "slot": 1.0, "stool": 0.9, "wheel": 0.6, "dark": 0.7, "props": 0.5, "print": 0.6}[kind]
        # rust and chipped paint only on metal (not the poker table's padding, the clay chips, the paper sign)
        metal = {"metal": 1.0, "slot": 1.0, "stool": 0.9, "wheel": 0.5, "dark": 0.0, "props": 0.3, "print": 0.0}[kind]
        c = desat(c, 0.3 * heavy)
        c = mix(c, c * col(255, 232, 196), 0.35 * heavy)              # yellowed, nicotine and dust
        if kind == "print":
            c = mix(c, c * col(230, 205, 150), smooth(sun, 0.4, 0.8) * 0.7)
        # rust: patches with ragged edges (broad shapes broken up by fine noise), the brown of old iron oxide rather
        # than orange (dark at the heart, lighter where it flakes), and specks of it scattered round the patches
        shape = 0.7 * fbm(size, 90, 5, seed + 4) + 0.3 * fbm(size, 10, 3, seed + 5)
        t0 = 0.655 - 0.03 * heavy
        rmask = smooth(shape, t0, t0 + 0.035)
        near = smooth(shape, t0 - 0.08, t0)
        specks = smooth(fbm(size, 4, 2, seed + 13), 0.62, 0.7) * near * (1 - rmask)
        tone = fbm(size, 14, 3, seed + 6)
        rust = mix(col(56, 36, 26), col(100, 60, 38), smooth(tone, 0.35, 0.65))
        rust = mix(rust, col(128, 82, 52), smooth(fbm(size, 5, 2, seed + 14), 0.62, 0.75) * 0.6)   # flaking, lighter
        rust = rust * (0.85 + 0.3 * fbm(size, 3, 2, seed + 15))[..., None]
        c = mix(c, rust, np.clip(rmask + specks * 0.8, 0, 1) * metal)
        # run-off: brown stains washed down from the rust (down the texture: most of these are laid out upright)
        import cv2
        run = cv2.blur(np.pad(rmask, ((80, 80), (0, 0)), mode="wrap").astype(np.float32), (3, 121))[80:-80]
        run = np.roll(run.astype(np.float64), 50, axis=0) * (0.6 + 0.8 * streaks(size, 40, seed + 16, np.pi / 2))
        c = mix(c, c * col(140, 104, 76), np.clip(run * 2.5, 0, 1) * (1 - rmask) * 0.6 * metal)
        # paint chipped off to bare grey metal (painted parts: not the bare steel)
        paint = 0.0 if kind == "metal" else metal
        chips = smooth(fbm(size, 22, 4, seed + 7), 0.67, 0.70) * smooth(grime, 0.45, 0.6)
        c = mix(c, col(110, 108, 102) * (0.8 + 0.4 * dust)[..., None], chips * 0.8 * paint)
        pits = speckle(size, 0.002 * heavy, seed + 8, 0.6) * (0.3 + rmask)
        c = c * (1 - np.clip(pits, 0, 1)[..., None] * 0.4)
        sc = lines(size, int(160 * heavy), 160, 1, seed + 9, 0.3, (40, 120))
        c = mix(c, c * 1.3 + 0.04, sc * 0.3)
        c = c * (1 - blotch[..., None] * (0.15 if kind == "metal" else 0.3) * heavy)  # grime
        if kind == "stool":  # the red leather: cracked, faded
            cr = cracks(size, 60, seed + 10)
            c = mix(c, c * 1.4 + col(40, 32, 20), smooth(cr, 0.3, 0.8) * 0.6)
            H -= cr * 1.0
        H -= np.clip(pits, 0, 1) * 0.5 + sc * 0.25 + chips * 0.4 * paint
        H += rmask * metal * (fbm(size, 5, 2, seed + 11) - 0.5) * 1.6 + (fbm(size, 120, 3, seed + 12) - 0.5) * 0.6
        dull += (rmask * 0.9 + run * 0.3 + chips * 0.3 * paint) * metal + 0.3

    # dust over everything, settled in the low spots
    c = mix(c, c * 0.8 + col(120, 110, 90) * 0.35, smooth(dust, 0.55, 0.8) * 0.35)
    return np.clip(c, 0, 1), H, np.clip(dull + blotch * 0.3, 0, 1)


def bump(normal, H, strength):
    """The height's slopes added to a normal map (0..1 RGB, DirectX: green is down)."""
    gy, gx = np.gradient(H)
    n = normal * 2 - 1
    n[..., 0] -= gx * strength
    n[..., 1] += gy * strength
    n /= np.linalg.norm(n, axis=-1, keepdims=True) + 1e-9
    return (n + 1) / 2


def load(path, size=None):
    im = Image.open(path).convert("RGB")
    if size and im.size != size:
        im = im.resize(size, Image.LANCZOS)
    return np.asarray(im, dtype=np.float64) / 255.0


def save(arr, path, fmt):
    img = Image.fromarray((np.clip(arr, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB")
    save_dds(img, path, fmt)
    print("  wrote", os.path.basename(path), img.size, fmt)


# ---------------------------------------------------------------- what's made from what

# name: (kind, has its own normal, has its own specular, seed)
TEXTURES = {
    "pokerwood":     ("wood", True, True, 101),
    "bjwood":        ("wood", True, True, 202),
    "poker":         ("dark", True, True, 303),
    "pokerfelt":     ("felt", True, False, 404),
    "bjfelt":        ("felt", True, False, 505),
    "layout_eu":     ("layout", False, False, 606),
    "layout_us":     ("layout", False, False, 606),  # (the same wear on both: switching wheels doesn't move it)
    "layoutgrid_eu": ("layout", False, False, 707),
    "layoutgrid_us": ("layout", False, False, 707),
    "pokerprint":    ("print", False, False, 808),
    "pokerrail":     ("rail", True, True, 909),
    "bjrail":        ("rail", True, True, 1010),
    "bjatlas":       ("props", False, False, 1111),
    "wheel_eu":      ("wheel", False, False, 1212),
    "wheel_us":      ("wheel", False, False, 1212),
    "metal":         ("metal", False, True, 1313),
    "SlotMachine":   ("slot", True, True, 1414),
    "SlotStool":     ("stool", True, True, 1515),
}
# the specular maps some of them share
SPECULAR = {"wheel": ("wheel_s", 1212), "felt": None}


# The roulette layout's betting grid is its own piece, laid on the felt (RouletteTable.nif's LayoutGrid* over Layout*):
# each one's UVs from its position on the table (u, v = M . (x, y, 1), both exact). The grid's wear is the felt's own
# under it, so the two meet without a seam: the weathered felt sampled where the grid lies, its printing laid over.
GRID_UV = np.array([[-0.0078, 3e-06, 0.881913], [1e-06, 0.016972, 0.841947]])
LAYOUT_UV = np.array([[-0.004393, 1e-06, 0.499969], [1e-06, 0.005138, 0.70186]])


def grid_from_layout(grid_clean, layout_clean, layout_used):
    import cv2
    h, w = grid_clean.shape[:2]
    lh, lw = layout_clean.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    u, v = (xx + 0.5) / w, (yy + 0.5) / h
    ai = np.linalg.inv(GRID_UV[:, :2])
    px = ai[0, 0] * (u - GRID_UV[0, 2]) + ai[0, 1] * (v - GRID_UV[1, 2])
    py = ai[1, 0] * (u - GRID_UV[0, 2]) + ai[1, 1] * (v - GRID_UV[1, 2])
    lu = LAYOUT_UV[0, 0] * px + LAYOUT_UV[0, 1] * py + LAYOUT_UV[0, 2]
    lv = LAYOUT_UV[1, 0] * px + LAYOUT_UV[1, 1] * py + LAYOUT_UV[1, 2]
    mx, my = (lu * lw - 0.5).astype(np.float32), (lv * lh - 0.5).astype(np.float32)
    sample = lambda img: cv2.remap(img.astype(np.float32), mx, my, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REFLECT).astype(np.float64)
    under_clean, under_used = sample(layout_clean), sample(layout_used)
    printed = grid_clean - under_clean                              # the lines, numbers and diamonds (the felt: ~0)
    darkened = np.clip((lum(under_clean) - lum(under_used)) * 3.0, 0, 1)  # burns and stains over the printing
    worn = 0.8 * (1 - 0.6 * darkened) * (0.85 + 0.3 * fbm((h, w), 60, 3, 4242))
    return np.clip(under_used + printed * worn[..., None], 0, 1)


def make_textures(tex):
    used = {}
    for name, (kind, own_n, own_s, seed) in TEXTURES.items():
        src = os.path.join(tex, name + "_d.dds")
        w, h = Image.open(src).size
        size = (S, S * h // w)  # 2K wide, the same shape
        print(name, kind, (w, h), "->", size)
        if name.startswith("layoutgrid_"):
            under = "layout_" + name.split("_")[1]
            ls = used[under].shape[:2]
            c = grid_from_layout(load(src, size), load(os.path.join(tex, under + "_d.dds"), ls[::-1]), used[under])
            H, dull = np.zeros(c.shape[:2]), np.zeros(c.shape[:2])
        else:
            c, H, dull = weather(load(src, size), kind, seed)
        if name.startswith("layout_"):
            used[name] = c
        save(c, os.path.join(tex, name + "_used_d.dds"), "DXT1")
        if own_n:
            n_src = os.path.join(tex, name + "_n.dds")
            nw, nh = size  # (2K, as the colour)
            n = load(n_src, (nw, nh))
            Hs = np.asarray(Image.fromarray(((H - H.min()) / (np.ptp(H) + 1e-9) * 65535).astype(np.uint16)).resize(
                (nw, nh), Image.BILINEAR), dtype=np.float64) / 65535.0 * np.ptp(H)
            save(bump(n, Hs, 6.0 * nw / S), os.path.join(tex, name + "_used_n.dds"), "BC7")
        if own_s:
            s_src = os.path.join(tex, name + "_s.dds")
            sw, sh = size
            s = load(s_src, (sw, sh))
            d = np.asarray(Image.fromarray((dull * 255).astype(np.uint8)).resize((sw, sh), Image.BILINEAR),
                           dtype=np.float64) / 255.0
            save(s * (0.75 - 0.6 * d)[..., None], os.path.join(tex, name + "_used_s.dds"), "DXT1")
    # the roulette wheel's specular (shared by both wheels) and a generic scratched normal for the flat-normal parts
    s = load(os.path.join(tex, "wheel_s.dds"))
    _, _, dull = weather(load(os.path.join(tex, "wheel_eu_d.dds"), (S, S)), "wheel", 1212)
    d = np.asarray(Image.fromarray((dull * 255).astype(np.uint8)).resize(s.shape[1::-1], Image.BILINEAR)) / 255.0
    save(s * (0.75 - 0.6 * d)[..., None], os.path.join(tex, "wheel_used_s.dds"), "DXT1")
    size = (S, S)
    H = -lines(size, 200, 320, 1, 77, 0.3, (40, 120)) * 0.3 + (fbm(size, 120, 3, 79) - 0.5) * 1.0
    flat = np.zeros(size + (3,)) + np.array([0.5, 0.5, 1.0])
    save(bump(flat, H, 4.0), os.path.join(tex, "used_n.dds"), "BC7")


# ---------------------------------------------------------------- models

def used_texture(path):
    """textures\\WastelandWagers\\x_d.dds -> x_used_d.dds; flat_n -> used_n; the shared bits (felt_s, metal_m,
    cubemaps, others' textures) as they are."""
    low = path.lower()
    if not low.startswith("textures\\wastelandwagers\\"):
        return path
    base = path.split("\\")[-1]
    stem = base[:-4]
    if stem == "flat_n":
        return path[: -len(base)] + "used_n.dds"
    if stem in ("felt_s", "metal_m", "card_s", "chip_s", "ball_s"):
        return path
    for suffix in ("_d", "_n", "_s"):
        if stem.endswith(suffix) and stem[: -len(suffix)] in TEXTURES:
            return path[: -len(base)] + stem[: -len(suffix)] + "_used" + suffix + ".dds"
        if stem == "wheel_s":
            return path[: -len(base)] + "wheel_used_s.dds"
    return path


def rewrite_nif(src, dst, strings_map=None):
    n = Nif(src)
    blocks = []
    for i in range(n.nblocks):
        b = n.block(i)
        if n.btype(i) == "BSShaderTextureSet":
            count, = struct.unpack_from("<I", b, 0)
            o = 4
            out = bytearray(struct.pack("<I", count))
            for _ in range(count):
                l, = struct.unpack_from("<I", b, o)
                t = b[o + 4:o + 4 + l].decode("latin1")
                o += 4 + l
                t2 = used_texture(t)
                if t2 != t:
                    print("   ", t, "->", t2.split("\\")[-1])
                e = t2.encode("latin1")
                out += struct.pack("<I", len(e)) + e
            out += b[o:]
            b = bytes(out)
        blocks.append(b)
    strings = [strings_map.get(s, s) for s in n.strings] if strings_map else n.strings
    d = n.d
    sizes_off = n.str_off - 4 * n.nblocks
    out = bytearray(d[:sizes_off])
    out += struct.pack("<%dI" % n.nblocks, *[len(b) for b in blocks])
    enc = [x.encode("latin1") for x in strings]
    out += struct.pack("<II", len(enc), max(len(x) for x in enc))
    for x in enc:
        out += struct.pack("<I", len(x)) + x
    out += d[n.str_end:n.blocks_off]
    for b in blocks:
        out += b
    out += n.tail
    with open(dst, "wb") as f:
        f.write(out)
    print("  wrote", os.path.basename(dst))


def rewrite_bgsm(src, dst, textures):
    """A BGSM with its texture paths (u32 length with the null, then the string) changed: {old: new}, case-blind."""
    d = open(src, "rb").read()
    out = bytearray()
    i = 0
    lowmap = {k.lower(): v for k, v in textures.items()}
    while i < len(d):
        if i + 4 <= len(d):
            l, = struct.unpack_from("<I", d, i)
            if 4 < l < 260 and i + 4 + l <= len(d) and d[i + 4 + l - 1] == 0:
                s = d[i + 4:i + 4 + l - 1]
                try:
                    t = s.decode("ascii")
                except UnicodeDecodeError:
                    t = None
                if t and t.lower().endswith(".dds") and t.isprintable():
                    new = lowmap.get(t.lower(), t).encode("ascii") + b"\0"
                    out += struct.pack("<I", len(new)) + new
                    i += 4 + l
                    continue
        out.append(d[i])
        i += 1
    with open(dst, "wb") as f:
        f.write(bytes(out))
    print("  wrote", os.path.basename(dst))


def make_models(data, vanilla_mats):
    mesh = os.path.join(data, "Meshes", "WastelandWagers")
    for name in ("RouletteTable", "PokerTable", "BlackjackTable"):
        print(name + "Used")
        rewrite_nif(os.path.join(mesh, name + ".nif"), os.path.join(mesh, name + "Used.nif"))
    mats = os.path.join(data, "Materials", "WastelandWagers")
    W = "WastelandWagers/"
    rewrite_bgsm(os.path.join(mats, "SlotMachine.BGSM"), os.path.join(mats, "SlotMachineUsed.BGSM"),
                 {W + "SlotMachine_d.dds": W + "SlotMachine_used_d.dds", W + "SlotMachine_n.dds": W + "SlotMachine_used_n.dds",
                  W + "SlotMachine_s.dds": W + "SlotMachine_used_s.dds"})
    rewrite_bgsm(os.path.join(mats, "SlotStool.BGSM"), os.path.join(mats, "SlotStoolUsed.BGSM"),
                 {W + "SlotStool_d.dds": W + "SlotStool_used_d.dds", W + "SlotStool_n.dds": W + "SlotStool_used_n.dds",
                  W + "SlotStool_s.dds": W + "SlotStool_used_s.dds"})
    stand = os.path.join(os.path.dirname(os.path.abspath(__file__)), "source_slots", "InstituteCafeCounter01a.BGSM")
    if not os.path.exists(stand):
        stand = os.path.join(vanilla_mats, "SetDressing", "Institute", "InstituteCafeCounter01a.BGSM")
    I = "SetDressing/Institute/InstituteCafeCounter01a"
    rewrite_bgsm(stand, os.path.join(mats, "SlotStandUsed.BGSM"),
                 {I + "_d.dds": W + "metal_used_d.dds", I + "_n.dds": W + "used_n.dds", I + "_s.dds": W + "metal_used_s.dds"})
    print("SlotMachineUsed")
    rewrite_nif(os.path.join(mesh, "SlotMachine.nif"), os.path.join(mesh, "SlotMachineUsed.nif"), {
        "Materials\\WastelandWagers\\SlotMachine.BGSM": "Materials\\WastelandWagers\\SlotMachineUsed.BGSM",
        "Materials\\WastelandWagers\\SlotStool.BGSM": "Materials\\WastelandWagers\\SlotStoolUsed.BGSM",
        "Materials\\SetDressing\\Institute\\InstituteCafeCounter01a.BGSM": "Materials\\WastelandWagers\\SlotStandUsed.BGSM",
    })


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    data = sys.argv[1]
    mats = sys.argv[2] if len(sys.argv) > 2 else ""
    tex = os.path.join(data, "Textures", "WastelandWagers")
    if "--models" not in sys.argv:
        make_textures(tex)
    make_models(data, mats)
    return 0


if __name__ == "__main__":
    sys.exit(main())
