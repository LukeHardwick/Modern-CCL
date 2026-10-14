# F1: outline of the model against the reference three-view (task T22e).
#
# The model's exterior triangles are rasterised as orthographic silhouettes on a fixed metric
# grid (side: y, z; top: x, y; front: x, z; row index grows as the vertical coordinate falls).
# The reference masks (tools/models/reference/make_reference_masks.py) are mapped onto the same
# grid: the reference is scaled to the PUBLISHED length / span / height on each axis and placed
# by two landmarks taken from the model (the spinner tip, i.e. the model's most forward point,
# and the lowest point, i.e. the wheel contact; the lateral axis is x = 0). Nothing is fitted:
# a model that is too long, too short or off in height shows up as outline error.

import math
import tomllib
from pathlib import Path

import numpy as np

import fidelity_model as fm
import raster_util as ru
import png_io

AXES = {"side": (1, 2), "top": (0, 1), "front": (0, 2)}   # (horizontal, vertical) model axes
AXNAME = "xyz"


class Grid:
    def __init__(self, hmin, hmax, vmin, vmax, pitch):
        self.hmin, self.vmax, self.pitch = hmin, vmax, pitch
        self.hmax, self.vmin = hmax, vmin
        self.w = int(round((hmax - hmin) / pitch))
        self.h = int(round((vmax - vmin) / pitch))

    def col_h(self, c):
        return self.hmin + (np.asarray(c) + 0.5) * self.pitch

    def row_v(self, r):
        return self.vmax - (np.asarray(r) + 0.5) * self.pitch

    def to_px(self, hv):
        """(N, 2) metres (h, v) -> (N, 2) pixel coordinates (x right, y down)."""
        hv = np.asarray(hv, dtype=np.float64)
        return np.stack([(hv[:, 0] - self.hmin) / self.pitch,
                         (self.vmax - hv[:, 1]) / self.pitch], axis=1)


def load_reference(path):
    path = Path(path)
    ref = tomllib.loads(path.read_text())
    masks = {}
    for view, info in ref["views"].items():
        arr, _ = png_io.read_png((path.parent / info["file"]).read_bytes())
        masks[view] = arr[..., 0] > 127
    return ref, masks


def reference_transform(view, ext, published, anchors):
    """Returns (col, row) = f(h, v) for the reference mask of a view (continuous pixel
    coordinates, pixel (c, r) covers [c, c + 1) x [r, r + 1))."""
    left, right, top, bottom = ext["left"], ext["right"], ext["top"], ext["bottom"]
    wpx, hpx = right - left + 1, bottom - top + 1
    if view == "side":
        sy, sz = published["length"] / wpx, published["height"] / hpx
        y0, z0 = anchors["y_nose"], anchors["z_ground"]
        return (lambda h, v: (left + (h - y0) / sy, bottom + 1 - (v - z0) / sz),
                {"h_m_per_px": sy, "v_m_per_px": sz})
    if view == "top":
        sx, sl = published["span"] / wpx, published["length"] / hpx
        x0, y0 = anchors["x_mid"], anchors["y_nose"]
        mid = (left + right + 1) / 2.0
        return (lambda h, v: (mid + (h - x0) / sx, bottom + 1 - (v - y0) / sl),
                {"h_m_per_px": sx, "v_m_per_px": sl})
    sx, sz = published["span"] / wpx, published["height"] / hpx
    x0, z0 = anchors["x_mid"], anchors["z_ground"]
    mid = (left + right + 1) / 2.0
    return (lambda h, v: (mid + (h - x0) / sx, bottom + 1 - (v - z0) / sz),
            {"h_m_per_px": sx, "v_m_per_px": sz})


def sample_reference(mask, transform, grid):
    cols = grid.col_h(np.arange(grid.w))
    rows = grid.row_v(np.arange(grid.h))
    hh, vv = np.meshgrid(cols, rows)
    cx, cy = transform(hh, vv)
    ix, iy = np.floor(cx).astype(np.int64), np.floor(cy).astype(np.int64)
    ok = (ix >= 0) & (ix < mask.shape[1]) & (iy >= 0) & (iy < mask.shape[0])
    out = np.zeros((grid.h, grid.w), dtype=bool)
    out[ok] = mask[iy[ok], ix[ok]]
    return out


