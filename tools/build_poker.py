"""Builds the poker table and the 52 cards in Blender (with PyNifly), for Fallout 4.

    the table: Pieter Ferreira's "Poker Table" (Sketchfab, CC BY 4.0), with the few chips and cards modelled lying
               on its felt taken off (the plugin deals its own). Scaled so the felt is 53 units (76 cm) up; origin on
               the floor under the middle of the felt; X across the table (the chip tray, the dealer's place, at +X),
               Y along it, Z up. One convex collision body round it. -> Meshes\\fo4roulette\\PokerTable.nif
    the cards: a playing card each (63 x 88 mm), face up with its top towards +Y, origin in the middle of its
               underside; the face from make_cards.py's atlas (cell k for card k), the back underneath.
               -> Meshes\\fo4roulette\\Cards\\Card_<rank><suit>.nif

Run inside Blender after setting the paths below; it reuses build_meshes.py's helpers (same folder).
"""

import math
import os

import bmesh
import bpy
from mathutils import Matrix, Vector

HERE = r"C:\Users\Dan\Desktop\table\work"
OBJ = r"C:\Users\Dan\Desktop\poker table\latest poker table.obj"
DATA = os.path.join(HERE, "Data")
FELT_HEIGHT = 53.0                    # units from the floor to the felt
FELT_MODEL = 2.862                    # the felt's height in the model (Y up)
CARD_W, CARD_H, CARD_T = 4.41, 6.16, 0.06
COLS, ROWS = 8, 7                     # make_cards.py's atlas
RANKS = "23456789TJQKA"
SUITS = "cdhs"

# build_meshes.py's helpers (ctx, clear, template_material, make_material, join, node, parent_keep, collision, export)
_src = open(os.path.join(HERE, "build_meshes.py"), encoding="utf-8").read()
_src = _src[:_src.index("\ndef main():")]
exec(compile(_src, "build_meshes.py", "exec"))
TEX = "textures\\fo4roulette\\"


def import_table():
    with ctx():
        bpy.ops.wm.obj_import(filepath=OBJ, forward_axis='NEGATIVE_Z', up_axis='Y')
    o = [x for x in bpy.data.objects if x.type == 'MESH'][0]
    o.data.transform(o.matrix_world)
    o.matrix_world = Matrix.Identity(4)
    # the chips and cards lying on the felt: off (single-face cards, and the little chip stacks in the middle; the
    # chips standing in the dealer's tray and the cup holders stay)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.faces.ensure_lookup_table()
    seen, drop = set(), []
    for f in bm.faces:
        if f.index in seen:
            continue
        part, stack = [], [f]
        seen.add(f.index)
        while stack:
            g = stack.pop()
            part.append(g)
            for e in g.edges:
                for h in e.link_faces:
                    if h.index not in seen:
                        seen.add(h.index)
                        stack.append(h)
        vs = {v for g in part for v in g.verts}
        xs = [v.co.x for v in vs]
        zs = [v.co.z for v in vs]
        if (len(part) == 1 and min(zs) > FELT_MODEL - 0.01) or \
           (len(part) == 80 and min(xs) > 0.0 and max(xs) < 0.2 and min(zs) > FELT_MODEL - 0.01):
            drop.extend(part)
    bmesh.ops.delete(bm, geom=drop, context='FACES')
    bm.to_mesh(o.data)
    bm.free()
    # to the table's space: the felt's middle over the origin, the floor at 0, 53 units up to the felt
    vs = [v.co for v in o.data.vertices]
    floor = min(v.z for v in vs)
    cx = (min(v.x for v in vs) + max(v.x for v in vs)) / 2
    cy = (min(v.y for v in vs) + max(v.y for v in vs)) / 2
    k = FELT_HEIGHT / (FELT_MODEL - floor)
    o.data.transform(Matrix.Scale(k, 4) @ Matrix.Translation((-cx, -cy, -floor)))
    return o, len(drop), k


