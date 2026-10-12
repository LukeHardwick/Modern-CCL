# Deterministic model audit (task T22c): shape accuracy, shading, needless triangles, hygiene,
# shape plausibility, profile fairness and budgets of a .glb against a denser reference build.
#
#   blender --background --factory-startup --python tools/models/audit_model.py -- \
#       --model M.glb --reference R.glb --config tools/models/audit/jet.toml --out DIR \
#       [--images] [--fast] [--strict]
#
# Writes DIR/<name>_audit.json (byte-identical for the same inputs), DIR/<name>_audit.md
# (PASS/FAIL per gate) and, with --images, heat-map PNGs. Exit status 1 when a hard gate fails
# (any gate with --strict), 2 on bad input. The method is explained in plain language in
# docs/research/04-model-audit.md; scripts/audit_models.sh builds the reference and runs this.
#
# Everything is measured on the file's own data (glb_io reads it with struct + numpy) in the
# modelling frame of tools/models: metres, x = vehicle left, y = aft (nose at -y), z = up.

import argparse
import hashlib
import json
import math
import sys
import time
import tomllib
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import glb_io  # noqa: E402

TOOL_VERSION = 1
EPS = 1e-12

# ---------------------------------------------------------------- configuration

DEFAULTS = {
    "model": {"name": None, "published_size": None, "eyepoints": [],
              "glass_materials": ["glass", "hud"], "default_group": None},
    "sampling": {"spacing": 0.05, "inset": 0.01, "weld": 1e-6, "kmax": 64,
                 "vis_spacing": 0.25, "vis_kmax": 4, "vis_inset": 0.001},
    "view": {"width": 1920, "height": 1080, "vfov_deg": 60.0},
    "visibility": {"directions": 162, "ray_offset": 1e-4, "clusters": 10},
    "headroom": {"ratios": [0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1, 0.05, 0.02, 0.01],
                 "min_component": 2000},
    "hygiene": {"degenerate_area": 1e-10, "sliver_deg": 2.0, "island_margin": 0.05},
    "reference": {"min_density": 4.0},
    "fast": {"spacing": 0.10, "directions": 42, "reverse_stride": 4,
             "ratios": [0.8, 0.6, 0.4, 0.2, 0.1, 0.05]},
    "gates": {"headroom_max": 0.15, "hidden_max": 0.02, "subpixel_max": 0.10,
              "verts_per_tri_max": 0.65, "index_max": 65535, "degenerate_max": 0,
              "duplicate_max": 0, "flipped_max": 0, "floating_max": 0, "winding_max": 0,
              "sliver_max": 0.01, "size_tolerance": 0.02,
              "hard": ["index_max", "degenerate_max", "duplicate_max", "flipped_max",
                       "floating_max", "budget_total"]},
    "budget": {},
    "groups": [],
    "poses": [{"name": "rest"}],
    "probes": [],
}

GROUP_DEFAULTS = {"distance": 8.0, "shape_p99": 0.001, "shape_max": 0.004,
                  "normal_p95_deg": 2.0, "normal_max_deg": 10.0}

PROBE_DEFAULTS = {"direction": [0.0, 0.0, -1.0], "step": 0.005, "materials": None,
                  "min_feature": 0.10, "prominence": 0.002, "segment": 0.05, "pose": "rest",
                  "max_maxima": None, "max_minima": None, "kink_max_deg": None,
                  "step_max": None}


def load_config(path):
    cfg = {k: (dict(v) if isinstance(v, dict) else list(v) if isinstance(v, list) else v)
           for k, v in DEFAULTS.items()}
    if path is not None:
        with open(path, "rb") as fh:
            user = tomllib.load(fh)
        for k, v in user.items():
            if isinstance(v, dict) and isinstance(cfg.get(k), dict):
                cfg[k] = {**cfg[k], **v}
            else:
                cfg[k] = v
    cfg["groups"] = [{**GROUP_DEFAULTS, **g} for g in cfg["groups"]]
    cfg["probes"] = [{**PROBE_DEFAULTS, **p} for p in cfg["probes"]]
    if not cfg["poses"] or cfg["poses"][0].get("name") != "rest" or cfg["poses"][0].get(
            "rotate") or cfg["poses"][0].get("hide"):
        cfg["poses"] = [{"name": "rest"}] + [p for p in cfg["poses"] if p.get("name") != "rest"]
    return cfg


# ---------------------------------------------------------------- small helpers


def rm(x):
    """Round a length (m) for the report: 1e-6 m."""
    return None if x is None else round(float(x), 6)


def rd(x):
    """Round an angle (degrees): 0.01 deg."""
    return None if x is None else round(float(x), 2)


def rf(x):
    """Round a fraction (0..1): 0.01 % = 1e-4."""
    return None if x is None else round(float(x), 4)


def ra(x):
    """Round an area (m^2): 1 mm^2."""
    return None if x is None else round(float(x), 6)


def vec(v, k=rm):
    return [k(c) for c in v]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def dot(a, b):
    return (a * b).sum(axis=-1)


def norm(a):
    return np.sqrt((a * a).sum(axis=-1))


def unit(a):
    n = norm(a)
    return a / np.maximum(n, EPS)[..., None]


def cross(a, b):
    return np.stack([a[..., 1] * b[..., 2] - a[..., 2] * b[..., 1],
                     a[..., 2] * b[..., 0] - a[..., 0] * b[..., 2],
                     a[..., 0] * b[..., 1] - a[..., 1] * b[..., 0]], axis=-1)


def angle_deg(a, b):
    """Angle between unit-ish vectors, robust for small angles (atan2 of |a x b| and a.b)."""
    return np.degrees(np.arctan2(norm(cross(a, b)), dot(a, b)))


def mat3_apply(m, p):
    """p (N,3) @ m.T without BLAS (keeps results bit-stable)."""
    return np.stack([p[:, 0] * m[i, 0] + p[:, 1] * m[i, 1] + p[:, 2] * m[i, 2]
                     for i in range(3)], axis=-1)


def wstats(values, weights):
    """Area-weighted max, p99, p95 and mean of per-triangle values."""
    v = np.asarray(values, dtype=np.float64)
    if len(v) == 0:
        return {"count": 0, "max": None, "p99": None, "p95": None, "mean": None}
    w = np.asarray(weights, dtype=np.float64)
    if w.sum() <= 0:
        w = np.ones_like(v)
    order = np.argsort(v, kind="stable")
    vs, ws = v[order], w[order]
    c = np.cumsum(ws) / ws.sum()

    def q(f):
        return float(vs[min(int(np.searchsorted(c, f, side="left")), len(vs) - 1)])
    return {"count": int(len(v)), "max": float(vs[-1]), "p99": q(0.99), "p95": q(0.95),
            "mean": float((v * w).sum() / w.sum())}


def round_stats(s, k):
    return {key: (k(val) if key != "count" else val) for key, val in s.items()}


def rodrigues(axis, deg):
    a = np.asarray(axis, dtype=np.float64)
    a = a / np.linalg.norm(a)
    t = math.radians(deg)
    k = np.array([[0, -a[2], a[1]], [a[2], 0, -a[0]], [-a[1], a[0], 0]])
    return np.eye(3) + math.sin(t) * k + (1 - math.cos(t)) * (k @ k)


# ---------------------------------------------------------------- geometry in one pose


class Geo:
    """All triangles of a model in one pose, in the modelling frame."""

    def __init__(self, model, pose=None, glass=()):
        pose = pose or {}
        rotate = pose.get("rotate", {}) or {}
        hide = set(pose.get("hide", []) or [])
        C = glb_io.GLTF_TO_MODEL

        def override(node):
            if node.name not in rotate:
                return None
            axis_index, deg = rotate[node.name]
            axis_m = np.eye(3)[int(axis_index)]
            r = np.eye(4)
            r[:3, :3] = rodrigues(C.T @ axis_m, float(deg))
            return node.local @ r
        world = model.world_matrices(override)
        self.world = world
        self.node_names = [model.nodes[i].name for i in model.scene_nodes]
        node_id = {n: i for i, n in enumerate(self.node_names)}
        self.mat_names = sorted({p.material for p in model.prims})
        mat_id = {m: i for i, m in enumerate(self.mat_names)}
        self.empties = {}
        for i in model.scene_nodes:
            node = model.nodes[i]
            if node.mesh is None:
                self.empties[node.name] = C @ world[node.name][:3, 3]
        tv, tn, tnode, tmat, tprim = [], [], [], [], []
        self.prim_info = []
        for pi, p in enumerate(model.prims):
            if p.node in hide:
                continue
            m = world[p.node]
            lin = C @ m[:3, :3]
            pos = mat3_apply(lin, p.positions) + C @ m[:3, 3]
            tri = p.indices
            v = pos[tri]
            if p.normals is not None:
                nlin = C @ np.linalg.inv(m[:3, :3]).T
                nrm = unit(mat3_apply(nlin, p.normals))
                n = nrm[tri]
            else:
                g = unit(cross(v[:, 1] - v[:, 0], v[:, 2] - v[:, 0]))
                n = np.repeat(g[:, None, :], 3, axis=1)
            tv.append(v)
            tn.append(n)
            tnode.append(np.full(len(tri), node_id[p.node]))
            tmat.append(np.full(len(tri), mat_id[p.material]))
            tprim.append(np.full(len(tri), pi))
        if tv:
            self.v = np.concatenate(tv)
            self.vn = np.concatenate(tn)
            self.node = np.concatenate(tnode).astype(np.int64)
            self.mat = np.concatenate(tmat).astype(np.int64)
            self.prim = np.concatenate(tprim).astype(np.int64)
        else:
            self.v = np.zeros((0, 3, 3))
            self.vn = np.zeros((0, 3, 3))
            self.node = self.mat = self.prim = np.zeros(0, dtype=np.int64)
        e0 = self.v[:, 1] - self.v[:, 0]
        e1 = self.v[:, 2] - self.v[:, 1]
        e2 = self.v[:, 0] - self.v[:, 2]
        c = cross(e0, -e2)
        self.area = 0.5 * norm(c)
        self.normal = unit(c)
        self.lengths = np.stack([norm(e0), norm(e1), norm(e2)], axis=1)
        self.lmax = self.lengths.max(axis=1) if len(self.v) else np.zeros(0)
        glass_ids = [mat_id[g] for g in glass if g in mat_id]
        self.glass = np.isin(self.mat, glass_ids)

    def node_index(self, name):
        return self.node_names.index(name) if name in self.node_names else None

    def mask(self, nodes=None, materials=None):
        m = np.ones(len(self.v), dtype=bool)
        if nodes is not None:
            m &= np.isin(self.node, [self.node_index(n) for n in nodes
                                     if self.node_index(n) is not None])
        if materials is not None:
            m &= np.isin(self.mat, [self.mat_names.index(x) for x in materials
                                    if x in self.mat_names])
        return m


