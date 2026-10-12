# Hero model: a generic advanced jet trainer, proportions from the published dimensions of the
# BAE Systems Hawk 128 (length 12.43 m, span 9.94 m, height 3.98 m, wing area ~16.7 m2). No
# markings, no traced drawings: every shape below is a parametric loft or primitive.
#
# Blender frame: nose -Y, up +Z, origin ~ centre of gravity, ground contact at z = GROUND.
# Vehicle LEFT is +X, RIGHT is -X.

import math

from mathutils import Matrix, Vector

from mm_common import (LEFT, RIGHT, MAT, Mesh, empty, foil_ring, grey, lerp, make_materials,
                       naca_t, pchip, smoothstep, srgb, superellipse_point)

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
GLASS = srgb(0.30, 0.42, 0.50)
RED = srgb(0.9, 0.05, 0.05)
GREEN = srgb(0.05, 0.8, 0.15)
WHITE_LAMP = srgb(0.95, 0.95, 0.9)

# --------------------------------------------------------------------------- fuselage
# (y, half width, top z, bottom z, squareness n)
FUS_KEYS = [
    (-5.75, 0.00, -0.06, -0.06, 2.0),
    (-5.60, 0.10, 0.06, -0.20, 2.0),
    (-5.25, 0.24, 0.26, -0.38, 2.0),
    (-4.80, 0.40, 0.46, -0.52, 2.1),
    (-4.00, 0.55, 0.70, -0.64, 2.3),
    (-3.40, 0.63, 0.86, -0.70, 2.5),
    (-2.60, 0.68, 0.92, -0.74, 2.6),
    (-1.20, 0.72, 0.98, -0.76, 2.6),
    (0.40, 0.72, 0.97, -0.76, 2.6),
    (1.80, 0.66, 0.88, -0.70, 2.5),
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
    if c.y < -4.95:
        return RADOME
    top = _ft(c.y)
    if -4.55 < c.y < -3.5 and c.z > top - 0.14 and abs(c.x) < 0.42:
        return BLACK  # anti-glare panel
    return PAINT


def segs(base, d, lo=8):
    return max(lo, int(round(base * d / 4.0)) * 4)


def count(base, d, lo=3):
    return max(lo, int(round(base * d)))


# --------------------------------------------------------------------------- canopy shell
CANOPY_Y0, CANOPY_Y1 = -3.55, -0.25
CANOPY_SPLIT = -2.55  # windscreen (fixed) forward of this, hinged canopy behind
_cy = [-3.55, -3.20, -2.85, -2.30, -1.60, -0.90, -0.45, -0.25]
_ch = [0.0, 0.42, 0.68, 0.80, 0.76, 0.58, 0.32, 0.0]
_canopy_h = pchip(_cy, _ch)


def sill_z(y):
    return _ft(y) - 0.14


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


def build_fuselage(d, mats):
    m = Mesh("fuselage")
    S = segs(64, d)
    N = count(150, d, 14)
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

    # --- windscreen glass and its frame (fixed)
    ws_thetas = [math.pi * k / count(24, d, 6) for k in range(count(24, d, 6) + 1)]
    ny = count(10, d, 3)
    m.rings(canopy_rings(CANOPY_Y0, CANOPY_SPLIT, ny, ws_thetas, 0.004), close_ring=False,
            solid=False, mat="glass", color=GLASS, smooth=True)
    for y in (CANOPY_Y0 + 0.02, CANOPY_SPLIT):
        pts = canopy_ring(y, [math.pi * k / 18 for k in range(19)], 0.01)
        m.sweep(pts, 0.024, 6, mat="paint", color=FRAME)
    centre = [Vector((0, y, sill_z(y) + _canopy_h(y) + 0.012))
              for y in [CANOPY_Y0 + (CANOPY_SPLIT - CANOPY_Y0) * i / 10 for i in range(11)]]
    m.sweep(centre, 0.018, 6, mat="paint", color=FRAME)

    # --- air intakes at the wing roots, one each side
    for side in (LEFT, RIGHT):
        iring = []
        nt = count(10, d, 4)
        for i in range(nt + 1):
            t = i / nt
            y = lerp(-1.90, -0.20, t)
            xc = side * lerp(0.84, 0.58, smoothstep(t))
            a, b = lerp(0.21, 0.19, t), lerp(0.30, 0.36, t)
            ring = []
            for k in range(segs(24, d, 8)):
                th = 2 * math.pi * k / segs(24, d, 8)
                sx, sz = superellipse_point(th, 2.4)
                ring.append(Vector((xc + a * sx, y, 0.12 + b * sz)))
            iring.append(ring)
        m.rings(iring, solid=False, mat="paint", color=PAINT, smooth=True)
        m.sweep(iring[0], 0.024, 6, mat="paint", color=STEEL, closed=True)
        # dark duct plug set back behind the lip
        cen = Vector((side * 0.84, 0, 0.12))
        duct = [[Vector((cen.x + (p.x - cen.x) * 0.93, y, cen.z + (p.z - cen.z) * 0.93))
                 for p in iring[0]] for y in (-1.74, -1.30)]
        m.rings(duct, mat="dark", color=BLACK, smooth=True)

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

    # --- nose pitot probe
    m.tube((0, Y_NOSE + 0.03, -0.06), (0, Y_NOSE - 0.15, -0.06), 0.022, 0.008, 8, mat="metal",
           color=STEEL)

    # --- blade antennas, dorsal and ventral
    m.plate([(0.0, -0.55, 0.96), (0.0, -0.20, 0.96), (0.0, -0.30, 1.22), (0.0, -0.45, 1.22)],
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

    # --- wing: fixed parts (flap and aileron are separate nodes)
    build_fixed_wing(m, d)

    # --- fin (fixed part) and tailplane stub
    fz = lambda z0, z1, b_y, k: [fin_ring(lerp(z0, z1, i / k), None, b_y, cnt=count(20, d, 5))
                                 for i in range(k + 1)]
    m.rings(fz(0.45, 0.62, None, 1), mat="paint", color=PAINT_WING, smooth=True)
    m.rings(fz(0.62, 2.42, RUDDER_HINGE, count(4, d, 2)), mat="paint", color=PAINT_WING,
            smooth=True)
    tip = fz(2.42, 2.46, None, 1)
    tip.append(fin_ring(2.50, None, None, 0.75, 0.85, count(20, d, 5)))
    tip.append(fin_ring(FIN_TOP, None, None, 0.35, 0.6, count(20, d, 5)))
    m.rings(tip, mat="paint", color=PAINT_WING, smooth=True)
    # white tail light on the fin tip
    m.tube((0, 5.74, 2.40), (0, 5.84, 2.40), 0.035, 0.035, 8, mat="light", color=WHITE_LAMP)

    # --- wing-tip lights: left (+x) red, right (-x) green
    for side, col in ((LEFT, RED), (RIGHT, GREEN)):
        le, te = wing_le_te(side, WING["x1"] - 0.02)
        m.tube((side * 4.93, 0.2, WING["z"]),
               (side * 4.97, 0.2, WING["z"]), 0.04, 0.03, 8, mat="light",
               color=col)

    return m


def build_fixed_wing(m, d):
    nsp = count(3, d, 1)
    cnt = count(22, d, 5)
    for side in (LEFT, RIGHT):
        # inboard (behind flap hinge), gap between flap and aileron (full chord),
        # outboard of aileron (full chord), then the rounded tip
        for xa, xb, b_y in ((WING["x0"], 2.80, FLAP_HINGE), (2.80, 3.00, None),
                            (3.00, 4.55, AIL_HINGE), (4.55, 4.90, None)):
            rings = [wing_station(side, lerp(xa, xb, i / nsp), None, b_y, cnt=cnt)
                     for i in range(nsp + 1)]
            m.rings(rings, mat="paint", color=PAINT_WING, smooth=True)
        rings = [wing_station(side, 4.90, None, None, cnt=cnt)]
        for x, sc, mid in ((4.925, 0.85, 0.94), (4.940, 0.55, 0.78), (4.945, 0.18, 0.55)):
            rings.append(wing_station(side, x, None, None, sc, mid, cnt))
        m.rings(rings, mat="paint", color=PAINT_WING, smooth=True)


def build_flap(side, d):
    m = Mesh("flap_l" if side == LEFT else "flap_r",
             origin=(side * sum(FLAP_X) / 2, FLAP_HINGE, WING["z"]))
    nsp = count(2, d, 1)
    cnt = count(14, d, 4)
    rings = [wing_station(side, lerp(FLAP_X[0], FLAP_X[1], i / nsp), FLAP_HINGE + GAP, None,
                          cnt=cnt) for i in range(nsp + 1)]
    m.rings(rings, mat="paint", color=PAINT_SURF, smooth=True)
    return m


def build_aileron(side, d):
    m = Mesh("aileron_l" if side == LEFT else "aileron_r",
             origin=(side * sum(AIL_X) / 2, AIL_HINGE, WING["z"]))
    nsp = count(2, d, 1)
    cnt = count(14, d, 4)
    rings = [wing_station(side, lerp(AIL_X[0], AIL_X[1], i / nsp), AIL_HINGE + GAP, None,
                          cnt=cnt) for i in range(nsp + 1)]
    m.rings(rings, mat="paint", color=PAINT_SURF, smooth=True)
    return m


def build_elevator(d):
    m = Mesh("elevator", origin=(0, TAIL_PIVOT_Y, TAIL_Z))
    nsp = count(3, d, 1)
    for side in (LEFT, RIGHT):
        rings = [tail_ring(side, lerp(0.25, TAIL_X1 - 0.07, i / nsp), cnt=count(18, d, 5))
                 for i in range(nsp + 1)]
        for x, sc, mid in ((TAIL_X1 - 0.035, 0.8, 0.9), (TAIL_X1, 0.3, 0.6)):
            rings.append(tail_ring(side, x, sc, mid, count(18, d, 5)))
        m.rings(rings, mat="paint", color=PAINT_SURF, smooth=True)
    return m


def build_rudder(d):
    m = Mesh("rudder", origin=(0, RUDDER_HINGE, 0.9))
    k = count(3, d, 1)
    rings = [fin_ring(lerp(0.64, 2.40, i / k), RUDDER_HINGE + GAP, None, cnt=count(14, d, 4))
             for i in range(k + 1)]
    m.rings(rings, mat="paint", color=PAINT_SURF, smooth=True)
    return m


def build_airbrake(d):
    y0, y1 = 2.20, 3.00
    zb = fus_point(y0, -math.pi / 2)[2]
    m = Mesh("airbrake", origin=(0, y0, zb))
    n = count(8, d, 3)
    outer, inner = [], []
    for y in (y0 + 0.008, y1):
        o, i_ = [], []
        for k in range(n + 1):
            th = math.radians(-90 - 24 + 48 * k / n)  # centred on the belly line
            o.append(fus_point(y, th, 0.012))
            i_.append(fus_point(y, th, -0.02))
        outer.append(o)
        inner.append(i_)
    # a thin curved slab: outer skin, inner skin, and edges
    m.rings([outer[0], outer[1]], close_ring=False, solid=False, mat="paint", color=PAINT_SURF,
            smooth=True, flip=False)
    m.rings([inner[0], inner[1]], close_ring=False, solid=False, mat="paint", color=PAINT_SURF,
            smooth=True, flip=True)
    for a, b in ((outer[0], inner[0]), (outer[1], inner[1])):
        m.rings([a, b], close_ring=False, solid=False, mat="paint", color=PAINT_SURF)
    for idx in (0, n):
        m.rings([[outer[0][idx], inner[0][idx]], [outer[1][idx], inner[1][idx]]],
                close_ring=False, solid=False, mat="paint", color=PAINT_SURF)
    m.recalc(list(m.bm.faces))
    return m


def build_canopy(d):
    zs = sill_z(CANOPY_Y1)
    m = Mesh("canopy", origin=(0, CANOPY_Y1, zs))
    nt = count(24, d, 6)
    thetas = [math.pi * k / nt for k in range(nt + 1)]
    ny = count(14, d, 3)
    m.rings(canopy_rings(CANOPY_SPLIT + 0.002, CANOPY_Y1, ny, thetas, 0.004),
            close_ring=False, solid=False, mat="glass", color=GLASS, smooth=True)
    for y in (CANOPY_SPLIT + 0.02, -1.55, CANOPY_Y1 - 0.02):
        pts = canopy_ring(y, [math.pi * k / 18 for k in range(19)], 0.012)
        m.sweep(pts, 0.026, 6, mat="paint", color=FRAME)
    for sx in (-1.0, 1.0):
        pts = []
        for i in range(9):
            y = lerp(CANOPY_SPLIT, CANOPY_Y1, i / 8)
            p = canopy_ring(y, [0.0 if sx > 0 else math.pi])[0]
            pts.append(Vector((p.x, y, p.z)))
        m.sweep(pts, 0.02, 6, mat="paint", color=FRAME)
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
    # nose gear
    yn = -3.90
    zn = GROUND + ro_n
    m.tube((0, yn, -0.60), (0, yn + 0.02, -0.98), 0.055, 0.055, 10, mat="metal", color=STEEL)
    m.tube((0, yn + 0.02, -0.98), (0, yn + 0.05, zn), 0.036, 0.036, 10, mat="metal",
           color=STEEL)
    for sx in (-1, 1):
        m.box((sx * (w_n / 2 + 0.03), yn + 0.05, zn + 0.09), (0.02, 0.08, 0.30), mat="metal",
              color=STEEL)
        m.box((sx * 0.27, yn + 0.12, -0.80), (0.012, 0.50, 0.24), mat="paint", color=PAINT_SURF)
        m.tube((sx * 0.30, yn + 0.12, -0.78), (sx * 0.05, yn - 0.04, -0.86), 0.012, 0.012, 6,
               mat="metal", color=STEEL)
    m.box((0, yn - 0.10, -0.82), (0.05, 0.14, 0.04), mat="metal", color=STEEL)  # torque link
    _wheel(m, (0, yn + 0.05, zn), ro_n, w_n, d)
    # main gear
    ro_m, w_m = 0.37, 0.22
    for side in (LEFT, RIGHT):
        x, y = side * 1.70, 0.45
        zm = GROUND + ro_m
        m.tube((x, y, -0.36), (x, y + 0.10, -0.80), 0.065, 0.065, 10, mat="metal", color=STEEL)
        m.tube((x, y + 0.10, -0.80), (x, y + 0.10, zm), 0.045, 0.045, 10, mat="metal",
               color=STEEL)
        m.tube((x, y - 0.55, -0.37), (x, y + 0.10, -0.78), 0.03, 0.03, 8, mat="metal",
               color=STEEL)  # drag brace
        m.tube((x - side * 0.30, y + 0.55, -0.40), (x, y + 0.10, -0.70), 0.028, 0.028, 8,
               mat="metal", color=STEEL)  # retraction jack
        m.box((x + side * (w_m / 2 + 0.03), y + 0.10, zm + 0.04), (0.02, 0.10, 0.34),
              mat="metal", color=STEEL)  # fork
        m.box((x - side * (w_m / 2 + 0.03), y + 0.10, zm + 0.04), (0.02, 0.10, 0.34),
              mat="metal", color=STEEL)
        m.box((x + side * 0.24, y + 0.05, -0.66), (0.015, 0.75, 0.36), mat="paint", color=PAINT_SURF)
        _wheel(m, (x, y + 0.10, zm), ro_m, w_m, d)
    return m


def build(d=1.0, cockpit=True):
    """Return (root object, expected node names)."""
    mats = make_materials()
    fus = build_fuselage(d, mats).finish(mats)
    names = ["fuselage"]
    parts = [build_flap(LEFT, d), build_flap(RIGHT, d), build_aileron(LEFT, d),
             build_aileron(RIGHT, d), build_elevator(d), build_rudder(d), build_airbrake(d),
             build_canopy(d), build_gear(d)]
    for p in parts:
        p.finish(mats, parent=fus)
        names.append(p.name)
    return fus, names
