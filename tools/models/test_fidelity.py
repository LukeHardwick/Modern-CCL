# Known-answer tests for the fidelity checker tools/models/fidelity_audit.py (task T22e).
# Run with plain Python (numpy) or headless Blender:
#
#   python3 tools/models/test_fidelity.py
#   blender --background --factory-startup --python-exit-code 2 --python tools/models/test_fidelity.py
#
# Exit status 0 when every test passes, 1 when one fails. Each test builds small meshes or
# images whose measured value is known in advance (areas, ellipse and superellipse ratios,
# rounded-rectangle corner radii, bevel radii, texel density, UV overlap and padding, crease
# share of a cube) and compares the checker's answer with it.

import copy
import hashlib
import json
import math
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.dont_write_bytecode = True

import fidelity_audit as fa  # noqa: E402
import fidelity_features  # noqa: E402
import fidelity_model as fm  # noqa: E402
import fidelity_outline as fo  # noqa: E402
import fidelity_section as fs  # noqa: E402
import fidelity_tex as ft  # noqa: E402
import glb_io  # noqa: E402
import png_io  # noqa: E402
import raster_util as ru  # noqa: E402

TMP = Path(tempfile.mkdtemp(prefix="test_fidelity_"))
NOTE = {}
REPO = HERE.parent.parent


# ---------------------------------------------------------------- builders


def to_gltf(p):
    return np.asarray(p, dtype=np.float64) @ glb_io.GLTF_TO_MODEL


def prim(v, t, material="paint", uvs=None, colors=None):
    d = {"positions": to_gltf(v), "indices": np.asarray(t, dtype=np.int64), "material": material}
    if uvs is not None:
        d["uvs"] = uvs
    if colors is not None:
        d["colors"] = colors
    return d


def write(name, nodes, materials=None, images=None):
    path = TMP / f"{name}.glb"
    glb_io.write_glb(path, nodes, materials or [{"name": "paint"}], images)
    return path


def box(lo, hi):
    lo, hi = np.asarray(lo, float), np.asarray(hi, float)
    v = np.array([[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])])
    quads = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    t = []
    for a, b, c, d in quads:
        t += [(a, b, c), (a, c, d)]
    return v, np.array(t)


def tube(poly, y0, y1):
    """Open tube: closed polygon `poly` (K, 2) in (x, z), extruded along y from y0 to y1."""
    k = len(poly)
    v = np.concatenate([np.column_stack([poly[:, 0], np.full(k, y0), poly[:, 1]]),
                        np.column_stack([poly[:, 0], np.full(k, y1), poly[:, 1]])])
    t = []
    for i in range(k):
        j = (i + 1) % k
        t += [(i, j, k + j), (i, k + j, k + i)]
    return v, np.array(t)


def ellipse(a, b, n=720, power=2.0):
    th = np.linspace(0, 2 * math.pi, n, endpoint=False)
    c, s = np.cos(th), np.sin(th)
    return np.column_stack([a * np.sign(c) * np.abs(c) ** (2 / power),
                            b * np.sign(s) * np.abs(s) ** (2 / power)])


def rounded_rect(w, h, r, per_corner=60):
    pts = []
    for cx, cy, a0 in ((w / 2 - r, h / 2 - r, 0), (-w / 2 + r, h / 2 - r, 90),
                       (-w / 2 + r, -h / 2 + r, 180), (w / 2 - r, -h / 2 + r, 270)):
        for a in np.linspace(a0, a0 + 90, per_corner):
            pts.append((cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))))
    return np.array(pts)


def png_bytes(arr):
    path = TMP / "tmp.png"
    png_io.write_png(path, np.asarray(arr, dtype=np.uint8))
    return path.read_bytes()


def quad(origin, du, dv):
    o = np.asarray(origin, float)
    v = np.array([o, o + du, o + du + dv, o + dv])
    return v, np.array([(0, 1, 2), (0, 2, 3)])


# ---------------------------------------------------------------- configs

BASE_CFG = {
    "model": {"published_size": {"length": 4.0, "span": 4.0, "height": 1.0},
              "exterior_exclude_nodes": [], "interior_nodes": []},
    "gates": {"hard": ["F1", "F2.required", "F3", "F4", "F5"]},
    "F1_outline": {"reference": "ref.toml", "raster_px_per_cm": 2,
                   "iou_min": {"side": 0.94, "top": 0.94, "front": 0.9}, "outline_p95_mm": 30,
                   "outline_max_mm": 80, "station_tol_mm": 40,
                   "views": {v: {"exclude_nodes": [], "exclude_prims": []} for v in ("side", "top", "front")}},
    "F2_features": {"required_fraction": 1.0, "optional_fraction": 0.75, "groove_tilt": 0.1,
                    "colour_delta": 40, "min_mark_px": 4},
    "F3_sections": {"nodes": []},
    "F4_textures": {"family": "textured", "exempt_materials": ["glass"], "max_texture_px": 2048,
                    "assumed_sheet_px": 2048, "density_spread_max": 2.0, "uv_overlap_px_max": 0,
                    "padding_px_min": 8, "bleed_px": 8, "bleed_diff_max": 12, "bleed_bad_max": 0.05,
                    "normal_unit_length_tol": 0.02, "metallic_binary_fraction_min": 0.95,
                    "base_colour_srgb_range": [30, 240], "base_colour_outside_max": 0.01,
                    "gpu_bytes_max": 268435456, "glb_bytes_max": 25165824,
                    "glb_bytes_max_vertex_colour": 10000000, "primitive_vertices_max": 65535,
                    "density_group": [{"name": "airframe", "min_px_per_m": 400, "default": True}]},
    "F5_edges": {"min_segments": 3, "resolved_radius_mm": 2.0, "no_named_edge_radius_over_mm": 40,
                 "hard_crease_fraction": [0.03, 0.08]},
}


def cfg_with(**over):
    c = copy.deepcopy(BASE_CFG)
    for k, v in over.items():
        c[k] = v
    return c