def min_angle_deg(geo):
    v = geo.v
    out = np.full(len(v), 180.0)
    for i in range(3):
        a = v[:, (i + 1) % 3] - v[:, i]
        b = v[:, (i + 2) % 3] - v[:, i]
        out = np.minimum(out, angle_deg(unit(a), unit(b)))
    return out


def weld(points, tol):
    """Vertex ids after merging points closer than about `tol` (grid quantisation)."""
    q = np.round(points / tol).astype(np.int64)
    _u, inv = np.unique(q, axis=0, return_inverse=True)
    return inv.reshape(-1), len(_u)


def tri_components(tris, nverts):
    """Connected components of triangles that share a vertex. Labels 0..C-1 in order of the
    first triangle of each component."""
    if len(tris) == 0:
        return np.zeros(0, dtype=np.int64)
    parent = np.arange(nverts)
    while True:
        r = parent[tris]
        m = r.min(axis=1)
        for col in range(3):
            np.minimum.at(parent, r[:, col], m)
        while True:
            pp = parent[parent]
            if np.array_equal(pp, parent):
                break
            parent = pp
        r = parent[tris]
        if (r == r[:, :1]).all():
            break
    roots = parent[tris[:, 0]]
    _u, first, inv = np.unique(roots, return_index=True, return_inverse=True)
    order = np.argsort(first, kind="stable")
    rank = np.empty_like(order)
    rank[order] = np.arange(len(order))
    return rank[inv.reshape(-1)]


class Topology:
    """Welded topology of one node: vertex ids, components."""

    def __init__(self, geo, tri_idx, tol):
        self.tri_idx = tri_idx
        pts = geo.v[tri_idx].reshape(-1, 3)
        ids, n = weld(pts, tol)
        self.tris = ids.reshape(-1, 3)
        self.nverts = n
        self.comp = tri_components(self.tris, n)
        self.ncomp = int(self.comp.max()) + 1 if len(self.comp) else 0

    def soft_folds(self, geo, lo=0.5, hi=45.0):
        """Triangles with an edge neighbour whose face normal differs by lo..hi degrees:
        part of a faceted curved surface (a flat-shaded strip or patch following a curve)."""
        t = self.tris
        T = len(t)
        a = np.concatenate([t[:, 0], t[:, 1], t[:, 2]])
        b = np.concatenate([t[:, 1], t[:, 2], t[:, 0]])
        owner = np.tile(np.arange(T), 3)
        key0, key1 = np.minimum(a, b), np.maximum(a, b)
        order = np.lexsort((owner, key1, key0))
        k0, k1, ow = key0[order], key1[order], owner[order]
        same = np.nonzero((k0[1:] == k0[:-1]) & (k1[1:] == k1[:-1]) & (k0[1:] != k1[1:]))[0]
        flag = np.zeros(T, dtype=bool)
        if len(same):
            t1, t2 = ow[same], ow[same + 1]
            n = geo.normal[self.tri_idx]
            ang = angle_deg(n[t1], n[t2])
            soft = (ang > lo) & (ang < hi)
            flag[t1[soft]] = True
            flag[t2[soft]] = True
        return flag


# ---------------------------------------------------------------- sampling


def bary_pattern(k):
    """Barycentric sample pattern for subdivision k: the regular grid (spacing 1/k), the
    centroid and the edge midpoints, centroid first, then sorted. Exact integer arithmetic."""
    den = 6 * k
    pts = {(2 * k, 2 * k, 2 * k), (3 * k, 3 * k, 0), (3 * k, 0, 3 * k), (0, 3 * k, 3 * k)}
    for i in range(k + 1):
        for j in range(k + 1 - i):
            pts.add((6 * i, 6 * j, 6 * (k - i - j)))
    centre = (2 * k, 2 * k, 2 * k)
    ordered = [centre] + sorted(p for p in pts if p != centre)
    return np.array(ordered, dtype=np.float64) / den


def make_samples(geo, tri_idx, spacing, inset, kmax, inset_abs=0.0):
    """Samples on the triangles `tri_idx` (sorted by triangle). Every sample is pulled towards
    the centroid by the fraction `inset`, or further when needed to keep it at least
    `inset_abs` metres inside every edge (capped at halfway). Returns (owner position in
    tri_idx (S,), barycentrics (S,3), points (S,3), start offsets (len(tri_idx)+1))."""
    if len(tri_idx) == 0:
        return (np.zeros(0, dtype=np.int64), np.zeros((0, 3)), np.zeros((0, 3)),
                np.zeros(1, dtype=np.int64))
    k = np.clip(np.ceil(geo.lmax[tri_idx] / spacing), 1, kmax).astype(np.int64)
    owners, barys = [], []
    for kk in np.unique(k):
        sel = np.nonzero(k == kk)[0]
        pat = bary_pattern(int(kk))
        owners.append(np.repeat(sel, len(pat)))
        barys.append(np.tile(pat, (len(sel), 1)))
    owner = np.concatenate(owners)
    bary = np.concatenate(barys)
    order = np.argsort(owner, kind="stable")
    owner, bary = owner[order], bary[order]
    # a point on an edge moves f * h / 3 away from it (h = that edge's altitude)
    hmin = 2.0 * geo.area[tri_idx] / np.maximum(geo.lmax[tri_idx], EPS)
    f = np.clip(np.maximum(inset, 3.0 * inset_abs / np.maximum(hmin, EPS)), inset, 0.5)
    fo = f[owner][:, None]
    bary = (1.0 - fo) * bary + fo / 3.0
    v = geo.v[tri_idx[owner]]
    pts = bary[:, 0:1] * v[:, 0] + bary[:, 1:2] * v[:, 1] + bary[:, 2:3] * v[:, 2]
    counts = np.bincount(owner, minlength=len(tri_idx))
    starts = np.concatenate([[0], np.cumsum(counts)])
    return owner, bary, pts, starts


def interp_normal(vn, bary):
    n = bary[:, 0:1] * vn[:, 0] + bary[:, 1:2] * vn[:, 1] + bary[:, 2:3] * vn[:, 2]
    return unit(n)


def closest_bary(p, a, b, c):
    """Barycentrics of the closest point on triangles (a, b, c) to points p (vectorised;
    Ericson, Real-Time Collision Detection, 5.1.5)."""
    n = len(p)
    out = np.zeros((n, 3))
    done = np.zeros(n, dtype=bool)
    ab, ac, ap = b - a, c - a, p - a
    d1, d2 = dot(ab, ap), dot(ac, ap)
    bp = p - b
    d3, d4 = dot(ab, bp), dot(ac, bp)
    cp = p - c
    d5, d6 = dot(ab, cp), dot(ac, cp)
    vc = d1 * d4 - d3 * d2
    vb = d5 * d2 - d1 * d6
    va = d3 * d6 - d5 * d4
    with np.errstate(divide="ignore", invalid="ignore"):
        m = (d1 <= 0) & (d2 <= 0)
        out[m] = (1, 0, 0)
        done |= m
        m = ~done & (d3 >= 0) & (d4 <= d3)
        out[m] = (0, 1, 0)
        done |= m
        m = ~done & (vc <= 0) & (d1 >= 0) & (d3 <= 0)
        t = d1 / (d1 - d3)
        out[m] = np.stack([1 - t, t, np.zeros(n)], axis=1)[m]
        done |= m
        m = ~done & (d6 >= 0) & (d5 <= d6)
        out[m] = (0, 0, 1)
        done |= m
        m = ~done & (vb <= 0) & (d2 >= 0) & (d6 <= 0)
        t = d2 / (d2 - d6)
        out[m] = np.stack([1 - t, np.zeros(n), t], axis=1)[m]
        done |= m
        m = ~done & (va <= 0) & ((d4 - d3) >= 0) & ((d5 - d6) >= 0)
        t = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        out[m] = np.stack([np.zeros(n), 1 - t, t], axis=1)[m]
        done |= m
        m = ~done
        s = va + vb + vc
        v_, w_ = vb / s, vc / s
        out[m] = np.stack([1 - v_ - w_, v_, w_], axis=1)[m]
    bad = ~np.isfinite(out).all(axis=1)
    out[bad] = (1, 0, 0)
    return out


def bvh(v):
    """BVH over triangles v (T,3,3); polygon i = triangle i."""
    from mathutils.bvhtree import BVHTree
    pts = v.reshape(-1, 3)
    faces = np.arange(len(pts)).reshape(-1, 3)
    return BVHTree.FromPolygons(pts.tolist(), faces.tolist(), all_triangles=True)


def nearest(tree, pts):
    """Index of the nearest triangle for each point (-1 when the tree is empty)."""
    fn = tree.find_nearest
    out = [fn(p)[2] for p in pts.tolist()]
    return np.array([-1 if i is None else i for i in out], dtype=np.int64)


# ---------------------------------------------------------------- check A + B per node


