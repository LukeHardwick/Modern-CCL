# Generates the sample database's models (task T22a) from code, so they have exactly the node
# names and pivots the CIGI articulated parts need and no licence questions.
#
#   blender --background --python tools/models/make_models.py -- \
#       --out databases/sample/models [--preview docs/guide/img/models] [--only jet]
#
# Axes: modelled in Blender's frame with the nose towards -Y and +Z up; the glTF exporter
# (+Y up) turns that into glTF's convention (front +Z, up +Y; glTF 2.0 spec 3.4). Units are
# metres. Each moving part is its own object whose origin is the hinge, so a part's attitude
# from Articulated Part Control (ICD 4.1.6) rotates it about the right point:
#   aileron_l / aileron_r / elevator: hinge along the body y axis -> part pitch
#   rudder: hinge along the body z axis -> part yaw
#   gear: shown or hidden as a whole (visibility component)

import argparse
import json
import math
import struct
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

# ---------------------------------------------------------------- scene helpers


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, rgba, roughness=0.6, metallic=0.0):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.diffuse_color = rgba  # Workbench preview colour
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = rgba
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    return mat


def object_from_bmesh(name, bm, mat, location=(0.0, 0.0, 0.0), parent=None, smooth=False):
    mesh = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(mat)
    if smooth:
        mesh.shade_smooth()
    obj = bpy.data.objects.new(name, mesh)
    obj.location = location
    bpy.context.scene.collection.objects.link(obj)
    if parent is not None:
        obj.parent = parent  # parents sit at their own origin, so no parent inverse is needed
    return obj


