# Generates the sample database's vehicle models (task T22a) from code, so they have exactly the
# node names and pivots the CIGI articulated parts need and no licence questions.
#
#   blender --background --python tools/models/make_models.py -- \
#       --out databases/sample/models [--preview docs/guide/img/models] [--only jet]
#
# Or use scripts/make_models.sh.
#
# Axes: modelled in Blender's frame with the nose towards -Y and +Z up; the glTF exporter
# (+Y up) turns that into glTF's convention (front +Z, up +Y; glTF 2.0 spec 3.4). Units are
# metres. Vehicle LEFT is +X and RIGHT is -X (in both frames), so `*_r` parts sit at -X.
# Each moving part is its own node whose origin is the hinge, so a part's attitude from
# Articulated Part Control (ICD 4.1.6) rotates it about the right point:
#   aileron_*, flap_*, elevator, airbrake: hinge along the X axis -> part pitch
#   rudder: hinge along the Z axis -> part yaw
#   canopy: hinge along X at the rear sill
#   gear: shown or hidden as a whole (visibility component)

import argparse
import json
import math
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import hawk  # noqa: E402
from mm_common import reset_scene  # noqa: E402

# name -> (builder, expected size (length, span, height) in metres or None)
MODELS = {
    "jet": (lambda detail: hawk.build(detail), (12.43, 9.94, 3.98)),
}

# Preview poses: node -> (axis index, degrees); used by --deflect.
DEFLECT = {
    "aileron_l": (0, 22.0), "aileron_r": (0, -22.0), "flap_l": (0, -28.0),
    "flap_r": (0, -28.0), "elevator": (0, -14.0), "rudder": (2, 22.0),
    "airbrake": (0, -50.0), "canopy": (0, -50.0), "turret": (2, 40.0), "barrel": (0, 15.0),
}

# ---------------------------------------------------------------- export and checks


def export_glb(path):
    for obj in bpy.context.scene.objects:
        obj.select_set(True)
    bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB", export_yup=True,
                              export_apply=True, export_animations=False,
                              export_materials="EXPORT", export_cameras=False,
                              export_lights=False, use_selection=True,
                              export_all_vertex_colors=False, export_extras=False)


def glb_json(path):
    data = Path(path).read_bytes()
    magic, _version, _length = struct.unpack_from("<4sII", data, 0)
    assert magic == b"glTF", "not a GLB file"
    chunk_length, chunk_type = struct.unpack_from("<II", data, 12)
    assert chunk_type == 0x4E4F534A, "first chunk is not JSON"
    return json.loads(data[20:20 + chunk_length])


def check_glb(path, expected):
    """Node names exist, left/right are on the right sides, no skins or animations."""
    doc = glb_json(path)
    nodes = {n.get("name"): n for n in doc.get("nodes", [])}
    problems = [f"missing node {n}" for n in expected if n not in nodes]
    for name, node in nodes.items():
        x = (node.get("translation") or [0.0, 0.0, 0.0])[0]
        if name.endswith("_l") and not x > 0.01:
            problems.append(f"{name}: left part must have +X (glTF), got x={x:.3f}")
        if name.endswith("_r") and not x < -0.01:
            problems.append(f"{name}: right part must have -X (glTF), got x={x:.3f}")
    if doc.get("skins"):
        problems.append("skins present")
    if doc.get("animations"):
        problems.append("animations present")
    return problems, doc


def scene_stats():
    tris, mats, dims_lo, dims_hi = 0, set(), Vector((1e9,) * 3), Vector((-1e9,) * 3)
    for obj in bpy.context.scene.objects:
        if obj.type != "MESH":
            continue
        tris += sum(len(p.vertices) - 2 for p in obj.data.polygons)
        for m in obj.data.materials:
            mats.add(m.name)
        for corner in obj.bound_box:
            w = obj.matrix_world @ Vector(corner)
            dims_lo = Vector(min(a, b) for a, b in zip(dims_lo, w))
            dims_hi = Vector(max(a, b) for a, b in zip(dims_hi, w))
    size = dims_hi - dims_lo
    return tris, size.y, size.x, size.z  # tris, length, span, height


# ---------------------------------------------------------------- preview rendering