class NodeMatch:
    """Model node against the same-named reference node."""

    def __init__(self, geo, ref, name, cfg, fast):
        smp = cfg["sampling"]
        spacing = cfg["fast"]["spacing"] if fast else smp["spacing"]
        self.name = name
        self.tri = np.nonzero((geo.node == geo.node_index(name)) &
                              (geo.area >= cfg["hygiene"]["degenerate_area"]))[0]
        self.rtri = np.nonzero((ref.node == ref.node_index(name)) &
                               (ref.area >= cfg["hygiene"]["degenerate_area"]))[0]
        owner, bary, pts, starts = make_samples(geo, self.tri, spacing, smp["inset"],
                                                smp["kmax"])
        self.samples = len(pts)
        rv = ref.v[self.rtri]
        tree = bvh(rv)
        j = nearest(tree, pts)
        a, b, c = rv[j, 0], rv[j, 1], rv[j, 2]
        rb = closest_bary(pts, a, b, c)
        q = rb[:, 0:1] * a + rb[:, 1:2] * b + rb[:, 2:3] * c
        dist = norm(pts - q)
        mn = interp_normal(geo.vn[self.tri[owner]], bary)
        rn = interp_normal(ref.vn[self.rtri[j]], rb)
        # Correspondence check: where the nearest reference face faces a very different way
        # (next to a crease that the two meshes place slightly differently), look along the
        # model face normal for a reference face that faces the same way, and compare
        # shading there. Distances always stay nearest-point distances.
        mg = geo.normal[self.tri[owner]]
        rg = ref.normal[self.rtri[j]]
        sus = np.nonzero(dot(mg, rg) < 0.5)[0]
        self.reprojected = 0
        if len(sus):
            from mathutils import Vector
            for s in sus.tolist():
                p = Vector(pts[s])
                nvec = Vector(mg[s])
                reach = max(2.0 * dist[s], dist[s] + 0.002)
                best = None
                for sign in (1.0, -1.0):
                    o = p - nvec * (sign * reach)
                    d = nvec * sign
                    travelled = 0.0
                    for _ in range(4):
                        hit = tree.ray_cast(o, d, 2.0 * reach - travelled)
                        if hit[0] is None:
                            break
                        t_hit = travelled + hit[3]
                        k = hit[2]
                        if np.dot(ref.normal[self.rtri[k]], mg[s]) >= 0.5:
                            off = abs(t_hit - reach)
                            if best is None or off < best[0]:
                                best = (off, k, hit[0])
                            break
                        travelled = t_hit + 1e-7
                        o = p - nvec * (sign * reach) + d * travelled
                if best is not None:
                    k = best[1]
                    tri = rv[k]
                    bb = closest_bary(np.array([tuple(best[2])]), tri[None, 0], tri[None, 1],
                                      tri[None, 2])
                    rn[s] = interp_normal(ref.vn[self.rtri[k]][None], bb)[0]
                    self.reprojected += 1
        nerr = angle_deg(mn, rn)
        ntri = len(self.tri)
        if ntri:
            self.err = np.maximum.reduceat(dist, starts[:-1]) if len(dist) else np.zeros(ntri)
            self.nerr = np.maximum.reduceat(nerr, starts[:-1]) if len(nerr) else np.zeros(ntri)
        else:
            self.err = self.nerr = np.zeros(0)
        self.tree = tree
        # reverse: reference centroids against the model node
        stride = cfg["fast"]["reverse_stride"] if fast else 1
        rsel = self.rtri[::stride]
        rc = ref.v[rsel].mean(axis=1)
        mv = geo.v[self.tri]
        mtree = bvh(mv)
        k = nearest(mtree, rc)
        bb = closest_bary(rc, mv[k, 0], mv[k, 1], mv[k, 2])
        q = bb[:, 0:1] * mv[k, 0] + bb[:, 1:2] * mv[k, 1] + bb[:, 2:3] * mv[k, 2]
        self.rerr = norm(rc - q)
        self.rarea = ref.area[rsel]
        self.rnear = self.tri[k]  # nearest model triangle (global index) per reference centroid
        self.rcent = rc
        self.rdensity = len(self.rtri) / max(1, len(self.tri))
        # identical-tessellation diagnostic (completed in Audit with the soft folds): triangles
        # that coincide with the reference and whose vertex normals spread (smooth-shaded
        # curve) cannot have their tessellation error measured by this reference.
        vn = geo.vn[self.tri]
        self.spread = np.maximum.reduce([angle_deg(vn[:, 0], vn[:, 1]),
                                         angle_deg(vn[:, 1], vn[:, 2]),
                                         angle_deg(vn[:, 2], vn[:, 0])]) if ntri else np.zeros(0)
        self.identical = (self.err < 1e-6) & (self.nerr < 0.05)


def decimate_measure(name, verts, faces, ratios, tree_ref, ref_v, rc_pts, thresholds, cfg,
                     spacing):
    """Decimate a welded component at each ratio (largest first) and re-measure shape
    accuracy; stops at the first ratio that fails. Returns the per-ratio records."""
    import bpy
    me = bpy.data.meshes.new(f"audit_{name}")
    me.from_pydata(verts.tolist(), [], faces.tolist())
    me.validate(verbose=False)
    obj = bpy.data.objects.new(f"audit_{name}", me)
    bpy.context.scene.collection.objects.link(obj)
    mod = obj.modifiers.new("decimate", "DECIMATE")
    mod.decimate_type = "COLLAPSE"
    mod.use_collapse_triangulate = True
    records = []
    try:
        for r in ratios:
            mod.ratio = r
            dg = bpy.context.evaluated_depsgraph_get()
            dg.update()
            ev = obj.evaluated_get(dg)
            m2 = ev.to_mesh()
            m2.calc_loop_triangles()
            nv = len(m2.vertices)
            co = np.zeros(nv * 3)
            m2.vertices.foreach_get("co", co)
            co = co.reshape(-1, 3)
            nt = len(m2.loop_triangles)
            tri = np.zeros(nt * 3, dtype=np.int64)
            m2.loop_triangles.foreach_get("vertices", tri)
            ev.to_mesh_clear()
            tv = co[tri.reshape(-1, 3)]
            rec = measure_patch(tv, tree_ref, ref_v, rc_pts, cfg, spacing)
            rec["ratio"] = r
            rec["triangles"] = int(nt)
            rec["pass"] = bool(rec["fwd_p99"] <= thresholds[0] and rec["fwd_max"] <= thresholds[1]
                               and rec["rev_p99"] <= thresholds[0]
                               and rec["rev_max"] <= thresholds[1])
            records.append(rec)
            if not rec["pass"]:
                break
    finally:
        bpy.data.objects.remove(obj)
        bpy.data.meshes.remove(me)
    return records


class Patch:
    """Minimal Geo-like wrapper for a loose triangle soup (used by the headroom check)."""

    def __init__(self, tv):
        self.v = tv
        e0 = tv[:, 1] - tv[:, 0]
        e2 = tv[:, 0] - tv[:, 2]
        self.area = 0.5 * norm(cross(e0, -e2))
        self.lmax = np.stack([norm(e0), norm(tv[:, 2] - tv[:, 1]), norm(e2)], axis=1).max(axis=1)


def measure_patch(tv, tree_ref, ref_v, rc_pts, cfg, spacing):
    smp = cfg["sampling"]
    patch = Patch(tv)
    keep = np.nonzero(patch.area >= cfg["hygiene"]["degenerate_area"])[0]
    owner, _bary, pts, starts = make_samples(patch, keep, spacing, smp["inset"], smp["kmax"])
    j = nearest(tree_ref, pts)
    a, b, c = ref_v[j, 0], ref_v[j, 1], ref_v[j, 2]
    rb = closest_bary(pts, a, b, c)
    q = rb[:, 0:1] * a + rb[:, 1:2] * b + rb[:, 2:3] * c
    d = norm(pts - q)
    err = np.maximum.reduceat(d, starts[:-1]) if len(d) else np.zeros(0)
    fwd = wstats(err, patch.area[keep])
    if len(rc_pts) and len(keep):
        ptree = bvh(tv[keep])
        k = nearest(ptree, rc_pts)
        pv = tv[keep]
        bb = closest_bary(rc_pts, pv[k, 0], pv[k, 1], pv[k, 2])
        q = bb[:, 0:1] * pv[k, 0] + bb[:, 1:2] * pv[k, 1] + bb[:, 2:3] * pv[k, 2]
        rev = wstats(norm(rc_pts - q), np.ones(len(rc_pts)))
    else:
        rev = {"p99": 0.0, "max": 0.0}
    return {"fwd_p99": fwd["p99"] or 0.0, "fwd_max": fwd["max"] or 0.0,
            "rev_p99": rev["p99"] or 0.0, "rev_max": rev["max"] or 0.0}


# ---------------------------------------------------------------- visibility (hidden, flipped)


def hemisphere(n):
    """n fixed directions on the unit hemisphere around +z, equal-area Fibonacci spiral,
    ordered from the pole (along the normal) to the horizon."""
    i = np.arange(n, dtype=np.float64)
    z = 1.0 - (i + 0.5) / n
    r = np.sqrt(np.maximum(0.0, 1.0 - z * z))
    phi = i * math.pi * (3.0 - math.sqrt(5.0))
    return np.stack([r * np.cos(phi), r * np.sin(phi), z], axis=1)


def frames(normals):
    """Deterministic tangent frames (t1, t2, n) per normal."""
    ref = np.where(np.abs(normals[:, 0:1]) < 0.9, [[1.0, 0.0, 0.0]], [[0.0, 1.0, 0.0]])
    t1 = unit(cross(normals, ref))
    t2 = cross(normals, t1)
    return t1, t2