def gate_of(gates, gid):
    for g in gates:
        if g["id"] == gid:
            return g
    raise KeyError(gid)


def make_reference(root, rects):
    """Synthetic reference: per view a mask that is entirely filled, of the given pixel size."""
    lines = ['[source]', 'svg_sha256 = "synthetic"', ""]
    for view, (w, h) in rects.items():
        png_io.write_png(root / f"{view}.png", np.full((h, w), 255, np.uint8))
        lines += [f"[views.{view}]", f'file = "{view}.png"', 'sha256 = "x"', f"size_px = [{w}, {h}]",
                  f"extent_px = {{ left = 0, right = {w - 1}, top = 0, bottom = {h - 1} }}", ""]
    (root / "ref.toml").write_text("\n".join(lines))


# ---------------------------------------------------------------- tests


class TestRaster(unittest.TestCase):
    def test_label_components(self):
        m = np.zeros((6, 8), bool)
        m[0:2, 0:2] = True
        m[3:5, 4:7] = True
        m[5, 7] = True       # diagonal neighbour of (4, 6)
        lab, n = ru.label_components(m, 4)
        self.assertEqual(n, 3)
        lab8, n8 = ru.label_components(m, 8)
        self.assertEqual(n8, 2)
        u = np.zeros((5, 5), bool)
        u[0, :] = u[:, 0] = u[:, 4] = True     # a U: one component
        self.assertEqual(ru.label_components(u, 4)[1], 1)

    def test_morphology(self):
        m = np.zeros((21, 21), bool)
        m[10, 10] = True
        d = ru.dilate(m, 3)
        self.assertEqual(int(d.sum()), len(ru.disk_offsets(3)))
        self.assertTrue(np.array_equal(ru.erode(d, 3), m))

    def test_triangle_area_and_no_double_count(self):
        tri = np.array([[[0.0, 0.0], [100.0, 0.0], [0.0, 100.0]]])
        img = ru.rasterise_triangles(tri, 120, 120)
        self.assertAlmostEqual(img.sum(), 5000, delta=100)
        # a square made of 2 triangles with vertices on pixel corners: every pixel exactly once
        sq = np.array([[[10.0, 10], [50, 10], [50, 50]], [[10.0, 10], [50, 50], [10, 50]]])
        img = ru.rasterise_triangles(sq, 64, 64)
        self.assertEqual(int(img.sum()), 1600)
        self.assertEqual(int(img.max()), 1)
        fan = np.array([[[32.0, 32], [32 + 20 * math.cos(a), 32 + 20 * math.sin(a)],
                         [32 + 20 * math.cos(a + 0.5), 32 + 20 * math.sin(a + 0.5)]]
                        for a in np.arange(0, 2 * math.pi - 0.4, 0.5)])
        self.assertEqual(int(ru.rasterise_triangles(fan, 64, 64).max()), 1)

    def test_nearest_distances(self):
        a = np.array([[0.0, 0.0], [3.0, 4.0]])
        b = np.array([[0.0, 1.0], [6.0, 8.0]])
        self.assertTrue(np.allclose(ru.nearest_distances(a, b), [1.0, math.hypot(3, 3)]))


class TestPng(unittest.TestCase):
    def _png(self, arr, ftype):
        h, w, c = arr.shape
        ctype = {1: 0, 3: 2, 4: 6}[c]
        raw = bytearray()
        prev = np.zeros(w * c, np.int32)
        for y in range(h):
            line = arr[y].reshape(-1).astype(np.int32)
            out = line.copy()
            for i in range(w * c):
                a = line[i - c] if i >= c else 0
                b = prev[i]
                cc = prev[i - c] if i >= c else 0
                if ftype == 1:
                    out[i] = (line[i] - a) & 255
                elif ftype == 2:
                    out[i] = (line[i] - b) & 255
                elif ftype == 3:
                    out[i] = (line[i] - ((a + b) >> 1)) & 255
                elif ftype == 4:
                    p = a + b - cc
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - cc)
                    pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else cc)
                    out[i] = (line[i] - pred) & 255
            raw.append(ftype)
            raw.extend(out.astype(np.uint8).tobytes())
            prev = line

        def chunk(t, body):
            return struct.pack(">I", len(body)) + t + body + struct.pack(">I", zlib.crc32(t + body) & 0xFFFFFFFF)
        return (png_io.SIG + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))

    def test_round_trip_and_all_filters(self):
        rng = np.random.default_rng(3)
        arr = rng.integers(0, 256, size=(13, 17, 4), dtype=np.uint8)
        for ft_ in range(5):
            got, info = png_io.read_png(self._png(arr, ft_))
            self.assertTrue(np.array_equal(got, arr), f"filter {ft_}")
        png_io.write_png(TMP / "rt.png", arr[..., :3])
        got, _ = png_io.read_png((TMP / "rt.png").read_bytes())
        self.assertTrue(np.array_equal(got, arr[..., :3]))

    def test_deterministic_bytes(self):
        arr = np.arange(300, dtype=np.uint8).reshape(10, 10, 3)
        png_io.write_png(TMP / "a.png", arr)
        png_io.write_png(TMP / "b.png", arr)
        self.assertEqual((TMP / "a.png").read_bytes(), (TMP / "b.png").read_bytes())


class TestGlbExtension(unittest.TestCase):
    def test_uv_color_image_round_trip(self):
        v, t = quad((0, 0, 0), (1, 0, 0), (0, 0, 1))
        uv = np.array([[0.1, 0.2], [0.9, 0.2], [0.9, 0.8], [0.1, 0.8]])
        img = png_bytes(np.full((8, 8, 3), 100))
        path = write("uvrt", [{"name": "n", "prims": [prim(v, t, uvs=uv, colors=np.ones((4, 3)),
                                                          material="paint")]}],
                     [{"name": "paint", "base_image": 0, "normal_image": 0, "orm_image": 0}], [img])
        m = glb_io.read_glb(path)
        self.assertTrue(np.allclose(m.prims[0].uvs, uv, atol=1e-6))
        self.assertTrue(m.prims[0].has_color)
        d = fm.ModelData(path)
        self.assertEqual(d.material_maps(0), {"baseColor": 0, "normal": 0, "occlusion": 0,
                                              "metallicRoughness": 0})
        raw, _ = d.image_bytes(0)
        self.assertEqual(raw, img)


