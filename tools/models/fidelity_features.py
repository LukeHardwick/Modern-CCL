# F2: feature checklist (task T22e).
#
# A feature is proven by data, not by a name. kind 'geometry': triangles whose centroid lies in
# a probe box (model frame, metres; optional mirror_x tests the box at -x too) and, when given,
# of one material or node, must number at least count_min and stand out by at least relief_mm
# (the spread, 2nd to 98th percentile, of the box's vertices along their mean normal). kind
# 'texture': through the UVs of the triangles in the box, the normal map must show at least
# count_min separate groove marks (tilt of the normal above groove_tilt), or, with mode =
# 'colour', separate regions that differ from the median colour by more than colour_delta.
# A feature without a probe box is "not defined yet" and counts as absent.

import numpy as np

import fidelity_model as fm
import fidelity_tex as ft
import raster_util as ru


def _boxes(feat):
    box = feat.get("box")
    if not box:
        return []
    b = np.asarray(box, dtype=np.float64).reshape(2, 3)
    out = [(b[0], b[1])]
    if feat.get("mirror_x"):
        out.append((np.array([-b[1][0], b[0][1], b[0][2]]), np.array([-b[0][0], b[1][1], b[1][2]])))
    return out


def _tris_in_boxes(data, feat, boxes):
    """[(prim, boolean mask over its triangles)] and the vertex indices used."""
    out = []
    for p in data.prims:
        if feat.get("nodes") and p.node not in feat["nodes"]:
            continue
        if feat.get("material") and p.material != feat["material"]:
            continue
        c = p.verts[p.tris].mean(axis=1)
        m = np.zeros(len(c), dtype=bool)
        for lo, hi in boxes:
            m |= ((c >= lo) & (c <= hi)).all(axis=1)
        if m.any():
            out.append((p, m))
    return out


def geometry_feature(data, feat):
    boxes = _boxes(feat)
    if not boxes:
        return None, "no probe box defined yet"
    hits = _tris_in_boxes(data, feat, boxes)
    count = int(sum(m.sum() for _p, m in hits))
    relief = 0.0
    if count:
        verts = np.concatenate([p.verts[p.tris[m]].reshape(-1, 3) for p, m in hits])
        axis = feat.get("axis")
        if axis:
            ax = "xyz".index(axis)
        else:       # the probe box is tight around the feature: its thinnest side is the normal
            lo, hi = boxes[0]
            ax = int(np.argmin(hi - lo))
        relief = float(verts[:, ax].max() - verts[:, ax].min())
    ok = count >= feat["count_min"] and relief * 1000.0 >= feat.get("relief_mm", 0.0)
    return ok, {"triangles": count, "relief_mm": fm.rnd(relief * 1000.0, 2)}


def texture_feature(data, feat, cfg):
    f2 = cfg["F2_features"]
    boxes = _boxes(feat)
    if not boxes:
        return None, "no probe box defined yet"
    hits = [(p, m) for p, m in _tris_in_boxes(data, feat, boxes) if p.uvs is not None]
    if not hits:
        return False, {"reason": "no triangle with UVs in the probe box"}
    mode = feat.get("mode", "groove")
    marks = 0
    for p, m in hits:
        maps = data.material_maps(p.material_index)
        idx = maps["normal"] if mode == "groove" else maps["baseColor"]
        if idx is None:
            return False, {"reason": "no " + ("normal" if mode == "groove" else "base colour") + " map"}
        raw, mime = data.image_bytes(idx)
        im = ft.decode_image(raw, mime)
        if im is None:
            return False, {"reason": "map not decodable here"}
        h, w = im.shape[:2]
        tri = p.uvs[p.tris[m]] * np.array([w, h])
        region = ru.rasterise_triangles(tri, w, h) > 0
        if mode == "groove":
            n = im[..., :3] * 2.0 - 1.0
            tilt = np.hypot(n[..., 0], n[..., 1])
            mark = region & (tilt > f2["groove_tilt"])
        else:
            rgb = im[..., :3] * 255.0
            med = np.median(rgb[region], axis=0)
            mark = region & (np.linalg.norm(rgb - med, axis=2) > f2["colour_delta"])
        lab, n = ru.label_components(mark, 8)
        if n:
            sizes = np.bincount(lab.ravel(), minlength=n + 1)[1:]
            marks += int((sizes >= f2["min_mark_px"]).sum())
    return marks >= feat["count_min"], {"marks": marks}


def run(data, cfg):
    f2 = cfg["F2_features"]
    hard = cfg["gates"]["hard"]
    gates, details = [], []
    req = [0, 0]
    opt = [0, 0]
    for feat in cfg.get("F2_feature", []):
        if feat["kind"] == "geometry":
            ok, info = geometry_feature(data, feat)
        else:
            ok, info = texture_feature(data, feat, cfg)
        present = bool(ok)
        reason = info if isinstance(info, str) else (info.get("reason") if isinstance(info, dict) else None)
        row = req if feat["required"] else opt
        row[1] += 1
        row[0] += 1 if present else 0
        gid = f"F2.feature.{feat['name']}"
        gates.append(fm.gate(gid, f"feature {feat['name']} ({feat['kind']}, "
                             f"{'required' if feat['required'] else 'optional'})",
                             None if not isinstance(info, dict) else info, "present", feat["count_min"], "",
                             present, False, reason=reason))
        details.append({"name": feat["name"], "kind": feat["kind"], "required": feat["required"],
                        "present": present, "measured": info if isinstance(info, dict) else None,
                        "reason": reason})
    for key, (got, tot), lim, label in (("required", req, f2["required_fraction"], "required"),
                                         ("optional", opt, f2["optional_fraction"], "optional")):
        frac = got / tot if tot else 1.0
        gid = f"F2.{key}"
        gates.append(fm.gate(gid, f"{label} features present ({got} of {tot})", fm.rnd(frac, 4),
                             ">=", lim, "frac", frac >= lim, fm.is_hard(gid, hard)))
    return gates, details
