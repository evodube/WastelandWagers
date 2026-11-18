"""Builds the blackjack table (Meshes/WastelandWagers/BlackjackTable.nif) in Blender (with PyNifly) from ddemon26's
"Blackjack Table" (Sketchfab, CC BY 4.0; BLACKJACK_GLB).

Scaled whole (one factor, no stretching) so the felt is the poker table's height (59.875 up); the floor at 0, the
middle of its footprint over the origin. Table space: X across (the shoe at +X), Y towards the dealer (the chip rack at
+Y), Z up. The model's parts:
- the felt: its own shape, mapped flat from above for make_blackjack.py's bjfelt (u = (x + 74) / 148,
  v = (54 - y) / 104)
- the body: sorted by what its atlas shows (build_poker_table.py's classify): the wood box-mapped with bjwood, the
  padded rail's black with bjrail, the rest (the odd trim) on the atlas (bjatlas, 2048)
- the shoe and the dealer's chip rack: on the atlas
One convex collision prism round it all (build_meshes.py's collision).

Run inside Blender after setting HERE; make_blackjack.py writes the textures.
"""

import colorsys
import math
import os

import bmesh
import bpy
from mathutils import Matrix

HERE = r"C:\Users\Dan\Desktop\table\work"
BLACKJACK_GLB = r"C:\Users\Dan\Desktop\table\model\blackjack_table.glb"
DATA = os.path.join(HERE, "Data")
FELT_Z = 59.875          # the felt's height, as the poker table's
MODEL_FELT = 1.11        # ...in the model (metres)
WOOD_TILE = 48.0
RAIL_TILE = 24.0
FELT_X, FELT_Y0, FELT_Y1 = 74.0, -50.0, 54.0  # make_blackjack.py's

# build_meshes.py's helpers (ctx, clear, template_material, make_material, join, node, parent_keep, collision, export)
_src = open(os.path.join(HERE, "build_meshes.py"), encoding="utf-8").read()
_src = _src[:_src.index("\ndef main():")]
exec(compile(_src, "build_meshes.py", "exec"))
TEX = "textures\\WastelandWagers\\"
# build_poker_table.py's classify and split (the atlas to sort by: the blackjack model's own)
_src = open(os.path.join(HERE, "build_poker_table.py"), encoding="utf-8").read()
_src = _src[_src.index("\ndef classify("):_src.index("\ndef main():")]
exec(compile(_src, "build_poker_table.py", "exec"))
ATLAS = os.path.join(HERE, "blackjack_atlas.png")


def import_model():
    before = set(bpy.data.objects)
    with ctx():
        bpy.ops.import_scene.gltf(filepath=BLACKJACK_GLB)
    new = [o for o in bpy.data.objects if o not in before]
    meshes = {}
    k = FELT_Z / MODEL_FELT
    for o in new:
        if o.type != 'MESH':
            continue
        o.data.transform(Matrix.Scale(k, 4) @ o.matrix_world)
        o.parent = None
        o.matrix_world = Matrix.Identity(4)
        # (one UV set: the first, the atlas's)
        while len(o.data.uv_layers) > 1:
            o.data.uv_layers.remove(o.data.uv_layers[-1])
        o.data.uv_layers.active = o.data.uv_layers[0]
        meshes[o.name.split(".")[0]] = o
    for o in new:
        if o.type != 'MESH':
            bpy.data.objects.remove(o, do_unlink=True)
    return meshes, k


def felt_uv(o):
    me = o.data
    uv = me.uv_layers.active
    for li, lp in enumerate(me.loops):
        c = me.vertices[lp.vertex_index].co
        uv.data[li].uv = ((c.x + FELT_X) / (2 * FELT_X), 1.0 - (FELT_Y1 - c.y) / (FELT_Y1 - FELT_Y0))
    for p in me.polygons:
        p.use_smooth = False


def main():
    clear()
    tmpl = template_material()
    m_felt = make_material(tmpl, "BjFelt", "bjfelt_d", "bjfelt_n", "felt_s", 0.2)
    m_wood = make_material(tmpl, "BjWood", "bjwood_d", "bjwood_n", "bjwood_s", 1.0)
    m_rail = make_material(tmpl, "BjRail", "bjrail_d", "bjrail_n", "bjrail_s", 0.6)
    m_atlas = make_material(tmpl, "BjAtlas", "bjatlas_d", "flat_n", "felt_s", 0.6)
    parts, k = import_model()
    felt, body, shoe, rack = parts["Object_11"], parts["Object_10"], parts["Object_6"], parts["Object_8"]
    root = node("BlackjackTable")
    root['pynRoot'] = True
    root['pynBlockName'] = 'NiNode'
    system, hull_n = collision(root, [body])
    bsx = bpy.data.objects.new("BSXFlags:BSX", None)
    bpy.context.scene.collection.objects.link(bsx)
    bsx.parent = root
    bsx.pyn_bsxflags.name = "BSX"
    bsx.pyn_bsxflags.value = "HAVOC | ARTICULATED"

    felt_uv(felt)
    felt = join([felt], "BlackjackFelt", m_felt)
    parent_keep(felt, root)
    props = join([shoe, rack], "BlackjackProps", m_atlas)
    parent_keep(props, root)
    body = join([body], "BlackjackBody", m_atlas)
    parent_keep(body, root)
    kinds = classify(body)
    wood, nw = split(body, [i for i, kk in kinds.items() if kk == "wood"], "BlackjackWood", m_wood, WOOD_TILE)
    kinds = classify(body)
    rail, nr = split(body, [i for i, kk in kinds.items() if kk == "rail"], "BlackjackRail", m_rail, RAIL_TILE)
    if not any(c.type == 'COPY_TRANSFORMS' for c in root.constraints):
        c = root.constraints.new('COPY_TRANSFORMS')
        c.target = system
    vs = [v.co for o in (body, wood, rail, felt) for v in o.data.vertices]
    size = [round(max(v[i] for v in vs) - min(v[i] for v in vs), 2) for i in range(3)]
    export([root, bsx, body, wood, rail, felt, props, system], os.path.join(DATA, "Meshes", "WastelandWagers", "BlackjackTable.nif"))
    return {"scale": round(k, 3), "size": size, "hull": hull_n, "wood_faces": nw, "rail_faces": nr, "left": len(body.data.polygons)}


result = main()