def silhouette(tris3, view, grid):
    ha, va = AXES[view]
    if len(tris3) == 0:
        return np.zeros((grid.h, grid.w), dtype=bool)
    flat = tris3[:, :, [ha, va]].reshape(-1, 2)
    px = grid.to_px(flat).reshape(-1, 3, 2)
    return ru.rasterise_triangles(px, grid.w, grid.h) > 0


def mask_points(mask, grid):
    b = ru.boundary(mask)
    r, c = np.nonzero(b)
    return np.stack([grid.col_h(c), grid.row_v(r)], axis=1)


def edge_stats(a_pts, b_pts):
    d = ru.nearest_distances(a_pts, b_pts)
    if len(d) == 0:
        return {"p95": None, "max": None, "mean": None, "n": 0}
    return {"p95": float(np.percentile(d, 95)), "max": float(d.max()),
            "mean": float(d.mean()), "n": int(len(d))}


# ---------------------------------------------------------------- stations


def _first_last(line, take):
    idx = np.nonzero(line)[0]
    if len(idx) == 0:
        return None
    return int(idx[0]) if take == "min" else int(idx[-1])


def measure_station(mask, grid, spec):
    """One named station on a mask; returns a coordinate in metres or None.
    kind 'extreme': edge = min_h | max_h | min_v | max_v (the outermost mask edge).
    kind 'scan': scan = 'h' | 'v' (axis scanned), at = coordinate of the other axis (m),
    take = min | max (first / last filled pixel along the scan), optional window = [lo, hi]
    along the scan axis, band_mm (lines averaged within +- band, default 15), sym (average
    the lines at +at and -at: for lateral positions).
    kind 'contact': window = [lo, hi] along h; the h-centre of the lowest filled pixels
    (within tol_mm of the lowest) among those columns."""
    kind = spec["kind"]
    p = grid.pitch
    if kind == "extreme":
        edge = spec["edge"]
        cols = np.nonzero(mask.any(axis=0))[0]
        rows = np.nonzero(mask.any(axis=1))[0]
        if len(cols) == 0:
            return None
        if edge == "min_h":
            return grid.hmin + cols[0] * p
        if edge == "max_h":
            return grid.hmin + (cols[-1] + 1) * p
        if edge == "max_v":
            return grid.vmax - rows[0] * p
        return grid.vmax - (rows[-1] + 1) * p
    if kind == "contact":
        lo, hi = spec["window"]
        c0 = max(0, int(math.floor((lo - grid.hmin) / p)))
        c1 = min(grid.w, int(math.ceil((hi - grid.hmin) / p)))
        sub = mask[:, c0:c1]
        rows = np.where(sub.any(axis=0), grid.h - 1 - np.argmax(sub[::-1], axis=0), -1)
        if (rows < 0).all():
            return None
        low = rows.max()          # lowest filled row index (largest r)
        sel = np.nonzero(rows >= low - int(round(spec.get("tol_mm", 15) / 1000.0 / p)))[0]
        return float(grid.hmin + (c0 + (sel.min() + sel.max() + 1) / 2.0) * p)
    # scan
    scan, take = spec["scan"], spec["take"]
    band = int(round(spec.get("band_mm", 15) / 1000.0 / p))
    ats = [spec["at"]] + ([-spec["at"]] if spec.get("sym") else [])
    vals = []
    for at in ats:
        got = []
        for k in range(-band, band + 1):
            if scan == "h":
                r = int(math.floor((grid.vmax - at) / p)) + k
                if not 0 <= r < grid.h:
                    continue
                line, lo_c = mask[r], 0
                if "window" in spec:
                    lo_c = max(0, int(math.floor((spec["window"][0] - grid.hmin) / p)))
                    hi_c = min(grid.w, int(math.ceil((spec["window"][1] - grid.hmin) / p)))
                    line = line[lo_c:hi_c]
                i = _first_last(line, take)
                if i is not None:
                    got.append(grid.hmin + (lo_c + i + (0 if take == "min" else 1)) * p)
            else:
                c = int(math.floor((at - grid.hmin) / p)) + k
                if not 0 <= c < grid.w:
                    continue
                line, lo_r = mask[:, c], 0
                if "window" in spec:
                    # rows from the top: vertical window [lo, hi] -> row range
                    r0 = max(0, int(math.floor((grid.vmax - spec["window"][1]) / p)))
                    r1 = min(grid.h, int(math.ceil((grid.vmax - spec["window"][0]) / p)))
                    line, lo_r = line[r0:r1], r0
                # min = lowest v = last row; max = highest v = first row
                i = _first_last(line, "min" if take == "max" else "max")
                if i is not None:
                    row = lo_r + i
                    got.append(grid.vmax - row * p if take == "max"
                               else grid.vmax - (row + 1) * p)
        if got:
            vals.append(float(np.mean(got)))
    return float(np.mean(vals)) if vals else None