def loft(stations, segments=20):
    """A closed body of revolution-like shape: stations are (y, radius, z_centre, z_scale)."""
    bm = bmesh.new()
    rings = []
    for y, r, zc, zs in stations:
        ring = []
        for i in range(segments):
            a = 2.0 * math.pi * i / segments
            ring.append(bm.verts.new((r * math.cos(a), y, zc + (r * zs * math.sin(a)))))
        rings.append(ring)
    for ra, rb in zip(rings, rings[1:]):
        for i in range(segments):
            j = (i + 1) % segments
            bm.faces.new((ra[i], ra[j], rb[j], rb[i]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def slab(outline, thickness, plane="xy", taper=None):
    """A flat solid: `outline` is a convex polygon (counter-clockwise) in the given plane,
    extruded symmetrically by `thickness`. `taper(u, v)` can scale thickness per vertex."""
    bm = bmesh.new()
    top, bottom = [], []
    for u, v in outline:
        t = thickness * (taper(u, v) if taper else 1.0) / 2.0
        if plane == "xy":  # horizontal surface, thickness along z
            top.append(bm.verts.new((u, v, t)))
            bottom.append(bm.verts.new((u, v, -t)))
        else:  # "yz": vertical surface (u = y, v = z), thickness along x
            top.append(bm.verts.new((t, u, v)))
            bottom.append(bm.verts.new((-t, u, v)))
    n = len(outline)
    bm.faces.new(top)
    bm.faces.new(list(reversed(bottom)))
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((top[i], bottom[i], bottom[j], top[j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def mirrored(outline):
    """Mirror an x>0 outline (u = x) to x<0, keeping counter-clockwise order."""
    return [(-u, v) for u, v in reversed(outline)]


def cylinder_x(bm, centre, radius, width, segments=12):
    """Add an axle-aligned (x) cylinder, e.g. a wheel, to `bm`."""
    bmesh.ops.create_cone(bm, cap_ends=True, segments=segments, radius1=radius, radius2=radius,
                          depth=width,
                          matrix=_matrix(centre, rot_y=math.pi / 2))


def cylinder_z(bm, centre, radius, length, segments=8):
    bmesh.ops.create_cone(bm, cap_ends=True, segments=segments, radius1=radius, radius2=radius,
                          depth=length, matrix=_matrix(centre))


def _matrix(centre, rot_y=0.0):
    from mathutils import Matrix
    return Matrix.Translation(Vector(centre)) @ Matrix.Rotation(rot_y, 4, "Y")


# ---------------------------------------------------------------- models


def build_jet():
    """Generic single-seat jet, 15 m long, 11 m span. Origin = centre of gravity."""
    body = material("jet_body", (0.55, 0.58, 0.62, 1.0), roughness=0.45, metallic=0.2)
    surface = material("jet_surface", (0.42, 0.45, 0.50, 1.0), roughness=0.5, metallic=0.2)
    glass = material("jet_canopy", (0.08, 0.12, 0.20, 1.0), roughness=0.1, metallic=0.6)
    dark = material("jet_gear", (0.08, 0.08, 0.08, 1.0), roughness=0.8)

    # Fuselage: nose at y = -7.5, exhaust at y = +7.5 (forward is -Y).
    fuselage = object_from_bmesh("fuselage", loft([
        (-7.5, 0.02, 0.00, 1.0),
        (-6.8, 0.30, 0.02, 1.0),
        (-5.6, 0.62, 0.05, 1.0),
        (-4.0, 0.85, 0.06, 1.05),
        (-1.0, 0.95, 0.05, 1.05),
        (3.0, 0.92, 0.02, 1.0),
        (6.0, 0.72, 0.00, 0.95),
        (7.5, 0.55, 0.00, 0.95),
    ]), body, smooth=True)

    # Canopy: a stretched half-dome on top, ahead of the wing.
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=20, v_segments=10, radius=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * 0.55, v.co.y * 1.9, max(v.co.z, -0.2) * 0.6))
    object_from_bmesh("canopy", bm, glass, location=(0.0, -3.6, 0.78), parent=fuselage,
                      smooth=True)

    # Wing: straight trailing edge at y = +3.0 (so the aileron hinge is along body y),
    # swept leading edge, thinner at the tip.
    half_wing = [(0.0, -1.5), (5.5, 1.8), (5.5, 3.0), (0.0, 3.0)]
    taper = lambda u, v: 1.0 - 0.6 * abs(u) / 5.5  # noqa: E731
    wing_r = slab(half_wing, 0.28, taper=taper)
    wing_l = slab(mirrored(half_wing), 0.28, taper=taper)
    wing = bmesh.new()
    for part in (wing_r, wing_l):
        mesh = bpy.data.meshes.new("tmp")
        part.to_mesh(mesh)
        part.free()
        wing.from_mesh(mesh)
        bpy.data.meshes.remove(mesh)
    object_from_bmesh("wing", wing, surface, location=(0.0, 0.0, -0.15), parent=fuselage)

    # Ailerons: outboard, hinged on the wing trailing edge (origin on the hinge line).
    for side, sign in (("r", 1.0), ("l", -1.0)):
        outline = [(3.0, 0.0), (5.3, 0.0), (5.3, 0.6), (3.0, 0.6)]
        if sign < 0:
            outline = mirrored(outline)
        bm = slab(outline, 0.12)
        object_from_bmesh(f"aileron_{side}", bm, body, location=(0.0, 3.0, -0.15),
                          parent=fuselage)

    # Horizontal stabiliser (fixed) and elevator (both halves move together).
    half_stab = [(0.4, 4.8), (2.8, 6.2), (2.8, 6.8), (0.4, 6.8)]
    stab = bmesh.new()
    for outline in (half_stab, mirrored(half_stab)):
        part = slab(outline, 0.16)
        mesh = bpy.data.meshes.new("tmp")
        part.to_mesh(mesh)
        part.free()
        stab.from_mesh(mesh)
        bpy.data.meshes.remove(mesh)
    object_from_bmesh("stabiliser", stab, surface, location=(0.0, 0.0, 0.05), parent=fuselage)

    elevator = bmesh.new()
    half_elev = [(0.5, 0.0), (2.8, 0.0), (2.8, 0.55), (0.5, 0.55)]
    for outline in (half_elev, mirrored(half_elev)):
        part = slab(outline, 0.1)
        mesh = bpy.data.meshes.new("tmp")
        part.to_mesh(mesh)
        part.free()
        elevator.from_mesh(mesh)
        bpy.data.meshes.remove(mesh)
    object_from_bmesh("elevator", elevator, body, location=(0.0, 6.8, 0.05), parent=fuselage)

    # Fin (fixed) with a vertical trailing edge at y = +6.9, and the rudder hinged on it.
    fin = slab([(4.4, 0.6), (6.9, 0.6), (6.9, 3.4), (6.3, 3.4)], 0.18, plane="yz")
    object_from_bmesh("fin", fin, surface, parent=fuselage)
    rudder = slab([(0.0, 0.9), (0.55, 0.9), (0.55, 3.3), (0.0, 3.3)], 0.1, plane="yz")
    object_from_bmesh("rudder", rudder, body, location=(0.0, 6.9, 0.0), parent=fuselage)

    # Landing gear: one node, nose leg + two main legs, wheels 2.3 m below the CG.
    gear = bmesh.new()
    for x, y, r in ((0.0, -5.0, 0.30), (1.3, 1.6, 0.38), (-1.3, 1.6, 0.38)):
        cylinder_z(gear, (x, y, -1.45), 0.07, 1.3)
        cylinder_x(gear, (x, y, -2.3 + r), r, 0.25)
    object_from_bmesh("gear", gear, dark, parent=fuselage)

    return fuselage, ["fuselage", "canopy", "wing", "aileron_l", "aileron_r", "stabiliser",
                      "elevator", "fin", "rudder", "gear"]


MODELS = {"jet": build_jet}

# ---------------------------------------------------------------- export, check, preview


def export_glb(path):
    bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB", export_yup=True,
                              export_apply=True, export_animations=False,
                              export_materials="EXPORT")


def glb_node_names(path):
    data = Path(path).read_bytes()
    magic, _version, _length = struct.unpack_from("<4sII", data, 0)
    assert magic == b"glTF", "not a GLB file"
    chunk_length, chunk_type = struct.unpack_from("<II", data, 12)
    assert chunk_type == 0x4E4F534A, "first chunk is not JSON"
    doc = json.loads(data[20:20 + chunk_length])
    return {node.get("name") for node in doc.get("nodes", [])}


def render_preview(path, root):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.render.resolution_x = 960
    scene.render.resolution_y = 600
    scene.render.film_transparent = False
    world = bpy.data.worlds.new("preview")
    world.color = (0.82, 0.86, 0.90)
    scene.world = world

    # Three-quarter view from the front left, slightly above.
    cam_data = bpy.data.cameras.new("preview_cam")
    cam_data.lens = 50
    cam = bpy.data.objects.new("preview_cam", cam_data)
    scene.collection.objects.link(cam)
    cam.location = (-16.0, -20.0, 9.0)
    direction = Vector((0.0, 0.5, -0.5)) - cam.location
    cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)


def deflect_parts():
    """Rotate each moving part about its origin (the hinge) to check the pivots by eye."""
    angles = {"aileron_l": ("X", 25.0), "aileron_r": ("X", -25.0), "elevator": ("X", 20.0),
              "rudder": ("Z", 25.0), "turret": ("Z", 40.0), "barrel": ("X", 15.0)}
    for name, (axis, deg) in angles.items():
        obj = bpy.data.objects.get(name)
        if obj is not None:
            obj.rotation_euler["XYZ".index(axis)] = math.radians(deg)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="make_models.py")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--preview", type=Path)
    parser.add_argument("--deflect", action="store_true",
                        help="also render a preview with the moving parts deflected")
    parser.add_argument("--only", choices=sorted(MODELS), action="append")
    args = parser.parse_args(argv)

    args.out.mkdir(parents=True, exist_ok=True)
    failed = False
    for name in args.only or sorted(MODELS):
        reset_scene()
        root, expected = MODELS[name]()
        glb = args.out / f"{name}.glb"
        export_glb(glb)
        missing = set(expected) - glb_node_names(glb)
        size_kb = glb.stat().st_size / 1024
        print(f"[make_models] {glb}: {size_kb:.0f} KB, nodes ok" if not missing else
              f"[make_models] {glb}: MISSING NODES {sorted(missing)}")
        failed |= bool(missing)
        if args.preview:
            args.preview.mkdir(parents=True, exist_ok=True)
            render_preview((args.preview / f"{name}.png").resolve(), root)
            if args.deflect:
                deflect_parts()
                bpy.context.scene.camera.location = (-14.0, 22.0, 10.0)  # from behind, above
                aim = Vector((0.0, 1.0, 0.0)) - bpy.context.scene.camera.location
                bpy.context.scene.camera.rotation_euler = aim.to_track_quat("-Z", "Y").to_euler()
                bpy.context.scene.render.filepath = str(
                    (args.preview / f"{name}_deflected.png").resolve())
                bpy.ops.render.render(write_still=True)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