class Visibility:
    """Per-triangle visibility over the configured poses.

    A triangle side is visible when, from any of its samples, a ray in any of the fixed
    hemisphere directions leaves the model's bounding sphere without hitting non-glass
    geometry, or (when they exist) an eyepoint sees it. front/back = that side is visible in
    some pose. The back side is tested when the front is not visible, and for the triangles
    in `want_back` (whose normal disagrees with the reference) in any case."""

    def __init__(self, model, geos, cfg, fast, want_back=None):
        vcfg = cfg["visibility"]
        smp = cfg["sampling"]
        self.ndir = cfg["fast"]["directions"] if fast else vcfg["directions"]
        hemi = hemisphere(self.ndir)
        rest = geos[0]
        T = len(rest.v)
        deg = rest.area < cfg["hygiene"]["degenerate_area"]
        want_back = np.zeros(T, dtype=bool) if want_back is None else want_back
        self.front = np.zeros(T, dtype=bool)
        self.back = np.zeros(T, dtype=bool)
        self.tested = ~deg
        self.rays = 0
        self.pose_retests = {}
        eyes = [e for e in cfg["model"]["eyepoints"]]
        # samples (rest pose barycentrics; posed points are recomputed per pose)
        idx = np.nonzero(~deg)[0]
        owner, bary, _pts, starts = make_samples(rest, idx, smp["vis_spacing"], smp["inset"],
                                                 smp["vis_kmax"], smp["vis_inset"])
        self.samples = len(owner)
        hits = {}  # triangle -> set of node ids that blocked a ray (non-visible triangles)
        for pi, (pose, geo) in enumerate(zip(cfg["poses"], geos)):
            moved = set()
            if pi > 0:
                moved_names = set((pose.get("rotate") or {}).keys()) | set(pose.get("hide") or [])
                # children move with their parents
                changed = True
                while changed:
                    changed = False
                    for n in model.nodes:
                        if n.parent is not None and model.nodes[n.parent].name in moved_names \
                                and n.name not in moved_names:
                            moved_names.add(n.name)
                            changed = True
                moved = {rest.node_index(n) for n in moved_names if rest.node_index(n) is not None}
            # geo for this pose has hidden nodes removed: map rest triangle -> pose triangle
            pose_map = self._pose_map(rest, geo, pose)
            occ = np.nonzero(~geo.glass)[0]
            tree = bvh(geo.v[occ]) if len(occ) else None
            occ_node = geo.node[occ]
            lo, hi = geo.v.reshape(-1, 3).min(axis=0), geo.v.reshape(-1, 3).max(axis=0)
            centre = (lo + hi) / 2
            radius = float(norm((hi - lo)[None])[0]) / 2 + 1.0
            reach = 2.0 * radius + 1.0
            eye_pos = [geo.empties[e] for e in eyes if e in geo.empties]
            if pi == 0:
                cand = idx
            else:
                # Re-test only what this pose can change: triangles of moved or hidden nodes,
                # and triangles that some moved node blocked in an earlier pose (a ray that
                # was blocked only by geometry that did not move stays blocked).
                cand = np.array(sorted(t for t in idx.tolist()
                                       if (not self.front[t] or (want_back[t] and
                                                                 not self.back[t]))
                                       and pose_map[t] >= 0 and
                                       (rest.node[t] in moved or (hits.get(t, set()) & moved))),
                                dtype=np.int64)
                self.pose_retests[pose["name"]] = int(len(cand))
            if len(cand) == 0 or tree is None:
                if tree is None and pi == 0:
                    self.front[idx] = True
                continue
            gi = pose_map[cand]
            nrm = geo.normal[gi]
            t1, t2 = frames(nrm)
            pos_in_idx = np.searchsorted(idx, cand)
            for ci, t in enumerate(cand.tolist()):
                g = int(gi[ci])
                p_i = pos_in_idx[ci]
                b = bary[starts[p_i]:starts[p_i + 1]]
                v = geo.v[g]
                pts = b[:, 0:1] * v[0] + b[:, 1:2] * v[1] + b[:, 2:3] * v[2]
                n = nrm[ci]
                dirs = hemi[:, 0:1] * t1[ci] + hemi[:, 1:2] * t2[ci] + hemi[:, 2:3] * n
                blockers = hits.setdefault(t, set())
                if not self.front[t] and self._side(tree, occ_node, pts, n, dirs, reach,
                                                    eye_pos, vcfg, blockers):
                    self.front[t] = True
                if (not self.front[t] or want_back[t]) and not self.back[t]:
                    if self._side(tree, occ_node, pts, -n, -dirs, reach, [], vcfg, blockers):
                        self.back[t] = True
                if self.front[t] and (self.back[t] or not want_back[t]):
                    hits.pop(t, None)
        self.hidden = self.tested & ~self.front & ~self.back
        self.back_only = self.tested & ~self.front & self.back
        self.centre = centre

    def _pose_map(self, rest, geo, pose):
        """Rest triangle index -> triangle index in the posed geo (-1 when hidden)."""
        if len(geo.v) == len(rest.v):
            return np.arange(len(rest.v))
        keep = ~np.isin(rest.node, [rest.node_index(n) for n in (pose.get("hide") or [])
                                    if rest.node_index(n) is not None])
        out = np.full(len(rest.v), -1, dtype=np.int64)
        out[keep] = np.arange(int(keep.sum()))
        return out

    def _side(self, tree, occ_node, pts, n, dirs, reach, eyes, vcfg, blockers):
        from mathutils import Vector
        rc = tree.ray_cast
        off = vcfg["ray_offset"]
        dl = [Vector(d) for d in dirs.tolist()]
        nv = Vector(n.tolist())
        for p in pts.tolist():
            o = Vector(p) + nv * off
            for d in dl:
                self.rays += 1
                hit = rc(o, d, reach)
                if hit[0] is None:
                    return True
                blockers.add(int(occ_node[hit[2]]))
            for e in eyes:
                to = Vector(e.tolist()) - o
                dist = to.length
                if dist < 1e-9 or to.dot(nv) <= 0:
                    continue
                self.rays += 1
                hit = rc(o, to / dist, dist)
                if hit[0] is None:
                    return True
                blockers.add(int(occ_node[hit[2]]))
        return False


# ---------------------------------------------------------------- hygiene


def hygiene(geo, topo, cfg, node_match):
    h = cfg["hygiene"]
    deg = geo.area < h["degenerate_area"]
    ang = min_angle_deg(geo)
    sliver = ~deg & (ang < h["sliver_deg"])
    out = {"degenerate": int(deg.sum()), "slivers": int(sliver.sum())}
    dup = 0
    wind = 0
    nonmanifold = 0
    boundary = 0
    for name, tp in topo.items():
        tris = tp.tris
        ok = (tris[:, 0] != tris[:, 1]) & (tris[:, 1] != tris[:, 2]) & (tris[:, 2] != tris[:, 0])
        st = np.sort(tris[ok], axis=1)
        if len(st):
            dup += len(st) - len(np.unique(st, axis=0))
        t = tris[ok]
        if len(t) == 0:
            continue
        a = np.concatenate([t[:, 0], t[:, 1], t[:, 2]])
        b = np.concatenate([t[:, 1], t[:, 2], t[:, 0]])
        key = np.stack([np.minimum(a, b), np.maximum(a, b)], axis=1)
        sign = np.where(a < b, 1, -1)
        _u, inv = np.unique(key, axis=0, return_inverse=True)
        inv = inv.reshape(-1)
        cnt = np.bincount(inv)
        ssum = np.bincount(inv, weights=sign)
        wind += int(((cnt == 2) & (np.abs(ssum) == 2)).sum())
        nonmanifold += int((cnt > 2).sum())
        boundary += int((cnt == 1).sum())
    out["duplicates"] = int(dup)
    out["inconsistent_winding_edges"] = wind
    out["nonmanifold_edges"] = nonmanifold
    out["boundary_edges"] = boundary
    # winding against the exported normals: the face normal points away from all three
    # vertex normals' mean
    mean_vn = geo.vn.mean(axis=1)
    against = ~deg & (dot(mean_vn, geo.normal) < 0)
    out["normals_against_winding"] = int(against.sum())
    return out, deg, sliver, ang, against


def floating_islands(geo, topo, margin):
    """Same rule as make_models.check_islands: every connected island must touch (within
    `margin`) the main cluster of geometry (the cluster with the most islands)."""
    boxes = []
    for name in sorted(topo):
        tp = topo[name]
        v = geo.v[tp.tri_idx]
        for c in range(tp.ncomp):
            sel = v[tp.comp == c].reshape(-1, 3)
            boxes.append((sel.min(axis=0), sel.max(axis=0), name, c))
    n = len(boxes)
    parent = list(range(n))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    order = sorted(range(n), key=lambda i: (boxes[i][0][0], i))
    for ii, i in enumerate(order):
        for j in order[ii + 1:]:
            if boxes[j][0][0] > boxes[i][1][0] + margin:
                break
            a, b = boxes[i], boxes[j]
            if all(a[0][k] <= b[1][k] + margin and b[0][k] <= a[1][k] + margin
                   for k in range(3)):
                ri, rj = find(i), find(j)
                if ri != rj:
                    parent[max(ri, rj)] = min(ri, rj)
    sizes = {}
    for i in range(n):
        sizes[find(i)] = sizes.get(find(i), 0) + 1
    if not sizes:
        return []
    main = min(sizes, key=lambda r: (-sizes[r], r))
    out = []
    for i in range(n):
        if find(i) != main:
            lo, hi = boxes[i][0], boxes[i][1]
            out.append({"node": boxes[i][2], "centre": vec((lo + hi) / 2),
                        "size": vec(hi - lo)})
    return out


# ---------------------------------------------------------------- profile fairness


def zigzag(h, thr):
    """Interior local maxima/minima with prominence >= thr (hysteresis scan)."""
    maxima, minima = [], []
    if len(h) < 3:
        return maxima, minima
    direction = 0
    cand_i = 0
    lo_i = hi_i = 0
    for i in range(1, len(h)):
        if direction == 0:
            if h[i] > h[hi_i]:
                hi_i = i
            if h[i] < h[lo_i]:
                lo_i = i
            if h[i] - h[lo_i] >= thr:
                direction, cand_i = 1, i  # rising: looking for a maximum
            elif h[hi_i] - h[i] >= thr:
                direction, cand_i = -1, i
        elif direction > 0:
            if h[i] >= h[cand_i]:
                cand_i = i
            elif h[cand_i] - h[i] >= thr:
                maxima.append(cand_i)
                direction, cand_i = -1, i
        else:
            if h[i] <= h[cand_i]:
                cand_i = i
            elif h[i] - h[cand_i] >= thr:
                minima.append(cand_i)
                direction, cand_i = 1, i
    return maxima, minima


def analyse_profile(s, h, step, prom, seg):
    """Outline h(s) -> interior extrema (prominence >= prom), the largest kink (change of
    slope between the two `seg`-long chords meeting at a sample) and the largest step (the
    jump between neighbouring samples beyond what the steeper adjacent chord explains).
    Locations are sample indices (a step sits between index i and i + 1)."""
    out = {"maxima": [], "minima": [], "kink_deg": 0.0, "kink_i": None, "step": 0.0,
           "step_i": None}
    n = len(h)
    if n < 3:
        return out
    out["maxima"], out["minima"] = zigzag(h, prom)
    w = max(1, int(round(seg / step)))
    if n > 2 * w:
        i = np.arange(w, n - w)
        a1 = np.degrees(np.arctan2(h[i] - h[i - w], s[i] - s[i - w]))
        a2 = np.degrees(np.arctan2(h[i + w] - h[i], s[i + w] - s[i]))
        k = np.abs(a2 - a1)
        j = int(np.argmax(k))
        out["kink_deg"] = float(k[j])
        out["kink_i"] = int(i[j])
    if n > 2 * w + 1:
        i = np.arange(w, n - w - 1)
        jump = np.abs(h[i + 1] - h[i])
        sl = np.abs((h[i] - h[i - w]) / (s[i] - s[i - w]))
        sr = np.abs((h[i + 1 + w] - h[i + 1]) / (s[i + 1 + w] - s[i + 1]))
        excess = jump - (s[i + 1] - s[i]) * np.maximum(sl, sr)
        j = int(np.argmax(excess))
        out["step"] = float(max(0.0, excess[j]))
        out["step_i"] = int(i[j])
    return out


