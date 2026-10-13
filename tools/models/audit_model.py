# Deterministic model audit (task T22c): shape accuracy, shading, needless triangles, hygiene,
# shape plausibility, profile fairness and budgets of a .glb against a denser reference build.
#
#   blender --background --factory-startup --python-exit-code 2 \
#       --python tools/models/audit_model.py -- \
#       --model M.glb --reference R.glb --config tools/models/audit/jet.toml --out DIR \
#       [--images] [--fast] [--strict] [--jobs N]
#
# Writes DIR/<name>_audit.json (byte-identical for the same inputs), DIR/<name>_audit.md
# (PASS/FAIL per gate) and, with --images, heat-map PNGs. Exit status 1 when a hard gate fails
# (any gate with --strict), 2 on bad input or an internal error (a traceback is printed;
# --python-exit-code 2 makes Blender exit 2 too if Python fails before main() runs). On large
# models the visibility rays are cast by N worker processes (Blender again, with
# --visibility-worker); the result does not depend on N. The method is explained in plain
# language in docs/research/04-model-audit.md; scripts/audit_models.sh builds the reference
# and runs this.
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
import traceback
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.dont_write_bytecode = True  # no __pycache__ next to the tools

import glb_io  # noqa: E402

TOOL_VERSION = 2
EPS = 1e-12


class ConfigError(ValueError):
    """A problem in the audit configuration (reported without a traceback, exit 2)."""

# ---------------------------------------------------------------- configuration

DEFAULTS = {
    "model": {"name": None, "published_size": None, "eyepoints": [],
              "glass_materials": ["glass", "hud"], "default_group": None},
    # kmax / vis_kmax cap the sample grid (k subdivisions per edge): 400 x 5 cm = 20 m and
    # 64 x 25 cm = 16 m; longer triangles are sampled more coarsely, with a warning.
    "sampling": {"spacing": 0.05, "inset": 0.01, "weld": 1e-6, "kmax": 400,
                 "vis_spacing": 0.25, "vis_kmax": 64, "vis_inset": 0.001},
    "view": {"width": 1920, "height": 1080, "vfov_deg": 60.0},
    "visibility": {"directions": 162, "ray_offset": 1e-4, "clusters": 10,
                   "parallel_min": 20000, "worker_timeout": 1200.0},
    "headroom": {"ratios": [0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1, 0.05, 0.02, 0.01],
                 "min_component": 2000, "slack": 0.05, "max_reverse_points": 50000},
    "hygiene": {"degenerate_area": 1e-10, "sliver_deg": 2.0, "island_margin": 0.05},
    "reference": {"min_density": 4.0},
    "fast": {"spacing": 0.10, "directions": 42, "reverse_stride": 4,
             "ratios": [0.8, 0.6, 0.4, 0.2, 0.1, 0.05]},
    "gates": {"headroom_max": 0.15, "hidden_max": 0.02, "subpixel_max": 0.10,
              "verts_per_tri_max": 0.65, "index_max": 65535, "degenerate_max": 0,
              "duplicate_max": 0, "flipped_max": 0, "floating_max": 0, "winding_max": 0,
              "sliver_max": 0.01, "size_tolerance": 0.02, "unmeasured_max": 0.02,
              "hard": ["index_max", "degenerate_max", "duplicate_max", "flipped_max",
                       "floating_max", "budget_total"]},
    "budget": {},
    "groups": [],
    "poses": [{"name": "rest"}],
    "probes": [],
}

GROUP_DEFAULTS = {"distance": 8.0, "shape_p99": 0.001, "shape_max": 0.004,
                  "normal_p95_deg": 2.0, "normal_max_deg": 10.0, "required": False}

# maxima / minima: exact counts; max_maxima / max_minima: upper limits (give one of each pair)
PROBE_DEFAULTS = {"direction": [0.0, 0.0, -1.0], "step": 0.005, "materials": None,
                  "min_feature": 0.10, "prominence": 0.002, "segment": 0.05, "pose": "rest",
                  "maxima": None, "minima": None, "max_maxima": None, "max_minima": None,
                  "kink_max_deg": None, "step_max": None, "min_coverage": 0.95}

# A model triangle that lies within this distance of the reference at every sample (and
# within 0.05 deg in normal) coincides with it: on a curved surface its tessellation error
# cannot be measured against this reference ("not measured").
IDENTICAL_DIST = 1e-6

# Keys a config file may use (a misspelt gate key would otherwise leave its gate unchecked
# without a word): section -> keys; array of tables -> (keys, required keys).
SECTION_KEYS = {k: set(v) for k, v in DEFAULTS.items() if isinstance(v, dict)}
SECTION_KEYS["budget"] = {"total", "file_bytes", "nodes", "materials", "primitives", "groups",
                          "node_triangles"}
ENTRY_KEYS = {
    "groups": (set(GROUP_DEFAULTS) | {"name", "nodes"}, ("name", "nodes")),
    "poses": ({"name", "rotate", "hide"}, ("name",)),
    "probes": (set(PROBE_DEFAULTS) | {"name", "start", "end", "nodes"},
               ("name", "start", "end", "nodes")),
}


def check_config_keys(user):
    """Refuse unknown sections and keys, entries without their required keys, two entries
    of one name (their gates would share ids) and zero-length probes (ConfigError)."""
    for sect, val in user.items():
        if sect in SECTION_KEYS:
            if not isinstance(val, dict):
                raise ConfigError(f"[{sect}] must be a table")
            unknown = sorted(set(val) - SECTION_KEYS[sect])
            if unknown:
                raise ConfigError(f"[{sect}]: unknown key(s) {', '.join(unknown)}")
        elif sect in ENTRY_KEYS:
            known, required = ENTRY_KEYS[sect]
            if not isinstance(val, list) or not all(isinstance(e, dict) for e in val):
                raise ConfigError(f"[[{sect}]] must be an array of tables")
            names = []
            for i, e in enumerate(val):
                label = f"[[{sect}]] {e['name']!r}" if "name" in e else f"[[{sect}]] #{i + 1}"
                unknown = sorted(set(e) - known)
                if unknown:
                    raise ConfigError(f"{label}: unknown key(s) {', '.join(unknown)}")
                missing = [k for k in required if k not in e]
                if missing:
                    raise ConfigError(f"{label}: missing {', '.join(missing)}")
                if sect == "probes" and list(e["start"]) == list(e["end"]):
                    raise ConfigError(f"{label}: start and end are the same point")
                names.append(e["name"])
            twice = sorted({n for n in names if names.count(n) > 1})
            if twice:
                raise ConfigError(f"[[{sect}]]: name used more than once: {', '.join(twice)}")
        else:
            raise ConfigError(f"unknown section [{sect}]")


def load_config(path):
    cfg = {k: (dict(v) if isinstance(v, dict) else list(v) if isinstance(v, list) else v)
           for k, v in DEFAULTS.items()}
    if path is not None:
        raw = Path(path).read_bytes()
        user = tomllib.loads(raw.decode("utf-8"))
        check_config_keys(user)
        # the report identifies the config by the bytes actually used
        cfg["_path"], cfg["_sha256"], cfg["_bytes"] = path, hashlib.sha256(raw).hexdigest(), \
            len(raw)
        for k, v in user.items():
            if isinstance(v, dict) and isinstance(cfg.get(k), dict):
                cfg[k] = {**cfg[k], **v}
            else:
                cfg[k] = v
    cfg["groups"] = [{**GROUP_DEFAULTS, **g} for g in cfg["groups"]]
    cfg["probes"] = [{**PROBE_DEFAULTS, **p} for p in cfg["probes"]]
    for p in cfg["probes"]:
        for key in ("maxima", "minima"):
            if p[key] is not None and p[f"max_{key}"] is not None:
                raise ConfigError(f"probe {p.get('name')!r}: give {key} (exact count) or "
                                  f"max_{key} (at most), not both")
    for p in cfg["poses"]:
        if p.get("name") == "rest" and (p.get("rotate") or p.get("hide")):
            raise ConfigError("pose 'rest' is the file's own pose: it cannot rotate or hide "
                              "nodes (give that pose another name)")
    if not cfg["poses"] or cfg["poses"][0].get("name") != "rest":
        cfg["poses"] = [{"name": "rest"}] + [p for p in cfg["poses"] if p.get("name") != "rest"]
    pose_names = [p.get("name") for p in cfg["poses"]]
    for p in cfg["probes"]:
        if p["pose"] not in pose_names:
            raise ConfigError(f"probe {p.get('name')!r}: no pose named {p['pose']!r}")
    return cfg


# ---------------------------------------------------------------- small helpers


def rm(x):
    """Round a length (m) for the report: 1e-6 m."""
    return None if x is None or not math.isfinite(x) else round(float(x), 6)


def rd(x):
    """Round an angle (degrees): 0.01 deg."""
    return None if x is None or not math.isfinite(x) else round(float(x), 2)


def rf(x):
    """Round a fraction (0..1): 0.01 % = 1e-4."""
    return None if x is None or not math.isfinite(x) else round(float(x), 4)


def ra(x):
    """Round an area (m^2): 1 mm^2."""
    return None if x is None or not math.isfinite(x) else round(float(x), 6)


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


def with_descendants(model, names):
    """The node names plus the names of all their descendants (a pose that rotates or hides
    a node moves or hides its whole subtree)."""
    out = set(names)
    stack = [n for n in model.nodes if n.name in out]
    while stack:
        node = stack.pop()
        for c in node.children:
            child = model.nodes[c]
            if child.name not in out:
                out.add(child.name)
                stack.append(child)
    return out