def card_mesh(name, k, mat):
    """card k of the atlas, face up; the back (cell 52) underneath; the edges white"""
    col, row = k % COLS, k // COLS
    bcol, brow = 52 % COLS, 52 // COLS
    eps = 0.5 / 2048

    def cell(c, r):
        return (c / COLS + eps, (c + 1) / COLS - eps, 1 - (r + 1) / ROWS + eps, 1 - r / ROWS - eps)
    u0, u1, v0, v1 = cell(col, row)
    b0, b1, c0, c1 = cell(bcol, brow)
    w, h, t = CARD_W / 2, CARD_H / 2, CARD_T
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")

    def quad(pts, uvs):
        f = bm.faces.new([bm.verts.new(p) for p in pts])
        for lp, c in zip(f.loops, uvs):
            lp[uv].uv = c
        f.smooth = False
        return f
    # face (seen from above: top of the card to +Y)
    quad([(-w, -h, t), (w, -h, t), (w, h, t), (-w, h, t)], [(u0, v0), (u1, v0), (u1, v1), (u0, v1)])
    # back (seen from below)
    quad([(-w, -h, 0), (-w, h, 0), (w, h, 0), (w, -h, 0)], [(b1, c0), (b1, c1), (b0, c1), (b0, c0)])
    # the edges: a white corner of the face's cell
    white = [(u0 + 0.002, v1 - 0.002)] * 4
    quad([(-w, -h, 0), (w, -h, 0), (w, -h, t), (-w, -h, t)], white)
    quad([(w, -h, 0), (w, h, 0), (w, h, t), (w, -h, t)], white)
    quad([(w, h, 0), (-w, h, 0), (-w, h, t), (w, h, t)], white)
    quad([(-w, h, 0), (-w, -h, 0), (-w, -h, t), (-w, h, t)], white)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    o['pynNodeName'] = name
    o['pynBlockName'] = 'BSTriShape'
    me.materials.append(mat)
    return o


def main():
    clear()
    tmpl = template_material()
    m_table = make_material(tmpl, "PokerTable", "poker_d", "poker_n", "poker_s", 1.0)
    m_card = make_material(tmpl, "Card", "cards_d", "flat_n", "card_s", 0.4,
                           flags1="SPECULAR | ZBUFFER_TEST")

    table, dropped, k = import_table()
    root = node("PokerTable")
    root['pynRoot'] = True
    root['pynBlockName'] = 'NiNode'
    system, hull_n = collision(root, [table])
    bsx = bpy.data.objects.new("BSXFlags:BSX", None)
    bpy.context.scene.collection.objects.link(bsx)
    bsx.parent = root
    bsx.pyn_bsxflags.name = "BSX"
    bsx.pyn_bsxflags.value = "HAVOC | ARTICULATED"
    body = join([table], "PokerTableBody", m_table)
    parent_keep(body, root)
    vs = [v.co for v in body.data.vertices]
    size = [round(max(v[i] for v in vs) - min(v[i] for v in vs), 2) for i in range(3)]
    export([root, bsx, body, system], os.path.join(DATA, "Meshes", "fo4roulette", "PokerTable.nif"))

    # the cards, each its own NIF
    for o in list(bpy.data.objects):
        o.hide_set(True)
    n = 0
    for s, suit in enumerate(SUITS):
        for r, rank in enumerate(RANKS):
            idx = s * 13 + r
            name = f"Card_{rank}{suit}"
            croot = node(name)
            croot['pynRoot'] = True
            croot['pynBlockName'] = 'NiNode'
            c = card_mesh(name + ":0", idx, m_card)
            c.parent = croot
            export([croot, c], os.path.join(DATA, "Meshes", "fo4roulette", "Cards", name + ".nif"))
            n += 1
    return {"dropped_faces": dropped, "scale": round(k, 3), "size": size, "hull": hull_n, "cards": n}


result = main()
