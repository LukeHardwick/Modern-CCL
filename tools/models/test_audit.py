# Known-answer tests for tools/models/audit_model.py (task T22c). Run headless:
#
#   blender --background --factory-startup --python-exit-code 2 \
#       --python tools/models/test_audit.py
#
# Exit status 0 when every test passes, 1 when one fails. Each test builds small GLB files
# with glb_io.write_glb (geometry given in the modelling frame: x left, y aft, z up) in a
# temporary directory and checks the audit's answer against a value known in advance.

import json
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.dont_write_bytecode = True  # no __pycache__ next to the tools

import audit_model as am  # noqa: E402
import glb_io  # noqa: E402

TMP = Path(tempfile.mkdtemp(prefix="test_audit_"))
NOTE = {}  # key numbers per test, printed at the end (quoted in docs/research/04)


# ---------------------------------------------------------------- geometry helpers


def to_gltf(p):
    """Modelling frame (rows) -> glTF frame: p_m = C p_g, so p_g = C^T p_m."""
    return np.asarray(p, dtype=np.float64) @ glb_io.GLTF_TO_MODEL


def face_normals(v, t):
    n = np.cross(v[t[:, 1]] - v[t[:, 0]], v[t[:, 2]] - v[t[:, 0]])
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-30)


def flat(v, t):
    """Split vertices per triangle so each face has its own (winding) normal."""
    v, t = np.asarray(v, dtype=np.float64), np.asarray(t, dtype=np.int64)
    n = face_normals(v, t)
    return v[t].reshape(-1, 3), np.repeat(n, 3, axis=0), np.arange(3 * len(t)).reshape(-1, 3)


def node(name, v, t, n=None, material="paint", index_type="u16", smooth=False):
    v, t = np.asarray(v, dtype=np.float64), np.asarray(t, dtype=np.int64)
    if n is None and not smooth:
        v, n, t = flat(v, t)
    elif n is None:
        fn = face_normals(v, t)
        n = np.zeros_like(v)
        for k in range(3):
            np.add.at(n, t[:, k], fn)
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-30)
    return {"name": name, "prims": [{"positions": to_gltf(v), "normals": to_gltf(n),
                                     "indices": t, "material": material,
                                     "index_type": index_type}]}


MATS = [{"name": "paint"}, {"name": "glass", "alpha_mode": "BLEND"}]


def write(name, nodes):
    path = TMP / f"{name}.glb"
    glb_io.write_glb(path, nodes, MATS)
    return path


def uv_sphere(r, n):
    """UV sphere, n segments around and n/2 bands, outward winding, analytic normals."""
    nr = n // 2
    v = [(0.0, 0.0, r)]
    for j in range(1, nr):
        phi = math.pi * j / nr
        for k in range(n):
            th = 2 * math.pi * k / n
            v.append((r * math.sin(phi) * math.cos(th), r * math.sin(phi) * math.sin(th),
                      r * math.cos(phi)))
    v.append((0.0, 0.0, -r))
    t = [(0, 1 + k, 1 + (k + 1) % n) for k in range(n)]
    for j in range(nr - 2):
        a, b = 1 + j * n, 1 + (j + 1) * n
        for k in range(n):
            k1 = (k + 1) % n
            t += [(a + k, b + k, b + k1), (a + k, b + k1, a + k1)]
    last, a = len(v) - 1, 1 + (nr - 2) * n
    t += [(last, a + (k + 1) % n, a + k) for k in range(n)]
    v = np.array(v)
    return v, np.array(t), v / r


def icosphere(r, level):
    """Subdivided icosahedron (uniform triangles), outward winding, analytic normals."""
    g = (1 + 5 ** 0.5) / 2
    v = [(-1, g, 0), (1, g, 0), (-1, -g, 0), (1, -g, 0), (0, -1, g), (0, 1, g), (0, -1, -g),
         (0, 1, -g), (g, 0, -1), (g, 0, 1), (-g, 0, -1), (-g, 0, 1)]
    v = [np.array(p, dtype=np.float64) / np.linalg.norm(p) for p in v]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4),
         (11, 10, 2), (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8),
         (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(level):
        cache = {}

        def mid(a, b):
            key = (min(a, b), max(a, b))
            if key not in cache:
                m = v[a] + v[b]
                v.append(m / np.linalg.norm(m))
                cache[key] = len(v) - 1
            return cache[key]
        nf = []
        for a, b, c in f:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        f = nf
    v, f = np.array(v), np.array(f)
    assert (np.einsum("ij,ij->i", face_normals(v, f), v[f].mean(axis=1)) > 0).all()
    return v * r, f, v


def box(lo, hi):
    """Closed box, outward winding, 12 triangles (2 per face, faces in the order -x, +x,
    -y, +y, -z, +z)."""
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    v = np.array([(x, y, z) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)])
    # vertex index = 4 * ix + 2 * iy + iz
    quads = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4),
             (1, 5, 7, 3)]
    t = []
    for a, b, c, d in quads:
        t += [(a, b, c), (a, c, d)]
    return v, np.array(t)


def grid_plate(n, size=1.0, z=0.0):
    xs = np.linspace(-size / 2, size / 2, n + 1)
    v = np.array([(x, y, z) for y in xs for x in xs])
    t = []
    for j in range(n):
        for i in range(n):
            a = j * (n + 1) + i
            t += [(a, a + 1, a + n + 2), (a, a + n + 2, a + n + 1)]
    return v, np.array(t)


def strip(poly, x0=-0.2, x1=0.2):
    """A strip across x following the outline poly = [(y, z), ...], facing +z."""
    v, t = [], []
    for y, z in poly:
        v += [(x0, y, z), (x1, y, z)]
    for i in range(len(poly) - 1):
        a = 2 * i
        t += [(a, a + 1, a + 3), (a, a + 3, a + 2)]
    return np.array(v), np.array(t)


def config(**over):
    cfg = am.load_config(None)
    cfg["groups"] = [{**am.GROUP_DEFAULTS, "name": "exterior", "nodes": over.pop("nodes", []),
                      "distance": 8.0}]
    cfg["model"]["default_group"] = "exterior"
    for key, val in over.items():
        sect, _, name = key.partition("__")
        if name:
            cfg[sect][name] = val
        else:
            cfg[sect] = val
    return cfg


def audit(model, ref=None, cfg=None, fast=False, jobs=1):
    a = am.Audit(model, ref, cfg or config(), fast=fast, log=lambda s: None, jobs=jobs)
    return a, a.run()


def gate(res, gid):
    for g in res["gates"]:
        if g["id"] == gid:
            return g
    raise KeyError(gid)


def hard_failures(res):
    return sorted(g["id"] for g in res["gates"] if g["hard"] and not g["pass"])


# ---------------------------------------------------------------- the tests


