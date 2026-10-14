# F3 (section shape), F5 (edge crispness) and the hard-crease share (task T22e).
#
# A section is a plane cut through the triangles of chosen nodes: every crossed triangle
# gives one 2-D segment. F3 draws the segments on a fine raster (2 mm), closes the gaps between
# separate parts (door gaps, 4 mm), fills the outside-unreachable area and measures width /
# height, fill ratio, side flatness and the tightest outline corner on that filled shape.
# F5 chains the same segments into exact polylines and measures a bevel directly on the
# polyline (no raster), so radii of a millimetre are resolved.

import math

import numpy as np

import fidelity_model as fm
import raster_util as ru

PLANE_AXES = {0: (1, 2), 1: (0, 2), 2: (0, 1)}   # cut axis -> the two remaining axes (u, v)
AXIS_INDEX = {"x": 0, "y": 1, "z": 2}


def cut_segments(tris, axis, pos):
    """Segments (N, 2, 2) in (u, v) where the plane model[axis] = pos crosses the triangles."""
    if len(tris) == 0:
        return np.zeros((0, 2, 2))
    d = tris[:, :, axis] - pos
    d = np.where(d == 0.0, 1e-12, d)
    neg = d < 0
    n_neg = neg.sum(axis=1)
    sel = (n_neg == 1) | (n_neg == 2)
    t, d, neg = tris[sel], d[sel], neg[sel]
    if len(t) == 0:
        return np.zeros((0, 2, 2))
    u, v = PLANE_AXES[axis]
    pts = []
    for i, j in ((0, 1), (1, 2), (2, 0)):
        cross = neg[:, i] != neg[:, j]
        den = np.where(cross, d[:, i] - d[:, j], 1.0)
        tt = d[:, i] / den
        p = t[:, i, :] + tt[:, None] * (t[:, j, :] - t[:, i, :])
        pts.append((cross, p[:, [u, v]]))
    # each selected triangle has exactly two crossing edges
    out = np.zeros((len(t), 2, 2))
    count = np.zeros(len(t), dtype=np.int64)
    for cross, p in pts:
        idx = np.nonzero(cross)[0]
        out[idx, count[idx]] = p[idx]
        count[idx] += 1
    return out


# ---------------------------------------------------------------- polylines (F5)


def chain_segments(segs, tol=1e-7):
    """Chain segments into polylines by shared endpoints (quantised to tol). Returns a list
    of (points (K, 2), closed bool)."""
    if len(segs) == 0:
        return []
    q = np.round(segs / tol).astype(np.int64)
    keys = {}
    ends = []
    for k in range(len(segs)):
        a, b = tuple(q[k, 0]), tuple(q[k, 1])
        if a == b:
            continue
        ends.append((a, b, k))
    adj = {}
    for a, b, k in ends:
        adj.setdefault(a, []).append((b, k))
        adj.setdefault(b, []).append((a, k))
    used = set()
    pos = {}
    for k in range(len(segs)):
        pos[tuple(q[k, 0])] = segs[k, 0]
        pos[tuple(q[k, 1])] = segs[k, 1]
    chains = []
    # start open chains at degree-1 vertices first, then loops
    order = sorted(adj, key=lambda p: (len(adj[p]) != 1, p))
    for start in order:
        for nxt, k in adj[start]:
            if k in used:
                continue
            chain = [start]
            cur, edge = nxt, k
            used.add(edge)
            chain.append(cur)
            while True:
                step = None
                for n2, k2 in adj[cur]:
                    if k2 not in used:
                        step = (n2, k2)
                        break
                if step is None:
                    break
                used.add(step[1])
                cur = step[0]
                chain.append(cur)
            closed = len(chain) > 2 and chain[0] == chain[-1]
            pts = np.array([pos[c] for c in chain])
            chains.append((pts[:-1] if closed else pts, closed))
    return chains


