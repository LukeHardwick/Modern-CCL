# Bake determinism smoke check (task T22e, recommended by Phase 0): do two headless Cycles bakes
# of a small mesh built in Python / bmesh (no modifiers, own UVs) give byte-identical images?
# The modelling phases rerun this before they trust a bake.
#
#   /opt/homebrew/bin/blender -b --factory-startup --python tools/models/bake_determinism_check.py -- --twice
#
# --twice : run the bake in two separate Blender processes and compare the hashes (exit 0 when
#           identical, 1 otherwise). Without it: bake once into --out DIR and print the hashes.
# Bakes: ambient occlusion on a bumpy plane with an occluding block, and a tangent-space normal map
# from a fine "high" grid onto a coarse "low" grid (selected to active). Cycles on the CPU,
# fixed seed and samples, no denoising, no adaptive sampling, one thread.

import hashlib
import json
import math
import os
import subprocess
import sys
import tempfile

import bpy
import bmesh
import numpy as np

SIZE = 256
SEED = 7
SAMPLES = 48


def grid(n, height_fn, name):
    bm = bmesh.new()
    verts = [[bm.verts.new((i / n * 2 - 1, j / n * 2 - 1, height_fn(i / n * 2 - 1, j / n * 2 - 1)))
              for j in range(n + 1)] for i in range(n + 1)]
    for i in range(n):
        for j in range(n):
            bm.faces.new((verts[i][j], verts[i + 1][j], verts[i + 1][j + 1], verts[i][j + 1]))
    uv = bm.loops.layers.uv.new("UVMap")
    for f in bm.faces:
        for lp in f.loops:
            lp[uv].uv = ((lp.vert.co.x + 1) / 2, (lp.vert.co.y + 1) / 2)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def bump(x, y):
    return 0.08 * math.exp(-8 * (x * x + y * y)) + 0.02 * math.sin(6 * x) * math.cos(5 * y)


def material_with_target(obj, image):
    mat = bpy.data.materials.new(obj.name + "_mat")
    mat.use_nodes = True
    node = mat.node_tree.nodes.new("ShaderNodeTexImage")
    node.image = image
    mat.node_tree.nodes.active = node
    obj.data.materials.append(mat)


def setup():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    cy = sc.cycles
    cy.device = "CPU"
    cy.samples = SAMPLES
    cy.seed = SEED
    cy.use_animated_seed = False
    cy.use_adaptive_sampling = False
    cy.use_denoising = False
    cy.bake_type = "AO"
    sc.render.threads_mode = "FIXED"
    sc.render.threads = 1
    return sc


def pixel_hash(image):
    px = np.array(image.pixels[:], dtype=np.float32)
    return hashlib.sha256(px.tobytes()).hexdigest()


def select_only(*objs):
    for o in bpy.context.scene.objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[-1]


def bake_once(out):
    sc = setup()
    res = {}
    # --- ambient occlusion on the bumpy plane, with a block above it
    plane = grid(48, bump, "plane")
    block = grid(2, lambda x, y: 0.35, "block")
    img = bpy.data.images.new("ao", SIZE, SIZE, float_buffer=True)
    material_with_target(plane, img)
    select_only(plane)
    sc.cycles.bake_type = "AO"
    bpy.ops.object.bake(type="AO", margin=4, use_clear=True)
    res["ao"] = pixel_hash(img)
    img.filepath_raw = os.path.join(out, "ao.png")
    img.file_format = "PNG"
    img.save()
    res["ao_png"] = hashlib.sha256(open(img.filepath_raw, "rb").read()).hexdigest()
    # --- tangent-space normal map: fine grid (high) onto coarse grid (low)
    for o in (plane, block):
        bpy.data.objects.remove(o)
    high = grid(96, bump, "high")
    low = grid(6, lambda x, y: 0.0, "low")
    nimg = bpy.data.images.new("nrm", SIZE, SIZE, float_buffer=True)
    material_with_target(low, nimg)
    select_only(high, low)
    sc.cycles.bake_type = "NORMAL"
    bpy.ops.object.bake(type="NORMAL", use_selected_to_active=True, cage_extrusion=0.2, margin=4, use_clear=True)
    res["normal"] = pixel_hash(nimg)
    nimg.filepath_raw = os.path.join(out, "normal.png")
    nimg.file_format = "PNG"
    nimg.save()
    res["normal_png"] = hashlib.sha256(open(nimg.filepath_raw, "rb").read()).hexdigest()
    px = np.array(nimg.pixels[:], dtype=np.float32).reshape(SIZE, SIZE, 4)
    res["normal_nonflat_texels"] = int((np.abs(px[..., 0] - 0.5) > 0.02).sum())
    return res


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = argv[argv.index("--out") + 1] if "--out" in argv else None
    if "--twice" in argv:
        results = []
        with tempfile.TemporaryDirectory() as tmp:
            for i in range(2):
                d = os.path.join(tmp, f"run{i}")
                os.makedirs(d)
                proc = subprocess.run([bpy.app.binary_path, "-b", "--factory-startup", "--python-exit-code", "2",
                                       "--python", os.path.abspath(__file__), "--", "--out", d],
                                      capture_output=True, text=True)
                line = [l for l in proc.stdout.splitlines() if l.startswith("BAKE_JSON ")]
                if proc.returncode != 0 or not line:
                    print(proc.stdout[-2000:], proc.stderr[-2000:])
                    print("[bake_check] a bake run failed")
                    sys.stdout.flush()
                    os._exit(2)
                results.append(json.loads(line[0][len("BAKE_JSON "):]))
        same = results[0] == results[1]
        print(f"[bake_check] run 1: {json.dumps(results[0], sort_keys=True)}")
        print(f"[bake_check] run 2: {json.dumps(results[1], sort_keys=True)}")
        print(f"[bake_check] {'IDENTICAL' if same else 'DIFFERENT'}: two bakes in separate processes")
        sys.stdout.flush()
        os._exit(0 if same else 1)
    out = out or tempfile.mkdtemp(prefix="bake_check_")
    os.makedirs(out, exist_ok=True)
    res = bake_once(out)
    print("BAKE_JSON " + json.dumps(res, sort_keys=True))
    sys.stdout.flush()
    os._exit(0)


main()
