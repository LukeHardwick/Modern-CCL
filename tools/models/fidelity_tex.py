# F4: materials, UV layout and textures (task T22e), plus the image decoder F2 shares.
#
# Everything is measured from the .glb alone. UV gates need TEXCOORD_0 but not the maps: at CP2
# a model has UVs and no textures yet, and the layout is judged on an assumed sheet size.
# Image gates need decodable PNGs (JPEG only when running inside Blender). A model without
# UVs or textures (T22d) fails the map / UV presence gates and reports the dependent
# measurements as not applicable.

import math

import numpy as np

import fidelity_model as fm
import png_io
import raster_util as ru

try:  # Blender's Python decodes JPEG; plain Python decodes PNG itself
    import bpy  # noqa: F401
    HAVE_BPY = True
except ImportError:
    HAVE_BPY = False


def decode_image(data, mime):
    """bytes -> float64 array (H, W, C) in 0..1 (no colour management), or None."""
    if data is None:
        return None
    if data[:8] == png_io.SIG:
        try:
            arr, _info = png_io.read_png(data)
        except (png_io.PngError, ValueError):
            arr = None
        if arr is not None:
            return arr.astype(np.float64) / (65535.0 if arr.dtype == np.uint16 else 255.0)
    if HAVE_BPY:
        import os
        import tempfile
        ext = ".jpg" if (mime or "").endswith("jpeg") else ".png"
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "img" + ext)
            with open(path, "wb") as fh:
                fh.write(data)
            im = bpy.data.images.load(path)
            try:
                im.colorspace_settings.name = "Non-Color"
                w, h = im.size
                ch = im.channels
                px = np.array(im.pixels[:], dtype=np.float32).reshape(h, w, ch)[::-1]
            finally:
                bpy.data.images.remove(im)
        return px.astype(np.float64)
    return None


def image_size(data):
    """(width, height) of a PNG without decoding it, else None."""
    if data and data[:8] == png_io.SIG and len(data) >= 24:
        return int.from_bytes(data[16:20], "big"), int.from_bytes(data[20:24], "big")
    return None


def is_pow2(n):
    return n > 0 and (n & (n - 1)) == 0


class Sheet:
    def __init__(self, name, w, h, textured):
        self.name, self.w, self.h, self.textured = name, w, h, textured
        self.prims = []


def build_sheets(data, f4, visible):
    """Assign visible primitives to sheets. Config [[F4_sheet]] (name, nodes) wins; otherwise
    the primitive's base-colour image index names the sheet; otherwise 'unassigned'."""
    mapping = {}
    for s in f4.get("sheet", []):
        for n in s["nodes"]:
            mapping[n] = s["name"]
    sheets, sizes = {}, {}
    for p in visible:
        maps = data.material_maps(p.material_index)
        img = maps["baseColor"]
        w = h = None
        if img is not None:
            raw, mime = data.image_bytes(img)
            sz = image_size(raw)
            if sz is None:
                im = decode_image(raw, mime)
                sz = None if im is None else (im.shape[1], im.shape[0])
            if sz:
                w, h = sz
        name = mapping.get(p.node) or (f"image{img}" if img is not None else "unassigned")
        sizes.setdefault(name, (w, h))
        if sizes[name][0] is None and w:
            sizes[name] = (w, h)
        sheets.setdefault(name, []).append(p)
    out = []
    for name in sorted(sheets):
        w, h = sizes[name]
        textured = w is not None
        sh = Sheet(name, w or f4["assumed_sheet_px"], h or f4["assumed_sheet_px"], textured)
        sh.prims = sheets[name]
        out.append(sh)
    return out


def tri_world_area(p):
    v = p.verts[p.tris]
    return 0.5 * np.linalg.norm(np.cross(v[:, 1] - v[:, 0], v[:, 2] - v[:, 0]), axis=1)


def tri_uv_area(p, w, h):
    uv = p.uvs[p.tris] * np.array([w, h])
    return 0.5 * np.abs((uv[:, 1, 0] - uv[:, 0, 0]) * (uv[:, 2, 1] - uv[:, 0, 1])
                        - (uv[:, 2, 0] - uv[:, 0, 0]) * (uv[:, 1, 1] - uv[:, 0, 1]))