def det3(m):
    """Determinant of a 3x3 matrix (explicit triple product)."""
    return float(np.dot(m[:, 0], np.cross(m[:, 1], m[:, 2])))


class Geo:
    """All triangles of a model in one pose, in the modelling frame.

    Triangles are in file order (primitives in node order, then index order); `src` gives
    each triangle's index in that order with nothing hidden, so triangles of different poses
    can be matched exactly (pose_index_map). A primitive under a node whose world matrix has a
    negative determinant (a mirror) has its winding reversed: glTF 2.0 says front faces are
    clockwise there, so this keeps "front" = the side the normal of (v1 - v0) x (v2 - v0)
    points to, for every triangle."""

    def __init__(self, model, pose=None, glass=()):
        pose = pose or {}
        rotate = pose.get("rotate", {}) or {}
        hide = with_descendants(model, pose.get("hide", []) or [])
        self.hidden_nodes = hide
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
            if node.mesh is None and node.name not in hide:
                self.empties[node.name] = C @ world[node.name][:3, 3]
        tv, tn, tnode, tmat, tprim, tsrc = [], [], [], [], [], []
        self.mirrored = set()
        offset = 0
        for pi, p in enumerate(model.prims):
            first, offset = offset, offset + len(p.indices)
            if p.node in hide:
                continue
            m = world[p.node]
            lin = C @ m[:3, :3]
            pos = mat3_apply(lin, p.positions) + C @ m[:3, 3]
            tri = p.indices
            if det3(m[:3, :3]) < 0:
                self.mirrored.add(p.node)
                tri = tri[:, [0, 2, 1]]
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
            tsrc.append(np.arange(first, offset))
        self.total = offset  # triangles in the file (hidden ones included)
        if tv:
            self.v = np.concatenate(tv)
            self.vn = np.concatenate(tn)
            self.node = np.concatenate(tnode).astype(np.int64)
            self.mat = np.concatenate(tmat).astype(np.int64)
            self.prim = np.concatenate(tprim).astype(np.int64)
            self.src = np.concatenate(tsrc).astype(np.int64)
        else:
            self.v = np.zeros((0, 3, 3))
            self.vn = np.zeros((0, 3, 3))
            self.node = self.mat = self.prim = self.src = np.zeros(0, dtype=np.int64)
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

    def index_map(self, other):
        """For each triangle of this geo, its index in `other` (another pose of the same
        model), or -1 where `other` hides it."""
        inv = np.full(max(self.total, other.total), -1, dtype=np.int64)
        inv[other.src] = np.arange(len(other.src))
        return inv[self.src]

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

    def closed_components(self, geo):
        """Per component: True when every edge is shared by an even number of its triangles
        (a closed surface, so inside/outside is well defined) and none of it is glass."""
        t = self.tris
        ok = (t[:, 0] != t[:, 1]) & (t[:, 1] != t[:, 2]) & (t[:, 2] != t[:, 0])
        closed = np.ones(self.ncomp, dtype=bool)
        if ok.any():
            tt = t[ok]
            a = np.concatenate([tt[:, 0], tt[:, 1], tt[:, 2]])
            b = np.concatenate([tt[:, 1], tt[:, 2], tt[:, 0]])
            owner = np.tile(np.nonzero(ok)[0], 3)
            key = np.stack([np.minimum(a, b), np.maximum(a, b)], axis=1)
            _u, inv = np.unique(key, axis=0, return_inverse=True)
            inv = inv.reshape(-1)
            odd = np.bincount(inv)[inv] % 2 == 1
            closed[np.unique(self.comp[owner[odd]])] = False
        closed[np.unique(self.comp[geo.glass[self.tri_idx]])] = False
        return closed

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
        self.rk = k               # nearest model triangle per reference centroid (index in tri)
        self.rnear = self.tri[k]  # the same as a global triangle index
        self.rcent = rc
        self.rdensity = len(self.rtri) / max(1, len(self.tri))
        # identical-tessellation diagnostic (completed in Audit.set_unmeasured with the soft
        # folds): triangles that coincide with the reference and whose vertex normals spread
        # (smooth-shaded curve) cannot have their tessellation error measured by this reference.
        vn = geo.vn[self.tri]
        self.spread = np.maximum.reduce([angle_deg(vn[:, 0], vn[:, 1]),
                                         angle_deg(vn[:, 1], vn[:, 2]),
                                         angle_deg(vn[:, 2], vn[:, 0])]) if ntri else np.zeros(0)
        self.identical = (self.err < IDENTICAL_DIST) & (self.nerr < 0.05)
        self.unmeasured = np.zeros(ntri, dtype=bool)
        self.runmeasured = np.zeros(len(rc), dtype=bool)

    def set_unmeasured(self, unmeasured):
        """Mark model triangles whose error cannot be measured (see Audit.run), and the
        reference centroids that coincide with one of them (the same triangle in both files):
        both are left out of the shape and shading statistics."""
        self.unmeasured = unmeasured
        if len(self.rk):
            self.runmeasured = unmeasured[self.rk] & (self.rerr < IDENTICAL_DIST)


def decimate(verts, faces, ratios, name="audit_decimate"):
    """Yield (ratio, triangle vertices (T,3,3)) for each ratio: Blender's Decimate modifier,
    collapse mode, triangulated, applied to the mesh (verts, faces) without other options.
    Closing the generator early removes the temporary object."""
    import bpy
    me = bpy.data.meshes.new(name)
    me.from_pydata(np.asarray(verts).tolist(), [], np.asarray(faces).tolist())
    me.validate(verbose=False)
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    mod = obj.modifiers.new("decimate", "DECIMATE")
    mod.decimate_type = "COLLAPSE"
    mod.use_collapse_triangulate = True
    try:
        for r in ratios:
            mod.ratio = r
            dg = bpy.context.evaluated_depsgraph_get()
            dg.update()
            ev = obj.evaluated_get(dg)
            m2 = ev.to_mesh()
            m2.calc_loop_triangles()
            co = np.zeros(len(m2.vertices) * 3)
            m2.vertices.foreach_get("co", co)
            tri = np.zeros(len(m2.loop_triangles) * 3, dtype=np.int64)
            m2.loop_triangles.foreach_get("vertices", tri)
            ev.to_mesh_clear()
            yield r, co.reshape(-1, 3)[tri.reshape(-1, 3)]
    finally:
        bpy.data.objects.remove(obj)
        bpy.data.meshes.remove(me)


def decimate_measure(name, verts, faces, ratios, tree_ref, ref_v, rc_pts, rc_w, thresholds,
                     cfg, spacing, region=None):
    """Decimate a welded component at each ratio (largest first) and re-measure shape
    accuracy (measure_patch, with the same reverse points, weights and region as the
    piece's own measurement); stops at the first ratio that fails. Returns the per-ratio
    records."""
    records = []
    gen = decimate(verts, faces, ratios, f"audit_{name}")
    try:
        for r, tv in gen:
            rec = measure_patch(tv, tree_ref, ref_v, rc_pts, rc_w, cfg, spacing, region=region)
            rec["ratio"] = r
            rec["triangles"] = int(len(tv))
            rec["pass"] = bool(rec["fwd_p99"] <= thresholds[0] and rec["fwd_max"] <= thresholds[1]
                               and rec["rev_p99"] <= thresholds[0]
                               and rec["rev_max"] <= thresholds[1])
            records.append(rec)
            if not rec["pass"]:
                break
    finally:
        gen.close()
    return records


class Patch:
    """Minimal Geo-like wrapper for a loose triangle soup (used by the headroom check)."""

    def __init__(self, tv):
        self.v = tv
        e0 = tv[:, 1] - tv[:, 0]
        e2 = tv[:, 0] - tv[:, 2]
        self.area = 0.5 * norm(cross(e0, -e2))
        self.lmax = np.stack([norm(e0), norm(tv[:, 2] - tv[:, 1]), norm(e2)], axis=1).max(axis=1)


