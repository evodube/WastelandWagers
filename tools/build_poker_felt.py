"""Gives the poker table its own felt: the felt's faces split off PokerTable.nif's body into a shape of their own
(PokerFelt), mapped flat from above for make_poker_felt.py's texture (pokerfelt_d/_n, felt_s).

Run inside Blender (with PyNifly) after build_poker.py, the paths below set; it reads the table build_poker.py wrote
and writes it back. (The felt is the body's flat faces at FELT_Z inside the felt's outline.)
"""

import os

import bmesh
import bpy

HERE = r"C:\Users\Dan\Desktop\table\work"
NIF = os.path.join(HERE, "Data", "Meshes", "fo4roulette", "PokerTable.nif")
FELT_Z = 59.875  # (build_poker.py: 53 + RAISE; a NIF's half floats keep it exactly)
HALF_W, HALF_L = 20.25, 63.5   # make_poker_felt.py's...
INSET = 3.4
PLAQUE_W, PLAQUE_H = 21.0, 3.4
PLAQUE_V = 1.0 - 166 / 256       # (the plaque's rows in pokerprint_d: the top 166 of 256)
PLAYERS_TITLE_Y = 21.0

_src = open(os.path.join(HERE, "build_meshes.py"), encoding="utf-8").read()
_src = _src[:_src.index("\ndef main():")]
exec(compile(_src, "build_meshes.py", "exec"))


def plaques(root, mat):
    """TEXAS HOLD'EM: a plaque on the felt's line on each long side (its own shape, its own sharp texture), upright for
    whoever's on that side: the players' (-X, along from the middle seat) and the dealer's (+X, in the middle)"""
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    r = HALF_W - INSET
    z = FELT_Z + 0.0625  # (clear of the felt: closer and they fight over which is drawn. A NIF keeps positions to
    # 1/32 at this height (half floats): two steps up)
    for sx, cy in ((-1, PLAYERS_TITLE_Y), (1, 0.0)):
        cx = sx * r
        up = -sx          # (which way the letters' tops point, in X: away from the reader)
        # reading left to right along -Y on the players' side, +Y on the dealer's
        right = -1 if sx < 0 else 1
        hw, hh = PLAQUE_W / 2, PLAQUE_H / 2
        corners = [  # (x, y, u, v): top-left, bottom-left, bottom-right, top-right
            (cx + up * hh, cy - right * hw, 0.0, 1.0),
            (cx - up * hh, cy - right * hw, 0.0, PLAQUE_V),
            (cx - up * hh, cy + right * hw, 1.0, PLAQUE_V),
            (cx + up * hh, cy + right * hw, 1.0, 1.0),
        ]
        vs = [bm.verts.new((x, y, z)) for x, y, _, _ in corners]
        f = bm.faces.new(vs)
        f.normal_update()
        if f.normal.z < 0:
            f.normal_flip()
        for lp in f.loops:
            k = vs.index(lp.vert)
            lp[uv].uv = (corners[k][2], corners[k][3])
    me = bpy.data.meshes.new("PokerPrint")
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new("PokerPrint", me)
    bpy.context.scene.collection.objects.link(o)
    o['pynNodeName'] = "PokerPrint"
    o['pynBlockName'] = 'BSTriShape'
    me.materials.append(mat)
    o.parent = root
    return o


def add_plaques_only():
    """(to a table already split: the plaques added)"""
    clear()
    tmpl = template_material()
    m_print = make_material(tmpl, "PokerPrint", "pokerprint_d", "flat_n", "felt_s", 0.2)
    with ctx():
        bpy.ops.import_scene.pynifly(filepath=NIF)
    body = bpy.data.objects["PokerTableBody"]
    root = body.parent
    system = bpy.data.objects["bhkPhysicsSystem"]
    bsx = next(o for o in bpy.data.objects if o.name.startswith("BSXFlags"))
    old = bpy.data.objects.get("PokerPrint")
    if old:
        bpy.data.objects.remove(old, do_unlink=True)
    pr = plaques(root, m_print)
    if not any(c.type == 'COPY_TRANSFORMS' for c in root.constraints):
        c = root.constraints.new('COPY_TRANSFORMS')
        c.target = system
    shapes = [o for o in bpy.data.objects if o.type == 'MESH' and o.parent == root and o is not pr]
    export([root, bsx] + shapes + [pr, system], NIF)
    return {"shapes": [o.name for o in shapes + [pr]]}


def main():
    clear()
    tmpl = template_material()
    m_felt = make_material(tmpl, "PokerFelt", "pokerfelt_d", "pokerfelt_n", "felt_s", 0.2)
    with ctx():
        bpy.ops.import_scene.pynifly(filepath=NIF)
    body = bpy.data.objects["PokerTableBody"]
    root = body.parent
    system = bpy.data.objects["bhkPhysicsSystem"]
    bsx = next(o for o in bpy.data.objects if o.name.startswith("BSXFlags"))

    # the felt's faces: flat, at the felt's height, within its outline
    bm = bmesh.new()
    bm.from_mesh(body.data)
    felt_faces = []
    for f in bm.faces:
        zs = [v.co.z for v in f.verts]
        if max(zs) - min(zs) < 0.02 and abs(sum(zs) / len(zs) - FELT_Z) < 0.05 and f.normal.z > 0.9 and \
           all(abs(v.co.x) <= HALF_W + 0.05 and abs(v.co.y) <= HALF_L + 0.05 for v in f.verts):
            felt_faces.append(f)
    n = len(felt_faces)
    # into a mesh of their own
    fb = bmesh.new()
    uv = fb.loops.layers.uv.new("UVMap")
    vmap = {}
    for f in felt_faces:
        vs = []
        for v in f.verts:
            if v.index not in vmap:
                vmap[v.index] = fb.verts.new(v.co)
            vs.append(vmap[v.index])
        g = fb.faces.new(vs)
        g.smooth = False
        for lp in g.loops:
            p = lp.vert.co
            lp[uv].uv = ((HALF_L - p.y) / (2 * HALF_L), (p.x + HALF_W) / (2 * HALF_W))  # (u along -Y: not mirrored)
    bmesh.ops.delete(bm, geom=felt_faces, context='FACES')
    bm.to_mesh(body.data)
    bm.free()
    me = bpy.data.meshes.new("PokerFelt")
    fb.to_mesh(me)
    fb.free()
    felt = bpy.data.objects.new("PokerFelt", me)
    bpy.context.scene.collection.objects.link(felt)
    felt['pynNodeName'] = "PokerFelt"
    felt['pynBlockName'] = 'BSTriShape'
    me.materials.append(m_felt)
    felt.parent = root
    # (the collision as it was: the table's root follows it)
    if not any(c.type == 'COPY_TRANSFORMS' for c in root.constraints):
        c = root.constraints.new('COPY_TRANSFORMS')
        c.target = system
    m_print = make_material(tmpl, "PokerPrint", "pokerprint_d", "flat_n", "felt_s", 0.2)
    pr = plaques(root, m_print)
    export([root, bsx, body, felt, pr, system], NIF)
    return {"felt_faces": n}


if __name__ == "felt_plaques":
    result = add_plaques_only()
else:
    result = main()
