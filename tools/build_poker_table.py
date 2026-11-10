"""Gives the poker table's wood and padded rail textures of their own (make_poker_table.py's pokerwood_* and
pokerrail_*): the model's one atlas squeezed the wood into a thin strip that the whole race-track and pedestal
stretched over, a blur. The body's faces are sorted by what the atlas shows them (wood, or the rail's black), each
lot split off into its own shape and box-mapped (a tile every WOOD_TILE / RAIL_TILE units, the projection by which
way each face looks); everything else (the cup holders, the chip tray) keeps the atlas.

Run inside Blender (with PyNifly) after build_poker.py and build_poker_felt.py, the paths below set; it reads the
table they wrote and writes it back. ATLAS: poker_d as a PNG (to sort by).
"""

import colorsys
import math
import os

import bmesh
import bpy

HERE = r"C:\Users\Dan\Desktop\table\work"
NIF = os.path.join(HERE, "Data", "Meshes", "fo4roulette", "PokerTable.nif")
ATLAS = os.path.join(HERE, "poker_d.png")
WOOD_TILE = 48.0   # make_poker_table.py's
RAIL_TILE = 24.0

_src = open(os.path.join(HERE, "build_meshes.py"), encoding="utf-8").read()
_src = _src[:_src.index("\ndef main():")]
exec(compile(_src, "build_meshes.py", "exec"))


def classify(body):
    img = bpy.data.images.load(ATLAS, check_existing=True)
    W, H = img.size
    px = list(img.pixels)
    me = body.data
    uvl = me.uv_layers.active.data
    out = {}
    for p in me.polygons:
        us = [uvl[li].uv for li in p.loop_indices]
        u = sum(a[0] for a in us) / len(us)
        v = sum(a[1] for a in us) / len(us)
        x = min(W - 1, max(0, int(u * W)))
        y = min(H - 1, max(0, int(v * H)))
        i = (y * W + x) * 4
        h, s, val = colorsys.rgb_to_hsv(px[i], px[i + 1], px[i + 2])
        if val < 0.16:
            out[p.index] = "rail"
        elif 0.02 < h < 0.12 and s > 0.4:
            out[p.index] = "wood"
    # (a face sorted unlike all its neighbours is the atlas's doing, not the table's: a cup holder's triangle whose
    # middle fell on the wood next to it in the atlas, a notch of walnut cut into the leather. It goes with its
    # neighbours: the most of them, a few passes)
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    for _ in range(3):
        changed = 0
        for f in bm.faces:
            kinds = []
            for e in f.edges:
                for g in e.link_faces:
                    if g.index != f.index:
                        kinds.append(out.get(g.index, "atlas"))
            if not kinds:
                continue
            mine = out.get(f.index, "atlas")
            best = max(set(kinds), key=kinds.count)
            if mine not in kinds and kinds.count(best) * 2 > len(kinds):
                if best == "atlas":
                    out.pop(f.index, None)
                else:
                    out[f.index] = best
                changed += 1
        if not changed:
            break
    bm.free()
    return out


HOLDERS = [(27.9, -47.1), (1.1, -71.2), (-25.8, -47.1), (-25.8, -0.2), (-25.8, 46.7), (1.1, 70.8), (27.9, 46.7)]
HOLDER_R = 3.3     # the leather tops reach 3.1 from their middles
HOLDER_UV_R = 0.066  # where the edge of a leather top lands in the atlas: inside the drawn disc's stitching