def measure_patch(tv, tree_ref, ref_v, rc_pts, rc_w, cfg, spacing, counted=None,
                  region=None):
    """Shape accuracy of the triangle soup tv against the reference, computed like the A gate:
    forward = area-weighted percentiles of the per-triangle error (largest over its samples);
    reverse = distances of the reference centroids rc_pts to tv, weighted by their triangles'
    areas rc_w. Only part of tv may count, as in the A gate (visible, not flipped, measured):
    `counted` (bool per triangle of tv) selects whole triangles (the undecimated piece);
    `region` = (BVH over the undecimated piece, bool per triangle of it) is used for a
    decimated piece, whose triangles cross the old ones: a sample counts when its nearest
    undecimated triangle counts, a triangle's error is the largest over its counted samples
    and its weight is its area times the share of its samples that count."""
    smp = cfg["sampling"]
    patch = Patch(tv)
    ok = patch.area >= cfg["hygiene"]["degenerate_area"]
    if counted is not None:
        ok &= counted
    keep = np.nonzero(ok)[0]
    if len(keep) == 0:  # nothing left: the piece is gone
        return {"fwd_p99": math.inf, "fwd_max": math.inf, "rev_p99": math.inf,
                "rev_max": math.inf}
    owner, _bary, pts, starts = make_samples(patch, keep, spacing, smp["inset"], smp["kmax"])
    j = nearest(tree_ref, pts)
    a, b, c = ref_v[j, 0], ref_v[j, 1], ref_v[j, 2]
    rb = closest_bary(pts, a, b, c)
    q = rb[:, 0:1] * a + rb[:, 1:2] * b + rb[:, 2:3] * c
    d = norm(pts - q)
    w = patch.area[keep]
    if region is not None:
        tree_old, old_counted = region
        inside = old_counted[nearest(tree_old, pts)]
        err = np.maximum.reduceat(np.where(inside, d, -1.0), starts[:-1])
        share = np.add.reduceat(inside.astype(np.float64), starts[:-1]) / np.diff(starts)
        sel = share > 0
        fwd = wstats(err[sel], (w * share)[sel])
    else:
        err = np.maximum.reduceat(d, starts[:-1])
        fwd = wstats(err, w)
    if fwd["count"] == 0:
        return {"fwd_p99": math.inf, "fwd_max": math.inf, "rev_p99": math.inf,
                "rev_max": math.inf}
    if len(rc_pts):
        live = np.nonzero(patch.area >= cfg["hygiene"]["degenerate_area"])[0]
        pv = tv[live]
        k = nearest(bvh(pv), rc_pts)
        bb = closest_bary(rc_pts, pv[k, 0], pv[k, 1], pv[k, 2])
        q = bb[:, 0:1] * pv[k, 0] + bb[:, 1:2] * pv[k, 1] + bb[:, 2:3] * pv[k, 2]
        rev = wstats(norm(rc_pts - q), rc_w)
    else:
        rev = {"p99": 0.0, "max": 0.0}
    return {"fwd_p99": fwd["p99"], "fwd_max": fwd["max"],
            "rev_p99": rev["p99"], "rev_max": rev["max"]}


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
    some pose. The back side is tested (from far away and from the eyepoints) when the front
    is not visible, and for the triangles in `want_back` (whose normal disagrees with the
    reference) in any case.

    Poses: a pose re-tests only the triangles it can change: those of the nodes it moves or
    hides (with their subtrees) and those whose rays a moved node blocked before. When an
    eyepoint moves in a pose (it hangs under a moved node), every undecided triangle is
    re-tested in that pose.

    Shortcut (exact, not an approximation): when a blocked ray hits a closed occluder piece
    (every edge shared by an even number of its triangles, no glass) and every sample lies
    inside that piece (odd number of crossings along two fixed rays), then every ray from
    every sample must cross the piece, so both sides are hidden without casting the rest.
    Each piece is tried once per triangle.

    `part = (i, n)` tests only every n-th non-degenerate triangle, starting at the i-th (one
    worker's share, see Audit.start_visibility). A triangle's result and its ray counts do not
    depend on any other triangle, so merged() over parts 0..n-1 equals one run over all."""

    PARITY_DIRS = ((0.53766, 0.29129, 0.79123), (-0.45192, 0.83174, -0.32269))

    def __init__(self, model, geos, cfg, fast, want_back=None, gcomp=None, closed=None,
                 part=(0, 1)):
        from mathutils import Vector
        vcfg = cfg["visibility"]
        smp = cfg["sampling"]
        self.ndir = cfg["fast"]["directions"] if fast else vcfg["directions"]
        hemi = hemisphere(self.ndir)
        rest = geos[0]
        T = len(rest.v)
        deg = rest.area < cfg["hygiene"]["degenerate_area"]
        want_back = np.zeros(T, dtype=bool) if want_back is None else want_back
        gcomp = np.zeros(T, dtype=np.int64) if gcomp is None else gcomp
        closed = np.zeros(int(gcomp.max()) + 1 if T else 0, dtype=bool) if closed is None \
            else closed
        closed_set = set(np.nonzero(closed)[0].tolist())
        self.front = np.zeros(T, dtype=bool)
        self.back = np.zeros(T, dtype=bool)
        self.tested = ~deg
        self.rays = 0
        self.enclosed = 0
        self.pose_retests = {}
        self.off = vcfg["ray_offset"]
        self.pdirs = [Vector(d).normalized() for d in self.PARITY_DIRS]
        eyes = [e for e in cfg["model"]["eyepoints"]]
        # samples (rest pose barycentrics; posed points are recomputed per pose)
        idx = np.nonzero(~deg)[0]
        owner, bary, _pts, starts = make_samples(rest, idx, smp["vis_spacing"], smp["inset"],
                                                 smp["vis_kmax"], smp["vis_inset"])
        self.samples = len(owner)
        mine = idx[int(part[0])::int(part[1])]
        hits = {}  # triangle -> set of node ids that blocked a ray (non-visible triangles)
        centre = np.zeros(3)
        for pi, (pose, geo) in enumerate(zip(cfg["poses"], geos)):
            moved = set()
            eye_moved = False
            if pi > 0:
                # children move (and hide) with their parents
                moved_names = with_descendants(
                    model, set((pose.get("rotate") or {}).keys()) | set(pose.get("hide") or []))
                moved = {rest.node_index(n) for n in moved_names if rest.node_index(n) is not None}
                for e in eyes:
                    a, b = rest.empties.get(e), geo.empties.get(e)
                    if (a is None) != (b is None) or (a is not None and not np.array_equal(a, b)):
                        eye_moved = True
            # geo for this pose has hidden nodes removed: map rest triangle -> pose triangle
            pose_map = rest.index_map(geo)
            rest_of = geo.index_map(rest)  # pose triangle -> rest triangle
            occ = np.nonzero(~geo.glass)[0]
            tree = bvh(geo.v[occ]) if len(occ) else None
            occ_node = geo.node[occ].tolist()
            occ_comp = gcomp[rest_of[occ]].tolist()
            lo, hi = geo.v.reshape(-1, 3).min(axis=0), geo.v.reshape(-1, 3).max(axis=0)
            centre = (lo + hi) / 2
            radius = float(norm((hi - lo)[None])[0]) / 2 + 1.0
            reach = 2.0 * radius + 1.0
            eye_pos = [Vector(geo.empties[e].tolist()) for e in eyes if e in geo.empties]
            if pi == 0:
                cand = mine
            else:
                # Re-test only what this pose can change: triangles of moved or hidden nodes,
                # and triangles that some moved node blocked in an earlier pose (a ray that
                # was blocked only by geometry that did not move stays blocked). An eyepoint
                # that moved can see anything new, so then every undecided triangle.
                cand = np.array(sorted(t for t in mine.tolist()
                                       if (not self.front[t] or (want_back[t] and
                                                                 not self.back[t]))
                                       and pose_map[t] >= 0 and
                                       (eye_moved or rest.node[t] in moved or
                                        (hits.get(t, set()) & moved))),
                                dtype=np.int64)
                self.pose_retests[pose["name"]] = int(len(cand))
            if len(cand) == 0 or tree is None:
                if tree is None and pi == 0:
                    self.front[mine] = True
                continue
            pieces = _Pieces(geo, rest_of, occ, gcomp, closed, reach, eye_pos, self)
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
                blockers = hits.setdefault(t, set())
                # closed pieces this triangle's rays may test for enclosure (each once)
                enclose = (closed_set, int(gcomp[t]), set(), pieces, blockers)
                dirs = hemi[:, 0:1] * t1[ci] + hemi[:, 1:2] * t2[ci] + hemi[:, 2:3] * n
                if not self.front[t]:
                    r = self._side(tree, occ_node, occ_comp, pts, n, dirs, reach, eye_pos,
                                   enclose)
                    if r == "enclosed":
                        self.enclosed += 1
                        continue  # every ray from every sample is blocked: both sides
                    if r:
                        self.front[t] = True
                if (not self.front[t] or want_back[t]) and not self.back[t]:
                    # the back side faces the eyepoints too (a reversed cockpit face that only
                    # the pilot can see is flipped, not hidden)
                    r = self._side(tree, occ_node, occ_comp, pts, -n, -dirs, reach, eye_pos,
                                   enclose)
                    if r == "enclosed":
                        self.enclosed += 1
                        continue
                    if r:
                        self.back[t] = True
                if self.front[t] and (self.back[t] or not want_back[t]):
                    hits.pop(t, None)
        self.hidden = self.tested & ~self.front & ~self.back
        self.back_only = self.tested & ~self.front & self.back
        self.centre = centre

    def as_part(self):
        """This run's results as plain arrays (what a worker process hands back)."""
        return {"front": self.front, "back": self.back, "rays": np.int64(self.rays),
                "enclosed": np.int64(self.enclosed), "samples": np.int64(self.samples),
                "retest_names": np.array(sorted(self.pose_retests), dtype=str),
                "retests": np.array([self.pose_retests[k] for k in sorted(self.pose_retests)],
                                    dtype=np.int64)}

    @classmethod
    def merged(cls, parts, rest, cfg, fast):
        """One result from the parts (i, n), i = 0..n-1, of a split run (see `part`)."""
        self = cls.__new__(cls)
        T = len(rest.v)
        self.ndir = cfg["fast"]["directions"] if fast else cfg["visibility"]["directions"]
        self.off = cfg["visibility"]["ray_offset"]
        self.tested = ~(rest.area < cfg["hygiene"]["degenerate_area"])
        self.front = np.zeros(T, dtype=bool)
        self.back = np.zeros(T, dtype=bool)
        self.rays = self.enclosed = 0
        self.pose_retests = {}
        for p in parts:
            self.front |= np.asarray(p["front"], dtype=bool)
            self.back |= np.asarray(p["back"], dtype=bool)
            self.rays += int(p["rays"])
            self.enclosed += int(p["enclosed"])
            for name, count in zip(np.asarray(p["retest_names"]).tolist(),
                                   np.asarray(p["retests"]).tolist()):
                self.pose_retests[name] = self.pose_retests.get(name, 0) + int(count)
        self.samples = int(parts[0]["samples"]) if parts else 0
        self.hidden = self.tested & ~self.front & ~self.back
        self.back_only = self.tested & ~self.front & self.back
        self.centre = None
        return self

    def _side(self, tree, occ_node, occ_comp, pts, n, dirs, reach, eyes, enclose):
        """True when a ray from some sample escapes (or reaches an eyepoint), "enclosed" when
        the samples turn out to lie inside a closed piece that a ray hit, else False.

        The eyepoint rays (one per sample and eyepoint) go first: they are few, and most
        cockpit triangles are seen from an eyepoint. The order changes only how many rays are
        cast, never the answer. `enclose` = (ids of the closed pieces, the triangle's own
        piece, pieces already tried for this triangle, _Pieces, nodes that blocked a ray)."""
        from mathutils import Vector
        closed_set, own, tried, pieces, blockers = enclose
        rc = tree.ray_cast
        nv = Vector(n.tolist())
        off = nv * self.off
        origins = [Vector(p) + off for p in pts.tolist()]
        rays = 0
        try:
            for e in eyes:
                for o in origins:
                    to = e - o
                    dist = to.length
                    if dist < 1e-9 or to.dot(nv) <= 0:
                        continue
                    rays += 1
                    hit = rc(o, to / dist, dist)
                    if hit[0] is None:
                        return True
                    blockers.add(occ_node[hit[2]])
            dl = [Vector(d) for d in dirs.tolist()]
            for o in origins:
                for d in dl:
                    rays += 1
                    hit = rc(o, d, reach)
                    if hit[0] is None:
                        return True
                    k = hit[2]
                    blockers.add(occ_node[k])
                    w = occ_comp[k]
                    if w in closed_set and w != own and w not in tried:
                        tried.add(w)
                        if pieces.encloses(w, pts):
                            blockers.add(pieces.node_of(w))
                            return "enclosed"
            return False
        finally:
            self.rays += rays


class _Pieces:
    """Closed occluder pieces of one pose, for the enclosure shortcut (built lazily)."""

    def __init__(self, geo, rest_of, occ, gcomp, closed, reach, eyes, owner):
        self.geo, self.reach, self.eyes, self.owner = geo, reach, eyes, owner
        self.rest_of, self.occ, self.gcomp, self.closed = rest_of, occ, gcomp, closed
        self.trees = {}
        self.nodes = {}
        self.eye_inside = {}

    def _tree(self, w):
        if w not in self.trees:
            sel = self.occ[self.gcomp[self.rest_of[self.occ]] == w]
            self.trees[w] = bvh(self.geo.v[sel])
            self.nodes[w] = int(self.geo.node[sel[0]])
            # not counted in `rays`: it is done once per piece, not per triangle, so a count
            # would depend on how the triangles are split between worker processes
            self.eye_inside[w] = any(self._inside(self.trees[w], e, count=False)
                                     for e in self.eyes)
        return self.trees[w]

    def node_of(self, w):
        self._tree(w)
        return self.nodes[w]

    def _crossings(self, tree, p, d, count=True):
        """Number of surface crossings along the ray, or None when the start point is
        within 0.1 mm of the surface (inside/outside not decidable there)."""
        n, o = 0, p
        for _ in range(10000):
            if count:
                self.owner.rays += 1
            hit = tree.ray_cast(o, d, self.reach)
            if hit[0] is None:
                return n
            if n == 0 and hit[3] < 1e-4:
                return None
            n += 1
            o = hit[0] + d * 2e-5
        return None

    def _inside(self, tree, p, count=True):
        counts = [self._crossings(tree, p, d, count) for d in self.owner.pdirs]
        return all(c is not None and c % 2 == 1 for c in counts)

    def encloses(self, w, pts):
        from mathutils import Vector
        tree = self._tree(w)
        if self.eye_inside[w]:
            return False
        return all(self._inside(tree, Vector(p)) for p in pts.tolist())


# ---------------------------------------------------------------- hygiene


def hygiene(geo, topo, cfg, node_match):
    h = cfg["hygiene"]
    deg = geo.area < h["degenerate_area"]
    ang = min_angle_deg(geo)
    sliver = ~deg & (ang < h["sliver_deg"])
    out = {"degenerate": int(deg.sum()), "slivers": int(sliver.sum())}
    dup = 0
    back_to_back = 0
    wind = 0
    nonmanifold = 0
    boundary = 0
    for name, tp in topo.items():
        tris = tp.tris
        ok = (tris[:, 0] != tris[:, 1]) & (tris[:, 1] != tris[:, 2]) & (tris[:, 2] != tris[:, 0])
        t = tris[ok]
        if len(t) == 0:
            continue
        # Duplicates: the same three corners in the same cyclic order (same winding) is a
        # fault (two coincident surfaces facing the same way flicker). The same corners in
        # the opposite order are back-to-back faces, a valid way to make a thin part seen
        # from both sides with back-face culling: counted (pairs), not a fault.
        r = np.argmin(t, axis=1)
        canon = np.take_along_axis(t, (r[:, None] + np.arange(3)[None, :]) % 3, axis=1)
        uniq = np.unique(canon, axis=0)
        dup += len(canon) - len(uniq)
        both = np.unique(np.concatenate([uniq, uniq[:, [0, 2, 1]]]), axis=0, return_counts=True)
        back_to_back += int((both[1] == 2).sum()) // 2
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
    out["back_to_back_pairs"] = int(back_to_back)
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
    `margin`) the main cluster of geometry (the cluster with the most islands). Boxes are
    swept in order of their lowest x, as there, with the overlap tests vectorised."""
    los, his, names = [], [], []
    for name in sorted(topo):
        tp = topo[name]
        v = geo.v[tp.tri_idx]
        lo = np.full((tp.ncomp, 3), np.inf)
        hi = np.full((tp.ncomp, 3), -np.inf)
        for k in range(3):
            np.minimum.at(lo, tp.comp, v[:, k])
            np.maximum.at(hi, tp.comp, v[:, k])
        los.append(lo)
        his.append(hi)
        names += [name] * tp.ncomp
    if not names:
        return []
    lo, hi = np.concatenate(los), np.concatenate(his)
    n = len(lo)
    order = np.lexsort((np.arange(n), lo[:, 0]))
    slo, shi = lo[order], hi[order]
    parent = np.arange(n)

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    ends = np.searchsorted(slo[:, 0], shi[:, 0] + margin, side="right")
    for ii in range(n):
        j0, j1 = ii + 1, ends[ii]
        if j1 <= j0:
            continue
        ok = np.all((slo[j0:j1] <= shi[ii] + margin) & (slo[ii] <= shi[j0:j1] + margin), axis=1)
        for jj in (np.nonzero(ok)[0] + j0).tolist():
            ri, rj = find(int(order[ii])), find(int(order[jj]))
            if ri != rj:
                parent[max(ri, rj)] = min(ri, rj)
    roots = np.array([find(i) for i in range(n)])
    ids, counts = np.unique(roots, return_counts=True)
    main = ids[np.lexsort((ids, -counts))[0]]
    out = []
    for i in np.nonzero(roots != main)[0].tolist():
        out.append({"node": names[i], "centre": vec((lo[i] + hi[i]) / 2),
                    "size": vec(hi[i] - lo[i])})
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
    slope between the two `seg`-long chords meeting at a sample) and the largest step: the
    change of height between neighbouring samples i and i + 1 that the outline's slope does
    not explain, min(|dh - ds * slope|) over the chords just left and just right of the pair
    (signed, so a drop on a rising outline counts in full). Locations are sample indices (a
    step sits between index i and i + 1). A figure is None when the outline is too short to
    compute it (fewer than 3 samples for extrema, 2 * seg / step + 1 for the kink, one more
    for the step)."""
    out = {"maxima": None, "minima": None, "kink_deg": None, "kink_i": None, "step": None,
           "step_i": None}
    n = len(h)
    if n >= 3:
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
        dh = h[i + 1] - h[i]
        ds = s[i + 1] - s[i]
        sl = (h[i] - h[i - w]) / (s[i] - s[i - w])
        sr = (h[i + 1 + w] - h[i + 1]) / (s[i + 1 + w] - s[i + 1])
        res = np.minimum(np.abs(dh - ds * sl), np.abs(dh - ds * sr))
        j = int(np.argmax(res))
        out["step"] = float(res[j])
        out["step_i"] = int(i[j])
    return out


