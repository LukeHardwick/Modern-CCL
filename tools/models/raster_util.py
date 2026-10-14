# Small raster helpers (numpy only) for the fidelity checker (task T22e): connected-component
# labelling, disk dilation / erosion, triangle rasterisation, boundary extraction and
# chunked nearest-point distances. No scipy: everything here is deterministic integer or
# float64 arithmetic whose result does not depend on the machine or on numpy's version.

import numpy as np

# Pixel centres are sampled at (i + 0.5 + TIE_X, j + 0.5 + TIE_Y): an irrational-looking
# offset so that a sample never lies exactly on a shared triangle edge (no double counting,
# no gaps between neighbours) without a top-left rule.
TIE_X = 1.234e-4
TIE_Y = 3.711e-4


def label_components(mask, connectivity=4):
    """Connected components of a boolean image by run-length union-find.
    Returns (labels int32 with 0 = background, count)."""
    mask = np.asarray(mask, dtype=bool)
    h, w = mask.shape
    labels = np.zeros((h, w), dtype=np.int32)
    parent = [0]

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            if ra < rb:
                parent[rb] = ra
            else:
                parent[ra] = rb

    pad = 1 if connectivity == 8 else 0
    prev = []  # runs of the previous row: (start, end_exclusive, id)
    for y in range(h):
        row = mask[y]
        if not row.any():
            prev = []
            continue
        d = np.diff(np.concatenate(([0], row.view(np.int8), [0])))
        starts = np.nonzero(d == 1)[0]
        ends = np.nonzero(d == -1)[0]
        cur = []
        j = 0
        for s, e in zip(starts.tolist(), ends.tolist()):
            parent.append(len(parent))
            rid = len(parent) - 1
            while j < len(prev) and prev[j][1] + pad <= s:
                j += 1
            k = j
            while k < len(prev) and prev[k][0] - pad < e:
                union(rid, prev[k][2])
                k += 1
            labels[y, s:e] = rid
            cur.append((s, e, rid))
        prev = cur
    if labels.max() == 0:
        return labels, 0
    roots = np.array([find(i) for i in range(len(parent))], dtype=np.int64)
    uniq = np.unique(roots[labels[labels > 0]])
    remap = np.zeros(len(parent), dtype=np.int32)
    remap[uniq] = np.arange(1, len(uniq) + 1, dtype=np.int32)
    out = np.where(labels > 0, remap[roots[labels]], 0).astype(np.int32)
    return out, len(uniq)


def disk_offsets(r):
    r = int(r)
    return [(dy, dx) for dy in range(-r, r + 1) for dx in range(-r, r + 1)
            if dx * dx + dy * dy <= r * r + 0.5]


def _shift(a, dy, dx, fill):
    h, w = a.shape
    out = np.full_like(a, fill)
    ys, ye = max(0, dy), min(h, h + dy)
    xs, xe = max(0, dx), min(w, w + dx)
    out[ys:ye, xs:xe] = a[ys - dy:ye - dy, xs - dx:xe - dx]
    return out


def dilate(mask, r):
    """Binary dilation by a disk of radius r pixels."""
    mask = np.asarray(mask, dtype=bool)
    if r <= 0:
        return mask.copy()
    out = np.zeros_like(mask)
    for dy, dx in disk_offsets(r):
        out |= _shift(mask, dy, dx, False)
    return out


def erode(mask, r):
    """Binary erosion by a disk of radius r pixels (outside the image counts as empty)."""
    mask = np.asarray(mask, dtype=bool)
    if r <= 0:
        return mask.copy()
    out = np.ones_like(mask)
    for dy, dx in disk_offsets(r):
        out &= _shift(mask, dy, dx, False)
    return out


def boundary(mask):
    """Pixels of the mask that have a 4-neighbour outside it (inside boundary)."""
    mask = np.asarray(mask, dtype=bool)
    p = np.pad(mask, 1, constant_values=False)
    inner = p[1:-1, 1:-1] & p[:-2, 1:-1] & p[2:, 1:-1] & p[1:-1, :-2] & p[1:-1, 2:]
    return mask & ~inner


