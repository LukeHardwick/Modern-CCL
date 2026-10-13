# Minimal glTF 2.0 binary (.glb) reader and writer for the model audit (task T22c).
#
# The audit measures what a viewer loads, so it reads the file itself (struct + numpy) instead
# of going through Blender's glTF importer, which may merge or split data. Supported: one BIN
# chunk, accessors with or without byteStride, normalised integer attributes, TRS or matrix
# nodes, triangle primitives (mode 4) with or without indices. Not supported (reported as an
# error): sparse accessors, external buffers, other primitive modes.
#
# The writer exists for the known-answer tests: it writes plain meshes (positions, normals,
# optional colours, u16 or u32 indices) under named nodes.

import hashlib
import json
import struct
from pathlib import Path

import numpy as np

GLB_MAGIC = b"glTF"
CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942

COMPONENT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32,
             5126: np.float32}
WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}
NORM_DIV = {5120: 127.0, 5121: 255.0, 5122: 32767.0, 5123: 65535.0}

# glTF (+Y up, front +Z) -> modelling frame of tools/models (x = vehicle left, y = aft, z = up):
# x_m = x_g, y_m = -z_g, z_m = y_g. A proper rotation, so distances and angles are unchanged.
GLTF_TO_MODEL = np.array([[1.0, 0.0, 0.0], [0.0, 0.0, -1.0], [0.0, 1.0, 0.0]])


class GlbError(Exception):
    pass


class Primitive:
    """One (node, material) draw: positions/normals in the node's local glTF frame."""

    def __init__(self, node, material, positions, normals, indices, index_type, has_color):
        self.node = node                  # node name
        self.material = material          # material name
        self.positions = positions        # (V, 3) float64
        self.normals = normals            # (V, 3) float64 or None
        self.indices = indices            # (T, 3) int64
        self.index_type = index_type      # glTF componentType of the index accessor (or None)
        self.has_color = has_color

    @property
    def vertex_count(self):
        return len(self.positions)

    @property
    def max_index(self):
        return int(self.indices.max()) if len(self.indices) else -1


class Node:
    def __init__(self, index, name, local, mesh, children):
        self.index = index
        self.name = name
        self.local = local                # 4x4 float64, glTF frame
        self.mesh = mesh                  # mesh index or None
        self.children = children
        self.parent = None


class GlbModel:
    def __init__(self, path, size, doc, nodes, materials, prims, scene_nodes, warnings,
                 sha256=None):
        self.path = path
        self.size = size
        self.sha256 = sha256              # of the bytes that were read
        self.doc = doc
        self.nodes = nodes                # list of Node (file order)
        self.materials = materials        # list of dicts: name, alpha_mode, double_sided
        self.prims = prims                # list of Primitive (node order, then file order)
        self.scene_nodes = scene_nodes    # node indices reachable from the default scene
        self.warnings = warnings

    def node(self, name):
        for n in self.nodes:
            if n.name == name:
                return n
        return None

    def world_matrices(self, local_override=None):
        """Node name -> 4x4 world matrix (glTF frame). `local_override(node) -> 4x4 or None`
        replaces a node's local matrix (used for poses)."""
        out = {}

        def visit(idx, parent_world):
            node = self.nodes[idx]
            local = node.local
            if local_override is not None:
                alt = local_override(node)
                if alt is not None:
                    local = alt
            world = parent_world @ local
            out[node.name] = world
            for c in node.children:
                visit(c, world)
        for root in self.scene_roots():
            visit(root, np.eye(4))
        return out

    def scene_roots(self):
        doc = self.doc
        scenes = doc.get("scenes") or []
        if scenes:
            return list(scenes[doc.get("scene", 0)].get("nodes", []))
        child = {c for n in self.nodes for c in n.children}
        return [n.index for n in self.nodes if n.index not in child]


def _quat_matrix(q):
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def node_local_matrix(n):
    if "matrix" in n:
        return np.array(n["matrix"], dtype=np.float64).reshape(4, 4).T  # column-major
    m = np.eye(4)
    r = _quat_matrix(n.get("rotation", [0.0, 0.0, 0.0, 1.0]))
    s = np.diag(n.get("scale", [1.0, 1.0, 1.0]))
    m[:3, :3] = r @ s
    m[:3, 3] = n.get("translation", [0.0, 0.0, 0.0])
    return m