def setup_preview_scene(ground_z):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1100
    scene.render.resolution_y = 700
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    scene.render.image_settings.compression = 100
    scene.render.dither_intensity = 0.0
    if hasattr(scene.eevee, "taa_render_samples"):
        scene.eevee.taa_render_samples = 24
    scene.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("preview")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.62, 0.74, 0.90, 1.0)
    bg.inputs["Strength"].default_value = 1.0
    scene.world = world

    sun_data = bpy.data.lights.new("sun", "SUN")
    sun_data.energy = 3.2
    sun = bpy.data.objects.new("sun", sun_data)
    sun.rotation_euler = (math.radians(50), math.radians(10), math.radians(-35))
    scene.collection.objects.link(sun)

    bpy.ops.mesh.primitive_plane_add(size=200.0, location=(0, 0, ground_z))
    ground = bpy.context.object
    mat = bpy.data.materials.new("ground")
    mat.use_nodes = True
    mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (
        0.18, 0.24, 0.14, 1.0)
    mat.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 1.0
    ground.data.materials.append(mat)
    return scene


def render_view(path, loc, target, lens=50.0, ortho=None, up_hint=None):
    scene = bpy.context.scene
    cam_data = bpy.data.cameras.new("cam")
    cam = bpy.data.objects.new("cam", cam_data)
    scene.collection.objects.link(cam)
    cam_data.clip_start, cam_data.clip_end = 0.05, 500.0
    if ortho:
        cam_data.type = "ORTHO"
        cam_data.ortho_scale = ortho
    else:
        cam_data.lens = lens
    cam.location = Vector(loc)
    direction = Vector(target) - cam.location
    quat = direction.to_track_quat("-Z", "Y")
    if up_hint is not None:  # roll the camera so `up_hint` points up in the image
        from mathutils import Matrix
        z = -direction.normalized()
        x = Vector(up_hint).cross(z).normalized()
        y = z.cross(x)
        quat = Matrix((x, y, z)).transposed().to_quaternion()
    cam.rotation_euler = quat.to_euler()
    scene.camera = cam
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam)


def pose(deflect):
    for name, (axis, deg) in DEFLECT.items():
        obj = bpy.data.objects.get(name)
        if obj is not None:
            obj.rotation_euler = (0.0, 0.0, 0.0)
            if deflect:
                obj.rotation_euler[axis] = math.radians(deg)


def render_previews(name, outdir, ground_z, c, size):
    """c = centre of the model, size = reference length."""
    scene = setup_preview_scene(ground_z)
    outdir.mkdir(parents=True, exist_ok=True)
    pose(False)
    cx, cy, cz = c
    k = size / 12.43
    views = {
        "34front": dict(loc=(cx + 13 * k, cy - 15 * k, cz + 6.5 * k), target=(cx, cy, cz - .2 * k),
                        lens=42),
        "34rear": dict(loc=(cx - 13 * k, cy + 15 * k, cz + 6 * k), target=(cx, cy, cz), lens=42),
        "side": dict(loc=(cx + 60, cy, cz), target=(cx, cy, cz), ortho=15.5 * k,
                     up_hint=(0, 0, 1)),
        "top": dict(loc=(cx, cy, cz + 60), target=(cx, cy, cz), ortho=21.5 * k,
                    up_hint=(0, -1, 0)),
    }
    for vname, v in views.items():
        render_view((outdir / f"{name}_{vname}.png").resolve(), **v)
    pose(True)
    render_view((outdir / f"{name}_deflected.png").resolve(),
                loc=(cx - 12 * k, cy + 14 * k, cz + 7 * k), target=(cx, cy + 0.5 * k, cz), lens=42)
    pose(False)


# ---------------------------------------------------------------- main


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="make_models.py")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--preview", type=Path, help="also render preview PNGs into this folder")
    parser.add_argument("--no-export", action="store_true", help="skip writing .glb files")
    parser.add_argument("--only", choices=sorted(MODELS), action="append")
    parser.add_argument("--detail", type=float, default=1.0)
    args = parser.parse_args(argv)

    args.out.mkdir(parents=True, exist_ok=True)
    failed = False
    for name in args.only or sorted(MODELS):
        reset_scene()
        build, want = MODELS[name]
        _root, expected = build(args.detail)
        tris, length, span, height = scene_stats()
        glb = args.out / f"{name}.glb"
        if not args.no_export:
            export_glb(glb)
            problems, _doc = check_glb(glb, expected)
            size_kb = glb.stat().st_size / 1024
        else:
            problems, size_kb = [], 0
        dim_err = [abs(a - b) / b for a, b in zip((length, span, height), want)]
        print(f"[make_models] {name}: {tris} tris, {size_kb:.0f} KB, "
              f"L={length:.2f} W={span:.2f} H={height:.2f} m (want {want})")
        if max(dim_err) > 0.04:
            problems.append(f"dimensions off by {max(dim_err) * 100:.1f} %")
        for p in problems:
            print(f"[make_models] {name}: PROBLEM: {p}")
        failed |= bool(problems)
        if args.preview:
            render_previews(name, args.preview, hawk.GROUND, (0.0, 0.3, 0.3), 12.43)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
