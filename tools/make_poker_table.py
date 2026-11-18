#!/usr/bin/env python3
"""Writes the poker table's own wood and padded rail (Data/Textures/WastelandWagers/pokerwood_*.dds, pokerrail_*.dds),
1024 each and tiling, for build_poker_table.py's shapes (box-mapped: a tile every WOOD_TILE / RAIL_TILE units). The
model's one atlas squeezed the wood into a thin strip that the whole race-track and pedestal stretched over, so it
was a blur; these repeat at a sharp scale instead.

    python tools/make_poker_table.py <out Data folder> [--preview]

- pokerwood: walnut (source_table/walnut.jpg, the user's), its grain along u, a clear lacquer (glossy spec)
- pokerrail: black padded leather: a fine pebble grain, soft sheen
"""

import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from dds import save_dds  # noqa: E402

N = 1024
WOOD_TILE = 48.0   # units a tile (build_poker_table.py's)
RAIL_TILE = 24.0


def periodic(shape, sigma, seed):
    """smooth noise that tiles: white noise blurred in frequency space (wraps at the edges), about -1..1"""
    rng = np.random.default_rng(seed)
    a = rng.normal(0, 1, shape)
    fy = np.fft.fftfreq(shape[0])[:, None]
    fx = np.fft.fftfreq(shape[1])[None, :]
    sy, sx = (sigma, sigma) if np.isscalar(sigma) else sigma
    g = np.exp(-2 * (np.pi ** 2) * ((fy * sy) ** 2 + (fx * sx) ** 2))
    out = np.real(np.fft.ifft2(np.fft.fft2(a) * g))
    return (out / (out.std() + 1e-9) / 2.5).astype(np.float32)


def normal_from(h, strength):
    gx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * strength
    gy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * strength
    nz = np.ones_like(h)
    ln = np.sqrt(gx * gx + gy * gy + nz * nz)
    rgb = np.stack([-gx / ln, gy / ln, nz / ln], -1) * 127.5 + 127.5
    return Image.fromarray(rgb.clip(0, 255).astype(np.uint8), "RGB")


def wood():
    """the user's walnut (source_table/walnut.jpg), turned so the grain runs along u, made to tile (blended with a
    copy of itself moved half a tile, so each edge meets its own other edge), 1024"""
    src = Image.open(os.path.join(os.path.dirname(__file__), "source_table", "walnut.jpg")).convert("RGB")
    src = src.rotate(90, expand=True).resize((N, N), Image.LANCZOS)
    a = np.array(src, np.float32)
    t = np.arange(N, dtype=np.float32) / N
    # made to tile one way and then the other: blended with a copy moved half a tile, the copy (seamless at the edges)
    # at the edges, the original (its own seam now in the copy's middle) everywhere else
    w = np.clip((np.minimum(t, 1 - t) * 2 - 0.25) / 0.5, 0, 1)
    w = w * w * (3 - 2 * w)
    a = a * w[None, :, None] + np.roll(a, N // 2, 1) * (1 - w[None, :, None])
    col = a * w[:, None, None] + np.roll(a, N // 2, 0) * (1 - w[:, None, None])
    d = Image.fromarray(col.clip(0, 255).astype(np.uint8), "RGB")
    lum = col.mean(-1) / 255.0
    blur = np.array(Image.fromarray((lum * 255).astype(np.uint8)).filter(__import__("PIL.ImageFilter", fromlist=["x"]).GaussianBlur(6)), np.float32) / 255
    h = lum - blur                                   # the fine grain, as height
    n = normal_from(h, 6.0)
    spec = np.clip(0.5 + 0.6 * h, 0, 1)              # lacquered
    gloss = np.full_like(spec, 0.68)
    s = Image.fromarray((np.stack([spec, gloss, np.zeros_like(spec)], -1) * 255).astype(np.uint8), "RGB")
    return d, n, s


def leather():
    # pebble grain: cells (from blurred noise thresholds) raised a little, fine creases between
    cells = periodic((N, N), 3.0, 11)
    creases = np.abs(periodic((N, N), 1.4, 12))
    broad = periodic((N, N), N / 8, 13)
    h = 0.8 * cells - 0.9 * np.exp(-creases / 0.08) + 0.3 * broad
    base = np.array((30, 27, 28), np.float32)
    tone = 1.0 + 0.22 * cells + 0.12 * broad - 0.3 * np.exp(-creases / 0.08)
    d = Image.fromarray((base * tone[..., None]).clip(0, 255).astype(np.uint8), "RGB")
    n = normal_from(h, 1.6)
    spec = np.clip(0.35 + 0.1 * cells - 0.15 * np.exp(-creases / 0.08), 0, 1)
    gloss = np.clip(0.5 + 0.08 * cells, 0, 1)
    s = Image.fromarray((np.stack([spec, gloss, np.zeros_like(spec)], -1) * 255).astype(np.uint8), "RGB")
    return d, n, s


def main() -> int:
    out = os.path.join(sys.argv[1], "Textures", "WastelandWagers")
    os.makedirs(out, exist_ok=True)
    for name, (d, n, s) in (("pokerwood", wood()), ("pokerrail", leather())):
        save_dds(d, os.path.join(out, f"{name}_d.dds"), "BC7")
        save_dds(n, os.path.join(out, f"{name}_n.dds"), "BC7")
        save_dds(s, os.path.join(out, f"{name}_s.dds"), "DXT1")
        if "--preview" in sys.argv:
            # 2 x 2 tiles: the seams show if they don't repeat
            t = Image.new("RGB", (2 * N, 2 * N))
            for i in range(2):
                for j in range(2):
                    t.paste(d, (i * N, j * N))
            t.resize((N, N)).save(os.path.join(out, f"{name}_preview.png"))
        print(name, d.size)
    return 0


if __name__ == "__main__":
    sys.exit(main())