def run_probe(probe, geo, topo, cfg, rest=None):
    """Trace one probe on `geo` (the probe's pose; `rest` = the rest-pose geo that `topo`
    indexes, needed when they differ)."""
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
    # topo indexes rest-pose triangles; map them to this pose (-1 = hidden here)
    pmap = rest.index_map(geo) if rest is not None and rest is not geo else None
    comp_tris = {}
    for name in probe["nodes"]:
        tp = topo.get(name)
        if tp is None:
            continue
        idx = tp.tri_idx if pmap is None else pmap[tp.tri_idx]
        live = idx >= 0
        if not live.any():
            continue  # the node is hidden in this pose
        v = geo.v[idx[live]]
        comp = tp.comp[live]
        proj = dot(v, u[None, None, :])
        for c in np.unique(comp).tolist():
            m = comp == c
            ext = float(proj[m].max() - proj[m].min())
            tris = idx[live][m]
            comp_tris[(name, c)] = tris
            if ext < probe["min_feature"]:
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
        v = geo.v[comp_tris[(name, c)]].reshape(-1, 3)
        lo, hi = v.min(axis=0), v.max(axis=0)
        ex.append({"node": name, "centre": vec((lo + hi) / 2), "size": vec(hi - lo)})

    def at(i, j=None):
        if i is None:
            return None
        return vec(pv[i] if j is None else (pv[i] + pv[j]) / 2)

    def count(x):
        return None if x is None else len(x)
    return {
        "maxima": count(res["maxima"]), "minima": count(res["minima"]),
        "maxima_at": [at(i) for i in res["maxima"] or []],
        "minima_at": [at(i) for i in res["minima"] or []],
        "kink_deg": rd(res["kink_deg"]), "kink_at": at(res["kink_i"]),
        "step": rm(res["step"]),
        "step_at": at(res["step_i"], None if res["step_i"] is None else res["step_i"] + 1),
        "samples": int(nstep), "hits": int(ok.sum()), "gaps": gaps,
        "coverage": rf(ok.sum() / max(1, nstep)), "excluded_parts": ex,
        "profile": [[rm(a), rm(b)] for a, b in zip(sv, hv)],
    }, (s, h, start, u, d, hitp)


