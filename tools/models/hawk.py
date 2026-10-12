# Hero model: a generic advanced jet trainer, proportions from the published dimensions of the
# BAE Systems Hawk 128 (length 12.43 m, span 9.94 m, height 3.98 m, wing area ~16.7 m2). No
# markings, no traced drawings: every shape below is a parametric loft or primitive.
#
# Blender frame: nose -Y, up +Z, origin ~ centre of gravity, ground contact at z = GROUND.
# Vehicle LEFT is +X, RIGHT is -X.

import math

from mathutils import Matrix, Vector

from mm_common import (LEFT, RIGHT, MAT, Mesh, empty, foil_ring, grey, hash01, lerp, make_ao,
                       make_materials, mix, naca_t, pchip, smoothstep, srgb,
                       superellipse_point, tone)

Y_NOSE = -5.75
Y_TAIL = 6.53
GROUND = -1.45

# --------------------------------------------------------------------------- colours
PAINT = srgb(0.80, 0.82, 0.84)
PAINT_WING = srgb(0.76, 0.78, 0.80)
PAINT_SURF = srgb(0.70, 0.72, 0.75)
RADOME = srgb(0.20, 0.21, 0.23)
FRAME = srgb(0.07, 0.07, 0.08)
BLACK = srgb(0.04, 0.04, 0.045)
RUBBER = srgb(0.05, 0.05, 0.05)
STEEL = srgb(0.55, 0.57, 0.60)
NOZZLE = srgb(0.22, 0.20, 0.19)
GLASS = srgb(0.16, 0.22, 0.28)
LINE = srgb(0.42, 0.44, 0.47)
SOOT = srgb(0.17, 0.155, 0.145)
WALK = srgb(0.46, 0.47, 0.49)
ERODE = srgb(0.36, 0.38, 0.41)
RED = srgb(0.9, 0.05, 0.05)
GREEN = srgb(0.05, 0.8, 0.15)
WHITE_LAMP = srgb(0.95, 0.95, 0.9)

# --------------------------------------------------------------------------- fuselage
# (y, half width, top z, bottom z, squareness n)
FUS_KEYS = [
    (-5.75, 0.00, -0.04, -0.04, 2.0),
    (-5.55, 0.07, 0.02, -0.14, 2.0),
    (-5.10, 0.18, 0.16, -0.30, 2.0),
    (-4.50, 0.33, 0.38, -0.46, 2.1),
    (-3.90, 0.48, 0.66, -0.58, 2.3),
    (-3.40, 0.60, 0.88, -0.66, 2.5),
    (-2.60, 0.68, 1.00, -0.74, 2.6),
    (-1.20, 0.72, 1.04, -0.76, 2.6),
    (0.40, 0.72, 1.00, -0.76, 2.6),
    (1.80, 0.66, 0.90, -0.70, 2.5),
    (3.00, 0.56, 0.80, -0.58, 2.4),
    (4.20, 0.46, 0.70, -0.42, 2.2),
    (5.20, 0.39, 0.60, -0.30, 2.1),
    (5.72, 0.36, 0.52, -0.26, 2.0),
]
_ys = [k[0] for k in FUS_KEYS]
_fw = pchip(_ys, [k[1] for k in FUS_KEYS])
_ft = pchip(_ys, [k[2] for k in FUS_KEYS])
_fb = pchip(_ys, [k[3] for k in FUS_KEYS])
_fn = pchip(_ys, [k[4] for k in FUS_KEYS])


def fus_section(y):
    return _fw(y), _ft(y), _fb(y), _fn(y)


def fus_point(y, theta, grow=0.0):
    """Point on the fuselage skin (angle theta: 0 = +x side, pi/2 = top)."""
    w, top, bot, n = fus_section(y)
    sx, sz = superellipse_point(theta, n)
    zm, hh = (top + bot) / 2.0, (top - bot) / 2.0
    return Vector(((w + grow) * sx, y, zm + (hh + grow) * sz))