def fix_holders(body):
    """The cup holders' leather tops onto their discs in the atlas cleanly: the model's UVs put the tops' edges a
    little outside the drawn discs, onto the dark bits round them (a notch of it showed at each holder's edge). Each
    top's UVs fitted as one flat map (least squares) and pulled in about its middle till its edge is inside the
    disc's stitching."""
    import numpy as np
    me = body.data
    uvl = me.uv_layers.active.data
    fixed = 0
    for hx, hy in HOLDERS:
        faces = [p for p in me.polygons if p.normal.z > 0.9 and math.hypot(p.center.x - hx, p.center.y - hy) < HOLDER_R]
        if not faces:
            continue
        top = max(p.center.z for p in faces)
        faces = [p for p in faces if abs(p.center.z - top) < 0.015]  # (the wood round them is 0.03 lower)
        rows, us = [], []
        for p in faces:
            for li in p.loop_indices:
                co = me.vertices[me.loops[li].vertex_index].co
                rows.append([co.x - hx, co.y - hy, 1.0])
                us.append(list(uvl[li].uv))
        A, *_ = np.linalg.lstsq(np.array(rows), np.array(us), rcond=None)  # (3 x 2)
        c = A[2]
        rim = max(math.hypot(r[0], r[1]) for r in rows)
        scale = np.linalg.norm(A[:2], axis=0).mean() * rim  # (the edge's distance from the middle, in the atlas)
        f = HOLDER_UV_R / scale
        for p in faces:
            for li in p.loop_indices:
                co = me.vertices[me.loops[li].vertex_index].co
                uv = np.array([co.x - hx, co.y - hy, 1.0]) @ A
                uvl[li].uv = tuple(c + (uv - c) * f)
        fixed += len(faces)
    return fixed


def split(body, faces, name, mat, tile):
    """the faces into a shape of their own, box-mapped"""
    bm = bmesh.new()
    bm.from_mesh(body.data)
    bm.faces.ensure_lookup_table()
    sel = [bm.faces[i] for i in faces]
    nb = bmesh.new()
    uv = nb.loops.layers.uv.new("UVMap")
    vmap = {}
    for f in sel:
        vs = []
        for v in f.verts:
            if v.index not in vmap:
                vmap[v.index] = nb.verts.new(v.co)
            vs.append(vmap[v.index])
        try:
            g = nb.faces.new(vs)
        except ValueError:
            continue
        g.smooth = f.smooth
        n = f.normal
        ax = max(range(3), key=lambda k: abs(n[k]))
        for lp in g.loops:
            p = lp.vert.co
            if ax == 2:
                lp[uv].uv = (p.x / tile, p.y / tile)
            elif ax == 0:
                lp[uv].uv = (p.y / tile, p.z / tile)
            else:
                lp[uv].uv = (p.x / tile, p.z / tile)
    bmesh.ops.delete(bm, geom=sel, context='FACES')
    bm.to_mesh(body.data)
    bm.free()
    me = bpy.data.meshes.new(name)
    nb.to_mesh(me)
    nb.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    o['pynNodeName'] = name
    o['pynBlockName'] = 'BSTriShape'
    me.materials.append(mat)
    o.parent = body.parent
    return o, len(me.polygons)


def main():
    clear()
    tmpl = template_material()
    m_wood = make_material(tmpl, "PokerWood", "pokerwood_d", "pokerwood_n", "pokerwood_s", 1.0)
    m_rail = make_material(tmpl, "PokerRail", "pokerrail_d", "pokerrail_n", "pokerrail_s", 0.6)
    with ctx():
        bpy.ops.import_scene.pynifly(filepath=NIF)
    body = bpy.data.objects["PokerTableBody"]
    root = body.parent
    system = bpy.data.objects["bhkPhysicsSystem"]
    bsx = next(o for o in bpy.data.objects if o.name.startswith("BSXFlags"))
    felt = bpy.data.objects["PokerFelt"]
    holders = fix_holders(body)
    kinds = classify(body)
    wood_faces = [i for i, k in kinds.items() if k == "wood"]
    rail_faces = [i for i, k in kinds.items() if k == "rail"]
    # (split one lot, then sort again: the face numbers change)
    wood, nw = split(body, wood_faces, "PokerWood", m_wood, WOOD_TILE)
    kinds = classify(body)
    rail, nr = split(body, [i for i, k in kinds.items() if k == "rail"], "PokerRail", m_rail, RAIL_TILE)
    if not any(c.type == 'COPY_TRANSFORMS' for c in root.constraints):
        c = root.constraints.new('COPY_TRANSFORMS')
        c.target = system
    extra = [o for o in bpy.data.objects if o.type == 'MESH' and o.parent == root and o.name == "PokerPrint"]
    export([root, bsx, body, felt, wood, rail] + extra + [system], NIF)
    return {"wood_faces": nw, "rail_faces": nr, "left": len(body.data.polygons), "holder_faces": holders}


result = main()