def run_probe(probe, geo, topo, cfg):
    start = np.array(probe["start"], dtype=np.float64)
    end = np.array(probe["end"], dtype=np.float64)
    d = np.array(probe["direction"], dtype=np.float64)
    d /= np.linalg.norm(d)
    along = end - start
    length = float(np.linalg.norm(along))
    u = along / length
    sel = geo.mask(probe["nodes"], probe["materials"])
    keep = sel.copy()
    excluded = []
    for name in probe["nodes"]:
        tp = topo.get(name)
        if tp is None:
            continue
        v = geo.v[tp.tri_idx]
        proj = dot(v, u[None, None, :])
        for c in range(tp.ncomp):
            m = tp.comp == c
            ext = float(proj[m].max() - proj[m].min())
            if ext < probe["min_feature"]:
                tris = tp.tri_idx[m]
                if sel[tris].any():
                    keep[tris] = False
                    excluded.append((name, c, tris))
    idx_all = np.nonzero(sel)[0]
    idx = np.nonzero(keep)[0]
    tree = bvh(geo.v[idx]) if len(idx) else None
    tree_all = bvh(geo.v[idx_all]) if len(idx_all) else None
    nstep = int(math.floor(length / probe["step"] + 1e-9)) + 1
    s = np.arange(nstep) * probe["step"]
    origins = start[None, :] + s[:, None] * u[None, :]
    from mathutils import Vector
    dv = Vector(d.tolist())
    h = np.full(nstep, np.nan)
    hitp = np.zeros((nstep, 3))
    excluded_hit = set()
    comp_of = {}
    for name, c, tris in excluded:
        for t in tris.tolist():
            comp_of[t] = (name, c)
    reach = 1000.0
    for i, o in enumerate(origins.tolist()):
        if tree is not None:
            hit = tree.ray_cast(Vector(o), dv, reach)
            if hit[0] is not None:
                hitp[i] = hit[0][:]
                h[i] = -float(np.dot(hitp[i], d))
        if tree_all is not None and excluded:
            hit = tree_all.ray_cast(Vector(o), dv, reach)
            if hit[0] is not None:
                t = int(idx_all[hit[2]])
                if t in comp_of:
                    excluded_hit.add(comp_of[t])
    ok = ~np.isnan(h)
    sv, hv, pv = s[ok], h[ok], hitp[ok]
    res = analyse_profile(sv, hv, probe["step"], probe["prominence"], probe["segment"])
    gaps = int((~ok).sum())
    ex = []
    for name, c in sorted(excluded_hit):
        tp = topo[name]
        v = geo.v[tp.tri_idx[tp.comp == c]].reshape(-1, 3)
        lo, hi = v.min(axis=0), v.max(axis=0)
        ex.append({"node": name, "centre": vec((lo + hi) / 2), "size": vec(hi - lo)})

    def at(i, j=None):
        if i is None:
            return None
        return vec(pv[i] if j is None else (pv[i] + pv[j]) / 2)
    return {
        "maxima": len(res["maxima"]), "minima": len(res["minima"]),
        "maxima_at": [at(i) for i in res["maxima"]], "minima_at": [at(i) for i in res["minima"]],
        "kink_deg": rd(res["kink_deg"]), "kink_at": at(res["kink_i"]),
        "step": rm(res["step"]),
        "step_at": at(res["step_i"], None if res["step_i"] is None else res["step_i"] + 1),
        "samples": int(nstep), "gaps": gaps, "excluded_parts": ex,
        "profile": [[rm(a), rm(b)] for a, b in zip(sv, hv)],
    }, (s, h, start, u, d, hitp)


# ---------------------------------------------------------------- the audit


