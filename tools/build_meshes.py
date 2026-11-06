"""Builds fo4roulette's meshes in Blender (with PyNifly) from the roulette table model.

Run inside Blender (Text editor, or blender --python), after setting the paths below:

    OBJ       the table model (RuletMasası.obj)
    TEMPLATE  vanilla Meshes\\SetDressing\\PoolTable\\PoolTable01.nif: only its shader setup and its collision
              object's structure are borrowed (its geometry is thrown away)
    OUT       the Data folder to write Meshes\\fo4roulette\\ into

Writes:
    RouletteTable.nif   the table, the bowl and its diamonds, both layouts (LayoutEU, LayoutUS: the plugin hides the
                        one not in use) and both rotors (NiNodes RotorEU and RotorUS, each the numbered disc and the
                        turret, origin on the wheel's axis: the plugin turns them). One convex collision body.
    Chip1/5/25/100.nif  chips, origin at the bottom centre
    Ball.nif            the ball, origin at its centre

Model units are scaled by SCALE; the table's origin is the centre of its footprint on the floor. The numbers the
plugin needs (wheel centre, layout cells, pocket ring, track) are in src/rl/Table.h, derived from the same values.
"""

import math
import os

import bmesh
import bpy
from mathutils import Matrix, Vector

OBJ = r"C:\Users\Dan\Desktop\table\model\RuletMasası.obj"
TEMPLATE = r"C:\Users\Dan\Desktop\table\work\PoolTable01.nif"
OUT = r"C:\Users\Dan\Desktop\table\work\Data"

SCALE = 6.6
ORIGIN = Vector((12.005, 3.755, -8.22))   # model units: footprint centre, floor
CHIP_R, CHIP_H = 0.34 * SCALE, 0.42
BALL_R = 0.16 * SCALE
TEX = "textures\\fo4roulette\\"


def ctx():
    win = bpy.context.window_manager.windows[0]
    area = next(a for a in win.screen.areas if a.type == 'VIEW_3D')
    region = next(r for r in area.regions if r.type == 'WINDOW')
    return bpy.context.temp_override(window=win, area=area, region=region)


def clear():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.images):
        for x in list(coll):
            coll.remove(x)


def template_material():
    """a BSLightingShaderProperty material as PyNifly imports one (from the vanilla pool table)"""
    with ctx():
        bpy.ops.import_scene.pynifly(filepath=TEMPLATE)
    shape = next(o for o in bpy.data.objects if o.type == 'MESH' and o.get('pynBlockName') == 'BSTriShape')
    mat = shape.active_material
    mat.name = "fo4roulette_template"
    mat.use_fake_user = True
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    return mat


def make_material(tmpl, name, diffuse, normal, specular, spec_str=1.0, flags1=None):
    m = tmpl.copy()
    m.name = name
    m.use_fake_user = False
    for k in list(m.keys()):
        if k.startswith('BSShaderTextureSet_'):
            del m[k]
    m['BSShaderTextureSet_Diffuse'] = TEX + diffuse + ".dds"
    m['BSShaderTextureSet_Normal'] = TEX + normal + ".dds"
    m['BSShaderTextureSet_Specular'] = TEX + specular + ".dds"
    m['BSLSP_Shader_Name'] = ""
    sh = m.pyn_shader
    sh.Shader_Flags_1 = flags1 or "SPECULAR | CAST_SHADOWS | ZBUFFER_TEST"
    sh.Shader_Flags_2 = "ZBUFFER_WRITE"
    sh.Shader_Type = "Default"
    sh.Spec_Str = spec_str
    for n in m.node_tree.nodes:
        if n.bl_idname == 'ShaderNodeTexImage':
            n.image = None
    return m


def import_model():
    with ctx():
        bpy.ops.wm.obj_import(filepath=OBJ, use_split_groups=True, forward_axis='NEGATIVE_Z', up_axis='Y')
    xf = Matrix.Scale(SCALE, 4) @ Matrix.Translation(-ORIGIN)
    for o in bpy.data.objects:
        o.data.transform(o.matrix_world)
        o.matrix_world = Matrix.Identity(4)
        o.data.transform(xf)
    return list(bpy.data.objects)


def join(objs, name, mat):
    objs = [o for o in objs]
    for o in bpy.data.objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    with ctx():
        if len(objs) > 1:
            bpy.ops.object.join()
    o = bpy.context.view_layer.objects.active
    o.name = name
    o.data.name = name
    o['pynNodeName'] = name
    o.data.materials.clear()
    o.data.materials.append(mat)
    return o


