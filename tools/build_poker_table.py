"""Gives the poker table's wood and padded rail textures of their own (make_poker_table.py's pokerwood_* and
pokerrail_*): the model's one atlas squeezed the wood into a thin strip that the whole race-track and pedestal
stretched over, a blur. The body's faces are sorted by what the atlas shows them (wood, or the rail's black), each
lot split off into its own shape and box-mapped (a tile every WOOD_TILE / RAIL_TILE units, the projection by which
way each face looks); everything else (the cup holders, the chip tray) keeps the atlas.

Run inside Blender (with PyNifly) after build_poker.py and build_poker_felt.py, the paths below set; it reads the
table they wrote and writes it back. ATLAS: poker_d as a PNG (to sort by).
"""

import colorsys
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
    return out


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
    return {"wood_faces": nw, "rail_faces": nr, "left": len(body.data.polygons)}


result = main()
