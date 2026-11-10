#!/usr/bin/env python3
"""Writes the poker table's own wood and padded rail (Data/Textures/fo4roulette/pokerwood_*.dds, pokerrail_*.dds),
1024 each and tiling, for build_poker_table.py's shapes (box-mapped: a tile every WOOD_TILE / RAIL_TILE units). The
model's one atlas squeezed the wood into a thin strip that the whole race-track and pedestal stretched over, so it
was a blur; these repeat at a sharp scale instead.

    python tools/make_poker_table.py <out Data folder> [--preview]

- pokerwood: mahogany, straight-grained boards along u, a clear lacquer (glossy spec)
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
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32) / N   # (rows: v, across the grain; columns: u, along it)
    # the boards: growth layers across v, gently wandering (all of it periodic, so the tile repeats seamlessly)
    wander = periodic((N, N), (N / 6, N / 1.5), 1) * 0.06 + periodic((N, N), (N / 30, N / 4), 2) * 0.012
    layers = (yy * 26.0 + wander * 26.0 + 0.4 * periodic((N, N), (N / 3, N / 3), 3)) % 1.0
    rings = np.where(layers < 0.78, layers / 0.78, (1.0 - layers) / 0.22) ** 0.85
    pores = periodic((N, N), (0.6, 40.0), 4)                       # fine streaks along the grain
    figure = periodic((N, N), (N / 10, N / 2), 5)                  # broad changes of tone along a board
    # three boards across the tile, each a slightly different shade, a fine dark seam between
    board = np.floor(yy * 3.0)
    shade = np.choose(board.astype(int) % 3, [0.0, 0.07, -0.05])
    seam = np.exp(-(((yy * 3.0) % 1.0) / 0.004) ** 2) + np.exp(-((1.0 - (yy * 3.0) % 1.0) / 0.004) ** 2)
    g = 0.55 * rings + 0.2 * (pores * 0.5 + 0.5) + 0.25 * (figure * 0.5 + 0.5) + shade
    dark = np.array((70, 28, 14), np.float32)
    light = np.array((150, 66, 32), np.float32)
    col = dark + (light - dark) * np.clip(g, 0, 1)[..., None]
    col *= (1 - 0.55 * np.clip(seam, 0, 1))[..., None]
    d = Image.fromarray(col.clip(0, 255).astype(np.uint8), "RGB")
    h = 0.6 * rings + 0.4 * pores - 2.0 * np.clip(seam, 0, 1)
    n = normal_from(h, 0.9)
    # lacquered: glossy, a little duller in the pores and seams
    spec = np.clip(0.55 - 0.15 * (pores * 0.5 + 0.5) - 0.3 * np.clip(seam, 0, 1), 0, 1)
    gloss = np.clip(0.7 - 0.1 * (pores * 0.5 + 0.5), 0, 1)
    s = Image.fromarray((np.stack([spec, gloss, np.zeros_like(spec)], -1) * 255).astype(np.uint8), "RGB")
    return d, n, s


def leather():
    # pebble grain: cells (from blurred noise thresholds) raised a little, fine creases between
    cells = periodic((N, N), 3.0, 11)
    creases = np.abs(periodic((N, N), 1.4, 12))
    broad = periodic((N, N), N / 8, 13)
    h = 0.8 * cells - 0.9 * np.exp(-creases / 0.08) + 0.3 * broad
    base = np.array((24, 22, 23), np.float32)
    tone = 1.0 + 0.10 * cells + 0.08 * broad - 0.15 * np.exp(-creases / 0.08)
    d = Image.fromarray((base * tone[..., None]).clip(0, 255).astype(np.uint8), "RGB")
    n = normal_from(h, 1.6)
    spec = np.clip(0.35 + 0.1 * cells - 0.15 * np.exp(-creases / 0.08), 0, 1)
    gloss = np.clip(0.5 + 0.08 * cells, 0, 1)
    s = Image.fromarray((np.stack([spec, gloss, np.zeros_like(spec)], -1) * 255).astype(np.uint8), "RGB")
    return d, n, s


def main() -> int:
    out = os.path.join(sys.argv[1], "Textures", "fo4roulette")
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