def node(name, loc=(0, 0, 0), parent=None, flags=None):
    e = bpy.data.objects.new(name, None)
    bpy.context.scene.collection.objects.link(e)
    e.location = loc
    e['pynNodeName'] = name
    e['pynBlockName'] = 'NiNode'
    if flags:
        e['pynNodeFlags'] = flags
    if parent:
        e.parent = parent
    return e


def parent_keep(child, parent):
    """parent without moving: the mesh's vertices are made relative to the parent"""
    child.data.transform(parent.matrix_world.inverted())
    child.parent = parent
    child.matrix_parent_inverse = Matrix.Identity(4)
    child.matrix_basis = Matrix.Identity(4)


def collision(root, body_objs):
    """one static convex prism round the table's body (a single polytope: compounds built from scratch crash)"""
    pts = [o.matrix_world @ v.co for o in body_objs for v in o.data.vertices]
    zmin = min(p.z for p in pts)
    ztop = max(p.z for p in pts)
    # 2D convex hull of the footprint, then thinned to at most 16 points
    xy = sorted({(round(p.x, 2), round(p.y, 2)) for p in pts})

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in xy:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(xy):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    hull = lower[:-1] + upper[:-1]
    while len(hull) > 16:
        # drop the point whose removal loses the least area
        best, bi = None, 0
        for i in range(len(hull)):
            a, b, c = hull[i - 1], hull[i], hull[(i + 1) % len(hull)]
            area = abs(cross(a, b, c))
            if best is None or area < best:
                best, bi = area, i
        hull.pop(bi)
    me = bpy.data.meshes.new("bhkPhysicsSystem_poly")
    bm = bmesh.new()
    vs = [bm.verts.new((x, y, zmin)) for x, y in hull] + [bm.verts.new((x, y, ztop)) for x, y in hull]
    bmesh.ops.convex_hull(bm, input=vs)
    bm.to_mesh(me)
    bm.free()
    system = bpy.data.objects.new("bhkPhysicsSystem", None)
    bpy.context.scene.collection.objects.link(system)
    system['pynRigidBody'] = 'bhkPhysicsSystem'
    system['pynCollisionBlockname'] = 'bhkNPCollisionObject'
    poly = bpy.data.objects.new("bhkPhysicsSystem_poly", me)
    bpy.context.scene.collection.objects.link(poly)
    poly.parent = system
    poly['pynRigidBody'] = 'bhkPhysicsSystem'
    poly['pynCollisionShapeType'] = 'polytope'
    for o in bpy.data.objects:
        o.select_set(False)
    poly.select_set(True)
    bpy.context.view_layer.objects.active = poly
    with ctx():
        bpy.ops.rigidbody.object_add(type='PASSIVE')
    c = root.constraints.new('COPY_TRANSFORMS')
    c.target = system
    return system, len(hull)