class TestF1(unittest.TestCase):
    def setUp(self):
        self.root = TMP / "f1ref"
        self.root.mkdir(exist_ok=True)
        make_reference(self.root, {"side": (400, 100), "top": (400, 400), "front": (400, 100)})

    def run_box(self, hi_z=1.0, hi_y=4.0, name="f1box"):
        v, t = box((-2, 0, 0), (2, hi_y, hi_z))
        path = write(name, [{"name": "body", "prims": [prim(v, t)]}])
        cfg = cfg_with()
        cfg["F1_stations"] = [
            {"name": "len", "view": "side", "kind": "extreme", "edge": "max_h"},
            {"name": "roof", "view": "side", "kind": "scan", "scan": "v", "at": 2.0, "take": "max"},
            {"name": "front_edge", "view": "side", "kind": "scan", "scan": "h", "at": 0.5, "take": "min"}]
        data = fm.ModelData(path)
        gates, det, _ = fo.run(data, cfg, self.root)
        return gates, det

    def test_identical_shape(self):
        gates, det = self.run_box()
        for v in ("side", "top", "front"):
            self.assertGreater(det["views"][v]["iou"], 0.985, v)
            self.assertLess(det["views"][v]["model_to_reference_mm"]["max"], 12.0)
        self.assertTrue(all(g["status"] == "pass" for g in gates), [g["id"] for g in gates if g["status"] != "pass"])
        NOTE["F1 identical box: IoU side/top/front"] = [det["views"][v]["iou"] for v in ("side", "top", "front")]

    def test_known_iou_and_distance(self):
        gates, det = self.run_box(hi_z=1.2)
        # side and front: the model is 1.2 m high against a 1.0 m reference: IoU = 1 / 1.2
        self.assertAlmostEqual(det["views"]["side"]["iou"], 1 / 1.2, delta=0.006)
        self.assertAlmostEqual(det["views"]["front"]["iou"], 1 / 1.2, delta=0.006)
        self.assertGreater(det["views"]["top"]["iou"], 0.985)
        self.assertAlmostEqual(det["views"]["side"]["model_to_reference_mm"]["max"], 200.0, delta=8.0)
        self.assertEqual(gate_of(gates, "F1.side.iou")["status"], "fail")
        roof = [s for s in det["stations"] if s["name"] == "roof"][0]
        self.assertAlmostEqual(roof["delta_mm"], 200.0, delta=12.0)
        self.assertEqual(gate_of(gates, "F1.station.roof")["status"], "fail")
        self.assertEqual(gate_of(gates, "F1.station.len")["status"], "pass")
        NOTE["F1 1.2 m box vs 1.0 m reference: IoU, max outline mm"] = (
            det["views"]["side"]["iou"], det["views"]["side"]["model_to_reference_mm"]["max"])

    def test_length_error_is_not_fitted_away(self):
        # 5 % too long: the reference is scaled to the PUBLISHED length, so the tail shows
        gates, det = self.run_box(hi_y=4.2, name="f1long")
        side = det["views"]["side"]
        self.assertAlmostEqual(side["iou"], 4.0 / 4.2, delta=0.006)
        st = [s for s in det["stations"] if s["name"] == "len"][0]
        self.assertAlmostEqual(st["delta_mm"], 200.0, delta=12.0)

    def test_wing_excluded_from_side_only(self):
        v, t = box((-2, 0, 0), (2, 4, 1))
        wv, wt = box((-2, 1, 0.4), (2, 2, 0.5))
        wv[:, 0] *= 2.0
        path = write("f1wing", [{"name": "body", "prims": [prim(v, t)]},
                                {"name": "wing", "prims": [prim(wv, wt)]}])
        cfg = cfg_with()
        cfg["F1_outline"]["views"]["side"]["exclude_nodes"] = ["wing"]
        data = fm.ModelData(path)
        sel = fo.view_setup(data, cfg, fo.load_reference(self.root / "ref.toml")[0], "side", 0.005)
        self.assertEqual([p.node for p in sel[0]], ["body"])