class KnownAnswers(unittest.TestCase):

    def test_1_sphere_chordal_error(self):
        """Sphere r = 1 m with 16 segments against a dense one (128): the largest measured
        deviation is within 5 % of the analytic chordal error of the 16-segment sphere."""
        from mathutils import Vector
        from mathutils.geometry import closest_point_on_tri
        v, t, n = uv_sphere(1.0, 16)
        rv, rt, rn = uv_sphere(1.0, 128)
        m = write("sphere16", [node("ball", v, t, n)])
        r = write("sphere128", [node("ball", rv, rt, rn)])
        # analytic: how far inside the true sphere each flat triangle reaches
        o = Vector((0, 0, 0))
        analytic = max(1.0 - closest_point_on_tri(o, *(Vector(p) for p in v[tri])).length
                       for tri in t)
        _a, res = audit(m, r, config(nodes=["ball"]))
        measured = res["nodes"]["ball"]["shape"]["max"]
        self.assertGreater(analytic, 0.005)
        NOTE["1 sphere: measured / analytic max (mm)"] = (round(measured * 1000, 3),
                                                        round(analytic * 1000, 3))
        self.assertLessEqual(abs(measured - analytic) / analytic, 0.05,
                             f"measured {measured:.6f} m, analytic {analytic:.6f} m")
        self.assertEqual(res["nodes"]["ball"]["hidden"], 0)
        self.assertEqual(res["hygiene"]["flipped"], 0)

    def test_1b_shading_flat_against_smooth(self):
        """Shading (check B). A flat triangle inscribed in a sphere faces the point f where
        the sphere's centre projects onto it, and the true normal at a point p of it differs
        from the face normal by atan(|p - f| / |f|): at a corner that is asin(circumradius /
        r). A 16-segment sphere, flat-shaded, against a smooth 128-segment one: the largest
        error is that angle at the worst corner sample (corners sit 1 % in towards the
        centroid). Smooth-shaded, a sphere's blended normal points along p itself, so the
        error is nearly 0."""
        v, t, n = uv_sphere(1.0, 16)
        rv, rt, rn = uv_sphere(1.0, 128)
        r = write("shade_ref", [node("ball", rv, rt, rn)])
        worst = 0.0
        for tri in v[t]:
            nrm = np.cross(tri[1] - tri[0], tri[2] - tri[0])
            nrm /= np.linalg.norm(nrm)
            h = float(np.dot(nrm, tri[0]))          # distance of the plane from the centre
            f = nrm * h                              # foot point = circumcentre
            c = tri.mean(axis=0)
            for p in tri:
                s = 0.99 * p + 0.01 * c              # the corner sample
                worst = max(worst, math.degrees(math.atan2(np.linalg.norm(s - f), h)))
        cfg = config(nodes=["ball"])
        _a, res = audit(write("shade_flat", [node("ball", v, t)]), r, cfg)
        flat_max = res["nodes"]["ball"]["shading_deg"]["max"]
        _a, res_s = audit(write("shade_smooth", [node("ball", v, t, n)]), r, cfg)
        smooth_max = res_s["nodes"]["ball"]["shading_deg"]["max"]
        NOTE["1b shading: flat max / expected, smooth max (deg)"] = (flat_max, round(worst, 2),
                                                                    smooth_max)
        self.assertAlmostEqual(flat_max, worst, delta=0.1)
        self.assertFalse(gate(res, "B.exterior.max")["pass"])
        self.assertLess(smooth_max, 0.2)
        self.assertTrue(gate(res_s, "B.exterior.max")["pass"])
        self.assertTrue(gate(res_s, "B.exterior.p95")["pass"])

    def test_1c_crease_reprojection(self):
        """Shading next to a crease. A unit cube against a reference box 3 mm larger at the
        sides and 8 mm larger at top and bottom: top-face samples within 5 mm of an edge are
        nearer to a reference side face (3 mm + their distance to the edge) than to the
        reference top (8 mm), which faces 90 degrees away. Looking along the model normal
        finds the reference top, so every sample compares like with like: shading error 0
        everywhere, some samples reprojected; the shape error stays the nearest distance
        (largest 8 mm, mid-top)."""
        v, t = box((-0.5, -0.5, -0.5), (0.5, 0.5, 0.5))
        rv, rt = box((-0.503, -0.503, -0.508), (0.503, 0.503, 0.508))
        m = write("crease", [node("cube", v, t)])
        r = write("crease_ref", [node("cube", rv, rt)])
        _a, res = audit(m, r, config(nodes=["cube"]))
        n = res["nodes"]["cube"]
        NOTE["1c crease: reprojected samples, shading max, shape max"] = (
            n["reprojected_samples"], n["shading_deg"]["max"], n["shape"]["max"])
        self.assertGreater(n["reprojected_samples"], 0)
        self.assertEqual(n["shading_deg"]["max"], 0.0)
        self.assertEqual(n["shading_deg_all"]["max"], 0.0)
        self.assertAlmostEqual(n["shape"]["max"], 0.008, delta=1e-6)

    def test_1d_reverse_finds_missing_stud(self):
        """The reverse direction finds shape the model lacks. The reference plate carries a
        2 x 2 cm stud 10 mm tall (an open box); the model is the plate alone. Every model
        point lies on the reference plate (forward error 0, gate passes), but the stud's top
        is 10 mm from the model (reverse max 10 mm, gate fails); the stud is too small a
        share of the area to move the reverse p99."""
        pv, pt = grid_plate(4)
        m = write("stud", [node("plate", pv, pt)])
        rv, rt = grid_plate(16)
        sv, st = box((-0.01, -0.01, 0.0), (0.01, 0.01, 0.01))
        st = np.array([f for f in st if not (sv[f][:, 2] == 0.0).all()])  # no bottom face
        fv, fn, ft = flat(rv, rt)
        gv, gn, gt = flat(sv, st)
        ref_node = {"name": "plate", "prims": [{
            "positions": to_gltf(np.concatenate([fv, gv])),
            "normals": to_gltf(np.concatenate([fn, gn])),
            "indices": np.concatenate([ft, gt + len(fv)]), "material": "paint"}]}
        r = write("stud_ref", [ref_node])
        _a, res = audit(m, r, config(nodes=["plate"]))
        g = res["groups"]["exterior"]
        NOTE["1d stud: forward max, reverse p99, reverse max (mm)"] = (
            g["shape"]["max"] * 1000, g["shape_reverse"]["p99"] * 1000,
            g["shape_reverse"]["max"] * 1000)
        self.assertEqual(g["shape"]["max"], 0.0)
        self.assertTrue(gate(res, "A.exterior.max")["pass"])
        self.assertAlmostEqual(g["shape_reverse"]["max"], 0.01, delta=1e-6)
        self.assertFalse(gate(res, "A.exterior.rev_max")["pass"])
        self.assertEqual(g["shape_reverse"]["p99"], 0.0)

    def test_1e_not_measured_left_out(self):
        """Triangles that coincide with the reference on a curved surface cannot show their
        error, so they are left out of checks A and B (and the headroom) and counted as "not
        measured". One node: a 128-segment sphere written identically in both files (99 % of
        the area) plus a coarse 8-segment sphere of r = 0.1 m that the reference has with 64
        segments. The shape figures must equal those of the small sphere audited on its own
        (they used to read 0), the not-measured share fails its gate, the big sphere's piece
        is skipped by the headroom, and the small piece's own error in the headroom equals
        the A gate's figures."""
        bv, bt, bn = uv_sphere(1.0, 128)
        c = np.array([0.9, 0.0, 0.9])
        sv, st, sn = uv_sphere(0.1, 8)
        fv, ft, fnn = uv_sphere(0.1, 64)

        def two(av, at, an, cv, ct, cn):
            return {"name": "ball", "prims": [{
                "positions": to_gltf(np.concatenate([av, cv + c])),
                "normals": to_gltf(np.concatenate([an, cn])),
                "indices": np.concatenate([at, ct + len(av)]), "material": "paint"}]}
        m = write("um_model", [two(bv, bt, bn, sv, st, sn)])
        r = write("um_ref", [two(bv, bt, bn, fv, ft, fnn)])
        cfg = config(nodes=["ball"], headroom__min_component=40)
        a, res = audit(m, r, cfg)
        ms = write("um_small", [node("ball", sv + c, st, sn)])
        rs = write("um_small_ref", [node("ball", fv + c, ft, fnn)])
        _b, alone = audit(ms, rs, config(nodes=["ball"]))
        g, s = res["groups"]["exterior"], alone["groups"]["exterior"]
        NOTE["1e not measured: share, p99 / max (mm), alone"] = (
            g["unmeasured_area_frac"], g["shape"]["p99"] * 1000, g["shape"]["max"] * 1000,
            s["shape"]["p99"] * 1000)
        self.assertEqual(res["nodes"]["ball"]["unmeasured_triangles"], len(bt))
        for key in ("shape", "shape_reverse", "shading_deg"):
            self.assertEqual(g[key], s[key], key)
        self.assertEqual(g["error_buckets_area"], s["error_buckets_area"])
        self.assertGreater(g["shape"]["p99"], 0.001)
        self.assertFalse(gate(res, "A.exterior.p99")["pass"])
        self.assertGreater(g["unmeasured_area_frac"], 0.98)
        ug = gate(res, "A.exterior.unmeasured")
        self.assertEqual((ug["status"], ug["hard"]), ("fail", False))
        self.assertEqual(alone["groups"]["exterior"]["unmeasured_area_frac"], 0.0)
        pieces = {h["triangles"]: h for h in res["headroom"]}
        self.assertIn("skipped", pieces[len(bt)])
        small = pieces[len(st)]
        self.assertEqual((small["base"]["fwd_p99"], small["base"]["fwd_max"]),
                         (g["shape"]["p99"], g["shape"]["max"]))
        self.assertEqual((small["base"]["rev_p99"], small["base"]["rev_max"]),
                         (g["shape_reverse"]["p99"], g["shape_reverse"]["max"]))

    def test_1f_not_measured_flat_shaded(self):
        """The not-measured rule also finds flat-shaded curves: a triangle that coincides
        with the reference and has an edge neighbour turned by 0.5 to 45 degrees (a facet of
        a curve). A flat-shaded 24-segment sphere written identically in both files: every
        triangle is not measured, so its A and B gates FAIL "not measured" and the
        not-measured share is 100 %. A flat-shaded box written identically: its edges turn by
        90 degrees (real creases), so every triangle is measured, error 0, and its A, B and
        not-measured gates pass."""
        v, t, _n = uv_sphere(1.0, 24)
        ball = write("um_flat_ball", [node("ball", v, t)])
        _a, res = audit(ball, ball, config(nodes=["ball"]))
        g = res["groups"]["exterior"]
        self.assertEqual(res["nodes"]["ball"]["unmeasured_triangles"], len(t))
        self.assertEqual(g["unmeasured_area_frac"], 1.0)
        ab = ("A.exterior.p99", "A.exterior.max", "A.exterior.rev_p99", "A.exterior.rev_max",
              "B.exterior.p95", "B.exterior.max")
        for gid in ab:
            self.assertEqual(gate(res, gid)["status"], "not_measured", gid)
        self.assertEqual(gate(res, "A.exterior.unmeasured")["status"], "fail")
        bv, bt = box((-0.5, -0.5, -0.5), (0.5, 0.5, 0.5))
        cube = write("um_flat_box", [node("cube", bv, bt)])
        _a, res2 = audit(cube, cube, config(nodes=["cube"]))
        g2 = res2["groups"]["exterior"]
        NOTE["1f flat-shaded: sphere not measured (tris, share); box not measured, max"] = (
            res["nodes"]["ball"]["unmeasured_triangles"], g["unmeasured_area_frac"],
            res2["nodes"]["cube"]["unmeasured_triangles"], g2["shape"]["max"])
        self.assertEqual(res2["nodes"]["cube"]["unmeasured_triangles"], 0)
        self.assertEqual((g2["shape"]["count"], g2["shape"]["max"]), (12, 0.0))
        for gid in ab + ("A.exterior.unmeasured",):
            self.assertEqual(gate(res2, gid)["status"], "pass", gid)

    def test_2a_dense_plate_headroom(self):
        """A 64 x 64 flat plate against a one-quad plate: Decimate can remove >= 95 %."""
        v, t = grid_plate(64)
        m = write("plate64", [node("plate", v, t)])
        rv, rt = grid_plate(1)
        r = write("plate1", [node("plate", rv, rt)])
        _a, res = audit(m, r, config(nodes=["plate"]))
        self.assertEqual(len(res["headroom"]), 1)
        h = res["headroom"][0]
        self.assertEqual(h["triangles"], 8192)
        NOTE["2a plate: headroom, triangles kept"] = (h["headroom"], h["triangles_kept"])
        self.assertGreaterEqual(h["headroom"], 0.95, h)
        self.assertFalse(gate(res, "C.headroom.plate.0")["pass"])

    def test_2b_two_triangle_plate(self):
        """A two-triangle plate against itself: headroom 0 and nothing flagged."""
        rv, rt = grid_plate(1)
        m = write("plate1m", [node("plate", rv, rt)])
        cfg = config(nodes=["plate"], headroom__min_component=1,
                     gates__verts_per_tri_max=10.0)
        _a, res = audit(m, m, cfg)
        self.assertEqual(len(res["headroom"]), 1)
        self.assertEqual(res["headroom"][0]["headroom"], 0.0)
        failed = [g["id"] for g in res["gates"] if not g["pass"]]
        self.assertEqual(failed, [])
        n = res["nodes"]["plate"]
        self.assertEqual((n["hidden"], n["flipped"], n["slivers"], n["degenerate"]),
                         (0, 0, 0, 0))
        self.assertEqual(n["shape"]["max"], 0.0)
        self.assertEqual(n["shading_deg"]["max"], 0.0)

    def test_2c_headroom_five_percent_rule(self):
        """A piece that already misses the shape gate passes a decimation step when the step
        is at most 5 % worse than the piece itself. (a) A 64 x 64 plate 5 mm below a
        one-square reference plate misses the gate (5 mm everywhere), but decimation keeps
        it flat, so the error stays 5 mm (within 5.25 mm) and the headroom is >= 95 %.
        (b) A uniformly fine sphere (icosphere, 5 120 triangles) 5 mm inside its reference:
        any collapse enlarges triangles on a curve, so the first step (90 %) is more than
        5 % worse and the headroom is 0."""
        v, t = grid_plate(64)
        rv, rt = grid_plate(1, z=0.005)
        a, res = audit(write("offset_plate", [node("plate", v, t)]),
                       write("offset_plate_ref", [node("plate", rv, rt)]),
                       config(nodes=["plate"]))
        h = res["headroom"][0]
        self.assertAlmostEqual(h["base"]["fwd_max"], 0.005, delta=1e-6)
        self.assertAlmostEqual(h["threshold_max"], 0.00525, delta=1e-6)
        self.assertGreaterEqual(h["headroom"], 0.95)
        v, t, n = icosphere(1.0, 4)
        rv, rt, rn = uv_sphere(1.005, 256)
        a, res2 = audit(write("offset_ball", [node("ball", v, t, n)]),
                        write("offset_ball_ref", [node("ball", rv, rt, rn)]),
                        config(nodes=["ball"]))
        h2 = res2["headroom"][0]
        first = h2["steps"][0]
        b = h2["base"]
        NOTE["2c 5 % rule: plate headroom; sphere base max, 90 % step max (mm), headroom"] = (
            h["headroom"], round(max(b["fwd_max"], b["rev_max"]) * 1000, 3),
            round(max(first["fwd_max"], first["rev_max"]) * 1000, 3), h2["headroom"])
        self.assertEqual(h2["triangles"], 5120)
        self.assertGreater(max(b["fwd_max"], b["rev_max"]), 0.004)  # misses the gate already
        self.assertEqual((first["ratio"], first["pass"]), (0.9, False))
        self.assertTrue(first["fwd_p99"] > h2["threshold_p99"] or
                        first["fwd_max"] > h2["threshold_max"] or
                        first["rev_p99"] > h2["threshold_p99"] or
                        first["rev_max"] > h2["threshold_max"])
        self.assertEqual(h2["headroom"], 0.0)

    def test_3_box_in_box(self):
        """A closed box inside a closed box: the inner one is 100 % hidden, the outer 0 %.
        The inner node also holds one degenerate triangle, which is not counted in its
        hidden share (12 of 12, not 12 of 13)."""
        ov, ot = box((-1, -1, -1), (1, 1, 1))
        iv, it = box((-0.5, -0.5, -0.5), (0.5, 0.5, 0.5))
        fv, fn, ft = flat(iv, it)
        fv = np.concatenate([fv, [[0.0, 0.0, 0.0], [0.1, 0.0, 0.0], [0.2, 0.0, 0.0]]])
        fn = np.concatenate([fn, np.tile([0.0, 0.0, 1.0], (3, 1))])
        ft = np.concatenate([ft, [[36, 37, 38]]])
        m = write("boxes", [node("outer", ov, ot), node("inner", fv, ft, fn)])
        _a, res = audit(m, None, config(nodes=["outer", "inner"]))
        NOTE["3 boxes: inner / outer hidden"] = (res["nodes"]["inner"]["hidden_frac"],
                                                 res["nodes"]["outer"]["hidden_frac"])
        self.assertEqual((res["nodes"]["inner"]["hidden"], res["nodes"]["inner"]["degenerate"]),
                         (12, 1))
        self.assertEqual(res["nodes"]["inner"]["hidden_frac"], 1.0)
        self.assertEqual(res["nodes"]["outer"]["hidden_frac"], 0.0)
        self.assertEqual(res["hygiene"]["flipped"], 0)

    def test_3c_enclosure_shortcut_is_exact(self):
        """The closed-piece shortcut gives the same hidden/flipped answer as casting every
        ray: a sphere holding one box fully inside and one poking out, plus a flipped box."""
        v, t, n = uv_sphere(1.0, 24)
        av, at = box((-0.3, -0.3, -0.3), (0.3, 0.3, 0.3))   # fully inside the sphere
        bv, bt = box((0.6, -0.2, -0.2), (1.4, 0.2, 0.2))    # half outside
        cv, ct = box((-0.2, -0.2, 0.5), (0.2, 0.2, 0.7))    # inside, wound inwards
        m = write("nested", [node("ball", v, t, n), node("inner", av, at),
                             node("poke", bv, bt), node("flip", cv, ct[:, ::-1])])
        a, res = audit(m, None, config(nodes=["ball", "inner", "poke", "flip"]))
        self.assertGreater(a.vis.enclosed, 0)
        full = am.Visibility(a.model, [a.geo], a.cfg, False)  # no shortcut
        self.assertTrue(np.array_equal(a.vis.hidden, full.hidden))
        self.assertTrue(np.array_equal(a.vis.back_only, full.back_only))
        self.assertLess(a.vis.rays, full.rays)
        NOTE["3c shortcut: enclosed tests, rays with / without"] = (a.vis.enclosed, a.vis.rays,
                                                                   full.rays)
        self.assertEqual(res["nodes"]["inner"]["hidden_frac"], 1.0)
        self.assertEqual(res["nodes"]["flip"]["hidden_frac"], 1.0)
        self.assertGreater(res["nodes"]["poke"]["hidden"], 0)
        self.assertLess(res["nodes"]["poke"]["hidden"], 12)

    def test_3d_worker_processes_match_one_process(self):
        """The visibility check split over 3 worker processes gives the same JSON, byte for
        byte, as one process (closed pieces, an inside-out box, glass, a lid that a pose
        opens)."""
        v, t, n = uv_sphere(1.0, 24)
        av, at = box((-0.3, -0.3, -0.3), (0.3, 0.3, 0.3))
        bv, bt = box((0.6, -0.2, -0.2), (1.4, 0.2, 0.2))
        cv, ct = box((-0.2, -0.2, 0.5), (0.2, 0.2, 0.7))
        gv, gt = box((-0.4, -0.4, 1.0), (0.4, 0.4, 1.3))
        sv, st = box((2.5, -0.5, -0.5), (3.5, 0.5, 0.0))
        lv, lt = box((-1.0, -0.5, 0.0), (0.0, 0.5, 0.05))  # hinged at its +x edge
        m = write("workers", [
            node("ball", v, t, n), node("inner", av, at), node("poke", bv, bt),
            node("flip", cv, ct[:, ::-1]), node("cap", gv, gt, material="glass"),
            node("base", sv, st),
            {**node("lid", lv, lt), "translation": to_gltf([[3.5, 0.0, 0.0]])[0],
             "parent": "base"}])
        cfg = config(nodes=["ball", "inner", "poke", "flip", "cap", "base", "lid"])
        cfg["poses"] = [{"name": "rest"}, {"name": "open", "rotate": {"lid": [1, 100.0]}}]
        cfg["visibility"]["parallel_min"] = 0
        one, res1 = audit(m, None, cfg, jobs=1)
        three, res3 = audit(m, None, cfg, jobs=3)
        self.assertEqual((one.vis_workers, three.vis_workers), (0, 3))
        self.assertTrue(np.array_equal(one.vis.hidden, three.vis.hidden))
        self.assertTrue(np.array_equal(one.vis.back_only, three.vis.back_only))
        self.assertEqual(am.dump_json(res1), am.dump_json(res3))
        # workers that fail are replaced by computing their parts here: same answer
        import contextlib
        import io
        broken = am.Audit(m, None, cfg, log=lambda s: None, jobs=2)
        broken.worker_command = [sys.executable, "-c", "raise SystemExit(3)"]
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            res_b = broken.run()
        self.assertEqual(err.getvalue().count("failed (exit 3)"), 2)
        self.assertEqual(broken.vis_workers, 0)
        self.assertEqual(am.dump_json(res1), am.dump_json(res_b))
        # workers that hang are stopped at [visibility] worker_timeout: same answer
        slow_cfg = {**cfg, "visibility": {**cfg["visibility"], "worker_timeout": 1.0}}
        slow = am.Audit(m, None, slow_cfg, log=lambda s: None, jobs=2)
        slow.worker_command = [sys.executable, "-c", "import time; time.sleep(120)"]
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            res_s = slow.run()
        self.assertEqual(err.getvalue().count("timed out"), 2)
        self.assertEqual(slow.vis_workers, 0)
        self.assertEqual(am.dump_json(res1), am.dump_json(res_s))
        NOTE["3d workers: hidden triangles, rays (1 and 3 processes)"] = (
            int(one.vis.hidden.sum()), one.vis.rays, three.vis.rays)
        self.assertEqual(res1["nodes"]["inner"]["hidden_frac"], 1.0)
        self.assertEqual(res1["nodes"]["flip"]["hidden_frac"], 1.0)

    def test_3e_eyepoint_sees_inside(self):
        """A cube inside a closed shell: hidden from outside, but with an eyepoint inside the
        shell in front of the cube's -y face, exactly that face (2 triangles) is seen. The
        closed-shell shortcut must not fire when an eyepoint is inside the shell."""
        ov, ot = box((-1, -1, -1), (1, 1, 1))
        cv, ct = box((-0.2, -0.2, -0.2), (0.2, 0.2, 0.2))
        eye = {"name": "eyepoint_front", "translation": to_gltf([[0.0, -0.6, 0.0]])[0]}
        m = write("eyebox", [node("shell", ov, ot), node("cube", cv, ct), eye])
        cfg = config(nodes=["shell", "cube"])
        cfg["model"]["eyepoints"] = []
        a, res = audit(m, None, cfg)
        self.assertEqual(res["nodes"]["cube"]["hidden"], 12)
        self.assertGreater(a.vis.enclosed, 0)
        cfg["model"]["eyepoints"] = ["eyepoint_front", "eyepoint_rear"]  # rear one absent
        a, res = audit(m, None, cfg)
        cube = np.nonzero(a.geo.node == a.geo.node_index("cube"))[0]
        seen = cube[~a.vis.hidden[cube]] - cube[0]
        NOTE["3e eyepoint: cube triangles seen, enclosed shortcuts"] = (seen.tolist(),
                                                                      a.vis.enclosed)
        self.assertEqual(seen.tolist(), [4, 5])  # the -y face
        self.assertEqual(res["nodes"]["shell"]["hidden"], 0)
        self.assertEqual(a.vis.enclosed, 0)
        self.assertEqual(res["hygiene"]["flipped"], 0)

    def test_3f_reversed_face_seen_from_eyepoint(self):
        """A panel inside a closed shell, wound so that its front faces away from the
        eyepoint: nothing outside sees it and the pilot sees only its back, so it is flipped
        (2 triangles), not hidden. Wound the other way it is simply seen."""
        ov, ot = box((-1, -1, -1), (1, 1, 1))
        pv = np.array([(-0.3, 0.3, -0.3), (0.3, 0.3, -0.3), (0.3, 0.3, 0.3), (-0.3, 0.3, 0.3)])
        away = np.array([(0, 2, 1), (0, 3, 2)])     # normal +y, away from the eye at -y
        eye = {"name": "eyepoint_front", "translation": to_gltf([[0.0, -0.6, 0.0]])[0]}
        cfg = config(nodes=["shell", "panel"])
        cfg["model"]["eyepoints"] = ["eyepoint_front"]
        a, res = audit(write("eye_back", [node("shell", ov, ot), node("panel", pv, away), eye]),
                       None, cfg)
        panel = np.nonzero(a.geo.node == a.geo.node_index("panel"))[0]
        NOTE["3f eyepoint back side: flipped, hidden (panel)"] = (
            res["nodes"]["panel"]["flipped"], res["nodes"]["panel"]["hidden"])
        self.assertTrue(a.vis.back_only[panel].all())
        self.assertEqual((res["nodes"]["panel"]["flipped"], res["nodes"]["panel"]["hidden"]),
                         (2, 0))
        self.assertEqual(res["hygiene"]["flipped"], 2)
        self.assertFalse(gate(res, "D.flipped")["pass"])
        _a, res = audit(write("eye_front", [node("shell", ov, ot),
                                            node("panel", pv, away[:, ::-1]), eye]), None, cfg)
        self.assertEqual((res["nodes"]["panel"]["flipped"], res["nodes"]["panel"]["hidden"]),
                         (0, 0))

    def test_3g_eyepoint_moves_with_pose(self):
        """An eyepoint hung under a pivot that a pose turns 180 degrees: at rest it sees the
        -y face of a cube inside a closed shell, in the pose the +y face. Every undecided
        triangle is re-tested in a pose that moves an eyepoint, so both faces count as
        seen (4 triangles, 8 hidden)."""
        ov, ot = box((-1, -1, -1), (1, 1, 1))
        cv, ct = box((-0.2, -0.2, -0.2), (0.2, 0.2, 0.2))
        nodes = [node("shell", ov, ot), node("cube", cv, ct), {"name": "pivot"},
                 {"name": "eyepoint_front", "translation": to_gltf([[0.0, -0.6, 0.0]])[0],
                  "parent": "pivot"}]
        m = write("eye_turn", nodes)
        cfg = config(nodes=["shell", "cube"])
        cfg["model"]["eyepoints"] = ["eyepoint_front"]
        cfg["poses"] = [{"name": "rest"}, {"name": "turn", "rotate": {"pivot": [2, 180.0]}}]
        a, res = audit(m, None, cfg)
        cube = np.nonzero(a.geo.node == a.geo.node_index("cube"))[0]
        seen = (cube[~a.vis.hidden[cube]] - cube[0]).tolist()
        NOTE["3g moving eyepoint: cube triangles seen"] = seen
        self.assertEqual(seen, [4, 5, 6, 7])  # the -y and +y faces
        self.assertEqual(res["nodes"]["cube"]["hidden"], 8)

    def test_3h_hide_parent_hides_child(self):
        """A pose that hides a node hides its children too. A closed hood around a cube is a
        child of another node; hiding that parent opens the cube to view (0 of 12 hidden;
        with the hood left in place all 12 would stay hidden). A profile probe in that pose
        traces the cube's top (z = 0.2 m), the same probe at rest the hood's (z = 0.5 m)."""
        cv, ct = box((-0.2, -0.2, -0.2), (0.2, 0.2, 0.2))
        sv, st = box((2.0, -0.5, -0.5), (3.0, 0.5, 0.5))
        hv, ht = box((-0.5, -0.5, -0.5), (0.5, 0.5, 0.5))
        m = write("hide_tree", [node("core", cv, ct), node("shell", sv, st),
                                {**node("hood", hv, ht), "parent": "shell"}])
        cfg = config(nodes=["core", "shell", "hood"])
        cfg["poses"] = [{"name": "rest"}, {"name": "open", "hide": ["shell"]}]
        probe = {**am.PROBE_DEFAULTS, "start": [-0.15, 0.0, 1.0], "end": [0.15, 0.0, 1.0],
                 "nodes": ["core", "hood"], "max_maxima": 0, "max_minima": 0}
        cfg["probes"] = [{**probe, "name": "at_rest"}, {**probe, "name": "open", "pose": "open"}]
        _a, res = audit(m, None, cfg)
        NOTE["3h hide subtree: core hidden; probe heights rest / open"] = (
            res["nodes"]["core"]["hidden"], res["probes"]["at_rest"]["profile"][0][1],
            res["probes"]["open"]["profile"][0][1])
        self.assertEqual(res["nodes"]["core"]["hidden"], 0)
        self.assertEqual(res["settings"]["pose_retests"]["open"], 12)
        for name, height in (("at_rest", 0.5), ("open", 0.2)):
            p = res["probes"][name]
            self.assertEqual(p["coverage"], 1.0)
            self.assertEqual({h for _s, h in p["profile"]}, {height})
            self.assertTrue(gate(res, f"F.{name}.maxima")["pass"])
        cfg["poses"] = [{"name": "rest"}]
        cfg["probes"] = []
        _a, res = audit(m, None, cfg)
        self.assertEqual(res["nodes"]["core"]["hidden"], 12)

    def test_3b_pose_reveals_hidden(self):
        """A lid that closes a box: the box's top face is hidden at rest and seen once the
        lid swings open in a configured pose (and only then)."""
        bv, bt = box((-0.5, -0.5, -0.5), (0.5, 0.5, 0.0))
        lv, lt = box((-0.5, -0.5, 0.0), (0.5, 0.5, 0.05))
        lv = lv - np.array([0.5, 0.0, 0.0])  # lid hinged at x = +0.5 (its node origin)
        nodes = [node("base", bv, bt), {**node("lid", lv, lt), "translation":
                                        to_gltf([[0.5, 0.0, 0.0]])[0], "parent": "base"}]
        m = write("lidbox", nodes)
        cfg = config(nodes=["base", "lid"])
        a, res = audit(m, None, cfg)
        top = (a.geo.node == a.geo.node_index("base")) & (a.geo.normal[:, 2] > 0.9)
        self.assertEqual(int(top.sum()), 2)
        self.assertTrue(a.vis.hidden[top].all())
        cfg["poses"] = [{"name": "rest"}, {"name": "open", "rotate": {"lid": [1, 100.0]}}]
        a, res = audit(m, None, cfg)
        self.assertFalse(a.vis.hidden[top].any())
        self.assertEqual(res["nodes"]["base"]["hidden"], 0)

    def test_4_cube_with_one_flipped_face(self):
        """A cube whose +z face is wound inwards: exactly that face (2 triangles) is reported
        flipped."""
        v, t = box((-0.5, -0.5, -0.5), (0.5, 0.5, 0.5))
        t = t.copy()
        t[10:12] = t[10:12][:, ::-1]
        m = write("flipcube", [node("cube", v, t)])
        a, res = audit(m, None, config(nodes=["cube"]))
        self.assertEqual(res["hygiene"]["flipped"], 2)
        NOTE["4 cube: flipped triangles, bad-winding edges"] = (
            sorted(np.nonzero(a.flipped_any)[0].tolist()),
            res["hygiene"]["inconsistent_winding_edges"])
        self.assertEqual(sorted(np.nonzero(a.flipped_any)[0].tolist()), [10, 11])
        self.assertEqual(res["hygiene"]["inconsistent_winding_edges"], 4)
        self.assertFalse(gate(res, "D.flipped")["pass"])
        self.assertTrue(gate(res, "D.flipped")["hard"])

    def test_4b_mirrored_node(self):
        """A node whose world matrix mirrors (scale -1 in x): glTF 2.0 makes its front faces
        clockwise, so a box wound outwards in its own frame is still seen from outside: 0
        flipped, 0 hidden, and a warning that raylib does not honour this. The same box
        wound inwards in its own frame is inside out: all 12 flipped."""
        v, t = box((0.5, -0.5, -0.5), (1.5, 0.5, 0.5))
        good = {**node("mbox", v, t), "scale": [-1.0, 1.0, 1.0]}
        a, res = audit(write("mirror_ok", [good]), None, config(nodes=["mbox"]))
        NOTE["4b mirrored: flipped / hidden outward box, flipped inward box"] = [
            res["hygiene"]["flipped"], res["nodes"]["mbox"]["hidden"]]
        self.assertEqual(res["model"]["mirrored_nodes"], ["mbox"])
        self.assertAlmostEqual(res["model"]["bbox_min"][0], -1.5, places=6)
        self.assertEqual((res["hygiene"]["flipped"], res["nodes"]["mbox"]["hidden"]), (0, 0))
        self.assertEqual(res["hygiene"]["normals_against_winding"], 0)
        self.assertTrue(any("mirrors" in w and "raylib" in w for w in res["warnings"]))
        bad = {**node("mbox", v, t[:, ::-1]), "scale": [-1.0, 1.0, 1.0]}
        _a, res = audit(write("mirror_bad", [bad]), None, config(nodes=["mbox"]))
        NOTE["4b mirrored: flipped / hidden outward box, flipped inward box"].append(
            res["hygiene"]["flipped"])
        self.assertEqual(res["hygiene"]["flipped"], 12)

    def test_5_degenerate_and_slivers(self):
        """3 degenerate and 4 sliver triangles among 5 good ones are counted exactly."""
        tris = []

        def tri_with_angle(deg, x):  # isosceles-free triangle whose smallest angle is deg
            a = math.radians(deg)
            return [(x, 0, 0), (x + 1, 0, 0), (x + 0.5 * math.cos(a), 0.5 * math.sin(a), 0)]
        for i, d in enumerate((60, 45, 30, 2.5, 20)):  # good (2.5 deg is just above 2)
            tris.append(tri_with_angle(d, 3.0 * i))
        for i, d in enumerate((1.9, 1.0, 0.5, 0.1)):  # slivers
            tris.append(tri_with_angle(d, 30.0 + 3.0 * i))
        tris.append([(60, 0, 0), (61, 0, 0), (62, 0, 0)])  # collinear
        tris.append([(63, 0, 0), (63, 0, 0), (64, 1, 0)])  # two equal corners
        tris.append([(65, 0, 0), (65 + 1e-6, 0, 0), (65, 1e-5, 0)])  # area 5e-12 m2
        v = np.array(tris, dtype=np.float64).reshape(-1, 3)
        t = np.arange(len(v)).reshape(-1, 3)
        nrm = np.tile([0.0, 0.0, 1.0], (len(v), 1))
        m = write("hygiene", [node("bits", v, t, nrm)])
        _a, res = audit(m, None, config(nodes=["bits"]))
        self.assertEqual(res["hygiene"]["degenerate"], 3)
        NOTE["5 hygiene: degenerate, slivers"] = (res["hygiene"]["degenerate"],
                                                 res["hygiene"]["slivers"])
        self.assertEqual(res["hygiene"]["slivers"], 4)

    def test_5b_floating_and_duplicates(self):
        """A box with a part 3 cm away (touching within the 5 cm margin), a part 20 cm away
        (floating), one triangle written twice with the same winding (a duplicate: hard
        fault) and a thin fin made of a square and its back-to-back copy (2 pairs of
        back-to-back faces, a valid double-sided part: reported, not a fault)."""
        bv, bt = box((-0.5, -0.5, -0.5), (0.5, 0.5, 0.5))
        nv, nt = box((0.53, -0.1, -0.1), (0.73, 0.1, 0.1))   # 3 cm gap: same cluster
        fv, ft = box((0.70, -0.1, 0.8), (0.90, 0.1, 1.0))    # 20 cm from the box: floating
        dv, dt = grid_plate(1, 0.2, 0.52)                    # 2 cm above the top: touching
        dt = np.concatenate([dt, dt[:1]])                     # one face twice
        sv = np.array([(-0.6, 0.0, -0.1), (-0.5, 0.0, -0.1), (-0.5, 0.0, 0.1),
                       (-0.6, 0.0, 0.1)])                     # on the -x face
        st = np.array([(0, 1, 2), (0, 2, 3)])
        st = np.concatenate([st, st[:, ::-1]])                # the back-to-back copy
        m = write("islands", [node("body", bv, bt), node("near", nv, nt),
                              node("far", fv, ft), node("dup", dv, dt), node("fin", sv, st)])
        _a, res = audit(m, None, config(nodes=["body", "near", "far", "dup", "fin"]))
        hy = res["hygiene"]
        NOTE["5b islands: floating, duplicates, back-to-back pairs"] = (
            hy["floating"], hy["duplicates"], hy["back_to_back_pairs"])
        self.assertEqual(hy["floating"], 1)
        self.assertEqual(hy["floating_islands"][0]["node"], "far")
        self.assertEqual(hy["duplicates"], 1)
        self.assertEqual(hy["back_to_back_pairs"], 2)
        self.assertEqual(res["nodes"]["fin"]["flipped"], 0)
        self.assertEqual(gate(res, "D.duplicates")["value"], 1)
        self.assertTrue(gate(res, "D.duplicates")["hard"])

    def test_6_index_limit(self):
        """A primitive with 70 000 vertices fails the 16-bit index gate (hard, exit 1) and no
        other hard gate: its two triangles form one 10 cm square, wound with their normals.
        The same with 65 536 vertices passes every hard gate."""
        def big(nv):
            v = np.zeros((nv, 3))
            v[:, 2] = -1.0 - np.arange(nv) * 1e-6  # unused vertices (not drawn)
            v[[0, 1, 2]] = [(0, 0, 0), (0.1, 0, 0), (0, 0.1, 0)]
            v[[nv - 3, nv - 2, nv - 1]] = [(0.1, 0, 0), (0.1, 0.1, 0), (0, 0.1, 0)]
            t = np.array([(0, 1, 2), (nv - 3, nv - 2, nv - 1)])  # counter-clockwise from +z
            return v, t, np.tile([0.0, 0.0, 1.0], (nv, 1))
        v, t, n = big(70000)
        m = write("big70k", [node("big", v, t, n, index_type="u32")])
        out = TMP / "out_big70k"
        code = am.main(["--model", str(m), "--out", str(out), "--name", "big"])
        res = json.loads((out / "big_audit.json").read_text())
        g = gate(res, "C.index.big.paint")
        self.assertEqual(g["value"], 69999)
        self.assertFalse(g["pass"])
        self.assertTrue(g["hard"])
        NOTE["6 index: max index, exit status, hard failures"] = (g["value"], code,
                                                                  hard_failures(res))
        self.assertEqual(hard_failures(res), ["C.index.big.paint"])
        self.assertEqual(code, 1)
        v, t, n = big(65536)
        m = write("big65k", [node("big", v, t, n, index_type="u32")])
        _a, res = audit(m)
        self.assertTrue(gate(res, "C.index.big.paint")["pass"])
        self.assertEqual(gate(res, "C.index.big.paint")["value"], 65535)
        self.assertEqual(hard_failures(res), [])

    def _probe(self, name, poly, extra=(), **settings):
        """Audit a strip following `poly` with one probe "p" along it (start y = 0.05, end
        y = 1.95 at x = 0 unless `settings` say otherwise). Returns the probe's result; the
        whole report is left in self.last."""
        sv, st = strip(poly)
        nodes = [node("skin", sv, st)] + list(extra)
        m = write(name, nodes)
        cfg = config(nodes=["skin"] + [e["name"] for e in extra])
        cfg["probes"] = [{**am.PROBE_DEFAULTS, "name": "p", "start": [0.0, 0.05, 1.0],
                          "end": [0.0, 1.95, 1.0], "nodes": ["skin"] +
                          [e["name"] for e in extra], **settings}]
        _a, self.last = audit(m, None, cfg)
        return self.last["probes"]["p"]

    def test_7a_profile_one_bump(self):
        """One bump gives one maximum; a 4 cm frame hoop on top of it is ignored."""
        ys = np.arange(0.0, 2.0001, 0.002)
        poly = [(y, 0.1 * math.exp(-((y - 1.0) / 0.2) ** 2)) for y in ys]
        hv, ht = box((-0.3, 0.98, 0.0), (0.3, 1.02, 0.13))  # narrow hoop across the bump
        p = self._probe("bump1", poly, [node("hoop", hv, ht)])
        self.assertEqual((p["maxima"], p["minima"]), (1, 0))
        NOTE["7a one bump: maxima, minima, ignored parts"] = (p["maxima"], p["minima"],
                                                             len(p["excluded_parts"]))
        self.assertEqual(len(p["excluded_parts"]), 1)
        self.assertAlmostEqual(p["maxima_at"][0][1], 1.0, delta=0.005)

    def test_7b_profile_two_bumps(self):
        """Two bumps with a dip between them give two maxima and one minimum."""
        ys = np.arange(0.0, 2.0001, 0.002)
        poly = [(y, 0.1 * math.exp(-((y - 0.7) / 0.15) ** 2)
                 + 0.1 * math.exp(-((y - 1.3) / 0.15) ** 2)) for y in ys]
        p = self._probe("bump2", poly)
        self.assertEqual((p["maxima"], p["minima"]), (2, 1))
        NOTE["7b two bumps: maxima, minima"] = (p["maxima"], p["minima"])
        self.assertAlmostEqual(p["minima_at"][0][1], 1.0, delta=0.005)

    def test_7c_profile_step_and_kink(self):
        """A 10 mm step and a 3 degree kink are detected where they are."""
        p = self._probe("step", [(0.0, 0.0), (1.0, 0.0), (1.0, 0.01), (2.0, 0.01)])
        NOTE["7c step (mm) at y"] = (round(p["step"] * 1000, 3), p["step_at"][1])
        self.assertAlmostEqual(p["step"], 0.01, delta=0.0005)
        self.assertAlmostEqual(p["step_at"][1], 1.0, delta=0.005)
        k = math.tan(math.radians(3.0))
        p = self._probe("kink", [(0.0, 0.0), (1.0, 0.0), (2.0, k)])
        self.assertAlmostEqual(p["kink_deg"], 3.0, delta=0.05)
        NOTE["7c kink (deg) at y"] = (p["kink_deg"], p["kink_at"][1])
        self.assertAlmostEqual(p["kink_at"][1], 1.0, delta=0.005)
        self.assertLess(p["step"], 0.0002)
        self.assertEqual((p["maxima"], p["minima"]), (0, 0))

    def test_7d_probe_coverage(self):
        """A probe that misses the model fails its coverage gate (0 % of the rays hit) and
        its other gates are FAIL "not measured" instead of passing on an empty outline. A
        probe that runs 1 m past the end of a 2 m strip hits 50 % and fails coverage while
        its outline is still analysed. An 8 cm probe (17 rays, all hit) is too short for two
        5 cm chords: its maxima pass, its kink and step are FAIL "not measured"."""
        flat_poly = [(0.0, 0.0), (2.0, 0.0)]
        gates = {"max_maxima": 1, "max_minima": 0, "kink_max_deg": 2.0, "step_max": 0.002}
        p = self._probe("probe_miss", flat_poly, start=[5.0, 0.05, 1.0], end=[5.0, 1.95, 1.0],
                        **gates)
        res = self.last
        self.assertEqual((p["hits"], p["coverage"]), (0, 0.0))
        cov = gate(res, "F.p.coverage")
        self.assertEqual((cov["status"], cov["value"]), ("fail", 0.0))
        for kind in ("maxima", "minima", "kink", "step"):
            g = gate(res, f"F.p.{kind}")
            self.assertEqual((g["status"], g["pass"]), ("not_measured", False), kind)
            self.assertIn("too few points on the outline: 0 of 381 rays hit", g["reason"])
        p = self._probe("probe_half", flat_poly, start=[0.0, 1.0, 1.0], end=[0.0, 3.0, 1.0],
                        **gates)
        NOTE["7d coverage: missed probe, half probe"] = (0.0, p["coverage"])
        self.assertAlmostEqual(p["coverage"], 0.5, delta=0.01)
        self.assertEqual(gate(self.last, "F.p.coverage")["status"], "fail")
        self.assertEqual(gate(self.last, "F.p.maxima")["status"], "pass")
        p = self._probe("probe_short", flat_poly, start=[0.0, 0.5, 1.0], end=[0.0, 0.58, 1.0],
                        **gates)
        self.assertEqual((p["hits"], p["samples"]), (17, 17))
        for kind in ("coverage", "maxima", "minima"):
            self.assertEqual(gate(self.last, f"F.p.{kind}")["status"], "pass", kind)
        for kind, need in (("kink", 21), ("step", 22)):
            g = gate(self.last, f"F.p.{kind}")
            self.assertEqual(g["status"], "not_measured", kind)
            self.assertIn(f"17 of 17 rays hit, this figure needs at least {need}", g["reason"])

    def test_7e_exact_maxima(self):
        """maxima = 1 asks for exactly one maximum: one bump passes, a flat outline (0) and
        two bumps (2) fail; giving both maxima and max_maxima is a configuration error."""
        ys = np.arange(0.0, 2.0001, 0.002)
        one = [(y, 0.1 * math.exp(-((y - 1.0) / 0.2) ** 2)) for y in ys]
        two = [(y, 0.1 * math.exp(-((y - 0.7) / 0.15) ** 2)
                + 0.1 * math.exp(-((y - 1.3) / 0.15) ** 2)) for y in ys]
        results = []
        for name, poly in (("exact_one", one), ("exact_flat", [(0.0, 0.0), (2.0, 0.0)]),
                           ("exact_two", two)):
            self._probe(name, poly, maxima=1)
            g = gate(self.last, "F.p.maxima")
            self.assertEqual((g["op"], g["limit"]), ("==", 1))
            results.append((g["value"], g["pass"]))
        NOTE["7e exact maxima = 1: (count, pass) one bump, flat, two bumps"] = results
        self.assertEqual(results, [(1, True), (0, False), (2, False)])
        path = TMP / "both.toml"
        path.write_text('[[probes]]\nname = "x"\nstart = [0, 0, 1]\nend = [0, 1, 1]\n'
                        'nodes = ["skin"]\nmaxima = 1\nmax_maxima = 1\n')
        with self.assertRaises(am.ConfigError):
            am.load_config(path)

    def test_7f_step_on_slope(self):
        """A 3 mm step in an outline that rises at 30 degrees is found whether it goes down
        or up (the step is measured against the slope with its sign, so a drop on a rising
        outline is not hidden by the rise)."""
        k = math.tan(math.radians(30.0))
        y0 = 1.0025  # between two rays
        found = []
        for sign in (-1, 1):
            poly = [(0.0, 0.0), (y0, k * y0), (y0, k * y0 + sign * 0.003),
                    (2.0, k * 2.0 + sign * 0.003)]
            p = self._probe(f"slope_step_{sign}", poly)
            found.append((round(p["step"] * 1000, 3), p["step_at"][1]))
            self.assertAlmostEqual(p["step"], 0.003, delta=0.0001)
            self.assertAlmostEqual(p["step_at"][1], y0, delta=0.005)
        NOTE["7f step on a 30 deg slope (mm, y): down, up"] = found
        p = self._probe("slope_plain", [(0.0, 0.0), (2.0, 2.0 * k)])
        self.assertLess(p["step"], 1e-5)

    def test_8_subpixel_boundary(self):
        """At 8 m (1080 px, 60 deg) a pixel is p = 8.553 mm wide: right triangles of area
        0.999 p^2 are sub-pixel, those of 1.001 p^2 are not."""
        p = 2 * 8.0 * math.tan(math.radians(30.0)) / 1080
        tris = []
        for i, f in enumerate([0.999] * 7 + [1.001] * 5):
            a = math.sqrt(2 * f * p * p)
            x = 0.1 * i
            tris.append([(x, 0, 0), (x + a, 0, 0), (x, a, 0)])
        v = np.array(tris).reshape(-1, 3)
        t = np.arange(len(v)).reshape(-1, 3)
        m = write("subpixel", [node("tiny", v, t, np.tile([0.0, 0.0, 1.0], (len(v), 1)))])
        _a, res = audit(m, None, config(nodes=["tiny"]))
        NOTE["8 sub-pixel: counted of 7 below, 5 above"] = res["nodes"]["tiny"]["subpixel"]
        self.assertEqual(res["nodes"]["tiny"]["subpixel"], 7)
        self.assertAlmostEqual(res["groups"]["exterior"]["pixel_size"], p, places=6)

    def test_9_decimate_is_deterministic(self):
        """Blender's Decimate gives identical output for identical input."""
        v, t, _n = uv_sphere(1.0, 48)
        out = []
        for _ in range(2):
            out.append([tv.copy() for _r, tv in am.decimate(v, t, [0.5, 0.2])])
        for x, y in zip(*out):
            self.assertEqual(x.shape, y.shape)
            self.assertTrue(np.array_equal(x, y))

    def test_9_two_runs_byte_identical(self):
        """Two separate audit processes on the same files write byte-identical JSON. The
        model exercises every check: shape, shading, headroom (a 3 968-triangle sphere),
        hidden faces over poses (in 2 worker processes), a profile probe, hygiene and
        budgets."""
        import bpy
        v, t, n = uv_sphere(1.0, 64)
        rv, rt, rn = uv_sphere(1.0, 256)
        lv, lt = box((-0.3, -0.3, 0.9), (0.3, 0.3, 1.1))
        iv, it = box((-0.2, -0.2, -0.2), (0.2, 0.2, 0.2))
        m = write("det_model", [node("ball", v, t, n), node("cap", lv, lt, material="glass"),
                                node("core", iv, it)])
        r = write("det_ref", [node("ball", rv, rt, rn), node("cap", lv, lt, material="glass"),
                              node("core", iv, it)])
        cfgp = TMP / "det.toml"
        cfgp.write_text(
            '[model]\nname = "det"\npublished_size = { length = 2.0, span = 2.0, height = 2.1 }\n'
            '[headroom]\nmin_component = 2000\n'
            '[visibility]\nparallel_min = 0\n'
            '[budget]\ntotal = 100000\n'
            '[[groups]]\nname = "exterior"\nnodes = ["ball", "cap", "core"]\n'
            '[[poses]]\nname = "rest"\n'
            '[[poses]]\nname = "tilt"\n[poses.rotate]\ncap = [0, 30.0]\n'
            '[[probes]]\nname = "crown"\nstart = [0.5, -0.9, 3.0]\nend = [0.5, 0.9, 3.0]\n'
            'nodes = ["ball"]\nmax_maxima = 1\n')
        blobs = []
        for k in range(2):
            out = TMP / f"det_out{k}"
            cmd = [bpy.app.binary_path, "--background", "--factory-startup",
                   "--python-exit-code", "2", "--python", str(HERE / "audit_model.py"), "--",
                   "--model", str(m), "--reference", str(r), "--config", str(cfgp),
                   "--out", str(out), "--jobs", "2"]
            proc = subprocess.run(cmd, capture_output=True, text=True)
            self.assertIn(proc.returncode, (0, 1), proc.stdout[-2000:] + proc.stderr[-2000:])
            self.assertNotIn("visibility worker", proc.stderr)  # no worker failed
            md = (out / "det_audit.md").read_text()
            self.assertIn("ran in 2 worker processes", md)
            blobs.append((out / "det_audit.json").read_bytes())
        NOTE["9 determinism: JSON bytes, identical"] = (len(blobs[0]), blobs[0] == blobs[1])
        self.assertEqual(blobs[0], blobs[1])
        res = json.loads(blobs[0])
        self.assertEqual(len(res["headroom"]), 1)
        self.assertEqual(res["nodes"]["core"]["hidden_frac"], 1.0)

    def test_10_missing_data_is_not_a_pass(self):
        """What cannot be measured never passes. A group whose nodes are all hidden: its A
        and B gates FAIL "not measured". A model node without a reference node, and a
        reference node the model lacks: FAIL; a group made only of such nodes: its A and B
        gates FAIL "not measured", naming the node. A group with no node in the model:
        ABSENT (not a pass, not a fail), or FAIL when it is required. No reference at all:
        the A and B gates FAIL "not measured"."""
        bv, bt = box((-0.5, -0.5, -0.5), (0.5, 0.5, 0.5))
        iv, it = box((-0.2, -0.2, -0.2), (0.2, 0.2, 0.2))
        sv, st = box((0.5, -0.1, -0.1), (0.7, 0.1, 0.1))
        xv, xt = box((-0.7, -0.1, -0.1), (-0.5, 0.1, 0.1))
        m = write("missing", [node("body", bv, bt), node("inner", iv, it), node("spare", sv, st)])
        r = write("missing_ref", [node("body", bv, bt), node("inner", iv, it),
                                  node("extra", xv, xt)])
        cfg = config(nodes=["body"])
        cfg["groups"][0]["required"] = True
        cfg["groups"] += [
            {**am.GROUP_DEFAULTS, "name": "hiddenpart", "nodes": ["inner"]},
            {**am.GROUP_DEFAULTS, "name": "loose", "nodes": ["spare"]},
            {**am.GROUP_DEFAULTS, "name": "cockpit", "nodes": ["seat"]},
            {**am.GROUP_DEFAULTS, "name": "interior", "nodes": ["tank"], "required": True}]
        _a, res = audit(m, r, cfg)
        st_ = {g["id"]: g["status"] for g in res["gates"]}
        NOTE["10 missing data: statuses"] = {k: st_[k] for k in (
            "A.hiddenpart.p99", "A.node.spare", "A.node.extra", "A.cockpit.present",
            "A.interior.present")}
        self.assertEqual(st_["A.exterior.present"], "pass")
        for gid in ("A.hiddenpart.p99", "A.hiddenpart.max", "A.hiddenpart.rev_p99",
                    "A.hiddenpart.rev_max", "B.hiddenpart.p95", "B.hiddenpart.max"):
            self.assertEqual(st_[gid], "not_measured", gid)
        self.assertIn("hidden", gate(res, "A.hiddenpart.p99")["reason"])
        self.assertEqual(st_["A.node.spare"], "not_measured")
        loose = gate(res, "A.loose.p99")
        self.assertEqual(loose["status"], "not_measured")
        self.assertIn("spare: no reference node of that name", loose["reason"])
        self.assertEqual(st_["A.node.extra"], "fail")
        self.assertEqual(st_["A.cockpit.present"], "absent")
        self.assertFalse(gate(res, "A.cockpit.present")["pass"])
        self.assertEqual(st_["A.interior.present"], "fail")
        self.assertEqual(res["summary"]["absent"], 1)
        self.assertNotIn("A.cockpit.p99", st_)
        md = am.write_md(res, "missing")
        self.assertIn("ABSENT", md)
        self.assertIn("not measured", md)
        _a, res = audit(m, None, cfg)
        g = gate(res, "A.exterior.p99")
        self.assertEqual((g["status"], g["reason"]), ("not_measured", "no reference given"))

    def test_11_internal_error_exits_2(self):
        """An unexpected error inside the audit prints its traceback and gives exit status
        2, never 0, also as the exit status of the Blender process (which exits 0 after an
        uncaught Python error unless --python-exit-code is given); a configuration error or
        a missing file gives 2 with a one-line message."""
        import contextlib
        import io
        import bpy
        v, t = box((0, 0, 0), (1, 1, 1))
        m = write("crash", [node("cube", v, t)])
        original = am.Audit.run
        am.Audit.run = lambda self: 1 / 0
        err = io.StringIO()
        try:
            with contextlib.redirect_stderr(err):
                code = am.main(["--model", str(m), "--out", str(TMP / "crash_out")])
        finally:
            am.Audit.run = original
        self.assertEqual(code, 2)
        self.assertIn("Traceback", err.getvalue())
        self.assertIn("ZeroDivisionError", err.getvalue())
        # the same bug in a Blender process started without --python-exit-code: exit 2
        expr = (f"import sys; sys.path.insert(0, {str(HERE)!r}); import audit_model as am; "
                "am.Audit.run = lambda self: 1 / 0; "
                f"sys.exit(am.main(['--model', {str(m)!r}, '--out', "
                f"{str(TMP / 'crash_out')!r}]))")
        proc = subprocess.run([bpy.app.binary_path, "--background", "--factory-startup",
                               "--python-expr", expr], capture_output=True, text=True)
        self.assertEqual(proc.returncode, 2, proc.stdout[-1500:] + proc.stderr[-1500:])
        self.assertIn("ZeroDivisionError", proc.stderr)
        # the real script on a missing model file: exit 2 and a one-line message
        proc_missing = subprocess.run(
            [bpy.app.binary_path, "--background", "--factory-startup", "--python-exit-code",
             "2", "--python", str(HERE / "audit_model.py"), "--", "--model",
             str(TMP / "no_such.glb"), "--out", str(TMP / "crash_out")],
            capture_output=True, text=True)
        self.assertEqual(proc_missing.returncode, 2)
        self.assertIn("[audit] error:", proc_missing.stderr)
        self.assertNotIn("Traceback", proc_missing.stderr)
        NOTE["11 internal error: exit status in process, Blender process; missing file"] = (
            code, proc.returncode, proc_missing.returncode)
        path = TMP / "bad_pose.toml"
        path.write_text('[[poses]]\nname = "rest"\nhide = ["cube"]\n')
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = am.main(["--model", str(m), "--config", str(path), "--out",
                            str(TMP / "crash_out")])
        self.assertEqual(code, 2)
        self.assertIn("[audit] error: pose 'rest'", err.getvalue())
        self.assertNotIn("Traceback", err.getvalue())

    def test_glb_variants(self):
        """The reader handles what the writer can also produce: a node with a matrix,
        positions and normals interleaved in one strided buffer view, two primitives of one
        node with the same material (whose index gates get distinct ids), and a BLEND
        material that the config does not call glass (a warning)."""
        v, t = box((0, 0, 0), (1, 2, 3))
        fv, fn, ft = flat(v, t)
        mat = np.eye(4)
        mat[:3, :3] = [[0, 0, 1], [0, 1, 0], [-1, 0, 0]]  # 90 degrees about y (glTF frame)
        mat[:3, 3] = [1.0, 2.0, 3.0]
        prims = [{"positions": to_gltf(fv), "normals": to_gltf(fn), "indices": ft,
                  "material": "paint", "interleave": True},
                 {"positions": to_gltf(fv + 2.0), "normals": to_gltf(fn), "indices": ft,
                  "material": "paint"}]
        nodes = [{"name": "twin", "prims": prims, "matrix": mat.T.reshape(-1)},
                 {**node("film", fv, ft, fn, material="film"), "translation": [0, 0, -3.0]}]
        path = TMP / "variants.glb"
        glb_io.write_glb(path, nodes, MATS + [{"name": "film", "alpha_mode": "BLEND"}])
        model = glb_io.read_glb(path)
        self.assertTrue(np.allclose(model.world_matrices()["twin"], mat))
        self.assertTrue(np.allclose(model.prims[0].positions, to_gltf(fv), atol=1e-6))
        self.assertTrue(np.allclose(model.prims[0].normals, to_gltf(fn), atol=1e-6))
        self.assertEqual([p.index for p in model.prims], [0, 1, 0])
        doc = model.doc
        self.assertEqual(doc["bufferViews"][doc["accessors"][0]["bufferView"]]["byteStride"], 24)
        _a, res = audit(path, None, config(nodes=["twin", "film"]))
        ids = sorted(g["id"] for g in res["gates"] if g["id"].startswith("C.index."))
        self.assertEqual(ids, ["C.index.film.film", "C.index.twin.paint.p0",
                               "C.index.twin.paint.p1"])
        self.assertTrue(any("material film: alphaMode BLEND" in w for w in res["warnings"]))
        self.assertFalse(any("material glass" in w for w in res["warnings"]))

    def test_glb_round_trip(self):
        """The reader returns what the writer wrote (positions, normals, indices, nodes,
        parent transforms, u16 and u32 indices)."""
        v, t = box((0, 0, 0), (1, 2, 3))
        nodes = [node("a", v, t), {**node("b", v, t, index_type="u32"),
                                   "translation": [1.0, 2.0, 3.0], "parent": "a"}]
        path = write("roundtrip", nodes)
        model = glb_io.read_glb(path)
        self.assertEqual([n.name for n in model.nodes], ["a", "b"])
        self.assertEqual([p.index_type for p in model.prims], [5123, 5125])
        w = model.world_matrices()
        self.assertTrue(np.allclose(w["b"][:3, 3], [1.0, 2.0, 3.0]))
        fv, _fn, ft = flat(v, t)
        self.assertTrue(np.allclose(model.prims[0].positions, to_gltf(fv), atol=1e-6))
        self.assertTrue(np.array_equal(model.prims[0].indices, ft))


if __name__ == "__main__":
    import os
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    prog = unittest.main(argv=["test_audit"] + argv, exit=False, verbosity=2)
    ok = prog.result.wasSuccessful()
    print(f"[test_audit] {prog.result.testsRun} tests, "
          f"{len(prog.result.failures) + len(prog.result.errors)} failed"
          + ("" if ok else f"; files kept in {TMP}"))
    for key in sorted(NOTE):
        print(f"[test_audit] {key}: {NOTE[key]}")
    if ok:
        import shutil
        shutil.rmtree(TMP, ignore_errors=True)
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(0 if ok else 1)
