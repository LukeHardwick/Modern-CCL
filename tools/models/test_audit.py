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


def audit(model, ref=None, cfg=None, fast=False):
    a = am.Audit(model, ref, cfg or config(), fast=fast, log=lambda s: None)
    return a, a.run()


def gate(res, gid):
    for g in res["gates"]:
        if g["id"] == gid:
            return g
    raise KeyError(gid)


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

    def test_3_box_in_box(self):
        """A closed box inside a closed box: the inner one is 100 % hidden, the outer 0 %."""
        ov, ot = box((-1, -1, -1), (1, 1, 1))
        iv, it = box((-0.5, -0.5, -0.5), (0.5, 0.5, 0.5))
        m = write("boxes", [node("outer", ov, ot), node("inner", iv, it)])
        _a, res = audit(m, None, config(nodes=["outer", "inner"]))
        NOTE["3 boxes: inner / outer hidden"] = (res["nodes"]["inner"]["hidden_frac"],
                                                 res["nodes"]["outer"]["hidden_frac"])
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

    def test_6_index_limit(self):
        """A primitive with 70 000 vertices fails the 16-bit index gate (hard); one with
        65 536 vertices passes."""
        def big(nv):
            v = np.zeros((nv, 3))
            v[:, 0] = np.arange(nv) * 1e-3
            v[:, 1] = (np.arange(nv) % 2) * 1e-3
            t = np.array([(0, 1, 2), (nv - 3, nv - 2, nv - 1)])
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
        NOTE["6 index: max index, exit status"] = (g["value"], code)
        self.assertEqual(code, 1)
        v, t, n = big(65536)
        m = write("big65k", [node("big", v, t, n, index_type="u32")])
        _a, res = audit(m)
        self.assertTrue(gate(res, "C.index.big.paint")["pass"])

    def _probe(self, name, poly, extra=()):
        sv, st = strip(poly)
        nodes = [node("skin", sv, st)] + list(extra)
        m = write(name, nodes)
        cfg = config(nodes=["skin"] + [e["name"] for e in extra])
        cfg["probes"] = [{**am.PROBE_DEFAULTS, "name": "p", "start": [0.0, 0.05, 1.0],
                          "end": [0.0, 1.95, 1.0], "nodes": ["skin"] +
                          [e["name"] for e in extra]}]
        _a, res = audit(m, None, cfg)
        return res["probes"]["p"]

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
        hidden faces over poses, a profile probe, hygiene and budgets."""
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
                   "--out", str(out)]
            proc = subprocess.run(cmd, capture_output=True, text=True)
            self.assertIn(proc.returncode, (0, 1), proc.stdout[-2000:] + proc.stderr[-2000:])
            blobs.append((out / "det_audit.json").read_bytes())
        NOTE["9 determinism: JSON bytes, identical"] = (len(blobs[0]), blobs[0] == blobs[1])
        self.assertEqual(blobs[0], blobs[1])
        res = json.loads(blobs[0])
        self.assertEqual(len(res["headroom"]), 1)
        self.assertEqual(res["nodes"]["core"]["hidden_frac"], 1.0)

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