class Audit:
    def __init__(self, model_path, ref_path, cfg, fast=False, log=print):
        self.cfg = cfg
        self.fast = fast
        self.log = log
        self.model_path = Path(model_path)
        self.ref_path = Path(ref_path) if ref_path else None
        self.warnings = []
        self.timing = {}

    def tick(self, label, t0):
        self.timing[label] = time.perf_counter() - t0
        self.log(f"[audit] {label}: {self.timing[label]:.1f} s")

    def run(self):
        cfg = self.cfg
        t_all = time.perf_counter()
        t0 = time.perf_counter()
        model = glb_io.read_glb(self.model_path)
        ref = glb_io.read_glb(self.ref_path) if self.ref_path else None
        self.model, self.ref = model, ref
        self.warnings += [f"model: {w}" for w in model.warnings]
        if ref is not None:
            self.warnings += [f"reference: {w}" for w in ref.warnings]
        glass = cfg["model"]["glass_materials"]
        geos = [Geo(model, p, glass) for p in cfg["poses"]]
        geo = geos[0]
        rgeo = Geo(ref, None, glass) if ref is not None else None
        self.geo, self.rgeo = geo, rgeo
        self.tick("load", t0)

        t0 = time.perf_counter()
        topo = {}
        for name in geo.node_names:
            ti = np.nonzero(geo.node == geo.node_index(name))[0]
            if len(ti):
                topo[name] = Topology(geo, ti, cfg["sampling"]["weld"])
        self.topo = topo
        groups = self.resolve_groups(geo)
        self.groups = groups
        self.tick("topology", t0)

        # A + B
        t0 = time.perf_counter()
        T = len(geo.v)
        self.err = np.zeros(T)
        self.nerr = np.zeros(T)
        self.has_ref = np.zeros(T, dtype=bool)
        self.unmeasured = np.zeros(T, dtype=bool)
        matches = {}
        if rgeo is not None:
            for name in sorted(topo):
                if rgeo.node_index(name) is None or not (rgeo.node == rgeo.node_index(name)).any():
                    self.warnings.append(f"node {name}: no reference node of that name; shape "
                                         "and shading not measured")
                    continue
                m = NodeMatch(geo, rgeo, name, cfg, self.fast)
                matches[name] = m
                self.err[m.tri] = m.err
                self.nerr[m.tri] = m.nerr
                self.has_ref[m.tri] = True
                tp = topo[name]
                pos = np.full(len(geo.v), -1, dtype=np.int64)
                pos[tp.tri_idx] = np.arange(len(tp.tri_idx))
                fold = tp.soft_folds(geo)[pos[m.tri]]
                m.unmeasured = m.identical & ((m.spread > 1.0) | fold)
                self.unmeasured[m.tri] = m.unmeasured
                if m.rdensity < cfg["reference"]["min_density"]:
                    self.warnings.append(
                        f"node {name}: reference has only {m.rdensity:.1f}x the model's "
                        f"triangles (want >= {cfg['reference']['min_density']:g}x); its "
                        "errors are underestimated")
            for name in rgeo.node_names:
                if rgeo.node_index(name) is not None and (rgeo.node == rgeo.node_index(name)).any() \
                        and name not in topo:
                    self.warnings.append(f"reference node {name} is missing from the model")
        else:
            self.warnings.append("no reference given: checks A, B and the headroom are skipped")
        self.matches = matches
        self.tick("shape+shading", t0)

        # C: headroom
        t0 = time.perf_counter()
        self.headroom = self.run_headroom(geo, rgeo, topo, matches, groups)
        self.tick("headroom", t0)

        # C: visibility (the back side is also tested where the normal disagrees with the
        # reference by more than 90 degrees, to tell which of the two is flipped)
        t0 = time.perf_counter()
        self.ref_flip = self.has_ref & (self.nerr > 90.0)
        self.vis = Visibility(model, geos, cfg, self.fast, want_back=self.ref_flip)
        self.tick("visibility", t0)

        # D
        t0 = time.perf_counter()
        hyg, deg, sliver, ang, against = hygiene(geo, topo, cfg, matches)
        self.deg, self.sliver = deg, sliver
        vis = self.vis
        # Flipped: only the back side can ever be seen; or the front is seen but the exported
        # normals point against the winding; or both sides are seen (an open shell) and the
        # normal is opposite to the reference's. Where only the front is seen and the
        # reference disagrees, the reference is the flipped one: a warning, not a fault.
        self.against = against & vis.front
        self.flip_ref = self.ref_flip & vis.front & vis.back
        self.flipped_any = vis.back_only | self.against | self.flip_ref
        ref_wrong = self.ref_flip & vis.front & ~vis.back
        self.ref_wrong = ref_wrong
        if ref_wrong.any():
            names = sorted({geo.node_names[i] for i in geo.node[ref_wrong]})
            self.warnings.append(
                f"{int(ref_wrong.sum())} visible triangles face the opposite way in the "
                f"reference (nodes {', '.join(names)}): the reference build is flipped there")
        self.floating = floating_islands(geo, topo, cfg["hygiene"]["island_margin"])
        self.hyg = hyg
        self.tick("hygiene", t0)

        # F
        t0 = time.perf_counter()
        self.probes = {}
        self.probe_raw = {}
        for probe in cfg["probes"]:
            pg = geos[[p["name"] for p in cfg["poses"]].index(probe["pose"])]
            res, raw = run_probe(probe, pg, topo, cfg)
            self.probes[probe["name"]] = res
            self.probe_raw[probe["name"]] = raw
        self.tick("profiles", t0)

        self.result = self.assemble()
        self.timing["total"] = time.perf_counter() - t_all
        return self.result

    # -------------------------------------------------------------- pieces

    def resolve_groups(self, geo):
        cfg = self.cfg
        groups = []
        assigned = set()
        for g in cfg["groups"]:
            present = [n for n in g["nodes"] if n in self.topo]
            absent = [n for n in g["nodes"] if n not in self.topo]
            groups.append({**g, "present": present, "absent": absent})
            assigned |= set(present)
        rest = [n for n in sorted(self.topo) if n not in assigned]
        if rest:
            target = cfg["model"].get("default_group")
            grp = next((g for g in groups if g["name"] == target), None)
            if grp is None:
                grp = {**GROUP_DEFAULTS, "name": "other", "nodes": [], "present": [],
                       "absent": []}
                groups.append(grp)
            grp["present"] = grp["present"] + rest
            self.warnings.append(f"nodes not listed in any group (put in "
                                 f"{grp['name']!r}): {', '.join(rest)}")
        return groups

    def group_of(self, name):
        for g in self.groups:
            if name in g["present"]:
                return g
        return None

    def run_headroom(self, geo, rgeo, topo, matches, groups):
        cfg = self.cfg
        hcfg = cfg["headroom"]
        ratios = cfg["fast"]["ratios"] if self.fast else hcfg["ratios"]
        spacing = cfg["fast"]["spacing"] if self.fast else cfg["sampling"]["spacing"]
        out = []
        if rgeo is None:
            return out
        for name in sorted(matches):
            m = matches[name]
            tp = topo[name]
            g = self.group_of(name)
            pos_in_node = np.full(len(geo.v), -1, dtype=np.int64)
            pos_in_node[tp.tri_idx] = np.arange(len(tp.tri_idx))
            comp_of_ref = tp.comp[pos_in_node[m.rnear]] if len(m.rnear) else np.zeros(0, int)
            sizes = np.bincount(tp.comp, minlength=tp.ncomp)
            for c in range(tp.ncomp):
                if sizes[c] < hcfg["min_component"]:
                    continue
                tris_local = np.nonzero(tp.comp == c)[0]
                gtris = tp.tri_idx[tris_local]
                wt = tp.tris[tris_local]
                used, inv = np.unique(wt.reshape(-1), return_inverse=True)
                faces = inv.reshape(-1, 3)
                pts = geo.v[gtris].reshape(-1, 3)
                first = np.zeros(len(used), dtype=np.int64)
                first[inv[::-1]] = np.arange(len(inv))[::-1]
                verts = pts[first]
                good = (faces[:, 0] != faces[:, 1]) & (faces[:, 1] != faces[:, 2]) & \
                       (faces[:, 2] != faces[:, 0])
                faces = faces[good]
                rc_pts = m.rcent[comp_of_ref == c]
                base = measure_patch(geo.v[gtris], m.tree, rgeo.v[m.rtri], rc_pts, cfg, spacing)
                # Pass mark: the group gate, or (for a piece that already misses it) the
                # piece's own error plus a 5 % slack, so that removing triangles may not make
                # a piece noticeably worse than it is.
                slack = hcfg.get("slack", 0.05)
                thr = (max(g["shape_p99"], (1 + slack) * max(base["fwd_p99"], base["rev_p99"])),
                       max(g["shape_max"], (1 + slack) * max(base["fwd_max"], base["rev_max"])))
                recs = decimate_measure(f"{name}_{c}", verts, faces, ratios, m.tree,
                                        rgeo.v[m.rtri], rc_pts, thr, cfg, spacing)
                passing = [r for r in recs if r["pass"]]
                best = passing[-1] if passing else None
                lo, hi = verts.min(axis=0), verts.max(axis=0)
                kept = best["triangles"] if best else len(gtris)
                out.append({
                    "node": name, "component": int(c), "triangles": int(len(gtris)),
                    "centre": vec((lo + hi) / 2), "size": vec(hi - lo),
                    "base": {k: rm(v) for k, v in base.items()},
                    "threshold_p99": rm(thr[0]), "threshold_max": rm(thr[1]),
                    "smallest_passing_ratio": best["ratio"] if best else None,
                    "triangles_kept": int(kept),
                    "headroom": rf(1.0 - kept / len(gtris)),
                    "steps": [{"ratio": r["ratio"], "triangles": r["triangles"],
                               "fwd_p99": rm(r["fwd_p99"]), "fwd_max": rm(r["fwd_max"]),
                               "rev_p99": rm(r["rev_p99"]), "rev_max": rm(r["rev_max"]),
                               "pass": r["pass"]} for r in recs],
                })
        return out

    def hidden_clusters(self, name, mask=None):
        tp = self.topo[name]
        hid = (self.vis.hidden if mask is None else mask)[tp.tri_idx]
        if not hid.any():
            return []
        loc = np.nonzero(hid)[0]
        sub = tp.tris[loc]
        _u, inv = np.unique(sub.reshape(-1), return_inverse=True)
        lab = tri_components(inv.reshape(-1, 3), len(_u))
        geo = self.geo
        out = []
        for c in range(int(lab.max()) + 1):
            t = tp.tri_idx[loc[lab == c]]
            v = geo.v[t].reshape(-1, 3)
            lo, hi = v.min(axis=0), v.max(axis=0)
            out.append({"node": name, "triangles": int(len(t)),
                        "area": ra(geo.area[t].sum()), "centre": vec((lo + hi) / 2),
                        "size": vec(hi - lo)})
        out.sort(key=lambda r: (-r["triangles"], r["centre"]))
        return out

    # -------------------------------------------------------------- results

    def assemble(self):
        cfg, geo, vis = self.cfg, self.geo, self.vis
        gates = cfg["gates"]
        view = cfg["view"]
        model = self.model
        res = {"tool": {"name": "tools/models/audit_model.py", "version": TOOL_VERSION,
                        "mode": "fast" if self.fast else "full"},
               "frame": "modelling frame of tools/models: metres, x = vehicle left, "
                        "y = aft (nose at -y), z = up",
               "inputs": {}}
        try:
            import bpy
            res["tool"]["blender"] = bpy.app.version_string
        except ImportError:
            pass
        for key, path in (("model", self.model_path), ("reference", self.ref_path),
                          ("config", self.cfg.get("_path"))):
            if path is not None:
                res["inputs"][key] = {"file": Path(path).name, "sha256": sha256(path),
                                      "bytes": Path(path).stat().st_size}
        res["settings"] = {
            "sample_spacing": rm(cfg["fast"]["spacing"] if self.fast
                                 else cfg["sampling"]["spacing"]),
            "directions": vis.ndir, "visibility_samples": vis.samples, "rays": vis.rays,
            "poses": [p["name"] for p in cfg["poses"]], "pose_retests": vis.pose_retests,
        }

        # ---- per node
        nodes = {}
        T = len(geo.v)
        area = geo.area
        self.seen = vis.front & ~self.flipped_any
        for name in sorted(self.topo):
            tp = self.topo[name]
            ti = tp.tri_idx
            g = self.group_of(name)
            p = 2.0 * g["distance"] * math.tan(math.radians(view["vfov_deg"]) / 2) / view[
                "height"]
            alt = 2.0 * area[ti] / np.maximum(geo.lmax[ti], EPS)
            prims = [pr for pr in model.prims if pr.node == name]
            nv = sum(pr.vertex_count for pr in prims)
            entry = {
                "group": g["name"], "triangles": int(len(ti)), "vertices": int(nv),
                "verts_per_tri": rf(nv / max(1, len(ti))), "area": ra(area[ti].sum()),
                "components": int(tp.ncomp),
                "hidden": int(vis.hidden[ti].sum()),
                "hidden_frac": rf(vis.hidden[ti].sum() / max(1, len(ti))),
                "hidden_area_frac": rf(area[ti][vis.hidden[ti]].sum() / max(EPS, area[ti].sum())),
                "flipped": int(self.flipped_any[ti].sum()),
                "flipped_clusters": self.hidden_clusters(name, self.flipped_any)[
                    :cfg["visibility"]["clusters"]],
                "subpixel": int((area[ti] < p * p).sum()),
                "subpixel_frac": rf((area[ti] < p * p).sum() / max(1, len(ti))),
                "thin_frac": rf((alt < 0.5 * p).sum() / max(1, len(ti))),
                "degenerate": int(self.deg[ti].sum()), "slivers": int(self.sliver[ti].sum()),
                "hidden_clusters": self.hidden_clusters(name)[:cfg["visibility"]["clusters"]],
            }
            if name in self.matches:
                m = self.matches[name]
                w = area[m.tri]
                # gates use what can be seen: front-visible, not flipped (hidden and flipped
                # triangles have their own checks); "_all" keeps every triangle
                ok = self.seen[m.tri]
                rok = self.seen[m.rnear]
                entry["shape"] = round_stats(wstats(m.err[ok], w[ok]), rm)
                entry["shape_reverse"] = round_stats(wstats(m.rerr[rok], m.rarea[rok]), rm)
                okb = ok & ~self.ref_wrong[m.tri]  # the reference is the flipped one there
                entry["shading_deg"] = round_stats(wstats(m.nerr[okb], w[okb]), rd)
                entry["shape_all"] = round_stats(wstats(m.err, w), rm)
                entry["shading_deg_all"] = round_stats(wstats(m.nerr, w), rd)
                entry["reference_triangles"] = int(len(m.rtri))
                entry["reference_density"] = round(m.rdensity, 2)
                entry["samples"] = int(m.samples)
                entry["reprojected_samples"] = int(m.reprojected)
                ratio = m.err[ok] / g["shape_p99"]
                entry["error_buckets_area"] = self.buckets(ratio, w[ok])
                entry["error_buckets_count"] = self.buckets(ratio, np.ones(len(ratio)))
                um = m.unmeasured
                entry["unmeasured_curved"] = int(um.sum())
                entry["unmeasured_curved_area_frac"] = rf(w[um].sum() / max(EPS, w.sum()))
                if um.any():
                    self.warnings.append(
                        f"node {name}: {int(um.sum())} triangles on curved surfaces "
                        f"({w[um].sum() / max(EPS, w.sum()) * 100:.1f} % of its area) are "
                        "tessellated exactly as in the reference, so their shape error is not "
                        "measured (the generator does not scale them with --detail)")
            nodes[name] = entry
        res["nodes"] = nodes

        # ---- primitives
        prims = []
        for pr in model.prims:
            pos = np.round(pr.positions / cfg["sampling"]["weld"]).astype(np.int64)
            upos = len(np.unique(pos, axis=0)) if len(pos) else 0
            if pr.normals is not None and len(pos):
                pn = np.concatenate([pos, np.round(pr.normals * 1e4).astype(np.int64)], axis=1)
                upn = len(np.unique(pn, axis=0))
            else:
                upn = upos
            prims.append({"node": pr.node, "material": pr.material,
                          "triangles": int(len(pr.indices)), "vertices": int(pr.vertex_count),
                          "unique_positions": int(upos), "unique_position_normals": int(upn),
                          "max_index": int(pr.max_index),
                          "index_type": {5121: "u8", 5123: "u16", 5125: "u32", None: "none"}.get(
                              pr.index_type, str(pr.index_type))})
        res["primitives"] = prims

        # ---- groups
        grp_out = {}
        for g in self.groups:
            if not g["present"]:
                grp_out[g["name"]] = {"present": [], "absent": g["absent"]}
                continue
            ti = np.nonzero(np.isin(geo.node, [geo.node_index(n) for n in g["present"]]))[0]
            p = 2.0 * g["distance"] * math.tan(math.radians(view["vfov_deg"]) / 2) / view[
                "height"]
            alt = 2.0 * area[ti] / np.maximum(geo.lmax[ti], EPS)
            entry = {"present": g["present"], "absent": g["absent"],
                     "triangles": int(len(ti)), "pixel_size": rm(p),
                     "design_distance": g["distance"],
                     "subpixel_frac": rf((area[ti] < p * p).sum() / max(1, len(ti))),
                     "thin_frac": rf((alt < 0.5 * p).sum() / max(1, len(ti)))}
            ms = [self.matches[n] for n in g["present"] if n in self.matches]
            if ms:
                ok = np.concatenate([self.seen[m.tri] for m in ms])
                rok = np.concatenate([self.seen[m.rnear] for m in ms])
                err = np.concatenate([m.err for m in ms])
                nerr = np.concatenate([m.nerr for m in ms])
                w = np.concatenate([area[m.tri] for m in ms])
                rerr = np.concatenate([m.rerr for m in ms])
                rw = np.concatenate([m.rarea for m in ms])
                entry["shape"] = round_stats(wstats(err[ok], w[ok]), rm)
                entry["shape_reverse"] = round_stats(wstats(rerr[rok], rw[rok]), rm)
                okb = ok & ~np.concatenate([self.ref_wrong[m.tri] for m in ms])
                entry["shading_deg"] = round_stats(wstats(nerr[okb], w[okb]), rd)
                entry["shape_all"] = round_stats(wstats(err, w), rm)
                entry["shading_deg_all"] = round_stats(wstats(nerr, w), rd)
                entry["error_buckets_area"] = self.buckets(err[ok] / g["shape_p99"], w[ok])
            grp_out[g["name"]] = entry
        res["groups"] = grp_out

        # ---- model totals
        nverts = sum(pr.vertex_count for pr in model.prims)
        lo = geo.v.reshape(-1, 3).min(axis=0) if T else np.zeros(3)
        hi = geo.v.reshape(-1, 3).max(axis=0) if T else np.zeros(3)
        dims = hi - lo
        res["model"] = {
            "triangles": int(T), "vertices": int(nverts),
            "verts_per_tri": rf(nverts / max(1, T)),
            "nodes": len(model.scene_nodes), "mesh_nodes": len(self.topo),
            "primitives": len(model.prims),
            "materials": len({pr.material for pr in model.prims}),
            "file_bytes": int(model.size),
            "max_vertices_per_primitive": int(max((pr.vertex_count for pr in model.prims),
                                                  default=0)),
            "max_index": int(max((pr.max_index for pr in model.prims), default=-1)),
            "bbox_min": vec(lo), "bbox_max": vec(hi),
            "length": rm(dims[1]), "span": rm(dims[0]), "height": rm(dims[2]),
            "hidden": int(vis.hidden.sum()), "hidden_frac": rf(vis.hidden.sum() / max(1, T)),
            "empties": {k: vec(v) for k, v in sorted(geo.empties.items())},
        }
        res["hygiene"] = {**self.hyg,
                          "flipped": int(self.flipped_any.sum()),
                          "flipped_back_visible": int(vis.back_only.sum()),
                          "flipped_normals_against_winding": int(self.against.sum()),
                          "flipped_normal_error_over_90": int(self.flip_ref.sum()),
                          "floating_islands": self.floating[:20],
                          "floating": len(self.floating)}
        res["hygiene"]["degenerate_examples"] = self.examples(self.deg)
        res["headroom"] = self.headroom
        res["probes"] = self.probes
        res["gates"] = self.gates(res)
        res["warnings"] = sorted(set(self.warnings))
        res["summary"] = {
            "gates": len(res["gates"]),
            "failed": sum(1 for g in res["gates"] if not g["pass"]),
            "hard_failed": sum(1 for g in res["gates"] if not g["pass"] and g["hard"]),
        }
        return res

    def examples(self, mask, n=10):
        idx = np.nonzero(mask)[0][:n]
        geo = self.geo
        return [{"node": geo.node_names[geo.node[t]], "centre": vec(geo.v[t].mean(axis=0))}
                for t in idx.tolist()]

    @staticmethod
    def buckets(ratio, w):
        edges = [0.1, 0.5, 1.0]
        b = np.digitize(ratio, edges)
        tot = max(EPS, float(np.sum(w)))
        names = ["lt_0.1", "0.1_0.5", "0.5_1", "gt_1"]
        return {names[i]: rf(np.sum(w[b == i]) / tot) for i in range(4)}

    def gates(self, res):
        cfg = self.cfg
        gc = cfg["gates"]
        hard = set(gc.get("hard", []))
        out = []

        def add(gid, check, label, value, limit, op, unit, key):
            if value is None or limit is None:
                return
            ok = value <= limit if op == "<=" else value >= limit if op == ">=" else \
                value == limit
            out.append({"id": gid, "check": check, "label": label, "value": value,
                        "limit": limit, "op": op, "unit": unit, "pass": bool(ok),
                        "hard": key in hard})
        for g in self.groups:
            ge = res["groups"].get(g["name"], {})
            if "shape" not in ge:
                continue
            name = g["name"]
            add(f"A.{name}.p99", "A", f"Shape accuracy {name}: model to reference p99",
                ge["shape"]["p99"], g["shape_p99"], "<=", "m", "shape")
            add(f"A.{name}.max", "A", f"Shape accuracy {name}: model to reference max",
                ge["shape"]["max"], g["shape_max"], "<=", "m", "shape")
            add(f"A.{name}.rev_p99", "A", f"Shape accuracy {name}: reference to model p99",
                ge["shape_reverse"]["p99"], g["shape_p99"], "<=", "m", "shape")
            add(f"A.{name}.rev_max", "A", f"Shape accuracy {name}: reference to model max",
                ge["shape_reverse"]["max"], g["shape_max"], "<=", "m", "shape")
            add(f"B.{name}.p95", "B", f"Shading {name}: normal error p95",
                ge["shading_deg"]["p95"], g["normal_p95_deg"], "<=", "deg", "shading")
            add(f"B.{name}.max", "B", f"Shading {name}: normal error max",
                ge["shading_deg"]["max"], g["normal_max_deg"], "<=", "deg", "shading")
        for h in res["headroom"]:
            add(f"C.headroom.{h['node']}.{h['component']}", "C",
                f"Simplification headroom {h['node']} component {h['component']} "
                f"({h['triangles']} tris)", h["headroom"], gc["headroom_max"], "<=", "frac",
                "headroom_max")
        for name, n in res["nodes"].items():
            add(f"C.hidden.{name}", "C", f"Hidden triangles {name}", n["hidden_frac"],
                gc["hidden_max"], "<=", "frac", "hidden_max")
        for g in self.groups:
            ge = res["groups"].get(g["name"], {})
            if "subpixel_frac" in ge:
                add(f"C.subpixel.{g['name']}", "C",
                    f"Sub-pixel triangles {g['name']} at {g['distance']:g} m",
                    ge["subpixel_frac"], gc["subpixel_max"], "<=", "frac", "subpixel_max")
        mres = res["model"]
        add("C.verts_per_tri", "C", "Vertices per triangle", mres["verts_per_tri"],
            gc["verts_per_tri_max"], "<=", "ratio", "verts_per_tri_max")
        for i, pr in enumerate(res["primitives"]):
            add(f"C.index.{pr['node']}.{pr['material']}", "C",
                f"Index limit {pr['node']} / {pr['material']} ({pr['vertices']} vertices)",
                pr["max_index"], gc["index_max"], "<=", "count", "index_max")
        hy = res["hygiene"]
        add("D.degenerate", "D", "Degenerate triangles", hy["degenerate"], gc["degenerate_max"],
            "<=", "count", "degenerate_max")
        add("D.duplicates", "D", "Duplicate faces", hy["duplicates"], gc["duplicate_max"], "<=",
            "count", "duplicate_max")
        add("D.flipped", "D", "Flipped faces", hy["flipped"], gc["flipped_max"], "<=", "count",
            "flipped_max")
        add("D.floating", "D", "Floating islands", hy["floating"], gc["floating_max"], "<=",
            "count", "floating_max")
        add("D.winding", "D", "Inconsistent winding (edges)", hy["inconsistent_winding_edges"],
            gc["winding_max"], "<=", "count", "winding_max")
        add("D.slivers", "D", "Sliver triangles (< %g deg)" % cfg["hygiene"]["sliver_deg"],
            rf(hy["slivers"] / max(1, mres["triangles"])), gc["sliver_max"], "<=", "frac",
            "sliver_max")
        ps = cfg["model"].get("published_size")
        if ps:
            for key in ("length", "span", "height"):
                if key in ps:
                    dev = rf(abs(mres[key] - ps[key]) / ps[key])
                    add(f"E.{key}", "E", f"{key.capitalize()} {mres[key]:.3f} m vs published "
                        f"{ps[key]:g} m", dev, gc["size_tolerance"], "<=", "frac",
                        "size_tolerance")
        for p in cfg["probes"]:
            r = res["probes"][p["name"]]
            n = p["name"]
            add(f"F.{n}.maxima", "F", f"Profile {n}: maxima", r["maxima"], p["max_maxima"], "<=",
                "count", "profile")
            add(f"F.{n}.minima", "F", f"Profile {n}: minima", r["minima"], p["max_minima"], "<=",
                "count", "profile")
            add(f"F.{n}.kink", "F", f"Profile {n}: largest kink", r["kink_deg"],
                p["kink_max_deg"], "<=", "deg", "profile")
            add(f"F.{n}.step", "F", f"Profile {n}: largest step", r["step"], p["step_max"], "<=",
                "m", "profile")
        b = cfg["budget"]
        add("G.total", "G", "Triangles, total", mres["triangles"], b.get("total"), "<=", "count",
            "budget_total")
        for gname, lim in sorted((b.get("groups") or {}).items()):
            ge = res["groups"].get(gname, {})
            if "triangles" in ge:
                add(f"G.group.{gname}", "G", f"Triangles, group {gname}", ge["triangles"], lim,
                    "<=", "count", "budget_group")
        for nname, lim in sorted((b.get("node_triangles") or {}).items()):
            if nname in res["nodes"]:
                add(f"G.node.{nname}", "G", f"Triangles, node {nname}",
                    res["nodes"][nname]["triangles"], lim, "<=", "count", "budget_node")
        add("G.file", "G", "File size", mres["file_bytes"], b.get("file_bytes"), "<=", "bytes",
            "budget_file")
        add("G.nodes", "G", "Nodes", mres["nodes"], b.get("nodes"), "<=", "count",
            "budget_nodes")
        add("G.materials", "G", "Materials", mres["materials"], b.get("materials"), "<=",
            "count", "budget_materials")
        add("G.primitives", "G", "Primitives (draw calls)", mres["primitives"],
            b.get("primitives"), "<=", "count", "budget_primitives")
        out.sort(key=lambda g: g["id"])
        return out


