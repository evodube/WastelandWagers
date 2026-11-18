"""Builds WastelandWagers's meshes in Blender (with PyNifly) from the roulette table model.

Run inside Blender (Text editor, or blender --python), after setting the paths below:

    OBJ       the table model (RuletMasası.obj)
    TEMPLATE  vanilla Meshes\\SetDressing\\PoolTable\\PoolTable01.nif: only its shader setup and its collision
              object's structure are borrowed (its geometry is thrown away)
    OUT       the Data folder to write Meshes\\WastelandWagers\\ into

Writes:
    RouletteTable.nif   the table, the bowl and its diamonds, both layouts (LayoutEU, LayoutUS: the plugin hides the
                        one not in use) and both rotors (NiNodes RotorEU and RotorUS, each the numbered disc and the
                        turret, origin on the wheel's axis: the plugin turns them). One convex collision body.
    Ball.nif            the ball, origin at its centre
(The chips are matveuk's set: build_chips.py.)

The rotor is rebuilt here rather than taken from the model: a surface of revolution with the model's profile, its
disc (pocket floors, number ring) with planar UVs for make_textures.py's wheel_eu/us_d, and the cone, a real fret
between every pocket (37 or 38) and the turret in polished metal with the game's own reflections (the pool table's
cube map).

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

SCALE = 5.6  # (6.6 until 0.4.3; src/rl/Table.h kScale)
# then (0.5.2) 10% narrower and 5% higher: across the table (Y) by WIDE, up (Z) by HIGH. The wheel stays round: within
# WHEEL_R of its axis it's only moved (in Y, to where its centre goes), and between there and WHEEL_R + BLEND the
# one way blends into the other (src/rl/Table.h kWide, kHigh)
WIDE = 0.855  # (0.9 at 0.5.2, then 5% narrower again)
HIGH = 1.155  # (1.05 at 0.5.2, then 10% higher again)
WHEEL_R = 7.2 * SCALE  # (the bowl reaches 7.1 from the axis; the table round it starts at 7.0 below the felt, 9.4 above)
BLEND = 2.0 * SCALE
WOOD_TILE = 48.0  # game units a tile of the walnut (make_poker_table.py's, as on the poker table)
ORIGIN = Vector((12.005, 3.755, -8.22))   # model units: footprint centre, floor
BALL_R = 0.16 * SCALE
TEX = "textures\\WastelandWagers\\"
CUBEMAP = "textures\\Shared\\Cubemaps\\mipblur_DefaultOutside1.dds"  # as the vanilla pool table uses

# the rotor's profile (model units: radius, height), from the model's disc; the wheel's axis is the model's origin
CONE = [(0.0, 1.752), (0.78, 1.752), (1.305, 1.679), (1.827, 1.585), (2.352, 1.517), (2.784, 1.481), (2.83, 1.481),
        (2.83, 1.356)]
DISC = [(2.83, 1.356), (3.73, 1.356), (3.73, 1.481), (3.784, 1.481), (4.45, 1.713)]
FRET_R = (2.83, 3.73)
FRET_TOP = 1.47
FRET_HALF = 0.032
ROTOR_UV = 4.5  # make_textures.py's planar mapping: u = 0.5 + x / (2 * ROTOR_UV)


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
    mat.name = "WastelandWagers_template"
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


def wheel_centre():
    """the wheel's axis in the table's space, before narrowing (the model's origin)"""
    return Vector((-ORIGIN.x * SCALE, -ORIGIN.y * SCALE, 0))


def narrow(o):
    """WIDE across and HIGH up, the wheel kept round (see WIDE)"""
    c = wheel_centre()
    for v in o.data.vertices:
        p = v.co
        d = math.hypot(p.x - c.x, p.y - c.y)
        moved = p.y + (WIDE - 1.0) * c.y   # (the wheel: only moved)
        scaled = p.y * WIDE                 # (the rest: narrower)
        t = min(1.0, max(0.0, (d - WHEEL_R) / BLEND))
        t = t * t * (3.0 - 2.0 * t)
        p.y = moved + (scaled - moved) * t
        p.z = p.z * HIGH
    o.data.update()


def import_model():
    with ctx():
        bpy.ops.wm.obj_import(filepath=OBJ, use_split_groups=True, forward_axis='NEGATIVE_Z', up_axis='Y')
    xf = Matrix.Scale(SCALE, 4) @ Matrix.Translation(-ORIGIN)
    for o in bpy.data.objects:
        o.data.transform(o.matrix_world)
        o.matrix_world = Matrix.Identity(4)
        o.data.transform(xf)
        narrow(o)
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
    o['pynBlockName'] = 'BSTriShape'  # without it PyNifly writes NiTriShape, which Fallout 4 can't load
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
    bpy.context.view_layer.update()  # a new empty's matrix_world is identity until the scene updates
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
        o['PYN_GAME'] = 'FO4'
    bpy.context.view_layer.objects.active = objs[0]
    with ctx():
        # intuit_defaults off: left on, PyNifly guesses the game from the objects and wrote Skyrim NIFs
        # (NiTriShape, user version 83), which crash Fallout 4 as soon as it loads them
        bpy.ops.export_scene.pynifly(filepath=path, target_game='FO4', preserve_hierarchy=True,
                                     export_colors=False, write_tris=False, intuit_defaults=False)


def revolve(name, profile, segments, mat):
    """a surface of revolution; each step of the profile its own band (hard edges between bands, smooth round)"""
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    for (r0, z0), (r1, z1) in zip(profile, profile[1:]):
        exp = Vector((-(z1 - z0), r1 - r0))  # the side that's seen: (radial, up)
        ring0, ring1 = [], []
        for j in range(segments):
            a = 2 * math.pi * j / segments
            ring0.append(bm.verts.new((r0 * math.cos(a), r0 * math.sin(a), z0)) if r0 > 0 or j == 0 else ring0[0])
            ring1.append(bm.verts.new((r1 * math.cos(a), r1 * math.sin(a), z1)))
        for j in range(segments):
            k = (j + 1) % segments
            vs = [ring0[j], ring1[j], ring1[k], ring0[k]] if r0 > 0 else [ring0[0], ring1[j], ring1[k]]
            f = bm.faces.new(vs)
            f.normal_update()
            a = 2 * math.pi * (j + 0.5) / segments
            want = Vector((exp.x * math.cos(a), exp.x * math.sin(a), exp.y))
            if f.normal.dot(want) < 0:
                f.normal_flip()
            f.smooth = True
            for lp in f.loops:
                lp[uv].uv = (0.5 + lp.vert.co.x / (2 * ROTOR_UV), 0.5 + lp.vert.co.y / (2 * ROTOR_UV))
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    return o


def frets(name, n):
    """a fret at each boundary between pockets: pocket k is centred at 90 - k*360/n degrees (clockwise from above)"""
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    step = 360.0 / n
    for k in range(n):
        a = math.radians(90.0 - (k + 0.5) * step)
        d = Vector((math.cos(a), math.sin(a), 0))
        s = Vector((-math.sin(a), math.cos(a), 0)) * FRET_HALF
        pts = {}
        for ri, r in enumerate(FRET_R):
            for si, sg in enumerate((-1, 1)):
                for zi, z in enumerate((DISC[0][1], FRET_TOP)):
                    pts[ri, si, zi] = d * r + s * sg + Vector((0, 0, z))
        quads = [
            [(0, 0, 1), (0, 1, 1), (1, 1, 1), (1, 0, 1)],  # top
            [(0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)],  # one side
            [(0, 1, 0), (0, 1, 1), (1, 1, 1), (1, 1, 0)],  # the other
            [(0, 0, 0), (0, 0, 1), (0, 1, 1), (0, 1, 0)],  # inner end
            [(1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1)],  # outer end
        ]
        centre = d * (sum(FRET_R) / 2) + Vector((0, 0, (DISC[0][1] + FRET_TOP) / 2))
        for q in quads:
            f = bm.faces.new([bm.verts.new(pts[i]) for i in q])
            f.normal_update()
            if f.normal.dot(f.calc_center_median() - centre) < 0:
                f.normal_flip()
            f.smooth = False
            for lp in f.loops:
                lp[uv].uv = (0.5 + lp.vert.co.x / (2 * ROTOR_UV), 0.5 + lp.vert.co.y / (2 * ROTOR_UV))
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    return o


def to_table(o):
    """model units to the table's space, as import_model does"""
    o.data.transform(Matrix.Scale(SCALE, 4) @ Matrix.Translation(-ORIGIN))
    narrow(o)


def add_envmap(path, shape_names):
    """PyNifly leaves environment maps out of Fallout 4 NIFs: put them in, as the vanilla pool table has them (shader
    type Environment Map, the flag, its cube map)"""
    import sys
    pn = next(m for k, m in sys.modules.items() if k.endswith('pyn.pynifly'))
    nif = pn.NifFile(path)
    done = []
    for s in nif.shapes:
        if s.name in shape_names:
            p = s.shader.properties
            p.Shader_Type = 1
            p.Shader_Flags_1 |= 0x80
            p.Env_Map_Scale = 1.4
            s.shader.write_properties()
            s.shader.textures['EnvMap'] = CUBEMAP
            s.shader.textures['EnvMask'] = TEX + "metal_m.dds"
            done.append(s.name)
    nif.save()
    return done


# the betting grid's rectangle (model units: make_textures.py's GRID) and the layout's UVs there (its grid_uv())
GRID = (9.3, 32.2, -6.6, 5.7)
GRID_UV = (0.003214, 0.566563, 0.250350, 0.552935)


def split_grid(o, name, mat):
    """the felt cut along the betting grid's rectangle, what's inside a shape of its own (name) with the grid's own
    sharp texture (make_textures.py's layoutgrid_*): its UVs the layout's, scaled to the rectangle"""
    x0, x1 = (GRID[0] - ORIGIN.x) * SCALE, (GRID[1] - ORIGIN.x) * SCALE
    y0, y1 = (GRID[2] - ORIGIN.y) * SCALE * WIDE, (GRID[3] - ORIGIN.y) * SCALE * WIDE
    bm = bmesh.new()
    bm.from_mesh(o.data)
    for co, no in (((x0, 0, 0), (1, 0, 0)), ((x1, 0, 0), (1, 0, 0)), ((0, y0, 0), (0, 1, 0)), ((0, y1, 0), (0, 1, 0))):
        geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
        bmesh.ops.bisect_plane(bm, geom=geom, plane_co=co, plane_no=no)
    uv = bm.loops.layers.uv.active
    inside = []
    for f in bm.faces:
        c = f.calc_center_median()
        if x0 < c.x < x1 and y0 < c.y < y1:
            inside.append(f)
    nb = bmesh.new()
    nuv = nb.loops.layers.uv.new("UVMap")
    u0, u1, v0, v1 = GRID_UV
    vmap = {}
    for f in inside:
        vs = []
        for v in f.verts:
            if v.index not in vmap:
                vmap[v.index] = nb.verts.new(v.co)
            vs.append(vmap[v.index])
        g = nb.faces.new(vs)
        g.smooth = f.smooth
        for lp, lo in zip(g.loops, f.loops):
            a, b = lo[uv].uv
            lp[nuv].uv = ((a - u0) / (u1 - u0), (b - v0) / (v1 - v0))
    bmesh.ops.delete(bm, geom=inside, context='FACES')
    bm.to_mesh(o.data)
    bm.free()
    me = bpy.data.meshes.new(name)
    nb.to_mesh(me)
    nb.free()
    g = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(g)
    g['pynNodeName'] = name
    g['pynBlockName'] = 'BSTriShape'
    me.materials.append(mat)
    return g, len(inside)


def box_map(o, tile):
    """UVs from where each face is (table space, a tile every tile units), projected the way it looks: from above
    the grain runs along the table's length (X)"""
    me = o.data
    uv = me.uv_layers.active or me.uv_layers.new(name="UVMap")
    for p in me.polygons:
        n = p.normal
        ax = max(range(3), key=lambda k: abs(n[k]))
        for li in p.loop_indices:
            c = me.vertices[me.loops[li].vertex_index].co
            if ax == 2:
                uv.data[li].uv = (c.x / tile, c.y / tile)
            elif ax == 0:
                uv.data[li].uv = (c.y / tile, c.z / tile)
            else:
                uv.data[li].uv = (c.x / tile, c.z / tile)


def main():
    clear()
    tmpl = template_material()
    objs = import_model()
    by = lambda key: [o for o in objs if key in o.name]  # noqa: E731

    # the wood: the poker table's walnut, tiling (make_poker_table.py), box-mapped (box_map): sharp everywhere, where
    # the model's own atlas (table_d) spread a little texture over a lot of table
    m_wood = make_material(tmpl, "Wood", "pokerwood_d", "pokerwood_n", "pokerwood_s")
    m_eu = make_material(tmpl, "WheelEU", "wheel_eu_d", "flat_n", "wheel_s", 1.2)
    m_us = make_material(tmpl, "WheelUS", "wheel_us_d", "flat_n", "wheel_s", 1.2)
    m_metal = make_material(tmpl, "Metal", "metal_d", "flat_n", "metal_s", 1.4)
    # (flat normals on the felt: with a normal map, the felt's few big triangles shade with hard diagonal edges)
    m_leu = make_material(tmpl, "LayoutEU", "layout_eu_d", "flat_n", "felt_s", 0.2)
    m_lus = make_material(tmpl, "LayoutUS", "layout_us_d", "flat_n", "felt_s", 0.2)

    disc = by("bek1")[0]
    turret = by(" Kol")
    diamonds = by("pSuperShape")  # the eight diamonds on the bowl
    layout = [o for o in objs if o.name.endswith("Masa2")][0]
    body_parts = [o for o in objs if o not in turret and o not in diamonds and o is not disc and o is not layout]
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
    box_map(body, WOOD_TILE)
    parent_keep(body, root)
    deco = join(diamonds, "Diamonds", m_metal)
    parent_keep(deco, root)
    l_eu = join([layout], "LayoutEU", m_leu)
    # the betting grid: its own shape and sharp texture, cut out of the felt (split_grid)
    m_geu = make_material(tmpl, "LayoutGridEU", "layoutgrid_eu_d", "flat_n", "felt_s", 0.2)
    m_gus = make_material(tmpl, "LayoutGridUS", "layoutgrid_us_d", "flat_n", "felt_s", 0.2)
    g_eu, grid_faces = split_grid(l_eu, "LayoutGridEU", m_geu)
    l_us = l_eu.copy()
    l_us.data = l_eu.data.copy()
    bpy.context.scene.collection.objects.link(l_us)
    l_us.name = "LayoutUS"
    l_us['pynNodeName'] = "LayoutUS"
    l_us.data.materials.clear()
    l_us.data.materials.append(m_lus)
    l_us['pynNodeFlags'] = "HIDDEN | SELECTIVE_UPDATE | SELECTIVE_UPDATE_TRANSF"
    g_us = g_eu.copy()
    g_us.data = g_eu.data.copy()
    bpy.context.scene.collection.objects.link(g_us)
    g_us.name = "LayoutGridUS"
    g_us['pynNodeName'] = "LayoutGridUS"
    g_us.data.materials.clear()
    g_us.data.materials.append(m_gus)
    g_us['pynNodeFlags'] = "HIDDEN | SELECTIVE_UPDATE | SELECTIVE_UPDATE_TRANSF"
    for o in (l_eu, l_us, g_eu, g_us):
        parent_keep(o, root)

    centre = wheel_centre()
    centre.y *= WIDE  # (where narrow() moved the wheel to)
    rotors = {}
    metal_shapes = ["Diamonds"]
    for tag, mat, pockets in (("EU", m_eu, 37), ("US", m_us, 38)):
        n = node("Rotor" + tag, centre, root, "SELECTIVE_UPDATE | SELECTIVE_UPDATE_TRANSF" +
                 (" | HIDDEN" if tag == "US" else ""))
        d = revolve("RotorDisc" + tag, DISC, pockets * 8, mat)
        to_table(d)
        d = join([d], "RotorDisc" + tag, mat)
        parent_keep(d, n)
        cone = revolve("RotorCone" + tag, CONE, pockets * 4, m_metal)
        fr = frets("RotorFrets" + tag, pockets)
        for o in (cone, fr):
            to_table(o)
        tparts = [cone, fr]
        for t in turret:
            c = t.copy()
            c.data = t.data.copy()
            bpy.context.scene.collection.objects.link(c)
            tparts.append(c)
        tt = join(tparts, "RotorMetal" + tag, m_metal)
        parent_keep(tt, n)
        rotors[tag] = (n, d, tt)
        metal_shapes.append("RotorMetal" + tag)
    for o in [disc] + turret:
        bpy.data.objects.remove(o, do_unlink=True)

    data = OUT
    table_objs = [root, bsx, body, deco, l_eu, l_us, g_eu, g_us] + [x for r in rotors.values() for x in r] + [system]
    table_path = os.path.join(data, "Meshes", "WastelandWagers", "RouletteTable.nif")
    export(table_objs, table_path)
    env = add_envmap(table_path, metal_shapes)

    # the ball
    for o in list(bpy.data.objects):
        o.hide_set(True)
    mb = make_material(tmpl, "Ball", "ball_d", "flat_n", "ball_s", 2.0)
    with ctx():
        bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=12, radius=BALL_R)
    ball = bpy.context.view_layer.objects.active
    ball.name = "Ball:0"
    ball['pynNodeName'] = "Ball:0"
    ball['pynBlockName'] = 'BSTriShape'
    ball.data.materials.append(mb)
    with ctx():
        bpy.ops.object.shade_smooth()
    rb = node("Ball")
    rb['pynRoot'] = True
    ball.parent = rb
    export([rb, ball], os.path.join(data, "Meshes", "WastelandWagers", "Ball.nif"))
    return {"grid_faces": grid_faces, "hull": hull_n, "wheel_centre": tuple(round(c, 3) for c in centre), "envmap": env}


result = main()