def texel_density(sheet):
    """Per-triangle texel density (px per metre) with its world area, over a sheet."""
    dens, area, prim_idx = [], [], []
    for k, p in enumerate(sheet.prims):
        wa = tri_world_area(p)
        ua = tri_uv_area(p, sheet.w, sheet.h)
        ok = wa > 1e-10
        dens.append(np.sqrt(ua[ok] / wa[ok]))
        area.append(wa[ok])
        prim_idx.append(np.full(int(ok.sum()), k))
    return (np.concatenate(dens) if dens else np.zeros(0), np.concatenate(area) if area else np.zeros(0),
            np.concatenate(prim_idx) if prim_idx else np.zeros(0, dtype=int))


def mirror_pair_keep(sheet, tol=1e-4):
    """Boolean keep masks per primitive: a triangle is dropped when another triangle has the
    identical UV triangle and its world triangle is the x -> -x mirror image (left / right
    parts sharing texture space)."""
    allp = []
    for k, p in enumerate(sheet.prims):
        uv = np.round(p.uvs[p.tris] * 1e5).astype(np.int64)         # (T, 3, 2)
        keyarr = np.sort(uv[:, :, 0] * 10_000_000 + uv[:, :, 1], axis=1)
        for t in range(len(p.tris)):
            allp.append((k, t, tuple(keyarr[t])))
    groups = {}
    for k, t, key in allp:
        groups.setdefault(key, []).append((k, t))
    keep = [np.ones(len(p.tris), dtype=bool) for p in sheet.prims]
    for key, members in groups.items():
        if len(members) != 2:
            continue
        (k1, t1), (k2, t2) = members
        a = sheet.prims[k1].verts[sheet.prims[k1].tris[t1]]
        b = sheet.prims[k2].verts[sheet.prims[k2].tris[t2]]
        am = a * np.array([-1.0, 1.0, 1.0])
        # mirrored vertex sets must coincide (any order)
        d = np.linalg.norm(am[:, None, :] - b[None, :, :], axis=2)
        if (d.min(axis=1) < tol).all() and (d.min(axis=0) < tol).all():
            keep[k2][t2] = False
    return keep


def uv_pixel_tris(p, w, h, mask=None):
    uv = p.uvs[p.tris] * np.array([w, h])
    return uv if mask is None else uv[mask]


def island_ids(p):
    """Island index (>= 1) per triangle of a primitive: triangles sharing a vertex index."""
    t = p.tris
    parent = np.arange(len(p.verts))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for a, b, c in t:
        parent[find(b)] = find(a)
        parent[find(c)] = find(a)
    roots = np.array([find(a) for a in t[:, 0]])
    _, inv = np.unique(roots, return_inverse=True)
    return inv.reshape(-1) + 1


def padding_gap(label, pad):
    """Smallest Chebyshev distance (in pixels, 1..pad) at which two different islands meet:
    None when every pair is at least pad + 1 apart. label: int image, 0 = empty."""
    big = np.iinfo(np.int32).max
    lab_max = np.where(label > 0, label, 0).astype(np.int32)
    lab_min = np.where(label > 0, label, big).astype(np.int32)

    for r in range(1, pad + 1):
        # square window, separable: rows then columns
        mx_rows = lab_max.copy()
        mn_rows = lab_min.copy()
        for k in range(1, r + 1):
            for sh in (k, -k):
                s = np.roll(lab_max, sh, axis=0)
                t = np.roll(lab_min, sh, axis=0)
                if sh > 0:
                    s[:sh] = 0
                    t[:sh] = big
                else:
                    s[sh:] = 0
                    t[sh:] = big
                mx_rows = np.maximum(mx_rows, s)
                mn_rows = np.minimum(mn_rows, t)
        mx2, mn2 = mx_rows.copy(), mn_rows.copy()
        for k in range(1, r + 1):
            for sh in (k, -k):
                s = np.roll(mx_rows, sh, axis=1)
                t = np.roll(mn_rows, sh, axis=1)
                if sh > 0:
                    s[:, :sh] = 0
                    t[:, :sh] = big
                else:
                    s[:, sh:] = 0
                    t[:, sh:] = big
                mx2 = np.maximum(mx2, s)
                mn2 = np.minimum(mn2, t)
        conflict = (label > 0) & (mx2 != mn2)
        if conflict.any():
            return r, int(conflict.sum())
    return None, 0


