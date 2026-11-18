"""Builds WastelandWagers's chips in Blender (with PyNifly) from matveuk's "Poker chip set with values" (Sketchfab, CC
BY-NC-ND 4.0): the 1, 5, 25 and 100 chips, unchanged apart from what the game needs (no-derivatives: the model
itself isn't edited). Each chip's modifiers are applied, it's scaled to the table's chip size with its origin at the
bottom centre, and split by material, each part getting a one-colour texture of its material's colour
(Textures\\WastelandWagers\\chipcol_<rrggbb>.dds, written by make_textures.py from the list this returns).

Run inside Blender after setting the paths below. Writes Meshes\\WastelandWagers\\Chip1/5/25/100.nif.
"""

import math
import os

import bmesh
import bpy
from mathutils import Matrix, Vector

CHIPS = r"C:\Users\Dan\Desktop\table\work\poker_chip_set_numbers.blend"
TEMPLATE = r"C:\Users\Dan\Desktop\table\work\PoolTable01.nif"
OUT = r"C:\Users\Dan\Desktop\table\work\Data"
DIAMETER = 0.34 * 5.6 * 2  # the table's chips (src/rl/Table.h kChipRadius)
TEX = "textures\\WastelandWagers\\"


def ctx():
    win = bpy.context.window_manager.windows[0]
    area = next(a for a in win.screen.areas if a.type == 'VIEW_3D')
    region = next(r for r in area.regions if r.type == 'WINDOW')
    return bpy.context.temp_override(window=win, area=area, region=region)


def to_srgb(c):
    c = max(0.0, min(1.0, c))
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def main():
    bpy.ops.wm.open_mainfile(filepath=CHIPS)
    with ctx():
        bpy.ops.import_scene.pynifly(filepath=TEMPLATE)
    shape = next(o for o in bpy.data.objects if o.type == 'MESH' and o.get('pynBlockName') == 'BSTriShape')
    tmpl = shape.active_material
    tmpl.use_fake_user = True
    for o in [o for o in bpy.data.objects if o.get('PYN_GAME') or o.name.startswith(('bhk', 'BSX', 'PoolTable'))]:
        bpy.data.objects.remove(o, do_unlink=True)

    colours = set()
    sizes = {}
    deps = bpy.context.evaluated_depsgraph_get()
    for value in (1, 5, 25, 100):
        src = bpy.data.objects[str(value)]
        # the chip as shown (modifiers applied) in its own space, with its transform (a mirroring scale on some)
        mesh = bpy.data.meshes.new_from_object(src.evaluated_get(deps))
        s = src.scale  # (no rotation on these; some mirror X with a negative scale)
        mesh.transform(Matrix.Diagonal((s.x, s.y, s.z, 1.0)))
        xs = [v.co.x for v in mesh.vertices]
        ys = [v.co.y for v in mesh.vertices]
        zs = [v.co.z for v in mesh.vertices]
        centre = Vector(((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, min(zs)))
        k = DIAMETER / max(max(xs) - min(xs), max(ys) - min(ys))
        mesh.transform(Matrix.Scale(k, 4) @ Matrix.Translation(-centre))
        sizes[value] = (max(zs) - min(zs)) * k
        if not mesh.uv_layers:
            mesh.uv_layers.new(name="UVMap")
        # for the game's per-vertex normals: hard edges split (as Blender's smooth-by-angle shows them, 30 degrees,
        # and any marked sharp), and every face made triangles properly (the numbers are concave n-gons, which a
        # simple fan breaks: the 1s vanished)
        bm = bmesh.new()
        bm.from_mesh(mesh)
        # every face facing outwards: the set's numbers (mirrored, solidified text) come in with their faces turned
        # inwards, which Blender draws two-sided but the game culls (the 1 of the 100 vanished, dark patches)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
        sharp = bm.edges.layers.bool.get("sharp_edge")
        hard = [e for e in bm.edges if len(e.link_faces) == 2 and (e.calc_face_angle(0.0) > math.radians(30.0) or (sharp and e[sharp]))]
        bmesh.ops.split_edges(bm, edges=hard)
        bmesh.ops.triangulate(bm, faces=bm.faces[:], quad_method='BEAUTY', ngon_method='BEAUTY')
        for f in bm.faces:
            f.smooth = True
        bm.to_mesh(mesh)
        bm.free()

        root = bpy.data.objects.new(f"Chip{value}", None)
        bpy.context.scene.collection.objects.link(root)
        root['pynRoot'] = True
        root['pynBlockName'] = 'NiNode'
        root['pynNodeName'] = f"Chip{value}"
        parts = [root]
        # one shape per material
        for mi, slot_mat in enumerate(mesh.materials):
            bm = bmesh.new()
            bm.from_mesh(mesh)
            bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index != mi], context='FACES')
            if not bm.faces:
                bm.free()
                continue
            part = bpy.data.meshes.new(f"Chip{value}:{mi}")
            bm.to_mesh(part)
            bm.free()
            bsdf = next((n for n in slot_mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None) if slot_mat and slot_mat.use_nodes else None
            col = bsdf.inputs['Base Color'].default_value if bsdf else (0.8, 0.8, 0.8, 1)
            hexcol = "".join(f"{int(round(to_srgb(c) * 255)):02x}" for c in col[:3])
            colours.add(hexcol)
            m = tmpl.copy()
            for key in list(m.keys()):
                if key.startswith('BSShaderTextureSet_'):
                    del m[key]
            m['BSShaderTextureSet_Diffuse'] = TEX + f"chipcol_{hexcol}.dds"
            m['BSShaderTextureSet_Normal'] = TEX + "flat_n.dds"
            m['BSShaderTextureSet_Specular'] = TEX + "chip_s.dds"
            m['BSLSP_Shader_Name'] = ""
            m.pyn_shader.Shader_Flags_1 = "SPECULAR | CAST_SHADOWS | ZBUFFER_TEST"
            m.pyn_shader.Shader_Flags_2 = "ZBUFFER_WRITE"
            m.pyn_shader.Shader_Type = "Default"
            m.pyn_shader.Spec_Str = 0.6
            for n in m.node_tree.nodes:
                if n.bl_idname == 'ShaderNodeTexImage':
                    n.image = None
            part.materials.clear()
            part.materials.append(m)
            o = bpy.data.objects.new(f"Chip{value}:{mi}", part)
            bpy.context.scene.collection.objects.link(o)
            o.parent = root
            o['pynNodeName'] = f"Chip{value}:{mi}"
            o['pynBlockName'] = 'BSTriShape'
            parts.append(o)
        path = os.path.join(OUT, "Meshes", "WastelandWagers", f"Chip{value}.nif")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        for o in bpy.data.objects:
            o.select_set(False)
        for o in parts:
            o.select_set(True)
            o['PYN_GAME'] = 'FO4'
        bpy.context.view_layer.objects.active = root
        with ctx():
            bpy.ops.export_scene.pynifly(filepath=path, target_game='FO4', preserve_hierarchy=True, export_colors=False,
                                         write_tris=False, intuit_defaults=False)
    return {"colours": sorted(colours), "heights": {str(v): round(h, 3) for v, h in sizes.items()}}


result = main()
