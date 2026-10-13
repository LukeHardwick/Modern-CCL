# Heat-map images for tools/models/audit_model.py (--images). EEVEE renders of the audited
# triangles exactly as measured (rest pose, modelling frame, flat shaded so every triangle
# shows), coloured by the audit result:
#   <name>_error_<view>.png   shape error per triangle / the group's p99 gate
#                             (blue = could be coarser, grey = about right, red = too coarse,
#                             magenta = hidden, dark grey = not measured)
#   <name>_hidden_<view>.png  see-through model; magenta = hidden in every pose,
#                             yellow = flipped (only the back side can be seen)
#   <name>_profile_<probe>.png the probe outline with its maxima, minima, kink and step
# PNGs are not part of the byte-identical output (the JSON is).

import math

import bpy
import numpy as np
from mathutils import Matrix, Vector

# sRGB colours (dataviz reference palette: diverging blue <-> red with a grey midpoint;
# magenta and the status yellow for the two fault classes).
COLORS = {
    "lt_0.1": "#104281", "0.1_0.5": "#5598e7", "0.5_1": "#d6d5cf", "gt_1": "#d03b3b",
    "unmeasured": "#6f6e69", "hidden": "#b3359f", "flipped": "#eda100",
    "ghost": "#8d8c87", "plain": "#d6d5cf", "ink": "#1b1b1a", "line": "#1c5cab",
    "max": "#d03b3b", "min": "#104281", "kink": "#eb6834", "background": "#fcfcfb",
}
LABELS = [("lt_0.1", "< 0.1 x gate (could be coarser)"), ("0.1_0.5", "0.1 - 0.5"),
          ("0.5_1", "0.5 - 1 (about right)"), ("gt_1", "> 1 (too coarse)"),
          ("hidden", "hidden"), ("unmeasured", "not measured")]

RES = (1100, 700)
CENTRE = Vector((0.0, 0.3, 0.3))


def srgb_to_linear(hexstr):
    h = hexstr.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255.0
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return out


# ---------------------------------------------------------------- scene


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x, scene.render.resolution_y = RES
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    scene.render.image_settings.compression = 100
    scene.render.dither_intensity = 0.0
    if hasattr(scene.eevee, "taa_render_samples"):
        scene.eevee.taa_render_samples = 16
    scene.view_settings.view_transform = "Standard"
    # light page-coloured background for the camera, dimmer ambient light for the surfaces
    world = bpy.data.worlds.new("audit")
    world.use_nodes = True
    nt = world.node_tree
    out = nt.nodes["World Output"]
    seen = nt.nodes["Background"]
    seen.inputs["Color"].default_value = (*srgb_to_linear(COLORS["background"]), 1.0)
    seen.inputs["Strength"].default_value = 1.0
    amb = nt.nodes.new("ShaderNodeBackground")
    amb.inputs["Color"].default_value = (*srgb_to_linear(COLORS["background"]), 1.0)
    amb.inputs["Strength"].default_value = 0.55
    path = nt.nodes.new("ShaderNodeLightPath")
    mix = nt.nodes.new("ShaderNodeMixShader")
    nt.links.new(path.outputs["Is Camera Ray"], mix.inputs[0])
    nt.links.new(amb.outputs[0], mix.inputs[1])
    nt.links.new(seen.outputs[0], mix.inputs[2])
    nt.links.new(mix.outputs[0], out.inputs["Surface"])
    scene.world = world
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 2.2
    sun.rotation_euler = (math.radians(40), math.radians(15), math.radians(-30))
    scene.collection.objects.link(sun)
    return scene


