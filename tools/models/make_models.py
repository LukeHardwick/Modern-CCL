# Generates the sample database's vehicle models (task T22a) from code, so they have exactly the
# node names and pivots the CIGI articulated parts need and no licence questions.
#
#   blender --background --python tools/models/make_models.py -- \
#       --out databases/sample/models [--preview docs/guide/img/models] [--deflect] [--only jet]
#       [--detail K]
#
# --detail K multiplies every tessellation count and never changes the shapes, so a --detail 4
# build is a dense reference for the audit (keep it out of the repository: it is about 16x the
# size, and its primitives exceed raylib's vertex limit, which is then only a note).
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
#   canopy: hinge along Y on the right (-X) sill, opens sideways to starboard
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

# raylib 6.0 keeps mesh indices as unsigned short: at most 65 536 vertices per (node, material)
# primitive (its glTF loader truncates 32-bit indices with only a warning).
MAX_PRIMITIVE_VERTICES = 65536

# Preview poses: node -> (axis index, degrees); used by --deflect.
DEFLECT = {
    "aileron_l": (0, 22.0), "aileron_r": (0, -22.0), "flap_l": (0, -28.0),
    "flap_r": (0, -28.0), "elevator": (0, -14.0), "rudder": (2, 22.0),
    "airbrake": (0, -50.0), "canopy": (1, -65.0), "turret": (2, 40.0), "barrel": (0, 15.0),
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


def glb_primitives(doc):
    """(node, material, vertices, triangles) for every primitive, as a viewer loads them."""
    acc = doc.get("accessors", [])
    mats = [m.get("name") for m in doc.get("materials", [])]
    out = []
    for node in doc.get("nodes", []):
        if "mesh" not in node:
            continue
        for prim in doc["meshes"][node["mesh"]]["primitives"]:
            nv = acc[prim["attributes"]["POSITION"]]["count"]
            nt = (acc[prim["indices"]]["count"] if "indices" in prim else nv) // 3
            mat = mats[prim["material"]] if "material" in prim else None
            out.append((node.get("name"), mat, nv, nt))
    return out


def check_glb(path, expected, vertex_limit=True):
    """Node names exist, left/right are on the right sides, no skins or animations, and every
    primitive fits raylib's 16-bit indices. Returns (problems, notes, doc): over-limit
    primitives are only notes when `vertex_limit` is off (dense reference builds)."""
    doc = glb_json(path)
    nodes = {n.get("name"): n for n in doc.get("nodes", [])}
    problems = [f"missing node {n}" for n in expected if n not in nodes]
    notes = []
    for name, node in nodes.items():
        x = (node.get("translation") or [0.0, 0.0, 0.0])[0]
        if name.endswith("_l") and not x > 0.01:
            problems.append(f"{name}: left part must have +X (glTF), got x={x:.3f}")
        if name.endswith("_r") and not x < -0.01:
            problems.append(f"{name}: right part must have -X (glTF), got x={x:.3f}")
    for node, mat, nv, _nt in glb_primitives(doc):
        if nv > MAX_PRIMITIVE_VERTICES:
            (problems if vertex_limit else notes).append(
                f"{node}/{mat}: {nv} vertices > {MAX_PRIMITIVE_VERTICES} (raylib 16-bit indices)")
    if doc.get("skins"):
        problems.append("skins present")
    if doc.get("animations"):
        problems.append("animations present")
    return problems, notes, doc


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


def check_islands(margin=0.05):
    """Every connected mesh island must touch (within `margin` m) the main cluster of
    geometry: catches detached/floating parts. Returns a list of problem strings."""
    import bmesh
    boxes = []
    for obj in bpy.context.scene.objects:
        if obj.type != "MESH":
            continue
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bm.verts.ensure_lookup_table()
        seen = set()
        for v0 in bm.verts:
            if v0.index in seen:
                continue
            stack, seen_here = [v0], {v0.index}
            lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
            while stack:
                v = stack.pop()
                w = obj.matrix_world @ v.co
                lo = Vector(min(a, b) for a, b in zip(lo, w))
                hi = Vector(max(a, b) for a, b in zip(hi, w))
                for e in v.link_edges:
                    o = e.other_vert(v)
                    if o.index not in seen_here:
                        seen_here.add(o.index)
                        stack.append(o)
            seen |= seen_here
            boxes.append((lo, hi, obj.name))
        bm.free()
    n = len(boxes)
    parent = list(range(n))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    order = sorted(range(n), key=lambda i: boxes[i][0].x)
    for ii, i in enumerate(order):
        for j in order[ii + 1:]:
            if boxes[j][0].x > boxes[i][1].x + margin:
                break
            a, b = boxes[i], boxes[j]
            if all(a[0][k] <= b[1][k] + margin and b[0][k] <= a[1][k] + margin
                   for k in range(3)):
                parent[find(i)] = find(j)
    sizes = {}
    for i in range(n):
        sizes[find(i)] = sizes.get(find(i), 0) + 1
    main = max(sizes, key=sizes.get)
    return [f"floating island in {boxes[i][2]} at "
            f"({(boxes[i][0].x + boxes[i][1].x) / 2:.2f}, {(boxes[i][0].y + boxes[i][1].y) / 2:.2f},"
            f" {(boxes[i][0].z + boxes[i][1].z) / 2:.2f})" for i in range(n) if find(i) != main]


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


def render_previews(name, outdir, ground_z, c, size, deflect=False):
    """c = centre of the model, size = reference length; `deflect` adds the deflected pose."""
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
    scene.render.resolution_x, scene.render.resolution_y = 960, 600
    render_view((outdir / f"{name}_closeup.png").resolve(), loc=(cx + 3.4, cy - 8.6, cz + 1.8),
                target=(cx + 0.1, cy - 3.0, cz + 0.2), lens=48)
    # canopy and spine side profile, intake close-up (left side)
    render_view((outdir / f"{name}_canopy.png").resolve(), loc=(cx + 60, cy - 1.0 * k, 1.25 * k),
                target=(cx, cy - 1.0 * k, 1.25 * k), ortho=6.6 * k, up_hint=(0, 0, 1))
    render_view((outdir / f"{name}_intake.png").resolve(), loc=(cx + 2.6 * k, -4.0 * k, 0.7 * k),
                target=(cx + 0.85 * k, -1.7 * k, 0.12 * k), lens=50)
    scene.render.resolution_x, scene.render.resolution_y = 1100, 700
    if deflect:
        pose(True)
        render_view((outdir / f"{name}_deflected.png").resolve(),
                    loc=(cx - 12 * k, cy + 14 * k, cz + 7 * k), target=(cx, cy + 0.5 * k, cz),
                    lens=42)
        pose(False)


# ---------------------------------------------------------------- main


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="make_models.py")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--preview", type=Path, help="also render preview PNGs into this folder")
    parser.add_argument("--deflect", action="store_true",
                        help="with --preview: also render the control surfaces deflected")
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
        floating = check_islands()
        glb = args.out / f"{name}.glb"
        notes = []
        if not args.no_export:
            export_glb(glb)
            # dense reference builds (--detail > 1) are not loaded by raylib
            problems, notes, doc = check_glb(glb, expected, vertex_limit=args.detail <= 1.0)
            size_kb = glb.stat().st_size / 1024
            prims = glb_primitives(doc)
            verts = sum(p[2] for p in prims)
            big = max(prims, key=lambda p: p[2])
            print(f"[make_models] {name}: glTF {sum(p[3] for p in prims)} tris, {verts} vertices "
                  f"({verts / max(1, sum(p[3] for p in prims)):.3f} per tri), {len(prims)} "
                  f"primitives, largest {big[0]}/{big[1]} {big[2]} vertices")
        else:
            problems, size_kb = [], 0
        dim_err = [abs(a - b) / b for a, b in zip((length, span, height), want)]
        print(f"[make_models] {name}: {tris} tris, {size_kb:.0f} KB, "
              f"L={length:.2f} W={span:.2f} H={height:.2f} m (want {want})")
        if max(dim_err) > 0.04:
            problems.append(f"dimensions off by {max(dim_err) * 100:.1f} %")
        problems += floating
        for n in notes:
            print(f"[make_models] {name}: note: {n}")
        for p in problems:
            print(f"[make_models] {name}: PROBLEM: {p}")
        failed |= bool(problems)
        if args.preview:
            render_previews(name, args.preview, hawk.GROUND, (0.0, 0.3, 0.3), 12.43,
                            args.deflect)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