def _read_accessor(doc, binary, index):
    acc = doc["accessors"][index]
    if "sparse" in acc:
        raise GlbError(f"accessor {index}: sparse accessors are not supported")
    ctype = acc["componentType"]
    dtype = np.dtype(COMPONENT[ctype]).newbyteorder("<")
    width = WIDTH[acc["type"]]
    count = acc["count"]
    if "bufferView" not in acc:
        return np.zeros((count, width), dtype=np.float64)
    view = doc["bufferViews"][acc["bufferView"]]
    if view.get("buffer", 0) != 0 or binary is None:
        raise GlbError(f"accessor {index}: only the GLB's own BIN chunk is supported")
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    elem = dtype.itemsize * width
    stride = view.get("byteStride") or elem
    if count == 0:
        return np.zeros((0, width), dtype=np.float64)
    end = start + stride * (count - 1) + elem
    if end > len(binary):
        raise GlbError(f"accessor {index}: reads past the end of the BIN chunk")
    raw = np.frombuffer(binary, dtype=np.uint8, count=end - start, offset=start)
    if stride == elem:
        arr = raw.view(dtype).reshape(count, width)
    else:
        rows = np.lib.stride_tricks.as_strided(raw, shape=(count, elem), strides=(stride, 1))
        arr = np.ascontiguousarray(rows).view(dtype).reshape(count, width)
    if acc.get("normalized") and ctype in NORM_DIV:
        return np.maximum(arr.astype(np.float64) / NORM_DIV[ctype], -1.0)
    if ctype == 5126:
        return arr.astype(np.float64)
    return arr.astype(np.int64)


def read_glb(path):
    path = Path(path)
    data = path.read_bytes()
    if len(data) < 20:
        raise GlbError("file too short for a GLB")
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    if magic != GLB_MAGIC or version != 2:
        raise GlbError("not a glTF 2.0 binary file")
    offset, doc, binary = 12, None, None
    while offset + 8 <= min(length, len(data)):
        clen, ctype = struct.unpack_from("<II", data, offset)
        chunk = data[offset + 8:offset + 8 + clen]
        if ctype == CHUNK_JSON and doc is None:
            doc = json.loads(chunk)
        elif ctype == CHUNK_BIN and binary is None:
            binary = chunk
        offset += 8 + clen + (-clen % 4)
    if doc is None:
        raise GlbError("no JSON chunk")
    warnings = []

    materials = []
    for i, m in enumerate(doc.get("materials", [])):
        materials.append({"name": m.get("name") or f"material_{i}",
                          "alpha_mode": m.get("alphaMode", "OPAQUE"),
                          "double_sided": bool(m.get("doubleSided", False))})

    raw_nodes = doc.get("nodes", [])
    names, seen = [], {}
    for i, n in enumerate(raw_nodes):
        name = n.get("name") or f"node_{i}"
        if name in seen:
            warnings.append(f"duplicate node name {name!r}: renamed to {name}#{i}")
            name = f"{name}#{i}"
        seen[name] = i
        names.append(name)
    nodes = [Node(i, names[i], node_local_matrix(n), n.get("mesh"), list(n.get("children", [])))
             for i, n in enumerate(raw_nodes)]
    for n in nodes:
        for c in n.children:
            nodes[c].parent = n.index

    model = GlbModel(path, len(data), doc, nodes, materials, [], [], warnings,
                     hashlib.sha256(data).hexdigest())
    reach = []

    def visit(i):
        reach.append(i)
        for c in nodes[i].children:
            visit(c)
    for r in model.scene_roots():
        visit(r)
    model.scene_nodes = sorted(reach)
    unreached = [n.name for n in nodes if n.index not in set(reach)]
    if unreached:
        warnings.append(f"nodes outside the default scene (ignored): {', '.join(unreached)}")

    meshes = doc.get("meshes", [])
    for i in model.scene_nodes:
        node = nodes[i]
        if node.mesh is None:
            continue
        for k, p in enumerate(meshes[node.mesh].get("primitives", [])):
            mode = p.get("mode", 4)
            if mode != 4:
                warnings.append(f"{node.name}: primitive {k} has mode {mode} (not triangles), "
                                "skipped")
                continue
            attrs = p.get("attributes", {})
            if "POSITION" not in attrs:
                warnings.append(f"{node.name}: primitive {k} has no POSITION, skipped")
                continue
            pos = _read_accessor(doc, binary, attrs["POSITION"])[:, :3].astype(np.float64)
            nrm = None
            if "NORMAL" in attrs:
                nrm = _read_accessor(doc, binary, attrs["NORMAL"])[:, :3].astype(np.float64)
            if "indices" in p:
                idx = _read_accessor(doc, binary, p["indices"]).reshape(-1)
                itype = doc["accessors"][p["indices"]]["componentType"]
            else:
                idx = np.arange(len(pos), dtype=np.int64)
                itype = None
            if len(idx) % 3:
                warnings.append(f"{node.name}: primitive {k} index count not a multiple of 3")
                idx = idx[:len(idx) - len(idx) % 3]
            if len(idx) and (idx.max() >= len(pos) or idx.min() < 0):
                raise GlbError(f"{node.name}: primitive {k} has indices outside its vertices")
            mat = p.get("material")
            mname = materials[mat]["name"] if mat is not None else "default"
            model.prims.append(Primitive(node.name, mname, pos, nrm, idx.reshape(-1, 3),
                                         itype, "COLOR_0" in attrs))
    return model