def export(objs, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for o in bpy.data.objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    with ctx():
        bpy.ops.export_scene.pynifly(filepath=path, target_game='FO4', preserve_hierarchy=True,
                                     export_colors=False, write_tris=False)


def chip_mesh(name, mat):
    """a cylinder; the faces cover the whole texture, the side wall sits on its outer rim"""
    seg = 40
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    bot = [bm.verts.new((CHIP_R * math.cos(2 * math.pi * i / seg), CHIP_R * math.sin(2 * math.pi * i / seg), 0))
           for i in range(seg)]
    top = [bm.verts.new((v.co.x, v.co.y, CHIP_H)) for v in bot]
    ft = bm.faces.new(top)
    fb = bm.faces.new(list(reversed(bot)))
    for f in (ft, fb):
        for lp in f.loops:
            lp[uv].uv = (0.5 + 0.5 * lp.vert.co.x / CHIP_R, 0.5 + 0.5 * lp.vert.co.y / CHIP_R)
    for i in range(seg):
        j = (i + 1) % seg
        f = bm.faces.new((bot[i], bot[j], top[j], top[i]))
        for lp in f.loops:
            a = math.atan2(lp.vert.co.y, lp.vert.co.x)
            if lp.vert in (bot[j], top[j]) and j == 0:
                a = 2 * math.pi
            rr = 0.49 if lp.vert.co.z > 0 else 0.47
            lp[uv].uv = (0.5 + rr * math.cos(a), 0.5 + rr * math.sin(a))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    me.materials.append(mat)
    for p in me.polygons:
        p.use_smooth = False
    o['pynNodeName'] = name
    return o


def main():
    clear()
    tmpl = template_material()
    objs = import_model()
    by = lambda key: [o for o in objs if key in o.name]  # noqa: E731

    m_wood = make_material(tmpl, "Wood", "table_d", "table_n", "table_s")
    m_eu = make_material(tmpl, "WheelEU", "wheel_eu_d", "flat_n", "wheel_s", 1.5)
    m_us = make_material(tmpl, "WheelUS", "wheel_us_d", "flat_n", "wheel_s", 1.5)
    m_leu = make_material(tmpl, "LayoutEU", "layout_eu_d", "flat_n", "felt_s", 0.2)
    m_lus = make_material(tmpl, "LayoutUS", "layout_us_d", "flat_n", "felt_s", 0.2)

    disc = by("bek1")[0]
    turret = by(" Kol")
    layout = [o for o in objs if o.name.endswith("Masa2")][0]
    body_parts = [o for o in objs if o not in turret and o is not disc and o is not layout]
    base_parts = [o for o in body_parts if "Masaalt" in o.name or "Masaevre" in o.name or "Masaçevre" in o.name]

    root = node("RouletteTable")
    root['pynRoot'] = True
    root['pynBlockName'] = 'NiNode'
    system, hull_n = collision(root, base_parts)
    # BSXFlags as on the vanilla tables: Havok (without it the collision isn't made) and Articulated
    bsx = bpy.data.objects.new("BSXFlags:BSX", None)
    bpy.context.scene.collection.objects.link(bsx)
    bsx.parent = root
    bsx.pyn_bsxflags.name = "BSX"
    bsx.pyn_bsxflags.value = "HAVOC | ARTICULATED"

    body = join(body_parts, "TableBody", m_wood)
    parent_keep(body, root)
    l_eu = join([layout], "LayoutEU", m_leu)
    l_us = l_eu.copy()
    l_us.data = l_eu.data.copy()
    bpy.context.scene.collection.objects.link(l_us)
    l_us.name = "LayoutUS"
    l_us['pynNodeName'] = "LayoutUS"
    l_us.data.materials.clear()
    l_us.data.materials.append(m_lus)
    l_us['pynNodeFlags'] = "HIDDEN | SELECTIVE_UPDATE | SELECTIVE_UPDATE_TRANSF"
    parent_keep(l_eu, root)
    parent_keep(l_us, root)

    centre = Vector((-ORIGIN.x * SCALE, -ORIGIN.y * SCALE, 0))
    rotors = {}
    for tag, mat in (("EU", m_eu), ("US", m_us)):
        n = node("Rotor" + tag, centre, root, "SELECTIVE_UPDATE | SELECTIVE_UPDATE_TRANSF" +
                 (" | HIDDEN" if tag == "US" else ""))
        d = disc.copy()
        d.data = disc.data.copy()
        bpy.context.scene.collection.objects.link(d)
        d = join([d], "RotorDisc" + tag, mat)
        parent_keep(d, n)
        tparts = []
        for t in turret:
            c = t.copy()
            c.data = t.data.copy()
            bpy.context.scene.collection.objects.link(c)
            tparts.append(c)
        tt = join(tparts, "RotorTurret" + tag, m_wood)
        parent_keep(tt, n)
        rotors[tag] = (n, d, tt)
    for o in [disc] + turret:
        bpy.data.objects.remove(o, do_unlink=True)

    data = OUT
    table_objs = [root, bsx, body, l_eu, l_us] + [x for r in rotors.values() for x in r] + [system]
    export(table_objs, os.path.join(data, "Meshes", "fo4roulette", "RouletteTable.nif"))

    # chips and ball, each on its own
    for v in (1, 5, 25, 100):
        for o in list(bpy.data.objects):
            o.hide_set(True)
        m = make_material(tmpl, f"Chip{v}", f"chip_{v}_d", "flat_n", "chip_s", 0.6)
        r = node(f"Chip{v}")
        r['pynRoot'] = True
        c = chip_mesh(f"Chip{v}:0", m)
        c.parent = r
        export([r, c], os.path.join(data, "Meshes", "fo4roulette", f"Chip{v}.nif"))
    mb = make_material(tmpl, "Ball", "ball_d", "flat_n", "ball_s", 2.0)
    with ctx():
        bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=12, radius=BALL_R)
    ball = bpy.context.view_layer.objects.active
    ball.name = "Ball:0"
    ball['pynNodeName'] = "Ball:0"
    ball.data.materials.append(mb)
    with ctx():
        bpy.ops.object.shade_smooth()
    rb = node("Ball")
    rb['pynRoot'] = True
    ball.parent = rb
    export([rb, ball], os.path.join(data, "Meshes", "fo4roulette", "Ball.nif"))
    return {"hull": hull_n, "wheel_centre": tuple(round(c, 3) for c in centre)}


result = main()