# ---------------------------------------------------------------- the gate


def reference_bounds(view, ref, published, anchors):
    """Metric extent (hmin, hmax, vmin, vmax) of the reference silhouette."""
    ext = ref["views"][view]["extent_px"]
    transform, _ = reference_transform(view, ext, published, anchors)
    c0, r0 = transform(0.0, 0.0)
    c1, r1 = transform(1.0, 1.0)
    kc, kr = c1 - c0, r1 - r0          # the map is separable: col = f(h), row = g(v)
    ha = [(ext["left"] - c0) / kc, (ext["right"] + 1 - c0) / kc]
    va = [(ext["top"] - r0) / kr, (ext["bottom"] + 1 - r0) / kr]
    return min(ha), max(ha), min(va), max(va)


def view_setup(data, cfg, ref, view, pitch):
    """Model silhouette inputs and the common grid of a view."""
    f1 = cfg["F1_outline"]
    vc = f1["views"][view]
    published = cfg["model"]["published_size"]
    excl = list(cfg["model"].get("exterior_exclude_nodes", [])) + list(vc.get("exclude_nodes", []))
    prims = data.select(excl, vc.get("exclude_prims", []))
    tris = data.tri_array(prims)
    ha, va = AXES[view]
    anchors = {"x_mid": 0.0,
               "y_nose": float(tris[:, :, 1].min()) if len(tris) else 0.0,
               "z_ground": float(tris[:, :, 2].min()) if len(tris) else 0.0}
    ext = ref["views"][view]["extent_px"]
    transform, scale = reference_transform(view, ext, published, anchors)
    rb = reference_bounds(view, ref, published, anchors)
    lo_h, hi_h, lo_v, hi_v = rb
    if len(tris):
        lo_h, hi_h = min(lo_h, tris[:, :, ha].min()), max(hi_h, tris[:, :, ha].max())
        lo_v, hi_v = min(lo_v, tris[:, :, va].min()), max(hi_v, tris[:, :, va].max())
    grid = Grid(math.floor(lo_h * 10 - 1) / 10.0, math.ceil(hi_h * 10 + 1) / 10.0,
                math.floor(lo_v * 10 - 1) / 10.0, math.ceil(hi_v * 10 + 1) / 10.0, pitch)
    return prims, tris, anchors, transform, scale, grid, sorted(set(excl + list(
        vc.get("exclude_prims", []))))


def compare_masks(mmask, rmask, grid):
    inter, union = int((mmask & rmask).sum()), int((mmask | rmask).sum())
    iou = inter / union if union else 0.0
    mp, rp = mask_points(mmask, grid), mask_points(rmask, grid)
    return iou, edge_stats(mp, rp), edge_stats(rp, mp)