class TestF3(unittest.TestCase):
    def sec(self, poly, extra=None, y=1.0, flat_h=None, nodes=None):
        v, t = tube(poly, 0.0, 2.0)
        n = [{"name": "fuselage", "prims": [prim(v, t)]}]
        path = write("sec", n + (extra or []))
        data = fm.ModelData(path)
        tris = data.tri_array(data.select(nodes=nodes))
        o = fs.section_outline(fs.cut_segments(tris, 1, y))
        return fs.section_metrics(o, flat_h), o

    def test_ellipse(self):
        a, b = 0.5, 0.4
        m, _ = self.sec(ellipse(a, b))
        self.assertAlmostEqual(m["ratio"], a / b, delta=0.005)
        self.assertAlmostEqual(m["fill"], math.pi / 4, delta=0.003)
        self.assertAlmostEqual(m["radius_m"], b * b / a, delta=0.32 * 0.06)   # tightest: b^2 / a
        NOTE["F3 ellipse 1.0 x 0.8: ratio, fill (pi/4), min radius (b^2/a = 0.32)"] = (
            round(m["ratio"], 4), round(m["fill"], 4), round(m["radius_m"], 4))

    def test_superellipse_fill(self):
        for n in (2.4, 4.0):
            m, _ = self.sec(ellipse(0.5, 0.35, 1440, power=n))
            exact = math.gamma(1 + 1 / n) ** 2 / math.gamma(1 + 2 / n)
            self.assertAlmostEqual(m["fill"], exact, delta=0.004, msg=f"n={n}")
            self.assertAlmostEqual(m["ratio"], 0.5 / 0.35, delta=0.004)

    def test_rounded_rectangle_corner_and_flatness(self):
        for r in (0.06, 0.12, 0.25):
            m, _ = self.sec(rounded_rect(1.0, 0.7, r), flat_h=0.15)   # straight part of the side: 0.2 m at r = 0.25
            self.assertAlmostEqual(m["radius_m"], r, delta=0.12 * r, msg=f"r={r}")
            self.assertAlmostEqual(m["fill"], (1.0 * 0.7 - (4 - math.pi) * r * r) / 0.7, delta=0.004)
            self.assertLess(m["flat_m"], 0.0015)
            NOTE[f"F3 rounded rectangle r={r}: measured radius"] = round(m["radius_m"], 4)

    def test_side_bulge(self):
        # right and left sides are circular arcs of sag s over the 0.6 m window: the least-squares
        # line deviates at most 2/3 s (parabola approximation)
        s = 0.020
        h = 0.7
        z = np.linspace(-h / 2, h / 2, 200)
        sag = s * (1 - (z / (h / 2)) ** 2)
        right = np.column_stack([0.5 + sag, z])
        left = np.column_stack([-0.5 - sag, z[::-1]])
        top = np.column_stack([np.linspace(0.5, -0.5, 60)[1:-1], np.full(58, h / 2)])
        bot = np.column_stack([np.linspace(-0.5, 0.5, 60)[1:-1], np.full(58, -h / 2)])
        poly = np.concatenate([right, top, left, bot])
        m, _ = self.sec(poly, flat_h=0.7)
        self.assertAlmostEqual(m["flat_m"], 2 * s / 3, delta=0.003)

    def test_open_roof_is_bridged(self):
        poly = rounded_rect(1.0, 0.7, 0.12)
        keep = ~((poly[:, 1] > 0.349) & (np.abs(poly[:, 0]) < 0.25))   # cut a gap in the roof
        # a tube over the remaining points: the polygon is open at the top
        pts = poly[keep]
        v, t = tube(pts, 0.0, 2.0)
        t = np.array([tri for tri in t if not (set(tri) & {len(pts) - 1} and set(tri) & {0})])
        # drop the wrap-around quad (it would close the gap)
        k = len(pts)
        t = np.array([tri for tri in tube(pts, 0, 2)[1]
                      if not ({int(x) % k for x in tri} == {0, k - 1} or
                              ({int(x) % k for x in tri} <= {0, k - 1} and len({int(x) % k for x in tri}) == 2))])
        path = write("open", [{"name": "fuselage", "prims": [prim(v, t)]}])
        data = fm.ModelData(path)
        o = fs.section_outline(fs.cut_segments(data.tri_array(data.prims), 1, 1.0))
        m = fs.section_metrics(o)
        self.assertAlmostEqual(m["height_m"], 0.7, delta=0.01)
        self.assertAlmostEqual(m["width_m"], 1.0, delta=0.01)
        self.assertGreater(m["fill"], 0.93)
        self.assertTrue(o["chord"].any())

    def test_fin_spike_does_not_change_body_radius(self):
        poly = ellipse(0.5, 0.4)
        base, _ = self.sec(poly)
        fin_v, fin_t = box((-0.006, 0.0, 0.38), (0.006, 2.0, 0.55))
        extra = [{"name": "fin", "prims": [prim(fin_v, fin_t)]}]
        m, _ = self.sec(poly, extra=extra)
        self.assertGreater(m["height_m"], 0.9 * 0.95 - 0.01)       # the fin counts in height
        self.assertAlmostEqual(m["radius_m"], base["radius_m"], delta=0.02)