def nearest_distances(a, b, chunk=2000):
    """For each point of a (N, 2) the distance to the nearest point of b (M, 2)."""
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    if len(b) == 0:
        return np.full(len(a), np.inf)
    out = np.empty(len(a))
    bb = (b * b).sum(1)
    for i in range(0, len(a), chunk):
        c = a[i:i + chunk]
        d2 = (c * c).sum(1)[:, None] + bb[None, :] - 2.0 * (c @ b.T)
        out[i:i + chunk] = np.sqrt(np.maximum(d2.min(axis=1), 0.0))
    return out


def rasterise_triangles(tri, width, height, owner=None, mode="count", max_cells=3_000_000):
    """Rasterise triangles given in pixel coordinates tri (N, 3, 2) (x right, y down; pixel
    (i, j) has its centre at (i + 0.5, j + 0.5)) onto a width x height grid.
    mode 'count': int32 image with the number of triangles covering each pixel;
    mode 'owner': int32 image holding the largest owner value (>= 1) covering a pixel (0 =
    empty); owner is an int array (N,)."""
    tri = np.asarray(tri, dtype=np.float64)
    out = np.zeros((height, width), dtype=np.int32)
    if len(tri) == 0:
        return out
    x = tri[:, :, 0]
    y = tri[:, :, 1]
    area2 = ((x[:, 1] - x[:, 0]) * (y[:, 2] - y[:, 0])
             - (x[:, 2] - x[:, 0]) * (y[:, 1] - y[:, 0]))
    keep = np.abs(area2) > 1e-12
    ix0 = np.maximum(np.floor(x.min(1) - 0.5 - TIE_X).astype(np.int64) + 0, 0)
    ix1 = np.minimum(np.ceil(x.max(1) - 0.5 - TIE_X).astype(np.int64), width - 1)
    iy0 = np.maximum(np.floor(y.min(1) - 0.5 - TIE_Y).astype(np.int64) + 0, 0)
    iy1 = np.minimum(np.ceil(y.max(1) - 0.5 - TIE_Y).astype(np.int64), height - 1)
    keep &= (ix1 >= ix0) & (iy1 >= iy0)
    idx = np.nonzero(keep)[0]
    if len(idx) == 0:
        return out
    bw = ix1[idx] - ix0[idx] + 1
    bh = iy1[idx] - iy0[idx] + 1
    cw = (1 << np.ceil(np.log2(np.maximum(bw, 1))).astype(np.int64))
    ch = (1 << np.ceil(np.log2(np.maximum(bh, 1))).astype(np.int64))
    key = cw * 1_000_003 + ch
    order = np.argsort(key, kind="stable")
    idx, key, cw, ch = idx[order], key[order], cw[order], ch[order]
    flat = out.reshape(-1)
    start = 0
    while start < len(idx):
        end = start
        while end < len(idx) and key[end] == key[start]:
            end += 1
        gw, gh = int(cw[start]), int(ch[start])
        step = max(1, max_cells // (gw * gh))
        for s in range(start, end, step):
            sel = idx[s:min(end, s + step)]
            gx = np.arange(gw)[None, None, :]
            gy = np.arange(gh)[None, :, None]
            px = ix0[sel][:, None, None] + gx + 0.5 + TIE_X
            py = iy0[sel][:, None, None] + gy + 0.5 + TIE_Y
            ax, ay = x[sel, 0][:, None, None], y[sel, 0][:, None, None]
            bx, by = x[sel, 1][:, None, None], y[sel, 1][:, None, None]
            cx, cy = x[sel, 2][:, None, None], y[sel, 2][:, None, None]
            e0 = (bx - ax) * (py - ay) - (by - ay) * (px - ax)
            e1 = (cx - bx) * (py - by) - (cy - by) * (px - bx)
            e2 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx)
            inside = ((e0 >= 0) & (e1 >= 0) & (e2 >= 0)) | ((e0 <= 0) & (e1 <= 0) & (e2 <= 0))
            inside &= (ix0[sel][:, None, None] + gx <= ix1[sel][:, None, None])
            inside &= (iy0[sel][:, None, None] + gy <= iy1[sel][:, None, None])
            t, yy, xx = np.nonzero(inside)
            if len(t) == 0:
                continue
            pix = (iy0[sel][t] + yy) * width + (ix0[sel][t] + xx)
            if mode == "count":
                flat += np.bincount(pix, minlength=flat.size).astype(np.int32)
            else:
                np.maximum.at(flat, pix, np.asarray(owner)[sel][t].astype(np.int32))
        start = end
    return out