# ---------------------------------------------------------------- the audit


class Audit:
    def __init__(self, model_path, ref_path, cfg, fast=False, log=print, jobs=1):
        self.cfg = cfg
        self.fast = fast
        self.log = log
        self.jobs = max(1, int(jobs))  # worker processes for the visibility check
        self.vis_workers = 0           # how many actually ran (0 = in this process)
        self.worker_command = None     # tests only: replaces the Blender command line
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
        self.check_inputs(model, geo)
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
        self.no_reference = {}  # model node -> why its shape and shading are not measured
        self.ref_only = []      # reference nodes the model does not have
        dmin = cfg["hygiene"]["degenerate_area"]
        if rgeo is not None:
            for name in sorted(topo):
                ri = rgeo.node_index(name)
                if ri is None or not (rgeo.node == ri).any():
                    self.no_reference[name] = "no reference node of that name"
                    continue
                if not ((rgeo.node == ri) & (rgeo.area >= dmin)).any():
                    self.no_reference[name] = "the reference node has no usable triangles"
                    continue
                if not ((geo.node == geo.node_index(name)) & (geo.area >= dmin)).any():
                    continue  # only degenerate triangles: check D reports them
                m = NodeMatch(geo, rgeo, name, cfg, self.fast)
                matches[name] = m
                self.err[m.tri] = m.err
                self.nerr[m.tri] = m.nerr
                self.has_ref[m.tri] = True
                tp = topo[name]
                pos = np.full(len(geo.v), -1, dtype=np.int64)
                pos[tp.tri_idx] = np.arange(len(tp.tri_idx))
                fold = tp.soft_folds(geo)[pos[m.tri]]
                m.set_unmeasured(m.identical & ((m.spread > 1.0) | fold))
                self.unmeasured[m.tri] = m.unmeasured
                if m.rdensity < cfg["reference"]["min_density"]:
                    self.warnings.append(
                        f"node {name}: reference has only {m.rdensity:.1f}x the model's "
                        f"triangles (want >= {cfg['reference']['min_density']:g}x); its "
                        "errors are underestimated")
            for name in sorted(rgeo.node_names):
                if (rgeo.node == rgeo.node_index(name)).any() and name not in topo:
                    self.ref_only.append(name)
            for name, why in sorted(self.no_reference.items()):
                self.warnings.append(f"node {name}: {why}; shape and shading not measured")
            for name in self.ref_only:
                self.warnings.append(f"reference node {name} is missing from the model")
        else:
            self.warnings.append("no reference given: checks A, B and the headroom are not "
                                 "measured")
        self.matches = matches
        self.tick("shape+shading", t0)

        # C: visibility (the back side is also tested where the normal disagrees with the
        # reference by more than 90 degrees, to tell which of the two is flipped). On a large
        # model worker processes cast most of the rays while this one casts a share.
        t0 = time.perf_counter()
        self.ref_flip = self.has_ref & (self.nerr > 90.0)
        gcomp = np.zeros(len(geo.v), dtype=np.int64)
        closed, offset = [], 0
        for name in sorted(topo):
            tp = topo[name]
            gcomp[tp.tri_idx] = tp.comp + offset
            closed.append(tp.closed_components(geo))
            offset += tp.ncomp
        closed = np.concatenate(closed) if closed else np.zeros(0, dtype=bool)
        vis_inputs = (model, geos, self.ref_flip, gcomp, closed)
        job = self.start_visibility(*vis_inputs)
        self.vis = self.finish_visibility(job, *vis_inputs)
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
        # What checks A and B (and the headroom) measure: triangles that can be seen, are not
        # flipped (hidden and flipped ones have their own checks) and whose error this
        # reference can measure.
        self.seen = vis.front & ~self.flipped_any
        self.counted = self.seen & self.has_ref & ~self.unmeasured
        self.tick("hygiene", t0)

        # C: headroom (after the visibility: it measures the same triangles as the A gate)
        t0 = time.perf_counter()
        self.headroom = self.run_headroom(geo, rgeo, topo, matches, groups)
        self.tick("headroom", t0)

        # F
        t0 = time.perf_counter()
        self.probes = {}
        self.probe_raw = {}
        for probe in cfg["probes"]:
            pg = geos[[p["name"] for p in cfg["poses"]].index(probe["pose"])]
            res, raw = run_probe(probe, pg, topo, cfg, rest=geo)
            self.probes[probe["name"]] = res
            self.probe_raw[probe["name"]] = raw
        self.tick("profiles", t0)

        self.result = self.assemble()
        self.timing["total"] = time.perf_counter() - t_all
        return self.result

    # -------------------------------------------------------------- visibility workers

    def check_inputs(self, model, geo):
        """Warnings about input that the audit would otherwise treat silently."""
        cfg = self.cfg
        glass = set(cfg["model"]["glass_materials"])
        used = {p.material for p in model.prims}
        for mat in model.materials:
            if mat["name"] in used and mat["alpha_mode"] == "BLEND" and mat["name"] not in glass:
                self.warnings.append(
                    f"material {mat['name']}: alphaMode BLEND but not listed in [model] "
                    "glass_materials, so the audit treats it as opaque (it blocks the "
                    "visibility rays)")
        for name in sorted(geo.mirrored):
            self.warnings.append(
                f"node {name}: its world matrix mirrors (negative determinant), so by glTF 2.0 "
                "its front faces are clockwise and the audit reverses its winding; raylib 6.0 "
                "bakes node transforms into the vertices without reversing the winding "
                "(rmodels.c, LoadGLTF), so the viewer would show this node inside out: "
                "mirror the mesh data instead")
        smp = cfg["sampling"]
        live = geo.lmax[geo.area >= cfg["hygiene"]["degenerate_area"]]
        spacing = cfg["fast"]["spacing"] if self.fast else smp["spacing"]
        for key, sp, kmax, what in (("shape", spacing, smp["kmax"], "shape samples"),
                                    ("vis", smp["vis_spacing"], smp["vis_kmax"],
                                     "visibility samples")):
            n = int((np.ceil(live / sp) > kmax).sum())
            if n:
                self.warnings.append(
                    f"{n} triangles are longer than {kmax * sp:g} m: their {what} are further "
                    f"apart than {sp:g} m ([sampling] {'kmax' if key == 'shape' else 'vis_kmax'}"
                    f" = {kmax})")

    def start_visibility(self, model, geos, want_back, gcomp, closed):
        """Start `jobs` Blender processes for parts 1..jobs of the visibility check split
        into jobs + 1 parts (Visibility `part`; finish_visibility() computes part 0 in this
        process meanwhile and collects the others), when jobs > 1 and the model has at least
        [visibility] parallel_min triangles. Returns None to run it all in this process."""
        cfg = self.cfg
        if self.jobs < 2 or len(geos[0].v) < cfg["visibility"]["parallel_min"]:
            return None
        command = self.worker_command
        if command is None:
            try:
                import bpy
            except ImportError:
                return None
            command = [bpy.app.binary_path, "--background", "--factory-startup",
                       "--python-exit-code", "2", "--python", str(Path(__file__).resolve()),
                       "--"]
        import shutil
        import subprocess
        import tempfile
        tmp = Path(tempfile.mkdtemp(prefix="audit_visibility_"))
        procs = []
        n = self.jobs + 1  # worker i does part i + 1; this process does part 0
        try:
            np.save(tmp / "want_back.npy", want_back)
            np.save(tmp / "gcomp.npy", gcomp)
            np.save(tmp / "closed.npy", closed)
            spec = {"model": str(self.model_path.resolve()), "sha256": self.model.sha256,
                    "config": {k: v for k, v in cfg.items() if not k.startswith("_")},
                    "fast": self.fast, "dir": str(tmp), "parts": n}
            for i in range(1, n):
                (tmp / f"spec{i}.json").write_text(json.dumps({**spec, "part": i}))
                with open(tmp / f"log{i}.txt", "w") as log:
                    procs.append(subprocess.Popen(
                        command + ["--visibility-worker", str(tmp / f"spec{i}.json")],
                        stdout=log, stderr=subprocess.STDOUT))
        except OSError as exc:  # could not start them: run the check in this process
            print(f"[audit] could not start visibility workers ({exc}); running here",
                  file=sys.stderr)
            for proc in procs:
                proc.kill()
                proc.wait()
            shutil.rmtree(tmp, ignore_errors=True)
            return None
        return {"dir": tmp, "procs": procs, "parts": n, "start": time.monotonic()}

    def finish_visibility(self, job, model, geos, want_back, gcomp, closed):
        """The visibility result: computed here (job None), or part 0 here and the others
        merged from the workers. A part whose worker failed, or did not finish within
        [visibility] worker_timeout seconds of being started, is computed here instead, so
        the result is the same."""
        cfg = self.cfg
        if job is None:
            return Visibility(model, geos, cfg, self.fast, want_back=want_back, gcomp=gcomp,
                              closed=closed)
        import shutil
        import subprocess
        n = job["parts"]
        worked = 0
        deadline = job["start"] + float(cfg["visibility"]["worker_timeout"])
        try:
            parts = [Visibility(model, geos, cfg, self.fast, want_back=want_back, gcomp=gcomp,
                                closed=closed, part=(0, n)).as_part()]
            for i, proc in enumerate(job["procs"], start=1):
                try:
                    code = proc.wait(timeout=max(0.0, deadline - time.monotonic()))
                    why = f"failed (exit {code})"
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
                    code = None
                    why = (f"timed out ({cfg['visibility']['worker_timeout']:g} s, "
                           "[visibility] worker_timeout)")
                part = None
                if code == 0:
                    try:
                        with np.load(job["dir"] / f"part{i}.npz") as z:
                            part = {k: z[k] for k in z.files}
                        worked += 1
                    except (OSError, ValueError):
                        part = None
                if part is None:
                    tail = (job["dir"] / f"log{i}.txt").read_text(errors="replace")[-1500:]
                    print(f"[audit] visibility worker {i}/{n - 1} {why}; computing its part "
                          f"here. Its log ends:\n{tail}", file=sys.stderr)
                    part = Visibility(model, geos, cfg, self.fast, want_back=want_back,
                                      gcomp=gcomp, closed=closed, part=(i, n)).as_part()
                parts.append(part)
        finally:
            for proc in job["procs"]:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
            shutil.rmtree(job["dir"], ignore_errors=True)
        self.vis_workers = worked
        return Visibility.merged(parts, geos[0], cfg, self.fast)

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
            if present and absent:
                self.warnings.append(f"group {g['name']}: listed nodes not in the model: "
                                     f"{', '.join(absent)}")
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
            # the reverse points the A gate uses: nearest model triangle seen, not coincident
            rok = self.seen[m.rnear] & ~m.runmeasured
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
                lo, hi = verts.min(axis=0), verts.max(axis=0)
                rec = {"node": name, "component": int(c), "triangles": int(len(gtris)),
                       "centre": vec((lo + hi) / 2), "size": vec(hi - lo)}
                counted = self.counted[gtris]
                if not counted.any():
                    out.append({**rec, "skipped": "no triangle of the piece is measured by the A "
                                                  "gate (all hidden, flipped or not measured)"})
                    continue
                sel = (comp_of_ref == c) & rok
                rc_pts, rc_w = m.rcent[sel], m.rarea[sel]
                cap = hcfg.get("max_reverse_points", 50000)
                if len(rc_pts) > cap:  # an even, deterministic subset keeps this fast
                    stride = int(math.ceil(len(rc_pts) / cap))
                    rc_pts, rc_w = rc_pts[::stride], rc_w[::stride]
                # The piece's own error, over the triangles the A gate measures; a decimated
                # piece is measured over the same part of the surface (region).
                base = measure_patch(geo.v[gtris], m.tree, rgeo.v[m.rtri], rc_pts, rc_w, cfg,
                                     spacing, counted=counted)
                region = None if counted.all() else (bvh(geo.v[gtris]), counted)
                # Pass mark: the group gate, or (for a piece that already misses it) the
                # piece's own error plus a 5 % slack, so that removing triangles may not make
                # a piece noticeably worse than it is.
                slack = hcfg.get("slack", 0.05)
                thr = (max(g["shape_p99"], (1 + slack) * max(base["fwd_p99"], base["rev_p99"])),
                       max(g["shape_max"], (1 + slack) * max(base["fwd_max"], base["rev_max"])))
                recs = decimate_measure(f"{name}_{c}", verts, faces, ratios, m.tree,
                                        rgeo.v[m.rtri], rc_pts, rc_w, thr, cfg, spacing,
                                        region=region)
                passing = [r for r in recs if r["pass"]]
                best = passing[-1] if passing else None
                kept = best["triangles"] if best else len(gtris)
                out.append({
                    **rec,
                    "measured_triangles": int(counted.sum()),
                    "reverse_points": int(len(rc_pts)),
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
        # identified by the bytes that were measured (hashed when they were read)
        for key, path, glb in (("model", self.model_path, self.model),
                               ("reference", self.ref_path, self.ref)):
            if glb is not None:
                res["inputs"][key] = {"file": Path(path).name, "sha256": glb.sha256,
                                      "bytes": int(glb.size)}
        if cfg.get("_path") is not None:
            res["inputs"]["config"] = {"file": Path(cfg["_path"]).name,
                                       "sha256": cfg["_sha256"], "bytes": cfg["_bytes"]}
        res["settings"] = {
            "sample_spacing": rm(cfg["fast"]["spacing"] if self.fast
                                 else cfg["sampling"]["spacing"]),
            "directions": vis.ndir, "visibility_samples": vis.samples, "rays": vis.rays,
            "enclosed_shortcuts": vis.enclosed,
            "poses": [p["name"] for p in cfg["poses"]], "pose_retests": vis.pose_retests,
        }

        # ---- per node
        nodes = {}
        T = len(geo.v)
        area = geo.area
        for name in sorted(self.topo):
            tp = self.topo[name]
            ti = tp.tri_idx
            g = self.group_of(name)
            p = 2.0 * g["distance"] * math.tan(math.radians(view["vfov_deg"]) / 2) / view[
                "height"]
            alt = 2.0 * area[ti] / np.maximum(geo.lmax[ti], EPS)
            prims = [pr for pr in model.prims if pr.node == name]
            nv = sum(pr.vertex_count for pr in prims)
            tested = int(vis.tested[ti].sum())  # degenerate triangles are not tested
            entry = {
                "group": g["name"], "triangles": int(len(ti)), "vertices": int(nv),
                "verts_per_tri": rf(nv / max(1, len(ti))), "area": ra(area[ti].sum()),
                "components": int(tp.ncomp),
                "hidden": int(vis.hidden[ti].sum()),
                "hidden_frac": rf(vis.hidden[ti].sum() / tested) if tested else None,
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
                entry.update(self.ab_stats([m], g, counts=True))
                entry["reference_triangles"] = int(len(m.rtri))
                entry["reference_density"] = round(m.rdensity, 2)
                entry["samples"] = int(m.samples)
                entry["reprojected_samples"] = int(m.reprojected)
                um = m.unmeasured
                if um.any():
                    frac = entry["unmeasured_area_frac"]
                    self.warnings.append(
                        f"node {name}: {int(um.sum())} triangles on curved surfaces"
                        + (f" ({frac * 100:.1f} % of its visible area)" if frac is not None
                           else "")
                        + " are tessellated exactly as in the reference, so their shape error "
                        "is not measured and they are left out of checks A and B (the "
                        "generator does not scale them with --detail)")
            elif name in self.no_reference:
                entry["not_measured"] = self.no_reference[name]
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
            prims.append({"node": pr.node, "material": pr.material, "primitive": int(pr.index),
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
                grp_out[g["name"]] = {"present": [], "absent": g["absent"],
                                      "required": bool(g["required"])}
                continue
            ti = np.nonzero(np.isin(geo.node, [geo.node_index(n) for n in g["present"]]))[0]
            p = 2.0 * g["distance"] * math.tan(math.radians(view["vfov_deg"]) / 2) / view[
                "height"]
            alt = 2.0 * area[ti] / np.maximum(geo.lmax[ti], EPS)
            entry = {"present": g["present"], "absent": g["absent"],
                     "required": bool(g["required"]),
                     "triangles": int(len(ti)), "pixel_size": rm(p),
                     "design_distance": g["distance"],
                     "subpixel_frac": rf((area[ti] < p * p).sum() / max(1, len(ti))),
                     "thin_frac": rf((alt < 0.5 * p).sum() / max(1, len(ti)))}
            ms = [self.matches[n] for n in g["present"] if n in self.matches]
            if self.rgeo is None:
                entry["not_measured"] = "no reference given"
            elif not ms:
                why = [f"{n}: {self.no_reference.get(n, 'only degenerate triangles')}"
                       for n in g["present"]]
                entry["not_measured"] = ("no node of the group can be compared with the "
                                         "reference (" + "; ".join(why) + ")")
            else:
                entry.update(self.ab_stats(ms, g))
                if entry["shape"]["count"] == 0:
                    entry["not_measured"] = ("nothing to measure: every triangle is hidden, "
                                             "flipped or not measured")
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
            "hidden": int(vis.hidden.sum()),
            "hidden_frac": rf(vis.hidden.sum() / max(1, int(vis.tested.sum()))),
            "empties": {k: vec(v) for k, v in sorted(geo.empties.items())},
            "mirrored_nodes": sorted(geo.mirrored),
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
        failing = [g for g in res["gates"] if g["status"] in ("fail", "not_measured")]
        res["summary"] = {
            "gates": len(res["gates"]),
            "passed": sum(1 for g in res["gates"] if g["status"] == "pass"),
            "failed": len(failing),
            "hard_failed": sum(1 for g in failing if g["hard"]),
            "not_measured": sum(1 for g in failing if g["status"] == "not_measured"),
            "absent": sum(1 for g in res["gates"] if g["status"] == "absent"),
        }
        return res

    def ab_stats(self, ms, g, counts=False):
        """Checks A and B over the matched nodes `ms` (NodeMatch list) of group `g`.

        The gate figures use only the triangles that are seen (front visible, not flipped:
        hidden and flipped ones have their own checks) and measured (not coinciding with the
        reference on a curved surface, see set_unmeasured), and the reverse points whose
        nearest model triangle is seen and that do not coincide with it. "_all" = every
        measured triangle, hidden and flipped ones included."""
        area = self.geo.area
        tri = np.concatenate([m.tri for m in ms])
        err = np.concatenate([m.err for m in ms])
        nerr = np.concatenate([m.nerr for m in ms])
        unm = np.concatenate([m.unmeasured for m in ms])
        rerr = np.concatenate([m.rerr for m in ms])
        rw = np.concatenate([m.rarea for m in ms])
        rnear = np.concatenate([m.rnear for m in ms])
        runm = np.concatenate([m.runmeasured for m in ms])
        w = area[tri]
        seen = self.seen[tri]
        ok = seen & ~unm
        rok = self.seen[rnear] & ~runm
        okb = ok & ~self.ref_wrong[tri]  # where the reference is the flipped one
        meas = ~unm
        vis_area = float(w[seen].sum())
        out = {
            "shape": round_stats(wstats(err[ok], w[ok]), rm),
            "shape_reverse": round_stats(wstats(rerr[rok], rw[rok]), rm),
            "shading_deg": round_stats(wstats(nerr[okb], w[okb]), rd),
            "shape_all": round_stats(wstats(err[meas], w[meas]), rm),
            "shading_deg_all": round_stats(wstats(nerr[meas], w[meas]), rd),
            "measured_triangles": int(ok.sum()),
            "unmeasured_triangles": int(unm.sum()),
            "visible_area": ra(vis_area),
            "unmeasured_area_frac": rf(w[seen & unm].sum() / vis_area) if vis_area > 0
            else None,
            "error_buckets_area": self.buckets(err[ok] / g["shape_p99"], w[ok]),
        }
        if counts:
            out["error_buckets_count"] = self.buckets(err[ok] / g["shape_p99"],
                                                      np.ones(int(ok.sum())))
        return out

    def examples(self, mask, n=10):
        idx = np.nonzero(mask)[0][:n]
        geo = self.geo
        return [{"node": geo.node_names[geo.node[t]], "centre": vec(geo.v[t].mean(axis=0))}
                for t in idx.tolist()]

    @staticmethod
    def buckets(ratio, w):
        """Share of the weight w by error / gate: < 0.1, 0.1-0.5, 0.5-1, > 1 (None when
        nothing was measured)."""
        if len(ratio) == 0 or float(np.sum(w)) <= 0:
            return None
        edges = [0.1, 0.5, 1.0]
        b = np.digitize(ratio, edges)
        tot = float(np.sum(w))
        names = ["lt_0.1", "0.1_0.5", "0.5_1", "gt_1"]
        return {names[i]: rf(np.sum(w[b == i]) / tot) for i in range(4)}

    def gates(self, res):
        cfg = self.cfg
        gc = cfg["gates"]
        hard = set(gc.get("hard", []))
        out = []

        def add(gid, check, label, value, limit, op, unit, key, status=None, reason=None):
            """One gate row. status: "pass" / "fail"; "not_measured" (a FAIL: the value
            could not be measured, `reason` says why); "absent" (a group that is not in the
            model and not required: neither a pass nor a fail). Gates without a limit in the
            config are not checked (no row), unless a status is given."""
            if status is None:
                if limit is None:
                    return
                if value is None:
                    status = "not_measured"
                else:
                    ok = value <= limit if op == "<=" else value >= limit if op == ">=" else \
                        value == limit
                    status = "pass" if ok else "fail"
            row = {"id": gid, "check": check, "label": label, "value": value, "limit": limit,
                   "op": op, "unit": unit, "status": status, "pass": status == "pass",
                   "hard": key in hard}
            if status in ("not_measured", "absent"):
                row["reason"] = reason or "nothing to measure"
            out.append(row)
        for g in self.groups:
            name = g["name"]
            ge = res["groups"].get(name, {})
            present = len(g["present"])
            if present == 0:
                add(f"A.{name}.present", "A", f"Group {name}: nodes in the model", 0, 1, ">=",
                    "count", "group_present", status="fail" if g["required"] else "absent",
                    reason="none of its nodes is in the model" +
                    ("" if g["required"] else " (the group is not required)"))
                continue
            add(f"A.{name}.present", "A", f"Group {name}: nodes in the model", present, 1, ">=",
                "count", "group_present")
            why = ge.get("not_measured")

            def val(key, field):
                return (ge.get(key) or {}).get(field)
            add(f"A.{name}.p99", "A", f"Shape accuracy {name}: model to reference p99",
                val("shape", "p99"), g["shape_p99"], "<=", "m", "shape", reason=why)
            add(f"A.{name}.max", "A", f"Shape accuracy {name}: model to reference max",
                val("shape", "max"), g["shape_max"], "<=", "m", "shape", reason=why)
            add(f"A.{name}.rev_p99", "A", f"Shape accuracy {name}: reference to model p99",
                val("shape_reverse", "p99"), g["shape_p99"], "<=", "m", "shape", reason=why)
            add(f"A.{name}.rev_max", "A", f"Shape accuracy {name}: reference to model max",
                val("shape_reverse", "max"), g["shape_max"], "<=", "m", "shape", reason=why)
            add(f"A.{name}.unmeasured", "A",
                f"Not measured {name}: share of the visible area", ge.get("unmeasured_area_frac"),
                gc.get("unmeasured_max"), "<=", "frac", "unmeasured_max", reason=why)
            add(f"B.{name}.p95", "B", f"Shading {name}: normal error p95",
                val("shading_deg", "p95"), g["normal_p95_deg"], "<=", "deg", "shading",
                reason=why)
            add(f"B.{name}.max", "B", f"Shading {name}: normal error max",
                val("shading_deg", "max"), g["normal_max_deg"], "<=", "deg", "shading",
                reason=why)
        # nodes that the two files do not both have
        for name, why in sorted(self.no_reference.items()):
            add(f"A.node.{name}", "A", f"Node {name}: shape and shading", None, None, "", "",
                "shape", status="not_measured", reason=why)
        for name in self.ref_only:
            add(f"A.node.{name}", "A", f"Node {name}: in the reference, not in the model", None,
                None, "", "", "shape", status="fail")
        for h in res["headroom"]:
            if "skipped" in h:
                continue
            add(f"C.headroom.{h['node']}.{h['component']}", "C",
                f"Simplification headroom {h['node']} component {h['component']} "
                f"({h['triangles']} tris)", h["headroom"], gc["headroom_max"], "<=", "frac",
                "headroom_max")
        for name, n in res["nodes"].items():
            add(f"C.hidden.{name}", "C", f"Hidden triangles {name}", n["hidden_frac"],
                gc["hidden_max"], "<=", "frac", "hidden_max",
                reason="no triangle to test (all degenerate)")
        for g in self.groups:
            ge = res["groups"].get(g["name"], {})
            if "subpixel_frac" in ge:
                add(f"C.subpixel.{g['name']}", "C",
                    f"Sub-pixel triangles {g['name']} at {g['distance']:g} m",
                    ge["subpixel_frac"], gc["subpixel_max"], "<=", "frac", "subpixel_max")
        mres = res["model"]
        add("C.verts_per_tri", "C", "Vertices per triangle", mres["verts_per_tri"],
            gc["verts_per_tri_max"], "<=", "ratio", "verts_per_tri_max")
        pairs = {}
        for pr in res["primitives"]:
            key = (pr["node"], pr["material"])
            pairs[key] = pairs.get(key, 0) + 1
        for pr in res["primitives"]:
            gid = f"C.index.{pr['node']}.{pr['material']}"
            label = f"Index limit {pr['node']} / {pr['material']}"
            if pairs[(pr["node"], pr["material"])] > 1:  # same node and material twice
                gid += f".p{pr['primitive']}"
                label += f" (primitive {pr['primitive']})"
            add(gid, "C", f"{label} ({pr['vertices']} vertices)", pr["max_index"],
                gc["index_max"], "<=", "count", "index_max")
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
            # hits that analyse_profile needs for each figure (w samples per chord)
            w = max(1, int(round(p["segment"] / p["step"])))
            need = {"maxima": 3, "minima": 3, "kink": 2 * w + 1, "step": 2 * w + 2}

            def few(kind):
                return (f"too few points on the outline: {r['hits']} of {r['samples']} rays "
                        f"hit, this figure needs at least {need[kind]}")
            add(f"F.{n}.coverage", "F", f"Profile {n}: share of rays that hit", r["coverage"],
                p["min_coverage"], ">=", "frac", "profile")
            for kind in ("maxima", "minima"):
                if p[kind] is not None:
                    add(f"F.{n}.{kind}", "F", f"Profile {n}: {kind} (exact count)", r[kind],
                        p[kind], "==", "count", "profile", reason=few(kind))
                else:
                    add(f"F.{n}.{kind}", "F", f"Profile {n}: {kind}", r[kind],
                        p[f"max_{kind}"], "<=", "count", "profile", reason=few(kind))
            add(f"F.{n}.kink", "F", f"Profile {n}: largest kink", r["kink_deg"],
                p["kink_max_deg"], "<=", "deg", "profile", reason=few("kink"))
            add(f"F.{n}.step", "F", f"Profile {n}: largest step", r["step"], p["step_max"], "<=",
                "m", "profile", reason=few("step"))
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


def num(n):
    """Whole number with a space as the thousands separator."""
    return f"{n:,}".replace(",", " ")


def write_md(res, name, timing=None, timing_note=None):
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
    extra = []
    if s["not_measured"]:
        extra.append(f"{s['not_measured']} of the failures not measured")
    if s["absent"]:
        extra.append(f"{s['absent']} absent (group not in the model, not required)")
    L.append(f"**{s['passed']} of {s['gates']} gates pass; {s['failed']} fail "
             f"({s['hard_failed']} hard)" + ("; " + "; ".join(extra) if extra else "") +
             f".** Overall: **{verdict}**.")
    L.append("")
    L.append("| Check | Gate | Value | Limit | Result |")
    L.append("|---|---|---|---|---|")
    for g in res["gates"]:
        hard = " (hard)" if g["hard"] else ""
        resl = {"pass": "PASS", "fail": f"**FAIL{hard}**",
                "not_measured": f"**FAIL{hard}: not measured** ({g.get('reason')})",
                "absent": f"ABSENT ({g.get('reason')})"}[g["status"]]
        limit = f"{g['op']} {fmt_value(g['limit'], g['unit'])}" if g["limit"] is not None \
            else "-"
        L.append(f"| {g['check']} | {g['label']} | {fmt_value(g['value'], g['unit'])} | "
                 f"{limit} | {resl} |")
    L.append("")
    L.append("## Model")
    L.append("")
    L.append(f"{num(m['triangles'])} triangles, {num(m['vertices'])} vertices "
             f"({m['verts_per_tri']:.2f} per triangle), {m['nodes']} nodes, "
             f"{m['primitives']} primitives (draw calls), {m['materials']} materials; largest "
             f"primitive {num(m['max_vertices_per_primitive'])} vertices. Size "
             f"{m['length']:.3f} × {m['span']:.3f} × {m['height']:.3f} m (length × span × "
             f"height). Hidden: {num(m['hidden'])} triangles ({m['hidden_frac'] * 100:.2f} % "
             "of the non-degenerate ones).")
    L.append("")
    L.append("## Per node")
    L.append("")
    L.append("Shape and shading over the triangles that are measured: seen (hidden and flipped "
             "ones are counted by their own checks) and not coinciding with the reference on a "
             "curved surface (\"not measured\", share of the visible area in its own column; "
             "the JSON also has the figures over all measured triangles).")
    L.append("")
    L.append("| Node | Group | Tris | Verts/tri | Shape p99 / max (mm) | Reverse p99 / max (mm) "
             "| Normal p95 / max (°) | Not measured | Hidden | Flipped | Sub-pixel | Thin "
             "| Slivers |")
    L.append("|---|---|---|---|---|---|---|---|---|---|---|---|---|")

    def deg2(s):
        return "-" if not s or s["p95"] is None else f"{s['p95']:.2f} / {s['max']:.2f}"

    def pct(v, digits=1):
        return "-" if v is None else f"{v * 100:.{digits}f} %"
    for name, n in res["nodes"].items():
        sh = n.get("shape")
        rv = n.get("shape_reverse")
        L.append(
            f"| {name} | {n['group']} | {n['triangles']} | {n['verts_per_tri']:.2f} | "
            + (f"{mm(sh['p99'])} / {mm(sh['max'])} | {mm(rv['p99'])} / {mm(rv['max'])} | "
               f"{deg2(n['shading_deg'])} | {pct(n['unmeasured_area_frac'])} | " if sh
               else f"- | - | - | {n.get('not_measured', 'no reference')} | ")
            + f"{pct(n['hidden_frac'])} | {n['flipped']} | "
              f"{pct(n['subpixel_frac'])} | {pct(n['thin_frac'])} | "
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
                 "shape error exceeds the gate or the piece's own error + 5 %, whichever is "
                 "larger; measured like the A gate (visible, not flipped, measured triangles, "
                 "area-weighted both ways).")
        L.append("")
        L.append("| Node | Piece (centre, m) | Tris | Own p99 / max (mm) | Smallest passing ratio "
                 "| Tris kept | Headroom |")
        L.append("|---|---|---|---|---|---|---|")
        for h in res["headroom"]:
            c = h["centre"]
            head = f"| {h['node']} | ({c[0]:.2f}, {c[1]:.2f}, {c[2]:.2f}) | {h['triangles']} | "
            if "skipped" in h:
                L.append(head + f"- | - | - | skipped: {h['skipped']} |")
                continue
            b = h["base"]
            L.append(head + f"{mm(max(b['fwd_p99'], b['rev_p99']))} / "
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
        L.append("| Probe | Rays that hit | Maxima | Minima | Largest kink | Largest step "
                 "| Excluded narrow parts |")
        L.append("|---|---|---|---|---|---|---|")
        for pname, p in res["probes"].items():
            ka = p["kink_at"]
            sa = p["step_at"]
            kink = f"{p['kink_deg']:.1f}° at y = {ka[1]:.2f}" if ka else "-"
            step = f"{p['step'] * 1000:.1f} mm at y = {sa[1]:.2f}" if sa else "-"
            L.append(f"| {pname} | {p['hits']} of {p['samples']} ({pct(p['coverage'])}) | "
                     f"{'-' if p['maxima'] is None else p['maxima']} | "
                     f"{'-' if p['minima'] is None else p['minima']} | {kink} | {step} | "
                     f"{len(p['excluded_parts'])} |")
        L.append("")
    hy = res["hygiene"]
    L.append("## Hygiene")
    L.append("")
    L.append(f"Degenerate {hy['degenerate']}, slivers {hy['slivers']}, duplicate faces (same "
             f"winding) {hy['duplicates']}, back-to-back face pairs (not a fault) "
             f"{hy['back_to_back_pairs']}, flipped {hy['flipped']} (back side visible "
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
                 (f"; {timing_note}" if timing_note else "") +
                 " (the only part of this report that changes between runs).")
        L.append("")
    return "\n".join(L)


# ---------------------------------------------------------------- main


def visibility_worker(spec_path):
    """Entry point of a worker process started by Audit.start_visibility: run one part of
    the visibility check and save it as part<i>.npz next to the spec."""
    import os
    spec = json.loads(Path(spec_path).read_text())
    out = Path(spec["dir"])
    if sha256(spec["model"]) != spec["sha256"]:
        print("[audit worker] the model file changed during the audit", file=sys.stderr)
        return 2
    cfg = spec["config"]
    model = glb_io.read_glb(spec["model"])
    geos = [Geo(model, p, cfg["model"]["glass_materials"]) for p in cfg["poses"]]
    vis = Visibility(model, geos, cfg, spec["fast"], want_back=np.load(out / "want_back.npy"),
                     gcomp=np.load(out / "gcomp.npy"), closed=np.load(out / "closed.npy"),
                     part=(spec["part"], spec["parts"]))
    final = out / f"part{spec['part']}.npz"
    partial = out / f"part{spec['part']}.partial"
    with open(partial, "wb") as fh:
        np.savez(fh, **vis.as_part())
    os.replace(partial, final)  # the parent never sees a half-written file
    return 0


def default_jobs():
    """Worker processes for the visibility check: up to 4, never more than the CPUs."""
    import os
    return max(1, min(4, os.cpu_count() or 1))


def main(argv=None):
    """Command line entry point. Returns the exit status: 0, 1 (a hard gate failed, or any
    gate with --strict) or 2 (bad input, or an internal error: the traceback is printed, so a
    crash can never look like a pass)."""
    if argv is None:
        argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    try:
        if argv[:1] == ["--visibility-worker"]:
            return visibility_worker(argv[1])
        return run_cli(argv)
    except (glb_io.GlbError, ConfigError, tomllib.TOMLDecodeError, OSError) as exc:
        print(f"[audit] error: {exc}", file=sys.stderr)
        return 2
    except Exception:  # noqa: BLE001 - anything else is a bug: show it and fail
        traceback.print_exc()
        print("[audit] internal error (traceback above)", file=sys.stderr)
        return 2


def run_cli(argv):
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
    ap.add_argument("--jobs", type=int, default=None,
                    help="worker processes for the visibility check on large models "
                         f"(default {default_jobs()} here; 1 = none). Results do not depend "
                         "on it.")
    args = ap.parse_args(argv)
    cfg = load_config(args.config)
    name = args.name or cfg["model"].get("name") or args.model.stem
    jobs = default_jobs() if args.jobs is None else args.jobs
    audit = Audit(args.model, args.reference, cfg, fast=args.fast, jobs=jobs)
    res = audit.run()
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / f"{name}_audit.json").write_text(dump_json(res))
    timing = dict(audit.timing)
    if args.images:
        t0 = time.perf_counter()
        import audit_render
        audit_render.render_all(audit, args.out, name)
        timing["images"] = time.perf_counter() - t0
    note = None
    if audit.vis_workers:
        note = (f"the visibility check ran in {audit.vis_workers} worker processes and this "
                "one")
    (args.out / f"{name}_audit.md").write_text(write_md(res, name, timing, note))
    s = res["summary"]
    print(f"[audit] {name}: {s['passed']}/{s['gates']} gates pass, "
          f"{s['failed']} fail ({s['hard_failed']} hard, {s['not_measured']} not measured), "
          f"{s['absent']} absent; {timing['total']:.1f} s; "
          f"report {args.out / (name + '_audit.md')}")
    if s["hard_failed"] or (args.strict and s["failed"]):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