class TestF5(unittest.TestCase):
    def wedge(self, r, n, half_angle_deg=10.0, length=0.2):
        """Extruded (along x) trailing-edge wedge in (y, z): straight sides at +-half_angle,
        tip rounded by an arc of radius r drawn with n segments; tip points to +y."""
        a = math.radians(half_angle_deg)
        # circle centre at y = 0; the arc spans -(90 - a) .. +(90 - a) degrees about +y
        t0 = (math.pi / 2 - a)
        ang = np.linspace(-t0, t0, n + 1)
        arc = np.column_stack([r * np.cos(ang), r * np.sin(ang)])
        tan_pt = arc[-1]
        # sides go back (-y) from the tangent points at the wedge angle
        back = length
        top = tan_pt + back * np.array([-math.cos(a), math.sin(a)])
        bot = arc[0] + back * np.array([-math.cos(a), -math.sin(a)])
        poly = np.vstack([bot, arc, top])
        return poly

    def extrude_x(self, poly2d):
        k = len(poly2d)
        v = np.concatenate([np.column_stack([np.full(k, -1.0), poly2d[:, 0], poly2d[:, 1]]),
                            np.column_stack([np.full(k, 1.0), poly2d[:, 0], poly2d[:, 1]])])
        t = []
        for i in range(k):
            j = (i + 1) % k
            t += [(i, j, k + j), (i, k + j, k + i)]
        # end caps so the polyline closes in the section: close the polygon with the back edge
        return v, np.array(t)

    def measure(self, poly, name="wedge"):
        v, t = self.extrude_x(poly)
        path = write(name, [{"name": "w", "prims": [prim(v, t)]}])
        data = fm.ModelData(path)
        return fs.edge_probe(data, {"nodes": ["w"], "plane": {"axis": "x", "at": 0.0}, "dir": "+y"})

    def test_bevel_radius_and_thickness(self):
        for r_mm, n in ((2.0, 8), (3.0, 12), (6.0, 16)):
            r = r_mm / 1000.0
            res = self.measure(self.wedge(r, n))
            self.assertTrue(res["found"])
            self.assertAlmostEqual(res["radius_m"] * 1000, r_mm, delta=0.15 * r_mm, msg=f"r={r_mm}")
            NOTE[f"F5 bevel r={r_mm} mm, {n} segments: measured mm, segments"] = (
                round(res["radius_m"] * 1000, 3), res["segments"])
        # thickness 1 mm behind the tip of a 2 mm bevel: 2 sqrt(r^2 - (r - 1)^2) = 3.46 mm
        res = self.measure(self.wedge(0.002, 16))
        self.assertAlmostEqual(res["thickness_m"] * 1000, 2 * math.sqrt(4 - 1), delta=0.2)

    def test_sharp_edge_reads_zero(self):
        poly = np.array([[-0.2, -0.03], [0.0, 0.0], [-0.2, 0.03]])
        res = self.measure(poly, "sharp")
        self.assertTrue(res["found"])
        self.assertLess(res["radius_m"] * 1000, 0.5)
        self.assertAlmostEqual(res["thickness_m"] * 1000, 0.3, delta=0.05)   # 2 x 1 mm x tan(wedge)
        self.assertEqual(res["segments"], 0)

    def test_flat_end_equivalent_radius(self):
        # a 3 mm flat end: two corners 3 mm apart read as one chamfer chord of 3 mm turning about 84 degrees: r = 3 / (2 sin 42) = 2.2 mm
        poly = np.array([[-0.2, -0.0015 - 0.02], [0.0, -0.0015], [0.0, 0.0015], [-0.2, 0.0015 + 0.02]])
        res = self.measure(poly, "flat")
        self.assertAlmostEqual(res["thickness_m"] * 1000, 3.2, delta=0.05)   # 3 mm + 2 x 1 mm x 0.1
        turn = math.radians(90) - math.atan(0.1)         # each corner turns 90 degrees less the wedge slope
        self.assertAlmostEqual(res["radius_m"] * 1000, 3.0 / (2 * math.sin(turn / 2)), delta=0.05)

    def test_missing_plane(self):
        v, t = box((-1, 0, 0), (1, 1, 1))
        path = write("noedge", [{"name": "w", "prims": [prim(v, t)]}])
        res = fs.edge_probe(fm.ModelData(path), {"nodes": ["w"], "plane": {"axis": "x", "at": 9.0}, "dir": "+y"})
        self.assertFalse(res["found"])

    def test_crease_fraction(self):
        v, t = box((0, 0, 0), (1, 1, 1))
        path = write("cube", [{"name": "c", "prims": [prim(v, t)]}])
        frac, hl, tl = fs.hard_crease_fraction(fm.ModelData(path), cfg_with())
        self.assertAlmostEqual(frac, 12 / (12 + 6 * math.sqrt(2)), delta=1e-6)
        # a smooth sphere has none
        n = 40
        pts, tris = [], []
        for i in range(n + 1):
            for j in range(n):
                th, ph = math.pi * i / n, 2 * math.pi * j / n
                pts.append((math.sin(th) * math.cos(ph), math.sin(th) * math.sin(ph), math.cos(th)))
        for i in range(n):
            for j in range(n):
                a, b = i * n + j, i * n + (j + 1) % n
                c, d = (i + 1) * n + j, (i + 1) * n + (j + 1) % n
                tris += [(a, c, b), (b, c, d)]
        path = write("sphere", [{"name": "s", "prims": [prim(np.array(pts), np.array(tris))]}])
        frac, _hl, _tl = fs.hard_crease_fraction(fm.ModelData(path), cfg_with())
        self.assertLess(frac, 0.001)
        NOTE["F5 crease share: cube (exact 0.586), sphere"] = (round(12 / (12 + 6 * math.sqrt(2)), 4), frac)