def run(data, cfg, root, keep_masks=False):
    """F1 for ModelData `data`: (gates, details, masks). masks: view -> (model, reference,
    grid) when keep_masks (for the overlay images)."""
    f1 = cfg["F1_outline"]
    ref, ref_masks = load_reference(Path(root) / f1["reference"])
    pitch = 0.01 / f1["raster_px_per_cm"]
    hard = cfg["gates"]["hard"]
    gates, masks = [], {}
    details = {"reference": {"file": f1["reference"], "svg_sha256": ref["source"]["svg_sha256"],
                             "mask_sha256": {v: ref["views"][v]["sha256"] for v in ref["views"]}},
               "views": {}, "stations": []}
    for view in ("side", "top", "front"):
        prims, tris, anchors, transform, scale, grid, excluded = view_setup(
            data, cfg, ref, view, pitch)
        mmask = silhouette(tris, view, grid)
        rmask = sample_reference(ref_masks[view], transform, grid)
        iou, m2r, r2m = compare_masks(mmask, rmask, grid)
        mm = lambda d: {k: fm.rnd(v * 1000 if k != "n" else v, 2) for k, v in d.items()}
        details["views"][view] = {
            "iou": fm.rnd(iou, 4),
            "model_area_m2": fm.rnd(mmask.sum() * pitch * pitch, 4),
            "reference_area_m2": fm.rnd(rmask.sum() * pitch * pitch, 4),
            "model_to_reference_mm": mm(m2r), "reference_to_model_mm": mm(r2m),
            "reference_m_per_px": {"h": fm.rnd(scale["h_m_per_px"], 6),
                                   "v": fm.rnd(scale["v_m_per_px"], 6)},
            "anchors": {k: fm.rnd(v, 4) for k, v in anchors.items()},
            "grid_px": [grid.w, grid.h], "primitives": len(prims), "excluded": excluded}
        base = f"F1.{view}"
        gates.append(fm.gate(f"{base}.iou", f"{view} silhouette IoU", fm.rnd(iou, 4), ">=",
                             f1["iou_min"][view], "ratio", iou >= f1["iou_min"][view],
                             fm.is_hard(f"{base}.iou", hard)))
        for key, lim in (("p95", f1["outline_p95_mm"]), ("max", f1["outline_max_mm"])):
            val = m2r[key] * 1000.0
            gates.append(fm.gate(f"{base}.outline_{key}", f"{view} outline distance {key}",
                                 fm.rnd(val, 2), "<=", lim, "mm", val <= lim,
                                 fm.is_hard(f"{base}.outline_{key}", hard)))
        for st in cfg.get("F1_stations", []):
            if st["view"] != view:
                continue
            a = measure_station(mmask, grid, st)
            b = measure_station(rmask, grid, st)
            delta = None if a is None or b is None else (a - b) * 1000.0
            ok = delta is not None and abs(delta) <= f1["station_tol_mm"]
            details["stations"].append({"name": st["name"], "view": view,
                                        "model_m": fm.rnd(a, 4), "reference_m": fm.rnd(b, 4),
                                        "delta_mm": fm.rnd(delta, 1)})
            gid = f"F1.station.{st['name']}"
            gates.append(fm.gate(gid, f"station {st['name']} ({view})",
                                 None if delta is None else fm.rnd(abs(delta), 1), "<=",
                                 f1["station_tol_mm"], "mm", ok, fm.is_hard(gid, hard),
                                 reason=None if delta is not None else "station not found",
                                 detail={"model_m": fm.rnd(a, 4), "reference_m": fm.rnd(b, 4)}))
        if keep_masks:
            masks[view] = (mmask, rmask, grid)
    return gates, details, masks


def overlay_png(path, mmask, rmask, step=2):
    """Overlay image: grey = both, red = reference only (model misses it), blue = model only.
    Downsampled by `step` (a pixel is set when any source pixel in its block is set)."""
    def ds(m):
        h, w = (m.shape[0] // step) * step, (m.shape[1] // step) * step
        return m[:h, :w].reshape(h // step, step, w // step, step).any(axis=(1, 3))
    a, b = ds(mmask), ds(rmask)
    img = np.full(a.shape + (3,), 255, dtype=np.uint8)
    both = a & b
    img[both] = (170, 170, 170)
    img[b & ~a] = (225, 60, 60)
    img[a & ~b] = (60, 90, 225)
    png_io.write_png(path, img)