def material(key, alpha=1.0, emit=False):
    name = f"{key}_{alpha:g}_{int(emit)}"
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    rgb = srgb_to_linear(COLORS[key])
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    if emit:
        sh = nt.nodes.new("ShaderNodeEmission")
        sh.inputs["Color"].default_value = (*rgb, 1.0)
        surf = sh.outputs[0]
    else:
        # mostly flat colour (emission) with some diffuse shading for the 3-D form
        em = nt.nodes.new("ShaderNodeEmission")
        em.inputs["Color"].default_value = (*[c * 0.55 for c in rgb], 1.0)
        df = nt.nodes.new("ShaderNodeBsdfDiffuse")
        df.inputs["Color"].default_value = (*rgb, 1.0)
        add = nt.nodes.new("ShaderNodeAddShader")
        nt.links.new(em.outputs[0], add.inputs[0])
        nt.links.new(df.outputs[0], add.inputs[1])
        surf = add.outputs[0]
    if alpha < 1.0:
        tr = nt.nodes.new("ShaderNodeBsdfTransparent")
        mix = nt.nodes.new("ShaderNodeMixShader")
        mix.inputs[0].default_value = alpha
        nt.links.new(tr.outputs[0], mix.inputs[1])
        nt.links.new(surf, mix.inputs[2])
        surf = mix.outputs[0]
        if hasattr(m, "surface_render_method"):
            m.surface_render_method = "BLENDED"
    m.use_backface_culling = False
    nt.links.new(surf, out.inputs["Surface"])
    return m


def tri_object(name, tv, mat_index, mats):
    """Flat-shaded triangle soup with a material per triangle."""
    me = bpy.data.meshes.new(name)
    n = len(tv)
    me.from_pydata(tv.reshape(-1, 3).tolist(), [], np.arange(3 * n).reshape(-1, 3).tolist())
    for m in mats:
        me.materials.append(m)
    me.polygons.foreach_set("material_index", np.asarray(mat_index, dtype=np.int32))
    me.update()
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def camera(loc, target, up=(0, 0, 1), ortho=None, lens=42.0):
    data = bpy.data.cameras.new("cam")
    cam = bpy.data.objects.new("cam", data)
    bpy.context.scene.collection.objects.link(cam)
    data.clip_start, data.clip_end = 0.05, 500.0
    if ortho:
        data.type = "ORTHO"
        data.ortho_scale = ortho
    else:
        data.lens = lens
    loc, target = Vector(loc), Vector(target)
    z = (loc - target).normalized()
    x = Vector(up).cross(z).normalized()
    y = z.cross(x)
    cam.matrix_world = Matrix.Translation(loc) @ Matrix((x, y, z)).transposed().to_4x4()
    bpy.context.scene.camera = cam
    return cam


def frame_width(cam, depth):
    """Visible width (world units) at `depth` in front of the camera."""
    d = cam.data
    if d.type == "ORTHO":
        return d.ortho_scale
    return 2.0 * depth * (d.sensor_width / 2.0) / d.lens


def overlay_text(cam, body, u, v, size, key="ink", align="LEFT"):
    """Text in image coordinates: u, v in 0..1 from the bottom-left corner; size as a
    fraction of the image width."""
    depth = 1.0
    w = frame_width(cam, depth)
    h = w * RES[1] / RES[0]
    cu = bpy.data.curves.new("txt", "FONT")
    cu.body = body
    cu.size = size * w
    cu.align_x = align
    cu.materials.append(material(key, emit=True))
    obj = bpy.data.objects.new("txt", cu)
    bpy.context.scene.collection.objects.link(obj)
    obj.parent = cam
    obj.location = ((u - 0.5) * w, (v - 0.5) * h, -depth)
    return obj


def overlay_swatch(cam, key, u, v, size):
    depth = 1.0
    w = frame_width(cam, depth)
    h = w * RES[1] / RES[0]
    s = size * w
    me = bpy.data.meshes.new("sw")
    me.from_pydata([(0, 0, 0), (s, 0, 0), (s, s, 0), (0, s, 0)], [], [(0, 1, 2, 3)])
    me.materials.append(material(key, emit=True))
    obj = bpy.data.objects.new("sw", me)
    bpy.context.scene.collection.objects.link(obj)
    obj.parent = cam
    obj.location = ((u - 0.5) * w, (v - 0.5) * h, -depth)
    return obj


def legend(cam, items, title):
    overlay_text(cam, title, 0.02, 0.95, 0.019)
    x = 0.02
    for key, label in items:
        overlay_swatch(cam, key, x, 0.025, 0.016)
        t = overlay_text(cam, label, x + 0.022, 0.03, 0.0135)
        x += 0.03 + 0.0085 * len(label)
    return t