# ---------------------------------------------------------------- report writers


def dump_json(res):
    return json.dumps(res, sort_keys=True, indent=1, ensure_ascii=True, allow_nan=False) + "\n"


def fmt_value(v, unit):
    if v is None:
        return "-"
    if unit == "m":
        return f"{v * 1000:.2f} mm"
    if unit == "deg":
        return f"{v:.2f}°"
    if unit == "frac":
        return f"{v * 100:.2f} %"
    if unit == "bytes":
        return f"{v / 1e6:.2f} MB"
    if unit == "ratio":
        return f"{v:.3f}"
    return f"{v:,}".replace(",", " ")


def mm(v):
    return "-" if v is None else f"{v * 1000:.2f}"


def write_md(res, name, timing=None):
    L = []
    s = res["summary"]
    m = res["model"]
    inp = res["inputs"]
    L.append(f"# Model audit: {name}")
    L.append("")
    ref = inp.get("reference")
    L.append(f"Model `{inp['model']['file']}` ({m['file_bytes'] / 1e6:.2f} MB, sha256 "
             f"`{inp['model']['sha256'][:12]}`)" +
             (f" against reference `{ref['file']}` (sha256 `{ref['sha256'][:12]}`)" if ref
              else " (no reference)") +
             (f", config `{inp['config']['file']}`" if "config" in inp else "") +
             f"; mode **{res['tool']['mode']}**.")
    L.append(f"Coordinates: {res['frame']}.")
    L.append("")
    verdict = "PASS" if s["failed"] == 0 else ("FAIL (hard)" if s["hard_failed"] else "FAIL")
    L.append(f"**{s['gates'] - s['failed']} of {s['gates']} gates pass; {s['failed']} fail "
             f"({s['hard_failed']} hard).** Overall: **{verdict}**.")
    L.append("")
    L.append("| Check | Gate | Value | Limit | Result |")
    L.append("|---|---|---|---|---|")
    for g in res["gates"]:
        resl = "PASS" if g["pass"] else ("**FAIL (hard)**" if g["hard"] else "**FAIL**")
        L.append(f"| {g['check']} | {g['label']} | {fmt_value(g['value'], g['unit'])} | "
                 f"{g['op']} {fmt_value(g['limit'], g['unit'])} | {resl} |")
    L.append("")
    L.append("## Model")
    L.append("")
    L.append(f"{m['triangles']:,} triangles, {m['vertices']:,} vertices "
             f"({m['verts_per_tri']:.2f} per triangle), {m['nodes']} nodes, "
             f"{m['primitives']} primitives (draw calls), {m['materials']} materials; largest "
             f"primitive {m['max_vertices_per_primitive']:,} vertices. Size "
             f"{m['length']:.3f} × {m['span']:.3f} × {m['height']:.3f} m (length × span × "
             f"height). Hidden: {m['hidden']:,} triangles ({m['hidden_frac'] * 100:.2f} %)."
             .replace(",", " "))
    L.append("")
    L.append("## Per node")
    L.append("")
    L.append("Shape and shading over the triangles that can be seen (hidden and flipped ones "
             "are counted by their own checks; the JSON also has the figures over all "
             "triangles).")
    L.append("")
    L.append("| Node | Group | Tris | Verts/tri | Shape p99 / max (mm) | Reverse p99 / max (mm) "
             "| Normal p95 / max (°) | Hidden | Flipped | Sub-pixel | Thin | Slivers |")
    L.append("|---|---|---|---|---|---|---|---|---|---|---|---|")

    def deg2(s):
        return "-" if not s or s["p95"] is None else f"{s['p95']:.2f} / {s['max']:.2f}"
    for name, n in res["nodes"].items():
        sh = n.get("shape")
        rv = n.get("shape_reverse")
        L.append(
            f"| {name} | {n['group']} | {n['triangles']} | {n['verts_per_tri']:.2f} | "
            + (f"{mm(sh['p99'])} / {mm(sh['max'])} | {mm(rv['p99'])} / {mm(rv['max'])} | "
               f"{deg2(n['shading_deg'])} | " if sh else "- | - | - | ")
            + f"{n['hidden_frac'] * 100:.1f} % | {n['flipped']} | "
              f"{n['subpixel_frac'] * 100:.1f} % | {n['thin_frac'] * 100:.1f} % | "
              f"{n['slivers']} |")
    L.append("")
    L.append("Error map (share of each node's area by per-triangle error ÷ the group's p99 "
             "gate; < 0.1 could be coarser, > 1 is too coarse):")
    L.append("")
    L.append("| Node | < 0.1 | 0.1–0.5 | 0.5–1 | > 1 |")
    L.append("|---|---|---|---|---|")
    for name, n in res["nodes"].items():
        b = n.get("error_buckets_area")
        if b:
            L.append(f"| {name} | {b['lt_0.1'] * 100:.1f} % | {b['0.1_0.5'] * 100:.1f} % | "
                     f"{b['0.5_1'] * 100:.1f} % | {b['gt_1'] * 100:.1f} % |")
    L.append("")
    if res["headroom"]:
        L.append("## Simplification headroom")
        L.append("")
        L.append("Large connected pieces, decimated (Blender Decimate, collapse) until the "
                 "shape error exceeds the gate or the piece's own error, whichever is larger.")
        L.append("")
        L.append("| Node | Piece (centre, m) | Tris | Own p99 / max (mm) | Smallest passing ratio "
                 "| Tris kept | Headroom |")
        L.append("|---|---|---|---|---|---|---|")
        for h in res["headroom"]:
            b = h["base"]
            c = h["centre"]
            L.append(f"| {h['node']} | ({c[0]:.2f}, {c[1]:.2f}, {c[2]:.2f}) | {h['triangles']} | "
                     f"{mm(max(b['fwd_p99'], b['rev_p99']))} / "
                     f"{mm(max(b['fwd_max'], b['rev_max']))} | "
                     f"{h['smallest_passing_ratio']} | {h['triangles_kept']} | "
                     f"{h['headroom'] * 100:.1f} % |")
        L.append("")
    for key, title in (("hidden_clusters", "Hidden triangles: largest clusters"),
                       ("flipped_clusters", "Flipped triangles: largest clusters")):
        L.append(f"## {title}")
        L.append("")
        clusters = [c for n in res["nodes"].values() for c in n[key]]
        clusters.sort(key=lambda r: (-r["triangles"], r["node"], r["centre"]))
        if clusters:
            L.append("| Node | Tris | Area (m²) | Centre (x, y, z) m | Size (m) |")
            L.append("|---|---|---|---|---|")
            for c in clusters[:15]:
                L.append(f"| {c['node']} | {c['triangles']} | {c['area']:.4f} | "
                         f"({c['centre'][0]:.2f}, {c['centre'][1]:.2f}, {c['centre'][2]:.2f}) | "
                         f"{c['size'][0]:.2f} × {c['size'][1]:.2f} × {c['size'][2]:.2f} |")
        else:
            L.append("None.")
        L.append("")
    if res["probes"]:
        L.append("## Profiles")
        L.append("")
        L.append("| Probe | Maxima | Minima | Largest kink | Largest step | Excluded narrow parts |")
        L.append("|---|---|---|---|---|---|")
        for pname, p in res["probes"].items():
            ka = p["kink_at"]
            sa = p["step_at"]
            L.append(f"| {pname} | {p['maxima']} | {p['minima']} | {p['kink_deg']:.1f}° at y = "
                     f"{ka[1]:.2f} | {p['step'] * 1000:.1f} mm at y = {sa[1]:.2f} | "
                     f"{len(p['excluded_parts'])} |" if ka and sa else
                     f"| {pname} | {p['maxima']} | {p['minima']} | - | - | "
                     f"{len(p['excluded_parts'])} |")
        L.append("")
    hy = res["hygiene"]
    L.append("## Hygiene")
    L.append("")
    L.append(f"Degenerate {hy['degenerate']}, slivers {hy['slivers']}, duplicate faces "
             f"{hy['duplicates']}, flipped {hy['flipped']} (back side visible "
             f"{hy['flipped_back_visible']}, normals against winding "
             f"{hy['flipped_normals_against_winding']}, normal error > 90° "
             f"{hy['flipped_normal_error_over_90']}), floating islands {hy['floating']}, "
             f"inconsistent winding edges {hy['inconsistent_winding_edges']}, non-manifold edges "
             f"{hy['nonmanifold_edges']}, open (boundary) edges {hy['boundary_edges']}.")
    L.append("")
    if res["warnings"]:
        L.append("## Warnings")
        L.append("")
        for w in res["warnings"]:
            L.append(f"- {w}")
        L.append("")
    if timing:
        L.append("## Runtime")
        L.append("")
        L.append(", ".join(f"{k} {v:.1f} s" for k, v in timing.items()) +
                 " (the only part of this report that changes between runs).")
        L.append("")
    return "\n".join(L)


