# Shared model access and result helpers for the fidelity checker (task T22e).
#
# ModelData flattens a .glb into world-frame (model frame: metres, x = vehicle left, y = aft,
# nose at -y, z = up) triangles per primitive, with materials, UVs and the file's texture
# table, so every gate reads the same arrays. Gates return dicts built by gate().

import math
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.dont_write_bytecode = True

import glb_io  # noqa: E402


class ConfigError(ValueError):
    pass


def rnd(x, nd=6):
    if x is None:
        return None
    x = float(x)
    return round(x, nd) if math.isfinite(x) else None


def gate(gid, label, value, op, limit, unit, ok, hard, reason=None, detail=None):
    """One result row. ok: True / False / None (None = not applicable or not measurable,
    status 'na'). A gate with status 'na' never passes; it counts as a failure only when its
    id is in the config's hard list and `hard` is set (callers pass hard according to the
    config)."""
    status = "na" if ok is None else ("pass" if ok else "fail")
    g = {"id": gid, "label": label, "value": value, "op": op, "limit": limit, "unit": unit,
         "status": status, "hard": bool(hard)}
    if reason:
        g["reason"] = reason
    if detail is not None:
        g["detail"] = detail
    return g


def is_hard(gid, hard_prefixes):
    return any(gid == p or gid.startswith(p + ".") for p in hard_prefixes)


class Prim:
    def __init__(self, node, material, material_index, verts, tris, uvs, has_color, index):
        self.node = node
        self.material = material
        self.material_index = material_index
        self.verts = verts          # (V, 3) model frame
        self.tris = tris            # (T, 3) int
        self.uvs = uvs              # (V, 2) or None
        self.has_color = has_color
        self.index = index

    @property
    def key(self):
        return f"{self.node}:{self.material}"

    def tri_pts(self):
        return self.verts[self.tris]


class ModelData:
    def __init__(self, path):
        self.glb = glb_io.read_glb(path)
        self.path = Path(path)
        self.size = self.glb.size
        self.sha256 = self.glb.sha256
        self.doc = self.glb.doc
        world = self.glb.world_matrices()
        c = glb_io.GLTF_TO_MODEL
        self.prims = []
        for p in self.glb.prims:
            m = world[p.node]
            v = (p.positions @ m[:3, :3].T + m[:3, 3]) @ c.T
            # a mirrored node (negative determinant) flips the winding: restore it
            tris = p.indices.copy()
            if np.linalg.det(m[:3, :3]) < 0:
                tris = tris[:, [0, 2, 1]]
            self.prims.append(Prim(p.node, p.material, p.material_index, v, tris,
                                   p.uvs, p.has_color, p.index))
        self.nodes = {n.name: n for n in self.glb.nodes}
        self.world = world

    @property
    def triangles(self):
        return sum(len(p.tris) for p in self.prims)

    def select(self, exclude_nodes=(), exclude_prims=(), nodes=None, materials=None):
        """Primitives not excluded. exclude_prims entries are 'node:material'; nodes limits
        the selection to those node names."""
        out = []
        for p in self.prims:
            if p.node in exclude_nodes or p.key in exclude_prims:
                continue
            if nodes is not None and p.node not in nodes:
                continue
            if materials is not None and p.material not in materials:
                continue
            out.append(p)
        return out

    @staticmethod
    def tri_array(prims):
        if not prims:
            return np.zeros((0, 3, 3))
        return np.concatenate([p.tri_pts() for p in prims])

    # -------- textures

    def image_count(self):
        return len(self.doc.get("images", []))

    def material_maps(self, material_index):
        """Image index per map for a material: dict baseColor / normal / occlusion /
        metallicRoughness -> image index or None."""
        out = {"baseColor": None, "normal": None, "occlusion": None, "metallicRoughness": None}
        if material_index is None:
            return out
        mat = self.doc.get("materials", [])[material_index]
        textures = self.doc.get("textures", [])

        def src(info):
            if not info or "index" not in info:
                return None
            t = textures[info["index"]]
            return t.get("source")
        pbr = mat.get("pbrMetallicRoughness", {})
        out["baseColor"] = src(pbr.get("baseColorTexture"))
        out["metallicRoughness"] = src(pbr.get("metallicRoughnessTexture"))
        out["normal"] = src(mat.get("normalTexture"))
        out["occlusion"] = src(mat.get("occlusionTexture"))
        return out

    def image_bytes(self, index):
        im = self.doc["images"][index]
        if "bufferView" in im:
            v = self.doc["bufferViews"][im["bufferView"]]
            off = v.get("byteOffset", 0)
            return bytes(self.glb.binary[off:off + v["byteLength"]]), im.get("mimeType")
        return None, im.get("mimeType")


def percentile(values, weights, q):
    """Weighted percentile (q in 0..100), deterministic, no interpolation surprises."""
    values = np.asarray(values, dtype=np.float64)
    weights = np.asarray(weights, dtype=np.float64)
    if len(values) == 0:
        return None
    order = np.argsort(values, kind="stable")
    v, w = values[order], weights[order]
    cum = np.cumsum(w)
    cutoff = q / 100.0 * cum[-1]
    return float(v[min(np.searchsorted(cum, cutoff, side="left"), len(v) - 1)])