def _resample_walk(poly, closed, i0, half_len, step):
    """Points along the polyline within +-half_len (arc length) of vertex i0, resampled every
    `step`, as (s, point) with s = 0 at the vertex."""
    pts = poly if not closed else np.vstack([poly, poly[:1]])
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    cum = np.concatenate([[0.0], np.cumsum(seg)])
    total = cum[-1]
    s0 = cum[i0]
    ss = np.arange(-half_len, half_len + step / 2, step)
    sq = s0 + ss
    if closed:
        sq = np.mod(sq, total)
    else:
        keep = (sq >= 0) & (sq <= total)
        ss, sq = ss[keep], sq[keep]
    x = np.interp(sq, cum, pts[:, 0])
    y = np.interp(sq, cum, pts[:, 1])
    return ss, np.stack([x, y], axis=1)


def measure_edge(poly, closed, direction, window=None, half_len_mm=30.0, depth_mm=1.0):
    """Radius and thickness of the edge of a section polyline at its extreme point along
    `direction` (unit 2-D). The radius is that of the arc through the polyline's own turning
    vertices between the 5 % and 95 % points of the cumulative turn, for an inscribed polygon
    (exact for a bevel drawn with equal segments); 'segments' is the number of mesh segments
    between them (0 = a sharp edge). Returns a dict or None."""
    d = np.asarray(direction, dtype=np.float64)
    d = d / np.linalg.norm(d)
    proj = poly @ d
    cand = np.arange(len(poly))
    if window is not None:
        along = poly @ np.array([-d[1], d[0]])
        cand = cand[(along >= window[0]) & (along <= window[1])]
        if len(cand) == 0:
            return None
    i0 = cand[np.argmax(proj[cand])]
    # a flat end face has several vertices at the same extreme: centre the walk on them
    ties = cand[proj[cand] >= proj[i0] - 1e-9]
    i0 = ties[len(ties) // 2] if len(ties) else i0
    half = half_len_mm / 1000.0
    ss, walk = _resample_walk(poly, closed, i0, half, 0.0001)
    if len(walk) < 5:
        return None
    # the polyline's own vertices within +-half of the tip: their turning angles
    pts = poly if not closed else np.vstack([poly, poly[:1]])
    seg0 = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    cum0 = np.concatenate([[0.0], np.cumsum(seg0)])
    tot = cum0[-1]
    n_v = len(poly)
    idx, svals = [], []
    for k in range(n_v):
        sk = cum0[k] - cum0[i0]
        if closed:
            sk = (sk + tot / 2) % tot - tot / 2
        if abs(sk) <= half and (closed or 0 < k < n_v - 1):
            idx.append(k)
            svals.append(sk)
    order = np.argsort(svals)
    idx = [idx[i] for i in order]
    verts = []          # (position, signed turn)
    for k in idx:
        a_, b_, c_ = poly[k - 1], poly[k], poly[(k + 1) % n_v]
        v1, v2 = b_ - a_, c_ - b_
        if np.linalg.norm(v1) == 0 or np.linalg.norm(v2) == 0:
            continue
        turn = math.atan2(v1[0] * v2[1] - v1[1] * v2[0], v1 @ v2)
        verts.append((b_, turn))
    total_signed = sum(t for _p, t in verts)
    if abs(total_signed) < math.radians(10):
        return {"radius_m": None, "thickness_m": None, "segments": 0,
                "turn_deg": math.degrees(abs(total_signed)), "reason": "no corner"}
    sg = 1.0 if total_signed > 0 else -1.0
    turns = [sg * t for _p, t in verts]
    big = [i for i, t in enumerate(turns) if t > math.radians(1.0)]
    total = sum(turns[i] for i in big)
    cum = np.cumsum([turns[i] for i in big])
    j1 = int(np.searchsorted(cum, 0.05 * total, side="left"))
    j2 = int(np.searchsorted(cum, 0.95 * total, side="left"))
    sel = big[j1:j2 + 1]
    n = len(sel)
    if n <= 1:
        radius = 0.0
    else:
        pos = np.array([verts[i][0] for i in sel])
        chord_sum = float(np.linalg.norm(np.diff(pos, axis=0), axis=1).sum())
        theta = sum(turns[i] for i in sel) - 0.5 * (turns[sel[0]] + turns[sel[-1]])
        m = n - 1
        radius = chord_sum / (2.0 * m * math.sin(min(theta / (2.0 * m), math.pi / 2)))
    # thickness: width at `depth` behind the tip
    behind = walk @ d
    perp = np.array([-d[1], d[0]])
    lo_side = np.nonzero(ss < 0)[0]
    hi_side = np.nonzero(ss >= 0)[0]
    thickness = None
    target = (poly[i0] @ d) - depth_mm / 1000.0
    if len(lo_side) > 1 and len(hi_side) > 1:
        def cross_at(idx):
            pr = behind[idx]
            hit = np.nonzero(pr <= target)[0]
            if len(hit) == 0:
                return None
            k = hit[0]
            return (walk[idx[k]] @ perp)
        a, b = cross_at(lo_side[::-1]), cross_at(hi_side)
        if a is not None and b is not None:
            thickness = abs(a - b)
    return {"radius_m": radius, "thickness_m": thickness, "segments": max(n - 1, 0),
            "turn_deg": math.degrees(total), "tip": poly[i0].tolist()}


def edge_probe(data, spec, cfg_nodes_default=()):
    """Cut the nodes of one F5 edge spec and measure it. spec keys: nodes, plane = 'y=-0.2'
    style (axis, value), dir = '+y' (axis and sign of the outward direction in the plane),
    window = [lo, hi] along the other in-plane axis (optional)."""
    prims = data.select(nodes=set(spec["nodes"]))
    tris = data.tri_array(prims)
    ax_s, val = spec["plane"]["axis"], float(spec["plane"]["at"])
    axis = AXIS_INDEX[ax_s]
    segs = cut_segments(tris, axis, val)
    if len(segs) == 0:
        return {"found": False, "reason": "the plane cuts no triangle of the nodes"}
    u, v = PLANE_AXES[axis]
    sign, dax = (1.0 if spec["dir"][0] == "+" else -1.0), AXIS_INDEX[spec["dir"][1]]
    if dax not in (u, v):
        return {"found": False, "reason": "direction axis lies in the cut plane normal"}
    d = np.zeros(2)
    d[0 if dax == u else 1] = sign
    chains = chain_segments(segs)
    best = None
    for pts, closed in chains:
        if len(pts) < 3:
            continue
        m = measure_edge(pts, closed, d, spec.get("window"), spec.get("half_len_mm", 30.0))
        if m is None or m.get("radius_m") is None:
            continue
        score = float((pts @ d).max())
        if best is None or score > best[0]:
            best = (score, m)
    if best is None:
        return {"found": False, "reason": "no polyline with a corner at that edge"}
    return {"found": True, **best[1]}


# ---------------------------------------------------------------- sections (F3)


def section_outline(segs, nbins=2880, x_clip=None, sample_mm=0.5):
    """Outer outline of a cut: the cut segments are sampled every sample_mm and the point
    farthest from the centre of their bounding box is kept in each of nbins angular sectors
    (rays from that centre). Openings of the shell (a roof closed by another node, door gaps)
    and anything inside the outline drop out; an opening becomes a straight chord between the
    neighbouring kept points. Returns dict(pts (K, 2) in angular order, chord (K,) bool: the
    edge from pts[k] to pts[k + 1] is a chord over an opening, centre) or None."""
    if len(segs) == 0:
        return None
    if x_clip is not None:
        segs = segs[np.abs(segs[:, :, 0]).max(axis=1) <= x_clip]
        if len(segs) == 0:
            return None
    ln = np.linalg.norm(segs[:, 1] - segs[:, 0], axis=1)
    n = np.maximum(np.ceil(ln / (sample_mm / 1000.0)).astype(np.int64), 1) + 1
    seg_id = np.repeat(np.arange(len(segs)), n)
    start = np.concatenate([[0], np.cumsum(n)[:-1]])
    k = np.arange(int(n.sum())) - np.repeat(start, n)
    t = k / np.repeat(n - 1, n)
    p = segs[seg_id, 0] + t[:, None] * (segs[seg_id, 1] - segs[seg_id, 0])
    c = (p.min(axis=0) + p.max(axis=0)) / 2.0
    q = p - c
    ang = np.arctan2(q[:, 1], q[:, 0])
    rad = np.hypot(q[:, 0], q[:, 1])
    b = np.minimum(((ang + math.pi) / (2 * math.pi) * nbins).astype(np.int64), nbins - 1)
    # per sector the point of largest radius (ties: lowest index)
    order = np.lexsort((np.arange(len(b)), -rad, b))
    first = np.concatenate([[True], b[order][1:] != b[order][:-1]])
    pick = order[first]
    pts = p[pick]                 # already in increasing sector order
    if len(pts) < 8:
        return None
    gap = np.linalg.norm(np.roll(pts, -1, axis=0) - pts, axis=1)
    rmean = float(np.hypot(*(pts - c).T).mean())
    chord = gap > max(3.5 * 2 * math.pi * rmean / nbins, 0.015)
    return {"pts": pts, "chord": chord, "centre": c}


def protrusion_skip(outline, pitch=0.002, open_mm=10.0, min_len_mm=30.0, skip_mm=30.0):
    """Mask (and its origin) of the neighbourhood of thin protrusions (a fin or antenna that
    crosses the plane): the filled outline is opened with a disk of open_mm; what the opening
    removes and that is longer than min_len_mm is a protrusion; its surroundings (skip_mm) are
    left out of the corner radius. Returns (mask, (u0, v0)); mask may be all False."""
    pts, c = outline["pts"], outline["centre"]
    lo = pts.min(axis=0) - 0.05
    w = int(math.ceil((pts[:, 0].max() + 0.05 - lo[0]) / pitch))
    h = int(math.ceil((pts[:, 1].max() + 0.05 - lo[1]) / pitch))
    px = np.concatenate([[(c - lo) / pitch] * len(pts)]).reshape(-1, 2)
    a = (pts - lo) / pitch
    bq = (np.roll(pts, -1, axis=0) - lo) / pitch
    tri = np.stack([px, a, bq], axis=1)
    mask = ru.rasterise_triangles(tri, w, h) > 0
    r = max(1, int(round(open_mm / 1000.0 / pitch)))
    opened = ru.dilate(ru.erode(mask, r), r)
    diff = mask & ~opened
    lab, n = ru.label_components(diff, 8)
    flag = np.zeros_like(mask)
    for i in range(1, n + 1):
        rows, cols = np.nonzero(lab == i)
        ext = max(rows.max() - rows.min(), cols.max() - cols.min()) * pitch
        if ext >= min_len_mm / 1000.0:
            flag[rows, cols] = True
    if flag.any():
        flag = ru.dilate(flag, int(round(skip_mm / 1000.0 / pitch)))
    return flag, lo


def polygon_area(pts):
    x, y = pts[:, 0], pts[:, 1]
    return 0.5 * abs(float(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1))))