def render(path):
    scene = bpy.context.scene
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)


def clear_cameras():
    for o in list(bpy.data.objects):
        if o.type == "CAMERA" or (o.parent is not None and o.parent.type == "CAMERA"):
            bpy.data.objects.remove(o)


VIEWS = {
    # name: camera kwargs (modelling frame; centre and distances as make_models previews)
    "34front": dict(loc=CENTRE + Vector((13, -15, 6.5)), target=CENTRE + Vector((0, 0, -0.2)),
                    lens=42),
    "34rear": dict(loc=CENTRE + Vector((-13, 15, 6)), target=CENTRE, lens=42),
    "side": dict(loc=CENTRE + Vector((60, 0, 0)), target=CENTRE, ortho=15.5),
    "top": dict(loc=CENTRE + Vector((0, 0, 60)), target=CENTRE, ortho=21.5, up=(0, -1, 0)),
    "bottom": dict(loc=CENTRE + Vector((0, 0, -60)), target=CENTRE, ortho=21.5, up=(0, -1, 0)),
    "closeup": dict(loc=CENTRE + Vector((3.4, -8.6, 1.8)), target=CENTRE + Vector((0.1, -3.0, 0.2)),
                    lens=48),
}


# ---------------------------------------------------------------- the images


def error_keys(audit):
    geo = audit.geo
    T = len(geo.v)
    gate = np.zeros(T)
    for g in audit.groups:
        for n in g["present"]:
            gate[geo.node == geo.node_index(n)] = g["shape_p99"]
    ratio = np.where(gate > 0, audit.err / np.maximum(gate, 1e-12), 0.0)
    keys = np.digitize(ratio, [0.1, 0.5, 1.0])  # 0..3
    keys = np.where(~audit.has_ref | audit.unmeasured, 5, keys)
    keys = np.where(audit.vis.hidden, 4, keys)
    return keys


def render_all(audit, outdir, name):
    outdir.mkdir(parents=True, exist_ok=True)
    geo = audit.geo
    written = []

    # 1. shape-error map
    reset()
    order = ["lt_0.1", "0.1_0.5", "0.5_1", "gt_1", "hidden", "unmeasured"]
    mats = [material(k) for k in order]
    tri_object("model", geo.v, error_keys(audit), mats)
    gates = ", ".join(f"{g['name']} {g['shape_p99'] * 1000:g} mm" for g in audit.groups
                      if g["present"])
    for view in ("34front", "34rear", "side", "top", "bottom", "closeup"):
        cam = camera(**VIEWS[view])
        legend(cam, LABELS, f"{name}: shape error per triangle / p99 gate ({gates}); {view}")
        path = outdir / f"{name}_error_{view}.png"
        render(path)
        written.append(path)
        clear_cameras()

    # 2. hidden and flipped triangles in a see-through model
    reset()
    keys = np.zeros(len(geo.v), dtype=np.int32)
    keys[audit.vis.hidden] = 1
    keys[audit.flipped_any] = 2
    mats = [material("ghost", alpha=0.10), material("hidden"), material("flipped")]
    tri_object("model", geo.v, keys, mats)
    for view in ("34front", "side", "top"):
        cam = camera(**VIEWS[view])
        legend(cam, [("hidden", "hidden in every pose"), ("flipped", "flipped"),
                     ("ghost", "visible (see-through)")],
               f"{name}: hidden and flipped triangles; {view}")
        path = outdir / f"{name}_hidden_{view}.png"
        render(path)
        written.append(path)
        clear_cameras()

    # 3. profile probes, each on the model in the probe's pose (no image when no ray hit)
    poses = [p["name"] for p in audit.cfg["poses"]]
    for probe in audit.cfg["probes"]:
        res = audit.probes[probe["name"]]
        s, h, start, u, d, hitp = audit.probe_raw[probe["name"]]
        reset()
        pv = audit.geos[poses.index(probe["pose"])].v
        tri_object("model", pv, np.zeros(len(pv), dtype=np.int32), [material("plain")])
        path = outdir / f"{name}_profile_{probe['name']}.png"
        if render_probe(probe, res, h, hitp, start, u, d, path):
            written.append(path)
    return written