def fus_color(c):
    if c.y < -5.10:
        return RADOME
    top = _ft(c.y)
    if -4.60 < c.y < -3.62 and c.z > top - 0.12 and abs(c.x) < 0.40:
        return BLACK  # anti-glare panel
    cell = (int(math.floor(c.y / 0.6)), int(math.degrees(math.atan2(c.z - 0.1, c.x)) // 25))
    col = tone(PAINT, 1.0 + 0.09 * (hash01(*cell) - 0.5))  # panel-to-panel tone, +-4.5 %
    soot = smoothstep((c.y - 4.2) / 1.6) * (1.0 if c.z < 0.15 else 0.7)
    if soot > 0:
        col = mix(col, SOOT, 0.7 * soot)
    # walkway: anti-slip patch on the left side of the spine behind the canopy handled in decals
    return col


def wing_color(c):
    cell = (int(math.floor(abs(c.x) / 0.7)), int(math.floor(c.y / 0.7)), c.x > 0)
    return tone(PAINT_WING, 1.0 + 0.08 * (hash01(*cell[:2]) - 0.5))


def segs(base, d, lo=8):
    return max(lo, int(round(base * d / 4.0)) * 4)


def count(base, d, lo=3):
    return max(lo, int(round(base * d)))


# --------------------------------------------------------------------------- canopy shell
CANOPY_Y0, CANOPY_Y1 = -3.60, -0.25
CANOPY_SPLIT = -2.95  # fixed windscreen forward of this, one-piece canopy behind
CANOPY_MID = -1.90  # frame between the seats (the rear seat is higher)
SILL = 0.80
_cy = [-3.60, -3.30, -2.90, -2.45, -2.10, -1.75, -1.40, -0.90, -0.55, -0.35, -0.25]
_ch = [0.08, 0.42, 0.80, 0.93, 0.90, 0.94, 1.12, 1.14, 0.95, 0.55, 0.0]
_canopy_h = pchip(_cy, _ch)


def sill_z(y):
    return SILL


def canopy_ring(y, thetas, grow=0.0):
    w, top, bot, n = fus_section(y)
    zs = sill_z(y)
    zm, hh = (top + bot) / 2.0, (top - bot) / 2.0
    zp = (zs - zm) / hh
    wc = w * max(0.0, 1.0 - abs(zp) ** n) ** (1.0 / n) * 0.985
    h = _canopy_h(y)
    pts = []
    for th in thetas:
        sx, sz = superellipse_point(th, 2.2)
        pts.append(Vector((wc * sx * (1 + grow), y, zs + (h + grow) * sz)))
    return pts


def canopy_rings(y0, y1, count_y, thetas, grow=0.0):
    out = []
    for i in range(count_y + 1):
        t = i / count_y
        out.append(canopy_ring(y0 + (y1 - y0) * t, thetas, grow))
    return out


# --------------------------------------------------------------------------- aerofoil surfaces
WING = dict(x0=0.50, x1=4.97, le0=-1.35, te=1.55, sweep=0.459, tc0=0.100, tc1=0.070, z=-0.30)
FLAP_HINGE = 0.95
AIL_HINGE = 1.19
FLAP_X = (0.78, 2.78)
AIL_X = (3.02, 4.53)
GAP = 0.012


def wing_le_te(side, x):
    le_y = WING["le0"] + (x - WING["x0"]) * WING["sweep"]
    return (Vector((side * x, le_y, WING["z"])), Vector((side * x, WING["te"], WING["z"])))


def wing_tc(x):
    return lerp(WING["tc0"], WING["tc1"], (x - WING["x0"]) / (WING["x1"] - WING["x0"]))


def wing_station(side, x, a_y=None, b_y=None, scale=1.0, mid=None, cnt=22):
    le, te = wing_le_te(side, x)
    c = te.y - le.y
    a = 0.0 if a_y is None else (a_y - le.y) / c
    b = 1.0 if b_y is None else (b_y - le.y) / c
    return foil_ring(le, te, (0, 0, 1), wing_tc(x), a, b, cnt, scale, mid)


def fin_le_te(z):
    le_y = 2.30 + (z - 0.45) * (2.25 / 2.05)
    te_y = 5.85 - (z - 0.45) * 0.05
    return Vector((0, le_y, z)), Vector((0, te_y, z))


def fin_tc(z):
    return lerp(0.085, 0.065, (z - 0.45) / 2.08)


RUDDER_HINGE = 5.25
FIN_TOP = 2.53


def fin_ring(z, a_y=None, b_y=None, scale=1.0, mid=None, cnt=20):
    le, te = fin_le_te(z)
    c = te.y - le.y
    a = 0.0 if a_y is None else (a_y - le.y) / c
    b = 1.0 if b_y is None else (b_y - le.y) / c
    return foil_ring(le, te, (1, 0, 0), fin_tc(z), a, b, cnt, scale, mid)


def tail_le_te(side, x):
    t = x - 0.25
    z = 0.15 - t * 0.14
    return (Vector((side * x, 3.90 + t * 0.70, z)), Vector((side * x, 5.60 + t * 0.17, z)))


def tail_ring(side, x, scale=1.0, mid=None, cnt=18):
    le, te = tail_le_te(side, x)
    tc = lerp(0.075, 0.060, (x - 0.25) / 1.7)
    return foil_ring(le, te, (0, 0, 1), tc, 0.0, 1.0, cnt, scale, mid)


TAIL_PIVOT_Y = 4.75
TAIL_Z = 0.15
TAIL_X1 = 1.95

# --------------------------------------------------------------------------- build




def _sgn(v):
    return math.copysign(1.0, v)


def fus_surf(u, v):
    """Fuselage skin as a surface: u = y, v = angle (0 = +x/left, pi/2 = top)."""
    p = fus_point(u, v)
    w, top, bot, n = fus_section(u)
    zm, hh = (top + bot) / 2.0, (top - bot) / 2.0
    x, z = p.x / w, (p.z - zm) / hh
    nrm = Vector((_sgn(x) * abs(x) ** (n - 1) / w, 0.0, _sgn(z) * abs(z) ** (n - 1) / hh))
    return p, nrm.normalized()


def wing_surf(side, upper):
    sg = 1.0 if upper else -1.0

    def f(u, v):
        le, te = wing_le_te(side, u)
        th = naca_t(v) * wing_tc(u) * (te.y - le.y)
        return le + (te - le) * v + Vector((0, 0, sg * th)), Vector((0, 0, sg))
    return f


def fin_surf(side):
    def f(u, v):
        le, te = fin_le_te(u)
        th = naca_t(v) * fin_tc(u) * (te.y - le.y)
        return le + (te - le) * v + Vector((side * th, 0, 0)), Vector((side, 0, 0))
    return f


def line(a, b, k):
    return [(lerp(a[0], b[0], i / k), lerp(a[1], b[1], i / k)) for i in range(k + 1)]


# --------------------------------------------------------------------------- surface detail


def fus_rect(m, y0, y1, t0, t1, hatch=False):
    """Access panel outline (4 lines), fastener rows on the long edges, optional raised hatch."""
    edges = [line((y0, t0), (y1, t0), max(2, int((y1 - y0) / 0.22))),
             line((y0, t1), (y1, t1), max(2, int((y1 - y0) / 0.22))),
             line((y0, t0), (y0, t1), max(2, int(abs(t1 - t0) / 0.2))),
             line((y1, t0), (y1, t1), max(2, int(abs(t1 - t0) / 0.2)))]
    for e in edges:
        m.ribbon(fus_surf, e, 0.007, 0.004, LINE)
    if hatch:
        m.patch(fus_surf, y0, y1, t0, t1, max(1, int((y1 - y0) / 0.3)),
                max(1, int(abs(t1 - t0) / 0.25)), 0.006,
                lambda c: tone(PAINT, 1.0 + 0.10 * (hash01(c.y, c.z) - 0.5) + 0.03))
    if y1 - y0 > 0.5:
        k = int((y1 - y0) / 0.12)
        for t in (t0, t1):
            for i in range(1, k):
                m.dot(fus_surf, lerp(y0, y1, i / k), lerp(t, t0 + (t1 - t0) / 2, 0.06), 0.012,
                      0.02, 0.005, LINE)


LEFT_RECTS = [  # (y0, y1, theta0, theta1, raised hatch), left side; right side is mirrored
    (-4.45, -3.80, 0.20, 1.20, True), (-4.70, -4.15, -0.25, 0.35, False),
    (-3.40, -2.70, -0.05, 0.40, False), (-2.60, -2.10, -0.45, 0.15, True),
    (0.10, 1.00, 0.40, 1.15, True), (1.30, 2.40, 0.25, 1.05, False),
    (2.60, 3.50, 0.30, 1.00, True), (3.70, 4.60, 0.20, 0.90, False),
    (0.20, 1.60, -0.55, -0.10, True), (1.80, 3.00, -0.65, -0.20, False),
    (3.30, 4.30, -0.75, -0.30, False), (4.50, 5.50, -0.55, 0.45, True),
    (-3.00, -2.20, -1.00, -0.55, False), (-1.60, -0.80, -1.00, -0.55, True),
    (-0.10, 0.90, -1.10, -0.70, False),
]


def fuselage_decals(m, d):
    for y in (-4.55, -3.75, -1.0, 0.0, 0.95, 2.0, 3.10, 4.15, 5.05):
        m.ribbon(fus_surf, line((y, -math.pi), (y, math.pi), 40), 0.007, 0.004, LINE)
    for t in (0.50, 2.64, -0.50, -2.64, 1.2, 1.94, -1.2, -1.94):
        for ya, yb in ((-4.5, -2.0), (-1.0, 1.8), (2.0, 5.0)):
            m.ribbon(fus_surf, line((ya, t), (yb, t), int((yb - ya) / 0.25)), 0.006, 0.004, LINE)
    for y0, y1, t0, t1, hatch in LEFT_RECTS:
        fus_rect(m, y0, y1, t0, t1, hatch)
        fus_rect(m, y0, y1, math.pi - t1, math.pi - t0, hatch)
    # anti-slip walkways beside the cockpit, boarding step recess, grab handle
    for a, b in ((0.05, 0.40), (math.pi - 0.40, math.pi - 0.05)):
        m.patch(fus_surf, -2.75, -2.05, a, b, 3, 2, 0.006, WALK)
    m.patch(fus_surf, -2.05, -1.75, 0.05, 0.30, 1, 1, 0.006, srgb(0.2, 0.2, 0.22))
    m.sweep([Vector(p) for p in ((0.56, -2.95, 0.80), (0.62, -2.95, 0.90), (0.62, -2.55, 0.90),
                                  (0.56, -2.55, 0.80))], 0.012, 6, mat="metal", color=STEEL)


def wing_decals(m, side, d):
    for upper in (True, False):
        s = wing_surf(side, upper)
        for x in (1.15, 1.75, 2.40, 3.50, 4.10, 4.70):
            m.ribbon(s, line((x, 0.05), (x, 0.62), 8), 0.006, 0.004, LINE)
        for v, xa, xb in ((0.14, 0.80, 4.80), (0.36, 0.80, 4.80), (0.56, 0.80, 4.40)):
            m.ribbon(s, line((xa, v), (xb, v), 14), 0.006, 0.004, LINE)
        for x in [0.9 + 0.1 * i for i in range(39)]:
            m.dot(s, x, 0.14, 0.012, 0.014, 0.005, LINE)
        m.patch(s, 0.80, 4.85, 0.004, 0.05, 10, 1, 0.004, ERODE)  # leading-edge strip
    up = wing_surf(side, True)
    m.patch(up, 0.78, 1.90, 0.24, 0.56, 4, 2, 0.006, WALK)  # walkway
    # vortex generators in a row near the leading edge, alternately toed in and out
    for i in range(14):
        x = 1.0 + 0.11 * i
        p, _n = up(x, 0.20)
        psi = math.radians(14.0 if i % 2 else -14.0)
        along = Vector((math.sin(psi), math.cos(psi), 0.0)) * 0.07
        across = Vector((math.cos(psi), -math.sin(psi), 0.0)) * 0.004
        m.plate([p, p + along, p + along * 0.5 + Vector((0, 0, 0.04))], across, mat="paint",
                color=PAINT_SURF)
    # wing fence
    pa, _ = up(2.62, 0.02)
    pb, _ = up(2.62, 0.66)
    m.plate([pa - Vector((0, 0.0, 0.02)), pb - Vector((0, 0.0, 0.02)), pb + Vector((0, -0.05, 0.10)),
             pa + Vector((0, 0.0, 0.14))], (side * 0.012, 0, 0), mat="paint", color=PAINT_SURF)


def fin_decals(m, side, d):
    s = fin_surf(side)
    for z in (0.9, 1.35, 1.8, 2.2):
        m.ribbon(s, line((z, 0.06), (z, 0.60), 8), 0.006, 0.004, LINE)
    for v in (0.18, 0.38):
        m.ribbon(s, line((0.8, v), (2.3, v), 8), 0.006, 0.004, LINE)
    for z in [0.8 + 0.1 * i for i in range(16)]:
        m.dot(s, z, 0.18, 0.014, 0.012, 0.005, LINE)
    m.patch(s, 0.8, 2.40, 0.004, 0.04, 6, 1, 0.004, ERODE)


def skin_x(y, z, side):
    """x of the fuselage skin at height z (outside surface)."""
    w, top, bot, n = fus_section(y)
    zm, hh = (top + bot) / 2.0, (top - bot) / 2.0
    zz = min(0.999, abs((z - zm) / hh))
    return side * w * (1.0 - zz ** n) ** (1.0 / n)


def shape_breaks(m, d):
    # wing-root fillets, upper and lower, both sides
    for side in (LEFT, RIGHT):
        for sg in (1.0, -1.0):
            pts = []
            for i in range(9):
                y = lerp(-1.0, 1.4, i / 8)
                le, te = wing_le_te(side, 0.72)
                v = (y - le.y) / (te.y - le.y)
                z = WING["z"] + sg * (naca_t(v) * wing_tc(0.72) * (te.y - le.y) + 0.004)
                pts.append(Vector((skin_x(y, z, side), y, z + sg * 0.012)))
            m.sweep(pts, 0.042, 6, mat="paint", color=PAINT_WING)
    # strakes ahead of the tailplane ("SMURFs"), ventral fins, tail bumper
    for side in (LEFT, RIGHT):
        w = fus_section(3.9)[0]
        m.plate([(side * (w - 0.03), 3.35, 0.20), (side * (w + 0.17), 3.85, 0.20),
                 (side * (w - 0.03), 4.30, 0.20)], (0, 0, 0.012), mat="paint", color=PAINT_SURF)
        x = side * 0.24
        m.plate([(x, 4.55, -0.36), (x, 5.45, -0.30), (x, 5.35, -0.64), (x, 4.75, -0.62)],
                (side * 0.014, 0, 0), mat="paint", color=PAINT_SURF)
    m.tube((0, 5.30, -0.30), (0, 5.78, -0.40), 0.04, 0.03, 8, mat="dark", color=RUBBER)
    # fuel vents, whip antenna, beacon, nose blade
    m.tube((0.30, 3.2, -0.52), (0.36, 3.45, -0.62), 0.012, 0.010, 6, mat="metal", color=STEEL)
    m.tube((-0.30, 3.2, -0.52), (-0.36, 3.45, -0.62), 0.012, 0.010, 6, mat="metal", color=STEEL)
    m.sweep([Vector(p) for p in ((0, 1.9, 1.10), (0, 2.0, 1.28), (0, 2.12, 1.42))], 0.008, 5,
            mat="metal", color=STEEL)
    m.tube((0, 0.6, 1.16), (0, 0.6, 1.26), 0.07, 0.05, 10, mat="light", color=RED)
    m.plate([(0, -4.4, 0.35), (0, -4.0, 0.35), (0, -4.1, 0.2), (0, -4.35, 0.2)], (0.01, 0, 0),
            mat="paint", color=PAINT_SURF)


def build_spine(m, d):
    keys = [(-0.30, 0.00, 1.02), (-0.15, 0.22, 1.10), (0.60, 0.28, 1.17), (1.80, 0.24, 1.12),
            (2.80, 0.19, 1.02), (3.60, 0.15, 0.95), (4.60, 0.0, 0.74)]
    ys = [k[0] for k in keys]
    fw = pchip(ys, [k[1] for k in keys])
    ft = pchip(ys, [k[2] for k in keys])
    n_y = count(36, d, 6)
    ns = segs(20, d, 8)
    rings = []
    for i in range(n_y + 1):
        y = lerp(-0.30, 4.60, 0.5 - 0.5 * math.cos(math.pi * i / n_y))
        w, top = fw(y), ft(y)
        hh = 0.32
        zc = top - hh
        ring = []
        for k in range(ns):
            sx, sz = superellipse_point(2 * math.pi * k / ns, 2.2)
            ring.append(Vector((w * sx, y, zc + hh * sz)))
        rings.append(ring)
    m.rings(rings, mat="paint", color=lambda c: tone(PAINT, 1.0 + 0.08 * (hash01(c.y) - 0.5)),
            smooth=True)


def build_fuselage(d):
    m = Mesh("fuselage")
    fine = d >= 0.5
    S = segs(72, d)
    N = count(180, d, 14)
    thetas = [2 * math.pi * k / S for k in range(S)]
    rings = []
    for i in range(N + 1):
        t = i / N
        y = Y_NOSE + (5.72 - Y_NOSE) * (0.5 - 0.5 * math.cos(math.pi * t))
        w, top, bot, n = fus_section(y)
        zm, hh = (top + bot) / 2.0, (top - bot) / 2.0
        ring = []
        for th in thetas:
            sx, sz = superellipse_point(th, n)
            ring.append(Vector((w * sx, y, zm + hh * sz)))
        rings.append(ring)
    m.rings(rings, mat="paint", color=fus_color, smooth=True)
    build_spine(m, d)

    # --- windscreen glass and its frame (fixed)
    nt = count(24, d, 6)
    ws_thetas = [math.pi * k / nt for k in range(nt + 1)]
    ny = count(12, d, 3)
    m.rings(canopy_rings(CANOPY_Y0, CANOPY_SPLIT, ny, ws_thetas, 0.004), close_ring=False,
            solid=False, mat="glass", color=GLASS, smooth=True)
    for y in (CANOPY_Y0 + 0.14, CANOPY_SPLIT):
        pts = canopy_ring(y, [math.pi * k / 18 for k in range(19)], 0.01)
        m.sweep(pts, 0.024, 6, mat="paint", color=FRAME)
    centre = [Vector((0, y, sill_z(y) + _canopy_h(y) + 0.012))
              for y in [CANOPY_Y0 + (CANOPY_SPLIT - CANOPY_Y0) * i / 10 for i in range(11)]]
    m.sweep(centre, 0.018, 6, mat="paint", color=FRAME)

    # --- air intakes at the wing roots, with a splitter plate and boundary-layer gap
    xc0 = 0.97
    for side in (LEFT, RIGHT):
        iring = []
        nt = count(10, d, 4)
        ns_i = segs(24, d, 8)
        for i in range(nt + 1):
            t = i / nt
            y = lerp(-1.90, -0.20, t)
            xc = side * lerp(xc0, 0.58, smoothstep(t))
            a, b = lerp(0.21, 0.19, t), lerp(0.30, 0.36, t)
            ring = []
            for k in range(ns_i):
                th = 2 * math.pi * k / ns_i
                sx, sz = superellipse_point(th, 2.4)
                ring.append(Vector((xc + a * sx, y, 0.12 + b * sz)))
            iring.append(ring)
        m.rings(iring, solid=False, mat="paint", color=PAINT, smooth=True)
        m.sweep(iring[0], 0.024, 6, mat="paint", color=STEEL, closed=True)
        cen = Vector((side * xc0, 0, 0.12))
        duct = [[Vector((cen.x + (p.x - cen.x) * 0.93, y, cen.z + (p.z - cen.z) * 0.93))
                 for p in iring[0]] for y in (-1.74, -1.30)]
        m.rings(duct, mat="dark", color=BLACK, smooth=True)
        m.plate([(side * 0.738, -1.85, -0.10), (side * 0.738, -1.15, -0.10),
                 (side * 0.738, -1.15, 0.38), (side * 0.738, -1.85, 0.32)], (side * 0.014, 0, 0),
                mat="paint", color=PAINT_SURF)

    # --- jet pipe and nozzle (profile of revolution about the Y axis, centre z = 0.13)
    nz = 0.13
    prof = [(5.60, 0.33), (5.80, 0.33), (6.20, 0.29), (6.48, 0.27), (6.53, 0.285),
            (6.53, 0.25), (6.40, 0.235), (6.00, 0.20), (5.90, 0.12), (5.90, 0.0)]
    rings = []
    ns = segs(32, d, 8)
    for y, r in prof:
        rings.append([Vector((r * math.cos(2 * math.pi * k / ns), y,
                              nz + r * math.sin(2 * math.pi * k / ns))) for k in range(ns)])
    m.rings(rings, mat="dark", color=NOZZLE, smooth=True)

    # --- nose pitot probe, blade antennas
    m.tube((0, Y_NOSE + 0.03, -0.04), (0, Y_NOSE - 0.15, -0.04), 0.022, 0.008, 8, mat="metal",
           color=STEEL)
    m.plate([(0.0, -0.20, 1.08), (0.0, 0.15, 1.10), (0.0, 0.08, 1.34), (0.0, -0.10, 1.34)],
            (0.012, 0, 0), mat="paint", color=PAINT_SURF)
    m.plate([(0.0, -2.5, -0.74), (0.0, -2.0, -0.74), (0.0, -2.05, -0.97), (0.0, -2.3, -0.97)],
            (0.012, 0, 0), mat="paint", color=PAINT_SURF)

    # --- underwing pylons (fixed), two per side
    for side in (LEFT, RIGHT):
        for xp in (1.90, 3.30):
            le, _te = wing_le_te(side, xp)
            ya = le.y + 0.45
            x0 = side * xp - 0.04
            m.plate([(x0, ya, -0.36), (x0, ya + 1.05, -0.36), (x0, ya + 0.95, -0.62),
                     (x0, ya + 0.12, -0.66)], (0.08, 0, 0), mat="paint", color=PAINT_SURF)

    build_fixed_wing(m, d)

    # --- fin (fixed part)
    fz = lambda z0, z1, b_y, k: [fin_ring(lerp(z0, z1, i / k), None, b_y, cnt=count(20, d, 5))
                                 for i in range(k + 1)]
    m.rings(fz(0.45, 0.62, None, 1), mat="paint", color=PAINT_WING, smooth=True)
    m.rings(fz(0.62, 2.42, RUDDER_HINGE, count(4, d, 2)), mat="paint", color=PAINT_WING,
            smooth=True)
    tip = fz(2.42, 2.46, None, 1)
    tip.append(fin_ring(2.50, None, None, 0.75, 0.85, count(20, d, 5)))
    tip.append(fin_ring(FIN_TOP, None, None, 0.35, 0.6, count(20, d, 5)))
    m.rings(tip, mat="paint", color=PAINT_WING, smooth=True)
    m.tube((0, 5.74, 2.40), (0, 5.84, 2.40), 0.035, 0.035, 8, mat="light", color=WHITE_LAMP)

    # --- wing-tip pods carrying the navigation lights: left (+x) red, right (-x) green
    for side, col in ((LEFT, RED), (RIGHT, GREEN)):
        y = 1.12
        m.tube((side * 4.78, y, WING["z"]), (side * 4.945, y, WING["z"]), 0.048, 0.040, 10,
               mat="paint", color=PAINT_WING)
        m.tube((side * 4.945, y, WING["z"]), (side * 4.97, y, WING["z"]), 0.040, 0.034, 10,
               mat="light", color=col)

    if fine:
        shape_breaks(m, d)
        fuselage_decals(m, d)
        for side in (LEFT, RIGHT):
            wing_decals(m, side, d)
            fin_decals(m, side, d)
    return m


def build_fixed_wing(m, d):
    nsp = count(3, d, 1)
    cnt = count(22, d, 5)
    for side in (LEFT, RIGHT):
        for xa, xb, b_y in ((WING["x0"], 2.80, FLAP_HINGE), (2.80, 3.00, None),
                            (3.00, 4.55, AIL_HINGE), (4.55, 4.90, None)):
            rings = [wing_station(side, lerp(xa, xb, i / nsp), None, b_y, cnt=cnt)
                     for i in range(nsp + 1)]
            m.rings(rings, mat="paint", color=wing_color, smooth=True)
        rings = [wing_station(side, 4.90, None, None, cnt=cnt)]
        for x, sc, mid in ((4.925, 0.85, 0.94), (4.940, 0.55, 0.78), (4.945, 0.18, 0.55)):
            rings.append(wing_station(side, x, None, None, sc, mid, cnt))
        m.rings(rings, mat="paint", color=wing_color, smooth=True)


def wick(m, p, direction, length=0.15):
    d_ = Vector(direction).normalized()
    m.tube(p, Vector(p) + d_ * length, 0.005, 0.003, 5, mat="dark", color=BLACK)


def _surf_tone(c):
    return tone(PAINT_SURF, 1.0 + 0.06 * (hash01(c.x, c.y) - 0.5))


def build_flap(side, d):
    m = Mesh("flap_l" if side == LEFT else "flap_r",
             origin=(side * sum(FLAP_X) / 2, FLAP_HINGE, WING["z"]))
    nsp = count(2, d, 1)
    cnt = count(14, d, 4)
    rings = [wing_station(side, lerp(FLAP_X[0], FLAP_X[1], i / nsp), FLAP_HINGE + GAP, None,
                          cnt=cnt) for i in range(nsp + 1)]
    m.rings(rings, mat="paint", color=_surf_tone, smooth=True)
    return m


def build_aileron(side, d):
    m = Mesh("aileron_l" if side == LEFT else "aileron_r",
             origin=(side * sum(AIL_X) / 2, AIL_HINGE, WING["z"]))
    nsp = count(2, d, 1)
    cnt = count(14, d, 4)
    rings = [wing_station(side, lerp(AIL_X[0], AIL_X[1], i / nsp), AIL_HINGE + GAP, None,
                          cnt=cnt) for i in range(nsp + 1)]
    m.rings(rings, mat="paint", color=_surf_tone, smooth=True)
    if d >= 0.5:
        wick(m, (side * AIL_X[1] * 0.98, WING["te"] - 0.01, WING["z"]), (0, 1, 0.1))
    return m


def build_elevator(d):
    m = Mesh("elevator", origin=(0, TAIL_PIVOT_Y, TAIL_Z))
    nsp = count(3, d, 1)
    for side in (LEFT, RIGHT):
        rings = [tail_ring(side, lerp(0.25, TAIL_X1 - 0.07, i / nsp), cnt=count(18, d, 5))
                 for i in range(nsp + 1)]
        for x, sc, mid in ((TAIL_X1 - 0.035, 0.8, 0.9), (TAIL_X1, 0.3, 0.6)):
            rings.append(tail_ring(side, x, sc, mid, count(18, d, 5)))
        m.rings(rings, mat="paint", color=_surf_tone, smooth=True)
        if d >= 0.5:
            _le, te = tail_le_te(side, TAIL_X1 - 0.05)
            wick(m, te, (0, 1, 0.05), 0.12)
    return m


def build_rudder(d):
    m = Mesh("rudder", origin=(0, RUDDER_HINGE, 0.9))
    k = count(3, d, 1)
    rings = [fin_ring(lerp(0.64, 2.40, i / k), RUDDER_HINGE + GAP, None, cnt=count(14, d, 4))
             for i in range(k + 1)]
    m.rings(rings, mat="paint", color=PAINT_SURF, smooth=True)
    if d >= 0.5:
        wick(m, (0, 5.83, 2.39), (0, 1, 0.1), 0.14)
    return m


def build_airbrake(d):
    y0, y1 = 3.15, 4.05  # ventral airbrake under the rear fuselage
    zb = fus_point(y0, -math.pi / 2)[2]
    m = Mesh("airbrake", origin=(0, y0, zb))
    n = count(8, d, 3)
    outer, inner = [], []
    for y in (y0 + 0.008, y1):
        o, i_ = [], []
        for k in range(n + 1):
            th = math.radians(-90 - 26 + 52 * k / n)
            o.append(fus_point(y, th, 0.012))
            i_.append(fus_point(y, th, -0.02))
        outer.append(o)
        inner.append(i_)
    m.rings([outer[0], outer[1]], close_ring=False, solid=False, mat="paint", color=PAINT_SURF,
            smooth=True)
    m.rings([inner[0], inner[1]], close_ring=False, solid=False, mat="paint", color=PAINT_SURF,
            smooth=True, flip=True)
    for a, b in ((outer[0], inner[0]), (outer[1], inner[1])):
        m.rings([a, b], close_ring=False, solid=False, mat="paint", color=PAINT_SURF)
    for idx in (0, n):
        m.rings([[outer[0][idx], inner[0][idx]], [outer[1][idx], inner[1][idx]]],
                close_ring=False, solid=False, mat="paint", color=PAINT_SURF)
    m.recalc(list(m.bm.faces))
    return m


CANOPY_HINGE = Vector((-0.52, (CANOPY_SPLIT + CANOPY_Y1) / 2, SILL))


def build_canopy(d):
    """One-piece stepped-tandem bubble; the Hawk canopy opens sideways to starboard (-X), so
    the hinge is along the right sill (axis parallel to Y)."""
    m = Mesh("canopy", origin=CANOPY_HINGE)
    nt = count(24, d, 6)
    thetas = [math.pi * k / nt for k in range(nt + 1)]
    ny = count(30, d, 4)
    m.rings(canopy_rings(CANOPY_SPLIT + 0.002, CANOPY_Y1, ny, thetas, 0.004),
            close_ring=False, solid=False, mat="glass", color=GLASS, smooth=True)
    for y in (CANOPY_SPLIT + 0.025, CANOPY_MID, CANOPY_Y1 - 0.02):
        pts = canopy_ring(y, [math.pi * k / 18 for k in range(19)], 0.012)
        m.sweep(pts, 0.026 if y != CANOPY_MID else 0.032, 6, mat="paint", color=FRAME)
    for sx in (-1.0, 1.0):
        pts = []
        for i in range(13):
            y = lerp(CANOPY_SPLIT, CANOPY_Y1, i / 12)
            p = canopy_ring(y, [0.0 if sx > 0 else math.pi])[0]
            pts.append(Vector((p.x, y, p.z)))
        m.sweep(pts, 0.022, 6, mat="paint", color=FRAME)
    crown = [Vector((0, y, SILL + _canopy_h(y) + 0.012))
             for y in [CANOPY_SPLIT + (CANOPY_Y1 - CANOPY_SPLIT) * i / 14 for i in range(15)]]
    m.sweep(crown, 0.012, 5, mat="paint", color=FRAME)
    return m


def _wheel(m, centre, ro, width, d):
    """Tyre, rim and hub about an axle parallel to X."""
    cx, cy, cz = centre
    rr = ro * 0.58
    h = ro - rr
    w = width
    ns = segs(30, d, 8)
    prof = [(-.5 * w, rr), (-.5 * w, rr + .40 * h), (-.40 * w, rr + .80 * h), (-.22 * w, ro),
            (.22 * w, ro), (.40 * w, rr + .80 * h), (.5 * w, rr + .40 * h), (.5 * w, rr)]
    m.revolve_x((cx, cy, cz), prof, ns, mat="dark", color=RUBBER, smooth=True,
                closed_profile=True)
    rim = [(-.47 * w, 0.0), (-.47 * w, .55 * rr), (-.40 * w, .92 * rr), (.40 * w, .92 * rr),
           (.47 * w, .55 * rr), (.47 * w, 0.0)]
    m.revolve_x((cx, cy, cz), rim, ns, mat="metal", color=STEEL, smooth=True)
    m.tube((cx - .62 * w, cy, cz), (cx + .62 * w, cy, cz), .05, .05, 8, mat="metal",
           color=STEEL)


def build_gear(d):
    m = Mesh("gear")
    ro_n, w_n = 0.27, 0.16
    yn = -3.90
    zn = GROUND + ro_n
    m.tube((0, yn, -0.60), (0, yn + 0.02, -0.98), 0.055, 0.055, 10, mat="metal", color=STEEL)
    m.tube((0, yn + 0.02, -0.98), (0, yn + 0.05, zn), 0.036, 0.036, 10, mat="metal",
           color=STEEL)
    for sx in (-1, 1):
        m.box((sx * (w_n / 2 + 0.03), yn + 0.05, zn + 0.09), (0.02, 0.08, 0.30), mat="metal",
              color=STEEL)
        m.box((sx * 0.27, yn + 0.12, -0.80), (0.012, 0.50, 0.24), mat="paint", color=PAINT_SURF)
        m.tube((sx * 0.27, yn + 0.12, -0.80), (sx * 0.05, yn - 0.04, -0.86), 0.012, 0.012, 6,
               mat="metal", color=STEEL)
    m.box((0, yn - 0.10, -0.82), (0.05, 0.14, 0.04), mat="metal", color=STEEL)  # torque link
    m.box((0, yn - 0.06, -0.98), (0.10, 0.035, 0.07), mat="light", color=WHITE_LAMP)  # taxi light
    _wheel(m, (0, yn + 0.05, zn), ro_n, w_n, d)
    ro_m, w_m = 0.37, 0.22
    for side in (LEFT, RIGHT):
        x, y = side * 1.70, 0.45
        zm = GROUND + ro_m
        m.tube((x, y, -0.36), (x, y + 0.10, -0.80), 0.065, 0.065, 10, mat="metal", color=STEEL)
        m.tube((x, y + 0.10, -0.80), (x, y + 0.10, zm), 0.045, 0.045, 10, mat="metal",
               color=STEEL)
        m.tube((x, y - 0.55, -0.37), (x, y + 0.10, -0.78), 0.03, 0.03, 8, mat="metal",
               color=STEEL)
        m.tube((x - side * 0.30, y + 0.55, -0.40), (x, y + 0.10, -0.70), 0.028, 0.028, 8,
               mat="metal", color=STEEL)
        m.box((x + side * (w_m / 2 + 0.03), y + 0.10, zm + 0.04), (0.02, 0.10, 0.34),
              mat="metal", color=STEEL)
        m.box((x - side * (w_m / 2 + 0.03), y + 0.10, zm + 0.04), (0.02, 0.10, 0.34),
              mat="metal", color=STEEL)
        m.box((x + side * 0.24, y + 0.05, -0.66), (0.015, 0.75, 0.36), mat="paint",
              color=PAINT_SURF)
        _wheel(m, (x, y + 0.10, zm), ro_m, w_m, d)
    return m


def build(d=1.0, cockpit=True):
    """Return (root object, expected node names)."""
    mats = make_materials()
    fus_m = build_fuselage(d)
    parts = [build_flap(LEFT, d), build_flap(RIGHT, d), build_aileron(LEFT, d),
             build_aileron(RIGHT, d), build_elevator(d), build_rudder(d), build_airbrake(d),
             build_canopy(d), build_gear(d)]
    ao = make_ao([fus_m] + parts) if d >= 0.4 else None
    fus = fus_m.finish(mats, ao=ao)
    names = ["fuselage"]
    for p in parts:
        p.finish(mats, parent=fus, ao=ao)
        names.append(p.name)
    return fus, names