class TestF4(unittest.TestCase):
    def textured_model(self, name, quads, uvs, mats=None, images=None, node_names=None, colors=False):
        nodes = []
        for k, ((v, t), uv) in enumerate(zip(quads, uvs)):
            nm = (node_names or [f"n{i}" for i in range(len(quads))])[k]
            nodes.append({"name": nm, "prims": [prim(v, t, uvs=uv, material="paint",
                                                     colors=np.ones((len(v), 3)) if colors else None)]})
        return write(name, nodes, mats, images)

    def run_f4(self, path, cfg=None, family=None):
        data = fm.ModelData(path)
        return ft.run(data, cfg or cfg_with(), family)

    def uv_quad(self, u0, v0, u1, v1):
        return np.array([[u0, v0], [u1, v0], [u1, v1], [u0, v1]], float)

    def test_texel_density_known(self):
        # 1 m x 1 m quad mapped to uv 0..0.5 on a 2048 sheet: 1024 px/m
        q = quad((0, 0, 0), (1, 0, 0), (0, 0, 1))
        path = self.textured_model("dens", [q], [self.uv_quad(0, 0, 0.5, 0.5)])
        gates, det = self.run_f4(path)
        g = gate_of(gates, "F4.density.airframe")
        self.assertAlmostEqual(g["value"], 1024.0, delta=1.0)
        self.assertEqual(g["status"], "pass")
        # 2 m x 1 m mapped to the whole sheet: sqrt(2048^2 / 2) = 1448.2 px/m
        q2 = quad((0, 0, 0), (2, 0, 0), (0, 0, 1))
        path = self.textured_model("dens2", [q2], [self.uv_quad(0, 0, 1, 1)])
        gates, det = self.run_f4(path)
        self.assertAlmostEqual(gate_of(gates, "F4.density.airframe")["value"], 1448.2, delta=1.0)
        # a low density fails: 1 m over 0.1 of the sheet = 204.8 px/m < 400
        path = self.textured_model("dens3", [q], [self.uv_quad(0, 0, 0.1, 0.1)])
        gates, _ = self.run_f4(path)
        self.assertEqual(gate_of(gates, "F4.density.airframe")["status"], "fail")
        NOTE["F4 texel density: 1 m on 0.5 sheet, 2x1 m on 1.0 sheet"] = (1024.0, 1448.2)

    def test_density_spread(self):
        q1 = quad((0, 0, 0), (1, 0, 0), (0, 0, 1))
        q2 = quad((3, 0, 0), (1, 0, 0), (0, 0, 1))
        path = self.textured_model("spread", [q1, q2],
                                   [self.uv_quad(0, 0, 0.2, 0.2), self.uv_quad(0.5, 0, 0.9, 0.4)],
                                   node_names=["fuselage", "wing_l"])
        cfg = cfg_with()
        gates, det = self.run_f4(path, cfg)
        spreadg = [g for g in gates if g["id"].startswith("F4.spread")]
        self.assertEqual(len(spreadg), 1)
        self.assertAlmostEqual(spreadg[0]["value"], 2.0, delta=0.01)   # 0.4 / 0.2 = 2

    def test_overlap_known_and_mirror_exception(self):
        qa = quad((0, 0, 0), (1, 0, 0), (0, 0, 1))
        qb = quad((3, 0, 0), (1, 0, 0), (0, 0, 1))
        # B's UV square shifted by half its width over A's: overlap = 0.25 of a 0.5-sheet square
        path = self.textured_model("ovl", [qa, qb], [self.uv_quad(0.0, 0.0, 0.5, 0.5),
                                                     self.uv_quad(0.25, 0.0, 0.75, 0.5)])
        gates, det = self.run_f4(path)
        g = [x for x in gates if x["id"].startswith("F4.overlap")][0]
        expect = 0.25 * 0.5 * 2048 * 2048
        self.assertAlmostEqual(g["value"], expect, delta=0.01 * expect)
        self.assertEqual(g["status"], "fail")
        # left / right mirror images with identical UV share texture space: no overlap
        ql = quad((1, 0, 0), (1, 0, 0), (0, 0, 1))
        qr = quad((-1, 0, 0), (-1, 0, 0), (0, 0, 1))
        path = self.textured_model("mirror", [ql, qr],
                                   [self.uv_quad(0, 0, 0.5, 0.5), self.uv_quad(0, 0, 0.5, 0.5)])
        data = fm.ModelData(path)
        sheet = ft.build_sheets(data, cfg_with()["F4_textures"], data.prims)[0]
        keep = ft.mirror_pair_keep(sheet)
        self.assertEqual(sum(int((~k).sum()) for k in keep), 2)
        # same UVs on two quads that are NOT mirror images: overlap stays
        path = self.textured_model("notmirror", [ql, quad((5, 0, 0), (1, 0, 0), (0, 0, 1))],
                                   [self.uv_quad(0, 0, 0.5, 0.5), self.uv_quad(0, 0, 0.5, 0.5)])
        gates, _ = self.run_f4(path)
        self.assertEqual([x for x in gates if x["id"].startswith("F4.overlap")][0]["status"], "fail")

    def test_padding_known(self):
        q1 = quad((0, 0, 0), (1, 0, 0), (0, 0, 1))
        q2 = quad((3, 0, 0), (1, 0, 0), (0, 0, 1))
        size = 2048

        def case(gap_px, name):
            w = 100
            u0 = w / size
            u1 = (w + gap_px) / size
            path = self.textured_model(name, [q1, q2], [self.uv_quad(0, 0, u0, u0),
                                                        self.uv_quad(u1, 0, u1 + u0, u0)])
            gates, det = self.run_f4(path)
            g = [x for x in gates if x["id"].startswith("F4.padding")][0]
            return g
        for gap, ok in ((5, False), (7, False), (8, True), (20, True)):
            g = case(gap, f"pad{gap}")
            self.assertEqual(g["status"] == "pass", ok, f"gap {gap}: {g}")
            if gap < 8:
                self.assertEqual(g["value"], gap)   # empty pixels between the islands
        NOTE["F4 padding: islands 5 / 7 / 8 empty px apart -> measured, pass"] = "5, 7 fail; 8 passes"

    def test_uv_range(self):
        q = quad((0, 0, 0), (1, 0, 0), (0, 0, 1))
        path = self.textured_model("range", [q], [self.uv_quad(0.0, 0.0, 1.3, 0.5)])
        gates, _ = self.run_f4(path)
        self.assertEqual([g for g in gates if g["id"].startswith("F4.uv_range")][0]["status"], "fail")

    def test_no_textures_no_uvs_reports_cleanly(self):
        v, t = box((0, 0, 0), (1, 1, 1))
        path = write("plain", [{"name": "n", "prims": [prim(v, t, colors=np.ones((8, 3)))]}])
        gates, det = self.run_f4(path)
        self.assertEqual(gate_of(gates, "F4.maps")["status"], "fail")
        self.assertEqual(gate_of(gates, "F4.uvs")["status"], "fail")
        for gid in ("F4.overlap", "F4.padding", "F4.spread", "F4.normal_length", "F4.base_colour_range"):
            self.assertEqual(gate_of(gates, gid)["status"], "na", gid)
        self.assertEqual(gate_of(gates, "F4.density.airframe")["status"], "na")
        # the vertex-colour family passes its own check
        gates, _ = self.run_f4(path, family="vertex_colour")
        self.assertEqual(gate_of(gates, "F4.vertex_colour")["status"], "pass")
        self.assertEqual(gate_of(gates, "F4.no_textures")["status"], "pass")

    def textured_quad_file(self, name, base, normal, orm, uv=None, colors=False):
        q = quad((0, 0, 0), (1, 0, 0), (0, 0, 1))
        imgs = [png_bytes(base), png_bytes(normal), png_bytes(orm)]
        mats = [{"name": "paint", "base_image": 0, "normal_image": 1, "orm_image": 2}]
        return self.textured_model(name, [q], [uv if uv is not None else self.uv_quad(0.05, 0.05, 0.95, 0.95)],
                                   mats, imgs, colors=colors)

    def test_image_content_gates(self):
        n = 64
        base = np.full((n, n, 3), 128, np.uint8)
        normal = np.zeros((n, n, 3), np.uint8)
        normal[..., 0] = 128
        normal[..., 1] = 128
        normal[..., 2] = 255
        orm = np.zeros((n, n, 3), np.uint8)
        orm[..., 0] = 255
        orm[..., 1] = 128
        orm[:, ::2, 2] = 255        # metallic half 0 half 1: binary
        path = self.textured_quad_file("good", base, normal, orm)
        cfg = cfg_with()
        cfg["F4_textures"]["padding_px_min"] = 2
        cfg["F4_textures"]["bleed_px"] = 2
        gates, det = self.run_f4(path, cfg)
        self.assertEqual(gate_of(gates, "F4.maps")["status"], "pass")
        self.assertEqual(gate_of(gates, "F4.sizes")["status"], "pass")
        self.assertEqual([g for g in gates if g["id"].startswith("F4.normal_length")][0]["status"], "pass")
        self.assertEqual([g for g in gates if g["id"].startswith("F4.metallic_binary")][0]["status"], "pass")
        self.assertEqual([g for g in gates if g["id"].startswith("F4.base_colour_range")][0]["status"], "pass")
        # texture UV density is judged on the real 64 px sheet: 0.9 * 64 = 57.6 px per metre
        self.assertAlmostEqual(gate_of(gates, "F4.density.airframe")["value"], 57.6, delta=0.2)
        # bad maps
        bad_base = np.full((n, n, 3), 5, np.uint8)
        bad_normal = np.full((n, n, 3), 255, np.uint8)       # length sqrt(3)
        bad_orm = orm.copy()
        bad_orm[..., 2] = (np.arange(n)[None, :] * 255 // n).astype(np.uint8)   # gradient
        path = self.textured_quad_file("bad", bad_base, bad_normal, bad_orm)
        gates, _ = self.run_f4(path, cfg)
        for pre in ("F4.normal_length", "F4.metallic_binary", "F4.base_colour_range"):
            self.assertEqual([g for g in gates if g["id"].startswith(pre)][0]["status"], "fail", pre)

    def test_double_tint_and_sizes(self):
        n = 48     # not a power of two
        base = np.full((n, n, 3), 128, np.uint8)
        normal = np.full((n, n, 3), 128, np.uint8)
        normal[..., 2] = 255
        path = self.textured_quad_file("tint", base, normal, base, colors=True)
        gates, _ = self.run_f4(path)
        self.assertEqual(gate_of(gates, "F4.colour_double_tint")["status"], "fail")
        self.assertEqual(gate_of(gates, "F4.sizes")["status"], "fail")

    def test_file_size_gates(self):
        v, t = box((0, 0, 0), (1, 1, 1))
        path = write("size", [{"name": "n", "prims": [prim(v, t)]}])
        cfg = cfg_with()
        cfg["F4_textures"]["glb_bytes_max"] = 100
        gates, _ = self.run_f4(path, cfg)
        self.assertEqual(gate_of(gates, "F4.glb_bytes")["status"], "fail")
        gates, _ = self.run_f4(path, cfg, family="vertex_colour")
        self.assertEqual(gate_of(gates, "F4.glb_bytes")["status"], "pass")

    def test_bleed(self):
        n = 64
        # island = the central 90 % (uv .05 .. .95): colour gradient inside, bleed outside
        base = np.zeros((n, n, 3), np.uint8)
        base[..., 0] = np.arange(n)[None, :] * 3
        base[..., 1] = np.arange(n)[:, None] * 3
        base[..., 2] = 100
        normal = np.full((n, n, 3), 128, np.uint8)
        normal[..., 2] = 255
        cfg = cfg_with()
        cfg["F4_textures"]["padding_px_min"] = 1
        cfg["F4_textures"]["bleed_px"] = 2
        # grow the island's colours outwards by 2 px: the image a baker's margin makes
        q = self.uv_quad(0.25, 0.25, 0.75, 0.75)
        qd = quad((0, 0, 0), (1, 0, 0), (0, 0, 1))
        cov = ru.rasterise_triangles(np.concatenate([q[[0, 1, 2]][None], q[[0, 2, 3]][None]]) * n, n, n) > 0
        bled, _ = ft.propagate_bleed(base.astype(float) / 255, cov, 2)
        good = (bled * 255 + 0.5).astype(np.uint8)
        # outside the covered region ring: break it
        for name, img, expect in (("bleed_good", good, "pass"), ("bleed_bad", np.where(cov[..., None], base, 0).astype(np.uint8), "fail")):
            imgs = [png_bytes(img), png_bytes(normal), png_bytes(normal)]
            path = self.textured_model(name, [qd], [q], [{"name": "paint", "base_image": 0, "normal_image": 1,
                                                          "orm_image": 2}], imgs)
            gates, _ = self.run_f4(path, cfg)
            g = [x for x in gates if x["id"].startswith("F4.bleed.") and x["id"].endswith("baseColor")][0]
            self.assertEqual(g["status"], expect, f"{name}: {g}")


class TestF2(unittest.TestCase):
    def feature(self, **kw):
        base = {"name": "f", "kind": "geometry", "required": True, "count_min": 4}
        base.update(kw)
        return base

    def test_geometry_relief_known(self):
        # a 1 m plate with a 5 mm tall ridge in the middle
        pv, pt = box((-0.5, 0.0, -0.001), (0.5, 1.0, 0.0))
        rv, rt = box((-0.4, 0.4, 0.0), (0.4, 0.45, 0.005))
        path = write("ridge", [{"name": "plate", "prims": [prim(pv, pt)]},
                               {"name": "ridge", "prims": [prim(rv, rt, material="dark")]}],
                     [{"name": "paint"}, {"name": "dark"}])
        data = fm.ModelData(path)
        ok, info = fidelity_features.geometry_feature(data, self.feature(
            box=[-0.45, 0.35, -0.01, 0.45, 0.5, 0.01], material="dark", count_min=10, relief_mm=4))
        self.assertTrue(ok, info)
        self.assertAlmostEqual(info["relief_mm"], 5.0, delta=0.6)
        ok, info = fidelity_features.geometry_feature(data, self.feature(
            box=[-0.45, 0.35, -0.01, 0.45, 0.5, 0.01], material="dark", count_min=10, relief_mm=8))
        self.assertFalse(ok)
        ok, info = fidelity_features.geometry_feature(data, self.feature(
            box=[-0.45, 0.0, -0.01, 0.45, 0.3, 0.01], material="paint", count_min=1, relief_mm=2))
        self.assertFalse(ok)        # the bare plate region: relief ~ 0
        ok, info = fidelity_features.geometry_feature(data, self.feature())
        self.assertIsNone(ok)       # no box defined

    def test_texture_marks_counted(self):
        n = 128
        normal = np.zeros((n, n, 3), np.uint8)
        normal[..., 0] = 128
        normal[..., 1] = 128
        normal[..., 2] = 255
        for x0 in (20, 50, 80):       # three vertical grooves 3 px wide
            normal[10:100, x0:x0 + 3, 0] = 40
            normal[10:100, x0:x0 + 3, 2] = 230
        q = quad((0, 0, 0), (1, 0, 0), (0, 0, 1))
        uv = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], float)
        path = write("grooves", [{"name": "p", "prims": [prim(*q, uvs=uv)]}],
                     [{"name": "paint", "base_image": 0, "normal_image": 1, "orm_image": 0}],
                     [png_bytes(np.full((n, n, 3), 128)), png_bytes(normal)])
        data = fm.ModelData(path)
        cfg = cfg_with()
        feat = self.feature(kind="texture", box=[-0.1, -0.1, -0.1, 1.1, 0.1, 1.1], count_min=3)
        ok, info = fidelity_features.texture_feature(data, feat, cfg)
        self.assertTrue(ok, info)
        self.assertEqual(info["marks"], 3)
        ok, info = fidelity_features.texture_feature(data, dict(feat, count_min=4), cfg)
        self.assertFalse(ok)

    def test_gate_fractions(self):
        v, t = box((0, 0, 0), (1, 1, 1))
        path = write("frac", [{"name": "n", "prims": [prim(v, t)]}])
        cfg = cfg_with()
        cfg["F2_feature"] = [self.feature(name="a", box=[-1, -1, -1, 2, 2, 2], count_min=1),
                             self.feature(name="b"),
                             self.feature(name="c", required=False, box=[-1, -1, -1, 2, 2, 2], count_min=1)]
        gates, det = fidelity_features.run(fm.ModelData(path), cfg)
        req = gate_of(gates, "F2.required")
        self.assertEqual(req["status"], "fail")
        self.assertAlmostEqual(req["value"], 0.5)
        self.assertEqual(gate_of(gates, "F2.optional")["status"], "pass")