# ---------------------------------------------------------------- writer (tests)


def write_glb(path, nodes, materials=None):
    """Write a simple GLB. `nodes`: list of dicts with keys name, translation (optional),
    parent (optional, name), prims (list of dicts: positions (V,3), normals (V,3) optional,
    indices (T,3), material (name), index_type 'u16'|'u32' optional). Positions and normals are
    in the glTF frame. `materials`: list of dicts name, alpha_mode, double_sided."""
    materials = materials or [{"name": "paint"}]
    mat_index = {m["name"]: i for i, m in enumerate(materials)}
    blob = bytearray()
    views, accessors = [], []

    def add(arr, ctype, typ, target, minmax=False):
        arr = np.ascontiguousarray(arr)
        data = arr.astype(np.dtype(COMPONENT[ctype]).newbyteorder("<")).tobytes()
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data),
                      "target": target})
        blob.extend(data)
        acc = {"bufferView": len(views) - 1, "componentType": ctype,
               "count": int(arr.shape[0]), "type": typ}
        if minmax:
            acc["min"] = [float(v) for v in arr.min(axis=0)]
            acc["max"] = [float(v) for v in arr.max(axis=0)]
        accessors.append(acc)
        return len(accessors) - 1

    meshes, gnodes = [], []
    name_index = {n["name"]: i for i, n in enumerate(nodes)}
    for n in nodes:
        gn = {"name": n["name"]}
        if n.get("translation") is not None:
            gn["translation"] = [float(v) for v in n["translation"]]
        if n.get("prims"):
            prims = []
            for p in n["prims"]:
                pos = np.asarray(p["positions"], dtype=np.float64)
                attrs = {"POSITION": add(pos, 5126, "VEC3", 34962, True)}
                if p.get("normals") is not None:
                    attrs["NORMAL"] = add(np.asarray(p["normals"]), 5126, "VEC3", 34962)
                idx = np.asarray(p["indices"], dtype=np.int64).reshape(-1)
                ctype = 5125 if p.get("index_type", "u16") == "u32" else 5123
                prim = {"attributes": attrs, "indices": add(idx, ctype, "SCALAR", 34963),
                        "material": mat_index[p.get("material", materials[0]["name"])]}
                prims.append(prim)
            meshes.append({"name": n["name"], "primitives": prims})
            gn["mesh"] = len(meshes) - 1
        gnodes.append(gn)
    roots = []
    for n in nodes:
        if n.get("parent"):
            gnodes[name_index[n["parent"]]].setdefault("children", []).append(
                name_index[n["name"]])
        else:
            roots.append(name_index[n["name"]])
    doc = {"asset": {"version": "2.0", "generator": "tools/models/glb_io.py"},
           "scene": 0, "scenes": [{"nodes": roots}], "nodes": gnodes, "meshes": meshes,
           "materials": [{"name": m["name"], "alphaMode": m.get("alpha_mode", "OPAQUE"),
                          "doubleSided": bool(m.get("double_sided", False))}
                         for m in materials],
           "accessors": accessors, "bufferViews": views,
           "buffers": [{"byteLength": len(blob)}]}
    js = json.dumps(doc, sort_keys=True, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    while len(blob) % 4:
        blob.append(0)
    total = 12 + 8 + len(js) + 8 + len(blob)
    out = struct.pack("<4sII", GLB_MAGIC, 2, total)
    out += struct.pack("<II", len(js), CHUNK_JSON) + js
    out += struct.pack("<II", len(blob), CHUNK_BIN) + bytes(blob)
    Path(path).write_bytes(out)