# ---------------------------------------------------------------- main


def main(argv=None):
    if argv is None:
        argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser(prog="audit_model.py", description=__doc__)
    ap.add_argument("--model", required=True, type=Path)
    ap.add_argument("--reference", type=Path)
    ap.add_argument("--config", type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--name", help="report name (default: config [model].name or file stem)")
    ap.add_argument("--images", action="store_true", help="also render heat-map PNGs")
    ap.add_argument("--fast", action="store_true",
                    help="fewer samples and directions, for iteration (not for checkpoints)")
    ap.add_argument("--strict", action="store_true", help="exit 1 when any gate fails")
    args = ap.parse_args(argv)
    try:
        cfg = load_config(args.config)
        cfg["_path"] = args.config
        name = args.name or cfg["model"].get("name") or args.model.stem
        audit = Audit(args.model, args.reference, cfg, fast=args.fast)
        res = audit.run()
    except (glb_io.GlbError, OSError, tomllib.TOMLDecodeError, KeyError, ValueError) as exc:
        print(f"[audit] error: {exc}", file=sys.stderr)
        return 2
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / f"{name}_audit.json").write_text(dump_json(res))
    timing = dict(audit.timing)
    if args.images:
        t0 = time.perf_counter()
        import audit_render
        audit_render.render_all(audit, args.out, name)
        timing["images"] = time.perf_counter() - t0
    (args.out / f"{name}_audit.md").write_text(write_md(res, name, timing))
    s = res["summary"]
    print(f"[audit] {name}: {s['gates'] - s['failed']}/{s['gates']} gates pass, "
          f"{s['failed']} fail ({s['hard_failed']} hard); {timing['total']:.1f} s; "
          f"report {args.out / (name + '_audit.md')}")
    if s["hard_failed"] or (args.strict and s["failed"]):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
