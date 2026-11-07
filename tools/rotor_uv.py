#!/usr/bin/env python3
"""Where each texel of the rotor's texture lands on the rotor: the disc's triangles from the table model, rasterised
in UV space, give each covered texel its point on the wheel (model units, Blender's axes: Z up). make_textures.py
paints the wheel from that, so the numbers and pockets land where they really are on the sloped, sunken rings
(a flat projection put them at the wrong radius on the number ring's slope)."""

import numpy as np


def load_disc(obj_path: str):
    V, VT, tris = [], [], []
    group = ""
    with open(obj_path, encoding="latin-1") as f:
        for line in f:
            if line.startswith("v "):
                x, y, z = map(float, line.split()[1:4])
                V.append((x, -z, y))  # OBJ Y-up -> Blender Z-up (forward -Z), as the import in build_meshes.py
            elif line.startswith("vt "):
                VT.append(tuple(map(float, line.split()[1:3])))
            elif line.startswith("g "):
                group = line[2:].strip()
            elif line.startswith("f ") and group.endswith("bek1") and "kasa" not in group:
                ids = [p.split("/") for p in line.split()[1:]]
                vs = [int(p[0]) - 1 for p in ids]
                ts = [int(p[1]) - 1 for p in ids]
                for k in range(1, len(vs) - 1):
                    tris.append(((vs[0], vs[k], vs[k + 1]), (ts[0], ts[k], ts[k + 1])))
    return np.array(V), np.array(VT), tris


def texel_positions(obj_path: str, size: int):
    """(H, W, 3) model-space point for each texel the disc uses, NaN elsewhere; row 0 is the image's top (v = 1)"""
    V, VT, tris = load_disc(obj_path)
    pos = np.full((size, size, 3), np.nan)
    for (a, b, c), (ta, tb, tc) in tris:
        uv = np.array([VT[ta], VT[tb], VT[tc]]) * size
        uv[:, 1] = size - uv[:, 1]
        x0, y0 = np.floor(uv.min(0)).astype(int)
        x1, y1 = np.ceil(uv.max(0)).astype(int)
        x0, y0 = max(x0, 0), max(y0, 0)
        x1, y1 = min(x1, size - 1), min(y1, size - 1)
        if x1 < x0 or y1 < y0:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (uv[1, 1] - uv[2, 1]) * (uv[0, 0] - uv[2, 0]) + (uv[2, 0] - uv[1, 0]) * (uv[0, 1] - uv[2, 1])
        if abs(d) < 1e-12:
            continue
        w0 = ((uv[1, 1] - uv[2, 1]) * (gx - uv[2, 0]) + (uv[2, 0] - uv[1, 0]) * (gy - uv[2, 1])) / d
        w1 = ((uv[2, 1] - uv[0, 1]) * (gx - uv[2, 0]) + (uv[0, 0] - uv[2, 0]) * (gy - uv[2, 1])) / d
        w2 = 1 - w0 - w1
        inside = (w0 >= -0.01) & (w1 >= -0.01) & (w2 >= -0.01)
        P = w0[..., None] * V[a] + w1[..., None] * V[b] + w2[..., None] * V[c]
        sub = pos[y0:y1 + 1, x0:x1 + 1]
        sub[inside] = P[inside]
    return pos
