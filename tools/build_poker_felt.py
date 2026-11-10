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
FELT_Z = 63.0
HALF_W, HALF_L = 20.25, 63.5   # make_poker_felt.py's

_src = open(os.path.join(HERE, "build_meshes.py"), encoding="utf-8").read()
_src = _src[:_src.index("\ndef main():")]
exec(compile(_src, "build_meshes.py", "exec"))


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
    export([root, bsx, body, felt, system], NIF)
    return {"felt_faces": n}


result = main()
