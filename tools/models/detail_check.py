# Checks that the generator's --detail only refines the tessellation and never changes a shape
# (task T22a), so that a --detail 4 build is a valid reference for the audit (T22c):
#
#   blender --background --factory-startup --python-exit-code 2 \
#       --python tools/models/detail_check.py -- [--only jet] [--detail 4] [--tolerance 0.001]
#
# The model is built at detail 1 and at detail K in one Blender process and compared node by
# node (rest pose, modelling frame: metres, x = vehicle left, y = aft, z = up):
#   * vertex test: every detail-1 vertex is a point the generator evaluated on its analytic
#     surface, so it must lie on the detail-K surface, within detail K's own chordal error
#     (about 1/K^2 of detail 1's); the largest distance must stay under --tolerance. A part
#     whose shape depends on the detail fails here, with the place named;
#   * reverse: the largest distance from a detail-K vertex to the detail-1 surface (the
#     detail-1 chordal error, for information);
#   * bounding boxes of both builds and their largest difference (detail 1 cuts inside
#     curves, so its box can be smaller by up to its chordal error);
#   * triangles at both details (each node should be refined) and the number of connected
#     pieces (the same parts at both details; a strip skipped where it is hidden can split
#     differently, so this is information, not a gate);
#   * loose vertices (used by no face): the exporter drops them, but they are generator junk
#     that the generator's own box and island checks would see, so any is a failure.
# Exit status 0 when every node passes the vertex test and has no loose vertices, 1 otherwise.

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.dont_write_bytecode = True

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils.bvhtree import BVHTree  # noqa: E402

import make_models  # noqa: E402
from mm_common import reset_scene  # noqa: E402


def collect():
    """Node name -> (world vertices used by triangles (V, 3), triangles (T, 3), connected
    pieces, loose vertices (used by no face; the glTF exporter drops them))."""
    out = {}
    bpy.context.view_layer.update()
    for obj in bpy.context.scene.objects:
        if obj.type != "MESH":
            continue
        me = obj.data
        me.calc_loop_triangles()
        co = np.empty(len(me.vertices) * 3)
        me.vertices.foreach_get("co", co)
        co = co.reshape(-1, 3)
        m = np.array(obj.matrix_world)
        world = co @ m[:3, :3].T + m[:3, 3]
        tris = np.empty(len(me.loop_triangles) * 3, dtype=np.int64)
        me.loop_triangles.foreach_get("vertices", tris)
        tris = tris.reshape(-1, 3)
        parent = np.arange(len(co))

        def find(i):
            root = i
            while parent[root] != root:
                root = parent[root]
            while parent[i] != root:
                parent[i], i = root, parent[i]
            return root
        for a, b in ((tris[:, 0], tris[:, 1]), (tris[:, 1], tris[:, 2])):
            for i, j in zip(a.tolist(), b.tolist()):
                ri, rj = find(i), find(j)
                if ri != rj:
                    parent[rj] = ri
        used = np.unique(tris)
        pieces = len({find(int(i)) for i in used})
        remap = np.full(len(co), -1, dtype=np.int64)
        remap[used] = np.arange(len(used))
        out[obj.name] = (world[used], remap[tris], pieces, len(co) - len(used))
    return out


def nearest(points, verts, tris):
    """Largest distance from `points` to the triangle mesh, and the point where it occurs."""
    tree = BVHTree.FromPolygons(verts.tolist(), tris.tolist(), all_triangles=True)
    worst, where = 0.0, None
    for p in points.tolist():
        hit = tree.find_nearest(p)
        d = hit[3] if hit[0] is not None else float("inf")
        if d > worst:
            worst, where = d, p
    return worst, where


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser(prog="detail_check.py")
    ap.add_argument("--only", default="jet", choices=sorted(make_models.MODELS))
    ap.add_argument("--detail", type=float, default=4.0)
    ap.add_argument("--tolerance", type=float, default=0.001,
                    help="largest allowed distance of a detail-1 vertex from the detail-K "
                         "surface (m)")
    args = ap.parse_args(argv)
    build = make_models.MODELS[args.only][0]
    builds = {}
    for d in (1.0, args.detail):
        reset_scene()
        build(d)
        builds[d] = collect()
    lo, hi = builds[1.0], builds[args.detail]
    k = args.detail
    print(f"[detail_check] {args.only}: detail 1 against detail {k:g} (rest pose, metres; "
          f"modelling frame x = left, y = aft, z = up)")
    print(f"{'node':10s} {'tris 1':>7s} {'tris K':>8s} {'xK':>6s} {'pieces':>9s} {'loose':>6s} "
          f"{'vertex test':>12s} {'reverse':>9s} {'box diff':>9s}  worst vertex / reverse at")
    failed = []
    for name in sorted(set(lo) | set(hi)):
        if name not in lo or name not in hi:
            failed.append(f"{name}: only in one build")
            print(f"{name:10s} only in one build")
            continue
        (v1, t1, p1, l1), (vk, tk, pk, lk) = lo[name], hi[name]
        fwd, where = nearest(v1, vk, tk)
        rev, rwhere = nearest(vk, v1, t1)
        box = max(np.abs(v1.min(axis=0) - vk.min(axis=0)).max(),
                  np.abs(v1.max(axis=0) - vk.max(axis=0)).max())
        mark = "" if fwd <= args.tolerance and l1 + lk == 0 else "  FAIL"
        if fwd > args.tolerance:
            failed.append(f"{name}: a detail-1 vertex is {fwd * 1000:.2f} mm off the detail-{k:g} "
                          f"surface at ({where[0]:.3f}, {where[1]:.3f}, {where[2]:.3f})")
        if l1 + lk:
            failed.append(f"{name}: {l1} and {lk} vertices used by no face")
        at = " / ".join(f"({w[0]:.3f}, {w[1]:.3f}, {w[2]:.3f})" if w else "-"
                        for w in (where, rwhere))
        print(f"{name:10s} {len(t1):7d} {len(tk):8d} {len(tk) / max(1, len(t1)):6.1f} "
              f"{p1:4d}/{pk:<4d} {l1 + lk:6d} "
              f"{fwd * 1000:9.3f} mm {rev * 1000:6.2f} mm {box * 1000:6.2f} mm"
              f"  {at}{mark}")
    for name in sorted(lo):
        if name in hi:
            v1, vk = lo[name][0], hi[name][0]
            print(f"[detail_check] {name} box detail 1: {np.round(v1.min(axis=0), 4).tolist()} .. "
                  f"{np.round(v1.max(axis=0), 4).tolist()}; detail {k:g}: "
                  f"{np.round(vk.min(axis=0), 4).tolist()} .. {np.round(vk.max(axis=0), 4).tolist()}")
    for f in failed:
        print(f"[detail_check] FAIL: {f}")
    print(f"[detail_check] {'PASS' if not failed else 'FAIL'}: every detail-1 vertex within "
          f"{args.tolerance * 1000:g} mm of the detail-{k:g} surface" if not failed else
          f"[detail_check] FAIL ({len(failed)} nodes)")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