def propagate_bleed(img, covered, steps):
    """Nearest-pixel extension of the covered texels by `steps` pixels (4-neighbour,
    deterministic order): the picture a baker's margin should have produced."""
    out = img.copy()
    cur = covered.copy()
    for _ in range(steps):
        grown = np.zeros_like(cur)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            src = np.roll(cur, (dy, dx), axis=(0, 1))
            sim = np.roll(out, (dy, dx), axis=(0, 1))
            take = src & ~cur & ~grown
            out[take] = sim[take]
            grown |= take
        cur = cur | grown
    return out, cur


def run(data, cfg, family=None):
    """F4 gates. family: 'textured' (default from config) or 'vertex_colour'."""
    f4 = cfg["F4_textures"]
    hard = cfg["gates"]["hard"]
    family = family or f4.get("family", "textured")
    gates, details = [], {"family": family}
    exempt = set(f4.get("exempt_materials", []))
    visible = [p for p in data.prims if p.material not in exempt]

    def add(gid, label, value, op, limit, unit, ok, reason=None, detail=None):
        gates.append(fm.gate(gid, label, value, op, limit, unit, ok, fm.is_hard(gid, hard),
                             reason, detail))

    # --- size and vertex limits (both families)
    mb = data.size
    textured_file = any(data.material_maps(p.material_index)["baseColor"] is not None
                        for p in data.prims)
    lim = f4["glb_bytes_max"] if family == "textured" else f4["glb_bytes_max_vertex_colour"]
    add("F4.glb_bytes", f"file size ({family} family)", mb, "<=", lim, "bytes", mb <= lim)
    maxv = max((len(p.verts) for p in data.prims), default=0)
    add("F4.primitive_vertices", "largest primitive (vertices)", maxv, "<=",
        f4["primitive_vertices_max"], "count", maxv <= f4["primitive_vertices_max"])
    details["textured_file"] = textured_file

    if family == "vertex_colour":
        miss = [p.key for p in visible if not p.has_color]
        add("F4.vertex_colour", "visible primitives with COLOR_0", len(visible) - len(miss), "==",
            len(visible), "count", not miss, reason=("missing on " + ", ".join(miss[:5])) if miss else None)
        add("F4.no_textures", "textures in the vertex-colour family", len(data.doc.get("images", [])),
            "==", 0, "count", len(data.doc.get("images", [])) == 0)
        return gates, details

    # --- maps present
    missing_maps, missing_uv, double_tint = [], [], []
    for p in visible:
        m = data.material_maps(p.material_index)
        orm_ok = (m["metallicRoughness"] is not None and m["occlusion"] is not None
                  and m["metallicRoughness"] == m["occlusion"])
        if m["baseColor"] is None or m["normal"] is None or not orm_ok:
            missing_maps.append(p.key)
        if p.uvs is None:
            missing_uv.append(p.key)
        if p.has_color and m["baseColor"] is not None:
            double_tint.append(p.key)
    add("F4.maps", "visible primitives with base colour, normal and ORM maps",
        len(visible) - len(missing_maps), "==", len(visible), "count", not missing_maps,
        reason=(f"{len(missing_maps)} of {len(visible)} lack them" +
                ("; the file has no textures" if not textured_file else "")) if missing_maps else None,
        detail=sorted(set(missing_maps))[:20])
    add("F4.uvs", "visible primitives with TEXCOORD_0", len(visible) - len(missing_uv), "==",
        len(visible), "count", not missing_uv,
        reason=f"{len(missing_uv)} of {len(visible)} have no UVs" if missing_uv else None,
        detail=sorted(set(missing_uv))[:20])
    add("F4.colour_double_tint", "textured primitives that also carry COLOR_0", len(double_tint),
        "==", 0, "count", not double_tint)
    # --- images
    n_img = data.image_count()
    sizes_bad, gpu = [], 0
    for i in range(n_img):
        raw, _mime = data.image_bytes(i)
        sz = image_size(raw)
        if sz is None:
            continue
        if not (is_pow2(sz[0]) and is_pow2(sz[1]) and max(sz) <= f4["max_texture_px"]):
            sizes_bad.append(f"image{i} {sz[0]}x{sz[1]}")
        gpu += int(sz[0] * sz[1] * 4 * 4 / 3)
    add("F4.sizes", "texture sizes power of two and within the limit", len(sizes_bad), "==", 0,
        "count", not sizes_bad if n_img else None, reason=None if n_img else "no textures",
        detail=sizes_bad)
    add("F4.gpu_bytes", "uncompressed RGBA8 with mips", gpu, "<=", f4["gpu_bytes_max"], "bytes",
        gpu <= f4["gpu_bytes_max"])

    # --- UV layout
    uv_prims = [p for p in visible if p.uvs is not None]
    if not uv_prims:
        for gid, label in (("F4.spread", "texel density spread"),
                           ("F4.uv_range", "UVs inside 0..1"), ("F4.overlap", "UV overlap"),
                           ("F4.padding", "island padding")):
            add(gid, label, None, "", None, "", None, reason="the model has no UVs")
        sheets = []
    else:
        sheets = build_sheets(data, f4, uv_prims)
    sheet_info = []
    groups = f4.get("density_group", [])
    group_vals = {g["name"]: ([], []) for g in groups}
    for sh in sheets:
        dens, area, pidx = texel_density(sh)
        info = {"name": sh.name, "size_px": [sh.w, sh.h], "has_image": sh.textured,
                "triangles": int(sum(len(p.tris) for p in sh.prims))}
        if len(dens):
            p5 = fm.percentile(dens, area, 5)
            p95 = fm.percentile(dens, area, 95)
            info.update({"density_p5": fm.rnd(p5, 1), "density_p95": fm.rnd(p95, 1),
                         "spread": fm.rnd(p95 / p5, 3) if p5 else None})
            add(f"F4.spread.{sh.name}", f"sheet {sh.name} density spread (p95 / p5)",
                fm.rnd(p95 / p5, 3) if p5 else None, "<=", f4["density_spread_max"], "ratio",
                bool(p5) and p95 / p5 <= f4["density_spread_max"])
            for k, p in enumerate(sh.prims):
                sel = pidx == k
                for g in groups:          # a primitive counts in the first group that claims it
                    if _in_group(p, g):
                        group_vals[g["name"]][0].append(dens[sel])
                        group_vals[g["name"]][1].append(area[sel])
                        break
        # range
        lo = min(float(p.uvs.min()) for p in sh.prims)
        hi = max(float(p.uvs.max()) for p in sh.prims)
        info["uv_range"] = [fm.rnd(lo, 4), fm.rnd(hi, 4)]
        add(f"F4.uv_range.{sh.name}", f"sheet {sh.name} UVs inside 0..1", [fm.rnd(lo, 4), fm.rnd(hi, 4)],
            "in", [0, 1], "uv", lo >= -1e-6 and hi <= 1 + 1e-6)
        # overlap + islands
        keep = mirror_pair_keep(sh)
        tri_px, owners = [], []
        base = 0
        for k, p in enumerate(sh.prims):
            ids = island_ids(p) + base
            base = int(ids.max()) if len(ids) else base
            tri_px.append(uv_pixel_tris(p, sh.w, sh.h, keep[k]))
            owners.append(ids[keep[k]])
        tri_px = np.concatenate(tri_px)
        owners = np.concatenate(owners)
        count = ru.rasterise_triangles(tri_px, sh.w, sh.h, mode="count")
        overlap = int((count > 1).sum())
        info["overlap_px"] = overlap
        info["mirror_pairs_shared"] = int(sum((~k).sum() for k in keep))
        add(f"F4.overlap.{sh.name}", f"sheet {sh.name} overlapping UV pixels", overlap, "<=",
            f4["uv_overlap_px_max"], "px", overlap <= f4["uv_overlap_px_max"])
        label = ru.rasterise_triangles(tri_px, sh.w, sh.h, owner=owners, mode="owner")
        gap, bad = padding_gap(label, f4["padding_px_min"])
        info["padding_ok_px"] = f4["padding_px_min"] if gap is None else gap - 1
        info["padding_violating_px"] = bad
        add(f"F4.padding.{sh.name}", f"sheet {sh.name} island padding", info["padding_ok_px"], ">=",
            f4["padding_px_min"], "px", gap is None)
        info["_label"] = label
        sheet_info.append(info)
    for g in groups:
        vals, wts = group_vals[g["name"]]
        if vals:
            d, w = np.concatenate(vals), np.concatenate(wts)
            p5 = fm.percentile(d, w, 5)
            add(f"F4.density.{g['name']}", f"texel density p5, {g['name']}", fm.rnd(p5, 1), ">=",
                g["min_px_per_m"], "px/m", p5 >= g["min_px_per_m"])
        elif uv_prims:
            add(f"F4.density.{g['name']}", f"texel density p5, {g['name']}", None, ">=",
                g["min_px_per_m"], "px/m", None, reason="no primitive in this group")
        else:
            add(f"F4.density.{g['name']}", f"texel density p5, {g['name']}", None, ">=",
                g["min_px_per_m"], "px/m", None, reason="the model has no UVs")

    # --- image content (needs decodable maps and UV coverage)
    content_done = False
    if textured_file and sheet_info:
        covered_by = {}
        for info, sh in zip(sheet_info, sheets):
            if sh.textured:
                covered_by[sh.name] = (info.pop("_label") > 0, sh)
        for sh_name, (cov, sh) in covered_by.items():
            maps = data.material_maps(sh.prims[0].material_index)
            res = {}
            for kind, key in (("baseColor", "baseColor"), ("normal", "normal"),
                              ("orm", "metallicRoughness")):
                idx = maps[key]
                if idx is None:
                    continue
                raw, mime = data.image_bytes(idx)
                im = decode_image(raw, mime)
                if im is None or im.shape[:2] != cov.shape:
                    res[kind] = None
                    continue
                res[kind] = im
            if res.get("normal") is not None:
                n = res["normal"][..., :3] * 2.0 - 1.0
                ln = np.linalg.norm(n, axis=2)[cov]
                zpos = float((n[..., 2][cov] > 0).mean())
                mean_len = float(ln.mean())
                add(f"F4.normal_length.{sh_name}", f"sheet {sh_name} normal vector length",
                    fm.rnd(mean_len, 4), "in", [1 - f4["normal_unit_length_tol"], 1 + f4["normal_unit_length_tol"]],
                    "ratio", abs(mean_len - 1) <= f4["normal_unit_length_tol"])
                add(f"F4.normal_z.{sh_name}", f"sheet {sh_name} normals with z > 0", fm.rnd(zpos, 4),
                    ">=", 0.999, "frac", zpos >= 0.999)
                content_done = True
            if res.get("orm") is not None:
                metal = res["orm"][..., 2][cov]
                frac = float(((metal < 0.1) | (metal > 0.9)).mean())
                add(f"F4.metallic_binary.{sh_name}", f"sheet {sh_name} metallic channel binary",
                    fm.rnd(frac, 4), ">=", f4["metallic_binary_fraction_min"], "frac",
                    frac >= f4["metallic_binary_fraction_min"])
                content_done = True
            if res.get("baseColor") is not None:
                rgb = res["baseColor"][..., :3] * 255.0
                luma = (0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2])[cov]
                lo, hi = f4["base_colour_srgb_range"]
                out = float(((luma < lo) | (luma > hi)).mean())
                add(f"F4.base_colour_range.{sh_name}", f"sheet {sh_name} base colour outside {lo}..{hi}",
                    fm.rnd(out, 4), "<=", f4["base_colour_outside_max"], "frac",
                    out <= f4["base_colour_outside_max"])
                content_done = True
            for kind in ("baseColor", "normal"):
                im = res.get(kind)
                if im is None:
                    continue
                grown, _ = propagate_bleed(im[..., :3], cov, f4["bleed_px"])
                ring = ru.dilate(cov, f4["bleed_px"]) & ~cov
                if ring.any():
                    diff = np.abs(grown - im[..., :3]).max(axis=2)[ring] * 255.0
                    bad = float((diff > f4["bleed_diff_max"]).mean())
                    add(f"F4.bleed.{sh_name}.{kind}", f"sheet {sh_name} {kind} edge bleed", fm.rnd(bad, 4),
                        "<=", f4["bleed_bad_max"], "frac", bad <= f4["bleed_bad_max"])
    for info in sheet_info:
        info.pop("_label", None)
    if not content_done:
        for gid, label in (("F4.normal_length", "normal vector length"),
                           ("F4.metallic_binary", "metallic channel binary"),
                           ("F4.base_colour_range", "base colour range"), ("F4.bleed", "edge bleed")):
            add(gid, label, None, "", None, "", None,
                reason="no decodable textures" if not textured_file else "textures not decodable here")
    details["sheets"] = sheet_info
    details["images"] = n_img
    return gates, details


def _in_group(p, g):
    if "nodes" in g and p.node in g["nodes"]:
        return True
    if "materials" in g and p.material in g["materials"]:
        return True
    return g.get("default", False)