def min_radius(pts, chord, skip=None, pitch=0.002, smooth_mm=6.0, arc_mm=24.0,
               chord_skip_mm=40.0):
    """Tightest radius (m) of a closed outline polygon: resampled by arc length every 1 mm,
    smoothed by a moving average of smooth_mm, then the radius of the circle through the
    points +-arc_mm along the outline. Outline within chord_skip_mm of a chord, and inside
    `skip` (mask, origin; see protrusion_skip), is ignored. Radii below about arc_mm / 2 read
    as roughly that size (not resolved)."""
    seg = np.linalg.norm(np.diff(np.vstack([pts, pts[:1]]), axis=0), axis=1)
    cum = np.concatenate([[0.0], np.cumsum(seg)])
    total = cum[-1]
    step = 0.001
    ss = np.arange(0, total, step)
    closed = np.vstack([pts, pts[:1]])
    x = np.interp(ss, cum, closed[:, 0])
    y = np.interp(ss, cum, closed[:, 1])
    cflag = np.zeros(len(pts) + 1)
    cflag[:-1] = chord
    cs = np.interp(ss, cum, cflag) > 0
    k = max(1, int(round(smooth_mm / 1000.0 / step)))
    if k > 1:
        pad = 2 * k
        ker = np.ones(k) / k
        xs = np.convolve(np.concatenate([x[-pad:], x, x[:pad]]), ker, mode="same")[pad:-pad]
        ys = np.convolve(np.concatenate([y[-pad:], y, y[:pad]]), ker, mode="same")[pad:-pad]
    else:
        xs, ys = x, y
    m = max(2, int(round(arc_mm / 1000.0 / step)))
    a = np.stack([np.roll(xs, m), np.roll(ys, m)], axis=1)
    bb = np.stack([xs, ys], axis=1)
    c = np.stack([np.roll(xs, -m), np.roll(ys, -m)], axis=1)
    ab = np.linalg.norm(a - bb, axis=1)
    bc = np.linalg.norm(bb - c, axis=1)
    ca = np.linalg.norm(c - a, axis=1)
    area2 = np.abs((bb[:, 0] - a[:, 0]) * (c[:, 1] - a[:, 1])
                   - (bb[:, 1] - a[:, 1]) * (c[:, 0] - a[:, 0]))
    with np.errstate(divide="ignore", invalid="ignore"):
        r = ab * bc * ca / (2.0 * area2)
    bad = np.zeros(len(ss), dtype=bool)
    if cs.any():
        width = int(round(chord_skip_mm / 1000.0 / step))
        for i in np.nonzero(cs)[0][::max(1, width // 4)]:
            bad[np.arange(i - width, i + width + 1) % len(ss)] = True
        bad |= cs
    if skip is not None and skip[0].any():
        mask, lo = skip
        ix = np.clip(((xs - lo[0]) / pitch).astype(np.int64), 0, mask.shape[1] - 1)
        iy = np.clip(((ys - lo[1]) / pitch).astype(np.int64), 0, mask.shape[0] - 1)
        # skip the samples of the arc (+-arc_mm) as well as the centre sample
        hit = mask[iy, ix]
        grown = hit.copy()
        for sh in range(1, m + 1, 4):
            grown |= np.roll(hit, sh) | np.roll(hit, -sh)
        bad |= grown
    ok = np.isfinite(r) & ~bad
    return float(r[ok].min()) if ok.any() else None


def section_metrics(outline, flat_height=None):
    pts = outline["pts"]
    x, z = pts[:, 0], pts[:, 1]
    width, height = float(x.max() - x.min()), float(z.max() - z.min())
    area = polygon_area(pts)
    out = {"width_m": width, "height_m": height, "ratio": width / height,
           "fill": area / (width * height), "area_m2": area}
    if flat_height:
        # window centred on the widest height; each side: line fit of x against z
        zs = np.linspace(z.min(), z.max(), 400)
        right = np.array([x[np.abs(z - zz) < 0.004].max() if (np.abs(z - zz) < 0.004).any() else np.nan
                          for zz in zs])
        left = np.array([x[np.abs(z - zz) < 0.004].min() if (np.abs(z - zz) < 0.004).any() else np.nan
                         for zz in zs])
        wid = right - left
        good = np.nan_to_num(wid, nan=-1) >= 0.98 * np.nanmax(wid)
        zc = float(zs[good].mean())
        win = (zs >= zc - flat_height / 2) & (zs <= zc + flat_height / 2) & np.isfinite(wid)
        dev = 0.0
        if win.sum() > 3:
            for side in (right, left):
                pf = np.polyfit(zs[win], side[win], 1)
                dev = max(dev, float(np.abs(side[win] - np.polyval(pf, zs[win])).max()))
        out["flat_m"] = dev
    out["radius_m"] = min_radius(pts, outline["chord"], protrusion_skip(outline))
    return out


def run_sections(data, cfg):
    f3 = cfg["F3_sections"]
    hard = cfg["gates"]["hard"]
    gates, details = [], []
    nodes = set(f3.get("nodes", []))
    prims = data.select(nodes=nodes) if nodes else data.select(
        cfg["model"].get("exterior_exclude_nodes", []))
    tris = data.tri_array(prims)
    for sec in cfg.get("F3_section", []):
        segs = cut_segments(tris, AXIS_INDEX[sec.get("axis", "y")], sec["at"])
        shape = section_outline(segs, x_clip=sec.get("x_clip_m"))
        base = f"F3.{sec['name']}"
        if shape is None:
            for key in ("ratio", "fill", "radius") + (("flat",) if "flat_mm_max" in sec else ()):
                gates.append(fm.gate(f"{base}.{key}", f"{sec['name']} {key}", None, "", None, "",
                                     False, fm.is_hard(f"{base}.{key}", hard),
                                     reason="the plane cuts no closed outline"))
            details.append({"name": sec["name"], "found": False})
            continue
        m = section_metrics(shape, sec.get("flat_height_m"))
        details.append({"name": sec["name"], "found": True,
                        **{k: fm.rnd(v, 4) for k, v in m.items() if v is not None}})
        lo, hi = sec["ratio"]
        gates.append(fm.gate(f"{base}.ratio", f"{sec['name']} width/height", fm.rnd(m["ratio"], 3),
                             "in", [lo, hi], "ratio", lo <= m["ratio"] <= hi,
                             fm.is_hard(f"{base}.ratio", hard)))
        gates.append(fm.gate(f"{base}.fill", f"{sec['name']} fill ratio", fm.rnd(m["fill"], 3),
                             ">=", sec["fill_min"], "ratio", m["fill"] >= sec["fill_min"],
                             fm.is_hard(f"{base}.fill", hard)))
        if "flat_mm_max" in sec:
            v = m.get("flat_m", 0.0) * 1000
            gates.append(fm.gate(f"{base}.flat", f"{sec['name']} side flatness", fm.rnd(v, 1),
                                 "<=", sec["flat_mm_max"], "mm", v <= sec["flat_mm_max"],
                                 fm.is_hard(f"{base}.flat", hard)))
        r = None if m["radius_m"] is None else m["radius_m"] * 1000
        lo, hi = sec["radius_mm"]
        gates.append(fm.gate(f"{base}.radius", f"{sec['name']} tightest corner", fm.rnd(r, 1),
                             "in", [lo, hi], "mm", None if r is None else lo <= r <= hi,
                             fm.is_hard(f"{base}.radius", hard)))
    return gates, details


# ---------------------------------------------------------------- F5


def hard_crease_fraction(data, cfg, angle_deg=30.0):
    """Share of the length of two-face edges whose faces meet at more than angle_deg."""
    excl = cfg["model"].get("exterior_exclude_nodes", [])
    total_len, hard_len = 0.0, 0.0
    cosmax = math.cos(math.radians(angle_deg))
    nodes = sorted({p.node for p in data.prims if p.node not in excl})
    for name in nodes:
        prims = [p for p in data.prims if p.node == name]
        verts = np.concatenate([p.verts for p in prims])
        off = np.cumsum([0] + [len(p.verts) for p in prims])
        tris = np.concatenate([p.tris + o for p, o in zip(prims, off[:-1])])
        key = np.round(verts / 1e-6).astype(np.int64)
        _, inv = np.unique(key, axis=0, return_inverse=True)
        inv = inv.reshape(-1)
        t = inv[tris]
        pa, pb, pc = verts[tris[:, 0]], verts[tris[:, 1]], verts[tris[:, 2]]
        nrm = np.cross(pb - pa, pc - pa)
        ln = np.linalg.norm(nrm, axis=1)
        ok = ln > 1e-14
        nrm[ok] /= ln[ok][:, None]
        e = np.concatenate([t[:, [0, 1]], t[:, [1, 2]], t[:, [2, 0]]])
        fi = np.concatenate([np.arange(len(t))] * 3)
        a = np.minimum(e[:, 0], e[:, 1])
        b = np.maximum(e[:, 0], e[:, 1])
        ekey = a.astype(np.int64) * (int(inv.max()) + 1) + b
        order = np.argsort(ekey, kind="stable")
        ekey, fi, a, b = ekey[order], fi[order], a[order], b[order]
        first = np.concatenate([[True], ekey[1:] != ekey[:-1]])
        starts = np.nonzero(first)[0]
        counts = np.diff(np.concatenate([starts, [len(ekey)]]))
        two = counts == 2
        s2 = starts[two]
        f1i, f2i = fi[s2], fi[s2 + 1]
        # length of each such edge from the unwelded positions of the welded vertex ids
        rep = np.zeros((int(inv.max()) + 1, 3))
        rep[inv] = verts
        el = np.linalg.norm(rep[a[s2]] - rep[b[s2]], axis=1)
        valid = ok[f1i] & ok[f2i]
        dots = (nrm[f1i] * nrm[f2i]).sum(axis=1)
        total_len += float(el[valid].sum())
        hard_len += float(el[valid & (dots < cosmax)].sum())
    return (hard_len / total_len if total_len else 0.0), hard_len, total_len


def _worst(values, lo, hi):
    """The value farthest outside [lo, hi] (relative to the range), else the one nearest an end."""
    def excess(v):
        return max(lo - v, v - hi, -(min(v - lo, hi - v)))
    return max(values, key=excess)


def run_edges(data, cfg):
    f5 = cfg["F5_edges"]
    hard = cfg["gates"]["hard"]
    gates, details = [], []
    worst = 0.0
    for e in cfg.get("F5_edge", []):
        base = f"F5.{e['name']}"
        probes = e.get("probe", [])
        results = [edge_probe(data, pr) for pr in probes]
        for pr, r in zip(probes, results):
            where = f"{'+'.join(pr['nodes'])} {pr['plane']['axis']}={pr['plane']['at']} {pr['dir']}"
            details.append({"name": e["name"], "probe": where,
                            **{k: (fm.rnd(v, 6) if isinstance(v, float) else v)
                               for k, v in r.items() if k != "tip"}})
        found = [r for r in results if r["found"]]
        if not probes or len(found) < len(results):
            reason = "no probe defined yet" if not probes else next(
                r["reason"] for r in results if not r["found"])
            gates.append(fm.gate(f"{base}.radius", f"{e['name']} edge radius", None, "in",
                                 e["radius_mm"], "mm", False, fm.is_hard(f"{base}.radius", hard),
                                 reason=reason))
            continue
        lo, hi = e["radius_mm"]
        radii = [r["radius_m"] * 1000.0 for r in found]
        worst = max(worst, max(radii))
        unresolved = [r for r in found if r["segments"] < f5["min_segments"]
                      and r["radius_m"] * 1000.0 < f5["resolved_radius_mm"]]
        r_worst = _worst(radii, lo, hi)
        ok = all(lo <= r <= hi for r in radii)
        gates.append(fm.gate(f"{base}.radius", f"{e['name']} edge radius (worst of {len(found)})",
                             fm.rnd(r_worst, 2), "in", [lo, hi], "mm", ok,
                             fm.is_hard(f"{base}.radius", hard),
                             reason=None if not unresolved else
                             f"{len(unresolved)} probe(s) with fewer than {f5['min_segments']} mesh segments"))
        if "thickness_mm" in e:
            ths = [None if r["thickness_m"] is None else r["thickness_m"] * 1000.0 for r in found]
            lo, hi = e["thickness_mm"]
            if any(t is None for t in ths):
                ok_t, val = None, None
            else:
                val = _worst(ths, lo, hi)
                ok_t = all(lo <= t <= hi for t in ths)
            gates.append(fm.gate(f"{base}.thickness", f"{e['name']} edge thickness (worst)",
                                 fm.rnd(val, 2), "in", [lo, hi], "mm", ok_t,
                                 fm.is_hard(f"{base}.thickness", hard),
                                 reason=None if ok_t is not None else "section thinner than 1 mm at the tip"))
    gates.append(fm.gate("F5.max_radius", "largest measured named-edge radius",
                         fm.rnd(worst, 2), "<=", f5["no_named_edge_radius_over_mm"], "mm",
                         worst <= f5["no_named_edge_radius_over_mm"],
                         fm.is_hard("F5.max_radius", hard)))
    frac, hl, tl = hard_crease_fraction(data, cfg)
    lo, hi = f5["hard_crease_fraction"]
    details.append({"name": "hard_crease_fraction", "hard_edge_length_m": fm.rnd(hl, 2),
                    "edge_length_m": fm.rnd(tl, 2)})
    gates.append(fm.gate("F5.hard_creases", "share of edge length with a >30 degree crease",
                         fm.rnd(frac, 4), "in", [lo, hi], "frac", lo <= frac <= hi,
                         fm.is_hard("F5.hard_creases", hard)))
    return gates, details