def render_probe(probe, res, h, hitp, start, u, d, path):
    start, u, d = Vector(start.tolist()), Vector(u.tolist()), Vector(d.tolist())
    side = u.cross(d).normalized()
    if side.dot(Vector((start.x, 0.0, 0.0))) < 0 or (abs(start.x) < 1e-9 and side.x < 0):
        side = -side
    ok = ~np.isnan(h)
    pts = [Vector(p) + side * 0.08 if good else None for p, good in zip(hitp.tolist(),
                                                                         ok.tolist())]
    good_pts = [p for p in pts if p is not None]
    if not good_pts:
        return False
    lo = Vector([min(p[i] for p in good_pts) for i in range(3)])
    hi_ = Vector([max(p[i] for p in good_pts) for i in range(3)])
    mid = (lo + hi_) / 2
    length = (hi_ - lo).length
    cam = camera(loc=mid + side * 40.0, target=mid, up=tuple(-d), ortho=max(1.0, length * 1.35))
    radius = 0.0025 * cam.data.ortho_scale
    # outline polyline (split at gaps)
    run = []
    for p in pts + [None]:
        if p is None:
            if len(run) >= 2:
                curve_line(run, radius, "line")
            run = []
        else:
            run.append(p)
    w = cam.data.ortho_scale

    def mark(at, key, label):
        if at is None:
            return
        o = Vector(at) + side * 0.12
        dot_(o, 0.008 * w, key)
        txt = bpy.data.curves.new("lab", "FONT")
        txt.body = label
        txt.size = 0.022 * w
        txt.align_x = "CENTER"
        txt.materials.append(material(key, emit=True))
        ob = bpy.data.objects.new("lab", txt)
        bpy.context.scene.collection.objects.link(ob)
        ob.matrix_world = cam.matrix_world.copy()
        ob.location = o - d * (0.03 * w) + side * 0.05
    for i, at in enumerate(res["maxima_at"]):
        mark(at, "max", f"max {i + 1}")
    for i, at in enumerate(res["minima_at"]):
        mark(at, "min", f"min {i + 1}")
    if res["kink_at"] is not None:
        mark(res["kink_at"], "kink", f"kink {res['kink_deg']:.1f}°")
    if res["step_at"] is not None and res["step"] >= 0.0005:
        mark(res["step_at"], "kink", f"step {res['step'] * 1000:.1f} mm")

    def shown(v, fmt):
        return "-" if v is None else fmt.format(v)
    overlay_text(cam, f"Profile {probe['name']}: rays every {probe['step'] * 1000:g} mm "
                 f"along the blue line ({res['hits']} of {res['samples']} hit); "
                 f"{shown(res['maxima'], '{}')} maxima, {shown(res['minima'], '{}')} minima, "
                 f"largest kink {shown(res['kink_deg'], '{:.1f}°')}, largest step "
                 f"{shown(res['step'] if res['step'] is None else res['step'] * 1000, '{:.1f} mm')}",
                 0.02, 0.95, 0.0165)
    overlay_text(cam, f"{len(res['excluded_parts'])} narrow parts ignored (shorter than "
                 f"{probe['min_feature'] * 100:g} cm along the probe)", 0.02, 0.03, 0.0145,
                 key="ink")
    render(path)
    return True


def curve_line(points, radius, key):
    cu = bpy.data.curves.new("line", "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = radius
    cu.bevel_resolution = 2
    sp = cu.splines.new("POLY")
    sp.points.add(len(points) - 1)
    for i, p in enumerate(points):
        sp.points[i].co = (p.x, p.y, p.z, 1.0)
    cu.materials.append(material(key, emit=True))
    obj = bpy.data.objects.new("line", cu)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def dot_(at, radius, key):
    bpy.ops.mesh.primitive_uv_sphere_add(radius=radius, location=at, segments=16, ring_count=8)
    obj = bpy.context.object
    obj.data.materials.append(material(key, emit=True))
    return obj
