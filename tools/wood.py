#!/usr/bin/env python3
"""The table's wood, made sharp: every texel of the table's texture (lambert1) is found on the model (its triangles
rasterised in UV space, as rotor_uv.py does for the wheel), and painted with a solid wood grain worked out from where
it is in 3D, so the grain runs continuously over the table whatever its UVs do. The model's own colours are kept,
blurred, as the tint underneath (so the base stays dark and the rail lighter, as the artist painted them), without
their blotchy, stretched detail.

    wood(albedo, obj, size) -> PIL image
"""

import numpy as np
from PIL import Image, ImageFilter


def load_wood(obj_path: str):
    """the triangles of the wood (lambert1, but not the turret or the diamonds, which are metal now)"""
    V, VT, tris = [], [], []
    group, mat = "", ""
    with open(obj_path, encoding="latin-1") as f:
        for line in f:
            if line.startswith("v "):
                x, y, z = map(float, line.split()[1:4])
                V.append((x, -z, y))  # OBJ Y-up -> Blender Z-up (forward -Z), as build_meshes.py imports it
            elif line.startswith("vt "):
                VT.append(tuple(map(float, line.split()[1:3])))
            elif line.startswith("g "):
                group = line[2:].strip()
            elif line.startswith("usemtl"):
                mat = line.split()[1]
            elif line.startswith("f ") and mat == "lambert1SG" and " Kol" not in group and "pSuperShape" not in group:
                ids = [p.split("/") for p in line.split()[1:]]
                vs = [int(p[0]) - 1 for p in ids]
                ts = [int(p[1]) - 1 for p in ids]
                for k in range(1, len(vs) - 1):
                    tris.append(((vs[0], vs[k], vs[k + 1]), (ts[0], ts[k], ts[k + 1])))
    return np.array(V), np.array(VT), tris


def texel_positions(obj_path: str, size: int):
    """(H, W, 3) model-space point of each texel the wood uses (NaN elsewhere); row 0 = the image's top"""
    V, VT, tris = load_wood(obj_path)
    pos = np.full((size, size, 3), np.nan, np.float32)
    for (a, b, c), (ta, tb, tc) in tris:
        uv = np.array([VT[ta], VT[tb], VT[tc]]) * size
        uv[:, 1] = size - uv[:, 1]
        x0, y0 = np.floor(uv.min(0)).astype(int) - 1
        x1, y1 = np.ceil(uv.max(0)).astype(int) + 1
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, size - 1), min(y1, size - 1)
        if x1 < x0 or y1 < y0:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (uv[1, 1] - uv[2, 1]) * (uv[0, 0] - uv[2, 0]) + (uv[2, 0] - uv[1, 0]) * (uv[0, 1] - uv[2, 1])
        if abs(d) < 1e-12:
            continue
        w0 = ((uv[1, 1] - uv[2, 1]) * (gx - uv[2, 0]) + (uv[2, 0] - uv[1, 0]) * (gy - uv[2, 1])) / d
        w1 = ((uv[2, 1] - uv[0, 1]) * (gx - uv[2, 0]) + (uv[0, 0] - uv[2, 0]) * (gy - uv[2, 1])) / d
        w2 = 1 - w0 - w1
        e = 0.6 / max(1.0, np.sqrt(abs(d)))  # a little over the edges, so islands don't fray
        inside = (w0 >= -e) & (w1 >= -e) & (w2 >= -e)
        P = w0[..., None] * V[a] + w1[..., None] * V[b] + w2[..., None] * V[c]
        sub = pos[y0:y1 + 1, x0:x1 + 1]
        sub[inside] = P[inside]
    return pos


def value_noise(p: np.ndarray, seed: int) -> np.ndarray:
    """smooth 3D value noise in -1..1 at points p (N, 3)"""
    i = np.floor(p).astype(np.int64)
    f = p - i
    f = f * f * (3 - 2 * f)

    def h(ix, iy, iz):
        n = (ix * 73856093) ^ (iy * 19349663) ^ (iz * 83492791) ^ (seed * 2654435761)
        n = (n ^ (n >> 13)) * 1274126177
        return ((n ^ (n >> 16)) & 0xFFFF).astype(np.float32) / 32767.5 - 1.0

    out = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                out = out + w * h(i[:, 0] + dx, i[:, 1] + dy, i[:, 2] + dz)
    return out


def fbm(p, seed, octaves=4):
    out, amp, tot = 0.0, 1.0, 0.0
    for o in range(octaves):
        out = out + amp * value_noise(p * (2 ** o), seed + o * 17)
        tot += amp
        amp *= 0.5
    return out / tot


def grain(P: np.ndarray) -> np.ndarray:
    """0..1 wood figure at model points (N, 3): growth rings round lines along the table's length (X), bent by
    noise, plus fine pores running along the grain"""
    x, y, z = P[:, 0], P[:, 1], P[:, 2]
    warp = fbm(np.stack([x * 0.15, y * 0.6, z * 0.6], -1), 3, 4)
    d = np.hypot(y * 1.0 + 3.0 * warp, (z + 6.0) * 1.0 + 3.0 * warp)
    rings = (d * 3.2 + 1.5 * fbm(np.stack([x * 0.05, y * 0.4, z * 0.4], -1), 9, 3)) % 1.0
    rings = np.where(rings < 0.72, rings / 0.72, (1.0 - rings) / 0.28)  # slow growth, then a sharp late-wood edge
    rings = rings ** 1.6
    pores = fbm(np.stack([x * 1.2, y * 25.0, z * 25.0], -1), 21, 2)  # long, thin streaks along X
    flecks = value_noise(np.stack([x * 4.0, y * 45.0, z * 45.0], -1), 33)
    g = 0.55 * rings + 0.3 * (pores * 0.5 + 0.5) + 0.15 * (flecks * 0.5 + 0.5)
    return np.clip(g, 0.0, 1.0)


def wood(albedo: Image.Image, obj_path: str, size: int = 4096) -> Image.Image:
    base = albedo.convert("RGB").resize((size, size), Image.LANCZOS)
    tint = np.array(base.filter(ImageFilter.GaussianBlur(size / 256)), np.float32)  # the artist's colours, no detail
    pos = texel_positions(obj_path, size)
    used = ~np.isnan(pos[..., 0])
    g = grain(pos[used].astype(np.float64)).astype(np.float32)
    out = np.array(base, np.float32)
    lum = 0.62 + 0.62 * g  # dark late wood .. light early wood, round the tint
    warm = np.stack([lum * 1.04, lum, lum * 0.93], -1)
    out[used] = np.clip(tint[used] * warm, 0, 255)
    # past the islands' edges, their own colours pulled outwards (so mipmaps don't bleed in the old texture)
    img = Image.fromarray(out.astype(np.uint8), "RGB")
    mask = Image.fromarray((used * 255).astype(np.uint8), "L")
    for _ in range(6):
        grown = mask.filter(ImageFilter.MaxFilter(5))
        m = np.array(mask) > 0
        g2 = (np.array(grown) > 0) & ~m
        arr = np.array(img)
        masked = Image.fromarray((arr * m[..., None]).astype(np.uint8), "RGB")
        sw = np.array(masked.filter(ImageFilter.BoxBlur(2)), np.float32)
        mw = np.array(mask.filter(ImageFilter.BoxBlur(2)), np.float32)[..., None] / 255.0
        fill = np.clip(sw / np.maximum(mw, 1e-3), 0, 255).astype(np.uint8)
        arr[g2] = fill[g2]
        img = Image.fromarray(arr, "RGB")
        mask = grown
    return img
