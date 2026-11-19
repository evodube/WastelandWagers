#!/usr/bin/env python3
"""The "Used" tables and slot machine: the same models as the clean ones, with post-war textures made from theirs.

    python3 tools/make_used.py <Data dir> [materials dir]

Reads the clean textures in <Data>\\Textures\\WastelandWagers and writes, beside them:
  - <name>_used_d.dds (2K, BC1): faded, sun-bleached and grimy; felt stained, burnt and worn bald, wood scratched with its
    varnish worn through and water rings, the padded rails cracked and patched with tape, metal rusted and chipped
  - <name>_used_n.dds (BC7) for the ones with their own normal map: the scratches, cracks and dents in it too
  - <name>_used_s.dds (BC1): duller, blotchy
  - used_n.dds (1K, BC7): a scratched, dented normal for the parts that had none (flat_n)
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


def weather(c, kind, seed):
    """c: HxWx3 in 0..1, at 2K. Returns (colour, height (dents and scratches, 0 = untouched), dull (0..1, more =
    less shine))."""
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

    elif kind == "wood":
        c = desat(c, 0.25)
        c = mix(c, c * col(255, 225, 185), 0.35)
        worn = smooth(fbm(size, 160, 5, seed + 4), 0.58, 0.70)        # varnish worn through to grey wood
        bare = desat(c, 0.75) * 1.55 + col(30, 26, 20)
        c = mix(c, bare, worn * 0.7)
        sc = lines(size, 700, 260, 2, seed + 5)
        deep = lines(size, 60, 500, 3, seed + 6, 0.15)
        c = mix(c, desat(c, 0.6) * 1.6 + 0.08, np.clip(sc * 0.55 + deep * 0.8, 0, 1))
        r = rings(size, 14, 24, 55, seed + 7)
        c = c * (1 - r[..., None] * 0.5)
        core, halo = spots(size, 6, 5, 9, seed + 8)
        c = mix(c, col(15, 12, 9), np.maximum(core, halo * 0.6))
        c = c * (1 - blotch[..., None] * 0.45)
        c = mix(c, c * 0.55, smooth(fbm(size, 90, 4, seed + 9), 0.64, 0.74))   # dark grime ground in
        H -= sc * 0.6 + deep * 1.2 + core * 0.6
        H += (fbm(size, 50, 3, seed + 10) - 0.5) * 0.8                          # dented
        dull += worn * 0.8 + 0.25

    elif kind == "rail":  # black padded vinyl
        c = c * 0.75 + col(34, 29, 22)                                # faded black to a dusty brown-grey
        c = mix(c, c * 1.6 + 0.05, smooth(sun, 0.55, 0.8) * 0.5)
        cr = cracks(size, 70, seed + 4)
        foam = col(176, 150, 92)                                      # split through to the yellow foam
        c = mix(c, foam, smooth(cr, 0.35, 0.8) * 0.75)
        sc = lines(size, 300, 200, 2, seed + 5, 0.3)
        c = mix(c, c * 1.8 + 0.06, sc * 0.5)
        t = tape(size, 7, seed + 6)
        tc = col(128, 126, 118) * (0.85 + 0.3 * fbm(size, 20, 3, seed + 7))[..., None]
        tc = tc * (1 - lines(size, 120, 60, 1, seed + 8, 0.0, (60, 160))[..., None] * 0.25)
        c = mix(c, tc, t)
        c = c * (1 - blotch[..., None] * 0.3)
        H -= cr * 1.2 + sc * 0.4
        H += t * 0.6 + (fbm(size, 60, 3, seed + 9) - 0.5) * 1.0
        dull += 0.5 - t * 0.2

    elif kind in ("metal", "slot", "stool", "wheel", "dark", "props", "print"):
        heavy = {"metal": 1.0, "slot": 1.0, "stool": 0.9, "wheel": 0.6, "dark": 0.7, "props": 0.5, "print": 0.6}[kind]
        c = desat(c, 0.3 * heavy)
        c = mix(c, c * col(255, 232, 196), 0.35 * heavy)              # yellowed, nicotine and dust
        if kind == "print":
            c = mix(c, c * col(230, 205, 150), smooth(sun, 0.4, 0.8) * 0.7)
        # rust coming through in blotches, streaked down a little
        rmask = smooth(fbm(size, 120, 5, seed + 4, aspect=0.6), 0.6 - 0.06 * heavy, 0.72)
        rust = col(110, 52, 24) * (0.6 + 0.8 * fbm(size, 12, 3, seed + 5))[..., None]
        rust = mix(rust, col(160, 92, 40), smooth(fbm(size, 25, 3, seed + 6), 0.5, 0.7))
        c = mix(c, rust, rmask * 0.9 * heavy)
        # paint chipped off to grey metal, at the edges of the blotches
        chips = smooth(fbm(size, 22, 4, seed + 7), 0.66, 0.70) * smooth(grime, 0.4, 0.6)
        c = mix(c, col(118, 116, 110) * (0.8 + 0.4 * dust)[..., None], chips * 0.85 * heavy)
        pits = speckle(size, 0.004 * heavy, seed + 8, 0.9)
        c = c * (1 - pits[..., None] * 0.45)
        sc = lines(size, int(400 * heavy), 160, 2, seed + 9, 0.3)
        c = mix(c, c * 1.5 + 0.06, sc * 0.45)
        c = c * (1 - blotch[..., None] * 0.4 * heavy)
        if kind == "stool":  # the red leather: cracked, faded
            cr = cracks(size, 60, seed + 10)
            c = mix(c, c * 1.4 + col(40, 32, 20), smooth(cr, 0.3, 0.8) * 0.6)
            H -= cr * 1.0
        H -= pits * 0.8 + sc * 0.5 + chips * 0.4
        H += rmask * (fbm(size, 6, 2, seed + 11) - 0.5) * 2.0 + (fbm(size, 70, 3, seed + 12) - 0.5) * 0.9
        dull += rmask * 0.9 + chips * 0.3 + 0.3

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


def make_textures(tex):
    for name, (kind, own_n, own_s, seed) in TEXTURES.items():
        src = os.path.join(tex, name + "_d.dds")
        w, h = Image.open(src).size
        size = (S, S * h // w)  # 2K wide, the same shape
        print(name, kind, (w, h), "->", size)
        c, H, dull = weather(load(src, size), kind, seed)
        save(c, os.path.join(tex, name + "_used_d.dds"), "DXT1")
        if own_n:
            n_src = os.path.join(tex, name + "_n.dds")
            nw, nh = Image.open(n_src).size
            n = load(n_src, (nw, nh))
            Hs = np.asarray(Image.fromarray(((H - H.min()) / (np.ptp(H) + 1e-9) * 65535).astype(np.uint16)).resize(
                (nw, nh), Image.BILINEAR), dtype=np.float64) / 65535.0 * np.ptp(H)
            save(bump(n, Hs, 6.0 * nw / S), os.path.join(tex, name + "_used_n.dds"), "BC7")
        if own_s:
            s_src = os.path.join(tex, name + "_s.dds")
            sw, sh = Image.open(s_src).size
            s = load(s_src, (sw, sh))
            d = np.asarray(Image.fromarray((dull * 255).astype(np.uint8)).resize((sw, sh), Image.BILINEAR),
                           dtype=np.float64) / 255.0
            save(s * (0.75 - 0.6 * d)[..., None], os.path.join(tex, name + "_used_s.dds"), "DXT1")
    # the roulette wheel's specular (shared by both wheels) and a generic scratched normal for the flat-normal parts
    s = load(os.path.join(tex, "wheel_s.dds"))
    _, _, dull = weather(load(os.path.join(tex, "wheel_eu_d.dds"), (S, S)), "wheel", 1212)
    d = np.asarray(Image.fromarray((dull * 255).astype(np.uint8)).resize(s.shape[1::-1], Image.BILINEAR)) / 255.0
    save(s * (0.75 - 0.6 * d)[..., None], os.path.join(tex, "wheel_used_s.dds"), "DXT1")
    size = (1024, 1024)
    H = -lines(size, 260, 160, 1, 77) * 0.6 - speckle(size, 0.003, 78, 0.8) * 0.6 + (fbm(size, 40, 3, 79) - 0.5) * 1.2
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