class TestDriver(unittest.TestCase):
    def test_config_validation(self):
        bad = TMP / "bad.toml"
        bad.write_text("[model]\n[gates]\n[bogus]\n")
        with self.assertRaises(fm.ConfigError):
            fa.load_config(bad)

    def test_real_reference_files_match_their_hash(self):
        import tomllib
        ref = tomllib.loads((REPO / "tools/models/reference/cessna_172_reference.toml").read_text())
        for view, info in ref["views"].items():
            data = (REPO / "tools/models/reference" / info["file"]).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), info["sha256"], view)
        svg = (REPO / "tools/models/reference/cessna_172_skyhawk_line_drawing.svg").read_bytes()
        self.assertEqual(hashlib.sha256(svg).hexdigest(), ref["source"]["svg_sha256"])

    def test_cli_exit_codes_and_determinism(self):
        cfg = REPO / "tools/models/audit/cessna_fidelity.toml"
        model = REPO / "databases/sample/models/cessna.glb"
        outs = []
        for i in range(2):
            out = TMP / f"run{i}"
            code = fa.main(["--model", str(model), "--config", str(cfg), "--out", str(out), "--name", "t"])
            self.assertEqual(code, 1)        # the T22d model fails hard gates: that is the point
            outs.append(out)
        for ext in ("json", "md"):
            a = (outs[0] / f"t_fidelity.{ext}").read_bytes()
            b = (outs[1] / f"t_fidelity.{ext}").read_bytes()
            self.assertEqual(a, b)
        # a separate process gives the same bytes
        out = TMP / "run_sub"
        subprocess.run([sys.executable, str(HERE / "fidelity_audit.py"), "--model", str(model),
                        "--config", str(cfg), "--out", str(out), "--name", "t"],
                       check=False, capture_output=True)
        self.assertEqual((out / "t_fidelity.json").read_bytes(), (outs[0] / "t_fidelity.json").read_bytes())
        res = json.loads((outs[0] / "t_fidelity.json").read_text())
        self.assertGreater(res["summary"]["hard_failed"], 0)
        NOTE["determinism: report bytes, JSON"] = len((outs[0] / "t_fidelity.json").read_bytes())

    def test_exit_code_two_on_bad_input(self):
        self.assertEqual(fa.main(["--model", str(TMP / "nope.glb"), "--config",
                                  str(REPO / "tools/models/audit/cessna_fidelity.toml"),
                                  "--out", str(TMP / "x")]), 2)

    def test_passing_model_exits_zero(self):
        # a config with only F1 gates against the synthetic reference, on a matching box
        root = TMP / "passref"
        root.mkdir(exist_ok=True)
        make_reference(root, {"side": (400, 100), "top": (400, 400), "front": (400, 100)})
        v, t = box((-2, 0, 0), (2, 4, 1))
        path = write("pass_box", [{"name": "body", "prims": [prim(v, t)]}])
        cfg = cfg_with()
        cfg["gates"]["hard"] = ["F1"]
        cfg["F1_outline"]["reference"] = "ref.toml"
        data, gates, detail, _ = fa.audit(path, cfg, root=root)
        s = fa.summarise([g for g in gates if g["id"].startswith("F1")])
        self.assertEqual(s["hard_failed"], 0)


if __name__ == "__main__":
    import os
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    prog = unittest.main(argv=["test_fidelity"] + argv, exit=False, verbosity=2)
    ok = prog.result.wasSuccessful()
    print(f"[test_fidelity] {prog.result.testsRun} tests, "
          f"{len(prog.result.failures) + len(prog.result.errors)} failed"
          + ("" if ok else f"; files kept in {TMP}"))
    for key in sorted(NOTE):
        print(f"[test_fidelity] {key}: {NOTE[key]}")
    if ok:
        import shutil
        shutil.rmtree(TMP, ignore_errors=True)
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(0 if ok else 1)
