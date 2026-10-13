# Hero model: a generic advanced jet trainer, proportions from the published dimensions of the
# BAE Systems Hawk 128 (length 12.43 m, span 9.94 m, height 3.98 m, wing area ~16.7 m2). No
# markings, no traced drawings: every shape below is a parametric loft or primitive.
#
# Blender frame: nose -Y, up +Z, origin ~ centre of gravity, ground contact at z = GROUND.
# Vehicle LEFT is +X, RIGHT is -X.
#
# Tessellation: every count scales with the detail factor `d` (see mm_common). Panel joints and
# colour boundaries are knots of the stations, so vertex colours change exactly at the panel
# lines and vertices are shared everywhere else.

import bisect
import math

from mathutils import Vector

from mm_common import (LEFT, RIGHT, Mesh, count, foil_ring, hash01, lerp, make_ao,
                       make_materials, mix, naca_t, pchip, smoothstep, srgb, station_counts,
                       stations, superellipse_normal, superellipse_point, tip_params, tone)

Y_NOSE = -5.75
Y_TAIL = 6.53
Y_SKIN_END = 5.72
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
ERODE = srgb(0.36, 0.38, 0.41)
RED = srgb(0.9, 0.05, 0.05)
GREEN = srgb(0.05, 0.8, 0.15)
WHITE_LAMP = srgb(0.95, 0.95, 0.9)

# --------------------------------------------------------------------------- fuselage
# (y, half width, top z, bottom z, squareness n)
FUS_KEYS = [
    (-5.75, 0.00, -0.04, -0.04, 2.0),  # ogive nose: radius grows fast at the tip, then slows
    (-5.62, 0.080, 0.050, -0.130, 2.0),
    (-5.35, 0.170, 0.150, -0.235, 2.0),
    (-4.95, 0.275, 0.280, -0.350, 2.05),
    (-4.45, 0.385, 0.440, -0.470, 2.15),
    (-3.90, 0.49, 0.64, -0.58, 2.3),
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


def fus_theta(p):
    """Angle parameter of a point on (or near) the skin: the inverse of fus_point."""
    w, top, bot, n = fus_section(p.y)
    zm, hh = (top + bot) / 2.0, (top - bot) / 2.0
    sx, sz = p.x / max(w, 1e-6), (p.z - zm) / hh
    return math.atan2(math.copysign(abs(sz) ** (n / 2.0), sz),
                      math.copysign(abs(sx) ** (n / 2.0), sx))


def skin_half_width(y, z):
    """Half-width of the skin at height z (0 above the top or below the bottom)."""
    w, top, bot, n = fus_section(y)
    zm, hh = (top + bot) / 2.0, (top - bot) / 2.0
    return w * max(0.0, 1.0 - abs((z - zm) / hh) ** n) ** (1.0 / n)


def skin_z_at(y, a):
    """Height on the upper skin where its half-width is a."""
    w, top, bot, n = fus_section(y)
    zm, hh = (top + bot) / 2.0, (top - bot) / 2.0
    r = min(1.0, max(0.0, a / w))
    return zm + hh * (1.0 - r ** n) ** (1.0 / n)


def skin_x(y, z, side):
    """x of the fuselage skin at height z (outside surface)."""
    w, top, bot, n = fus_section(y)
    zm, hh = (top + bot) / 2.0, (top - bot) / 2.0
    zz = min(0.999, abs((z - zm) / hh))
    return side * w * (1.0 - zz ** n) ** (1.0 / n)


# Panel layout: circumferential joints (y) and longitudinal joints (theta). The skin has vertex
# rings on every joint, so the panel tones change exactly under the panel lines.
FUS_Y_JOINTS = (-4.55, -3.62, -1.00, 0.00, 0.95, 2.00, 3.10, 4.15, 5.05)
FUS_T_JOINTS = (-2.64, -1.94, -1.20, -0.50, 0.50, 1.20, 1.94, 2.64)
RADOME_Y = -5.10
ANTIGLARE = (-4.55, -3.62, 1.20, 1.94)  # y0, y1, theta0, theta1: in front of the windscreen


def soot(p):
    """Exhaust staining towards the jet pipe, heavier underneath (continuous)."""
    return smoothstep((p.y - 4.2) / 1.6) * lerp(1.0, 0.7, smoothstep((p.z - 0.05) / 0.2))


def fus_color(p, c):
    """Radome, anti-glare panel and a tone per skin panel (regions from the face centre c),
    soot (continuous, from the vertex p)."""
    if c.y < RADOME_Y:
        return RADOME
    t = fus_theta(c)
    if ANTIGLARE[0] < c.y < ANTIGLARE[1] and ANTIGLARE[2] < t < ANTIGLARE[3]:
        return BLACK
    # panel cell; the bands either side of theta = +-pi (the right side) are one panel
    cell = (bisect.bisect(FUS_Y_JOINTS, c.y), bisect.bisect(FUS_T_JOINTS, t) % len(FUS_T_JOINTS))
    col = panel_tone(PAINT, cell)
    s = soot(p)
    return mix(col, SOOT, 0.7 * s) if s > 0.0 else col


def panel_tone(base, cell, share=0.30, amount=0.10):
    """Tone of the skin panel `cell` (a tuple of indices): most panels keep the base tone and
    about `share` of them, chosen by a hash of the cell, are repainted a little lighter or
    darker. Vertices split only along boundaries between panels of different tone."""
    h = hash01(*cell)
    if h >= share:
        return base
    return tone(base, 1.0 + amount if h < share / 2 else 1.0 - amount)


def segs(base, d, lo=8):
    return max(lo, int(round(base * d / 4.0)) * 4)


# --------------------------------------------------------------------------- dorsal surfaces
# Windscreen, canopy and spine share one smooth definition, so the canopy outline is a single
# bubble (rising to the raised rear seat without a dip) and its rear frame meets the spine with
# no step or kink. The section at y is a superellipse arc standing on the skin at the base
# height zs(y), half-width a(y), crown height crown(y). Along the cockpits the base is the sill
# (z = SILL); behind the rear seat the section narrows and its base climbs the skin into the spine.
DORSAL_Y0, DORSAL_Y1 = -3.60, 4.40
CANOPY_SPLIT = -2.95  # fixed windscreen forward of this, one-piece canopy behind
CANOPY_MID = -1.90  # frame between the seats
CANOPY_Y1 = -0.30  # canopy rear frame; the spine continues the same surface behind it
SILL = 0.80
DORSAL_N = 2.2
_crown = pchip(*zip(*[
    (-3.60, 0.80), (-3.30, 1.20), (-2.90, 1.58), (-2.45, 1.70), (-2.00, 1.79), (-1.55, 1.87),
    (-1.20, 1.925), (-0.90, 1.95), (-0.55, 1.86), (-0.20, 1.64), (0.30, 1.40), (1.00, 1.25),
    (1.80, 1.14), (2.80, 1.03), (3.60, 0.95), (4.40, 0.80)]))
_spine_w = pchip(*zip(*[
    (-1.40, 0.56), (-0.30, 0.47), (0.30, 0.38), (1.00, 0.30), (1.80, 0.26), (2.80, 0.20),
    (3.60, 0.15), (4.40, 0.06)]))


def dorsal_frame(y):
    """(base height zs, half-width a, crown height) of the dorsal section at y."""
    a_sill = skin_half_width(y, SILL) * 0.985
    s = smoothstep((y + 1.6) / 1.4)
    if s <= 0.0:
        return SILL, a_sill, _crown(y)
    a = lerp(a_sill, _spine_w(y), s)
    return skin_z_at(y, a / 0.985), a, _crown(y)


def dorsal_point(y, th, grow=0.0):
    zs, a, top = dorsal_frame(y)
    sx, sz = superellipse_point(th, DORSAL_N)
    return Vector(((a + grow) * sx, y, zs + (top - zs + grow) * sz))


def dorsal_normal(y, th, h=1e-3):
    """Outward unit normal of the dorsal surface (finite differences)."""
    py = dorsal_point(y + h, th) - dorsal_point(y - h, th)
    pt = dorsal_point(y, th + h) - dorsal_point(y, th - h)
    return py.cross(pt).normalized()


def dorsal_rings(ys, thetas, grow=0.0):
    return [[dorsal_point(y, th, grow) for th in thetas] for y in ys]


def dorsal_band(m, y, width, thick, d, mat="paint", color=FRAME):
    """Frame hoop across the dorsal section at y: a flat band on the glass."""
    ths = stations(0.0, math.pi, count(20, d, 8), cosine=False)
    m.band([dorsal_point(y, th) for th in ths], [dorsal_normal(y, th) for th in ths], width,
           thick, mat=mat, color=color)


def sill_band(m, y0, y1, side, width, thick, d, mat="paint", color=FRAME):
    """Frame along the lower edge of the windscreen or canopy on one side."""
    th = 0.0 if side > 0 else math.pi
    ys = stations(y0, y1, count(max(4.0, 12 * (y1 - y0)), d, 4), cosine=False)
    m.band([dorsal_point(y, th) for y in ys], [dorsal_normal(y, th) for y in ys], width, thick,
           mat=mat, color=color)


def spine_color(p, c):
    return panel_tone(PAINT, (bisect.bisect(FUS_Y_JOINTS, c.y), 7))


def build_spine(m, d):
    """The dorsal surface behind the canopy, with a short skirt sunk into the skin."""
    ns = count(16, d, 6)
    ths = [-0.3] + stations(0.0, math.pi, ns, cosine=False) + [math.pi + 0.3]
    ys = stations(CANOPY_Y1, DORSAL_Y1, count(36, d, 6), FUS_Y_JOINTS)
    m.rings(dorsal_rings(ys, ths), close_ring=False, mat="paint", color=spine_color,
            smooth=True)


# --------------------------------------------------------------------------- intakes
# D-shaped intakes against the fuselage sides above the wing roots: the flat side of the D is a
# splitter plate standing `gap` off the skin at the lip (boundary-layer diverter, closing behind
# it), the curved side bulges outboard. The fairing fades into the skin and the wing root.
INTAKE = dict(lip=-1.90, end=0.35, r=0.016, a_in=0.06, n_out=2.4, n_in=6.0, duct=0.50)
_in_gap = pchip(*zip(*[(-1.90, 0.025), (-1.75, 0.018), (-1.50, -0.015), (0.35, -0.09)]))
_in_depth = pchip(*zip(*[(-1.90, 0.30), (-1.45, 0.285), (-0.85, 0.20), (-0.25, 0.10),
                         (0.35, 0.062)]))
_in_b = pchip(*zip(*[(-1.90, 0.28), (-1.20, 0.26), (-0.40, 0.20), (0.35, 0.15)]))
_in_zc = pchip(*zip(*[(-1.90, 0.10), (-1.00, 0.07), (0.35, 0.02)]))


def intake_params(y):
    """(x of the splitter wall, depth, half-height, centre height) of the mean D at y (+x side)."""
    zc = _in_zc(y)
    return skin_half_width(y, zc) + _in_gap(y), _in_depth(y), _in_b(y), zc


def intake_curve(side, y, k_out, k_in, offset=0.0):
    """Closed D outline at y (bottom, round the outer side to the top, then down the flat
    splitter side), offset along its outward normal: (points, outward 2-D normals)."""
    xw, depth, b, zc = intake_params(y)
    a_in = INTAKE["a_in"]
    xc = xw + a_in
    a_out = max(depth - a_in, 0.002)
    pts, nrm = [], []
    halves = ((a_out, INTAKE["n_out"], -0.5 * math.pi, 0.5 * math.pi, k_out, True),
              (a_in, INTAKE["n_in"], 0.5 * math.pi, 1.5 * math.pi, k_in, False))
    for a, n, phi0, phi1, k, inclusive in halves:
        for i in range(k + 1 if inclusive else k):
            if not inclusive and i == 0:
                continue
            sx, sz = superellipse_point(phi0 + (phi1 - phi0) * i / k, n)
            nx, nz = superellipse_normal(sx, sz, a, b, n)
            pts.append(Vector((side * (xc + a * sx + offset * nx), y, zc + b * sz + offset * nz)))
            nrm.append(Vector((side * nx, 0.0, nz)))
    return pts, nrm


def build_intake(m, side, d):
    lip, end, r = INTAKE["lip"], INTAKE["end"], INTAKE["r"]
    k_out, k_in = count(16, d, 6), count(8, d, 4)
    ys = stations(end, lip, count(18, d, 6))
    rings = [intake_curve(side, y, k_out, k_in, r)[0] for y in ys]
    n_outer = len(rings)
    q, nu = intake_curve(side, lip, k_out, k_in)
    k_roll = count(6, d, 3)
    for i in range(1, k_roll + 1):
        phi = math.pi * i / k_roll
        rings.append([qq + nn * (r * math.cos(phi)) + Vector((0.0, -r * math.sin(phi), 0.0))
                      for qq, nn in zip(q, nu)])
    outer = m.rings(rings, close_ring=True, solid=False, mat="paint", color=PAINT, smooth=True)
    duct_start = m.last_rings[-1]
    centre = Vector((side * (intake_params(lip)[0] + 0.12), 0.0, intake_params(lip)[3]))

    def out(c):
        return Vector((c.x - centre.x, 0.0, c.z - centre.z))
    per_ring = len(rings[0])
    m.orient_all(outer, outer[:per_ring * (n_outer - 1)], out)
    k_duct = count(4, d, 2)
    drings = [None]
    inboard = Vector((side * intake_params(lip)[0], 0.0, intake_params(lip)[3]))
    for i in range(1, k_duct + 1):
        t = i / k_duct
        y = lip + INTAKE["duct"] * t
        shrink = 1.0 - 0.45 * smoothstep(t)  # the duct narrows and turns inboard
        drings.append([Vector((inboard.x + (p.x - inboard.x) * shrink, y,
                               inboard.z + (p.z - inboard.z) * shrink)) for p in
                       [qq - nn * r for qq, nn in zip(q, nu)]])
    drings[0] = [v.co + m.origin for v in duct_start]
    duct = m.rings(drings, close_ring=True, solid=False, mat="dark", color=BLACK, smooth=True,
                   ends=(duct_start, None))
    m.orient_all(duct, duct, lambda c: -out(c))
    cap = m.face(list(reversed(m.last_rings[-1])), "dark", BLACK)
    if cap is not None:
        m.orient([cap], lambda c: (0.0, -1.0, 0.0))


def intake_covers(p):
    """Skin points under an intake fairing."""
    if not INTAKE["lip"] + 0.05 < p.y < INTAKE["end"]:
        return False
    _xw, _depth, b, zc = intake_params(p.y)
    return abs(p.z - zc) < b and abs(p.x) > 0.3


# --------------------------------------------------------------------------- aerofoil surfaces
WING = dict(x0=0.50, x1=4.97, le0=-1.35, te=1.55, sweep=0.459, tc0=0.100, tc1=0.070, z=-0.30,
            dihedral=2.0)
FLAP_HINGE = 0.95
AIL_HINGE = 1.19
FLAP_X = (0.78, 2.78)
AIL_X = (3.02, 4.53)
GAP = 0.012
WING_X_JOINTS = (1.15, 1.75, 2.40, 3.50, 4.10, 4.70)  # chordwise panel lines (span stations)
WING_V_JOINTS = (0.14, 0.36, 0.56)  # spanwise panel lines (chord fractions)
WALKWAY = (1.75, 0.36, 0.56)  # root walkway: outboard edge x, chord fractions (upper surface)


def wing_z(x):
    """Height of the wing's chord plane at span station |x| (dihedral)."""
    return WING["z"] + (abs(x) - WING["x0"]) * math.tan(math.radians(WING["dihedral"]))


def wing_le_te(side, x):
    le_y = WING["le0"] + (x - WING["x0"]) * WING["sweep"]
    z = wing_z(x)
    return (Vector((side * x, le_y, z)), Vector((side * x, WING["te"], z)))


def wing_tc(x):
    return lerp(WING["tc0"], WING["tc1"], (x - WING["x0"]) / (WING["x1"] - WING["x0"]))


def wing_station(side, x, a_y=None, b_y=None, scale=1.0, mid=None, cnt=22, counts=None,
                 knots=WING_V_JOINTS):
    le, te = wing_le_te(side, x)
    c = te.y - le.y
    a = 0.0 if a_y is None else (a_y - le.y) / c
    b = 1.0 if b_y is None else (b_y - le.y) / c
    xs = stations(a, b, cnt, knots, counts=counts)
    return foil_ring(le, te, (0, 0, 1), wing_tc(x), a, b, cnt, scale, mid, xs)


def wing_color(side):
    """Tone per wing panel (between the panel lines) and the root walkway, a slightly darker
    panel bounded by panel lines (regions from the face centre)."""
    def f(p, c):
        x = abs(c.x)
        le, te = wing_le_te(side, x)
        v = (c.y - le.y) / (te.y - le.y)
        upper = c.z > wing_z(x)
        if upper and x < WALKWAY[0] and WALKWAY[1] < v < WALKWAY[2]:
            return tone(PAINT_WING, 0.91)
        cell = (bisect.bisect(WING_X_JOINTS, x), bisect.bisect(WING_V_JOINTS, v), upper, side)
        return panel_tone(PAINT_WING, cell)
    return f


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


TAIL_PIVOT_Y = 4.75
TAIL_Z = 0.15
TAIL_X1 = 1.95
TAIL_ANHEDRAL = 8.0  # degrees


def tail_le_te(side, x):
    t = x - 0.25
    z = TAIL_Z - t * math.tan(math.radians(TAIL_ANHEDRAL))
    return (Vector((side * x, 3.90 + t * 0.70, z)), Vector((side * x, 5.60 + t * 0.17, z)))


def tail_ring(side, x, scale=1.0, mid=None, cnt=18):
    le, te = tail_le_te(side, x)
    tc = lerp(0.075, 0.060, (x - 0.25) / 1.7)
    return foil_ring(le, te, (0, 0, 1), tc, 0.0, 1.0, cnt, scale, mid)


# --------------------------------------------------------------------------- surface functions


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


def skin_hidden(p):
    """Skin points covered by the spine, an intake fairing or a wing root (no decals there)."""
    if CANOPY_Y1 < p.y < DORSAL_Y1:
        zs, a, _top = dorsal_frame(p.y)
        if abs(p.x) < a * 0.97 and p.z > zs - 0.02:
            return True
    if intake_covers(p):
        return True
    x = abs(p.x)
    if x > WING["x0"]:
        le, te = wing_le_te(1.0, x)
        if le.y < p.y < te.y:
            v = (p.y - le.y) / (te.y - le.y)
            if abs(p.z - wing_z(x)) < naca_t(v) * wing_tc(x) * (te.y - le.y) + 0.03:
                return True
    return False


def fus_rect(m, y0, y1, t0, t1, d):
    """Access panel: outline (4 panel lines) and fastener rows on the long edges."""
    ky = count(max(2, int((y1 - y0) / 0.22)), d, 2)
    kt = count(max(2, int(abs(t1 - t0) / 0.12)), d, 2)  # sags at most ~1.3 mm under the 4 mm lift
    loop = (line((y0, t0), (y1, t0), ky)[:-1] + line((y1, t0), (y1, t1), kt)[:-1]
            + line((y1, t1), (y0, t1), ky)[:-1] + line((y0, t1), (y0, t0), kt)[:-1])
    m.ribbon(fus_surf, loop, 0.007, 0.004, LINE, skip=skin_hidden, closed=True)
    if y1 - y0 > 0.5:
        k = int((y1 - y0) / 0.18)
        for t in (t0, t1):
            for i in range(1, k):
                u, v = lerp(y0, y1, i / k), lerp(t, t0 + (t1 - t0) / 2, 0.06)
                if not skin_hidden(fus_point(u, v)):
                    m.dot(fus_surf, u, v, 0.012, 0.02, 0.005, LINE)


LEFT_RECTS = [  # (y0, y1, theta0, theta1), left side; the right side is mirrored
    (-4.45, -3.80, 0.20, 1.20), (-4.70, -4.15, -0.25, 0.35), (-3.40, -2.70, -0.05, 0.40),
    (-2.60, -2.10, -0.45, 0.15), (0.10, 1.00, 0.40, 1.15), (1.30, 2.40, 0.25, 1.05),
    (2.60, 3.50, 0.30, 1.00), (3.70, 4.60, 0.20, 0.90), (0.20, 1.60, -0.55, -0.10),
    (1.80, 3.00, -0.65, -0.20), (3.30, 4.30, -0.75, -0.30), (4.50, 5.50, -0.55, 0.45),
    (-3.00, -2.20, -1.00, -0.55), (-1.60, -0.80, -1.00, -0.55), (-0.10, 0.90, -1.10, -0.70),
]


def fuselage_decals(m, d):
    for y in FUS_Y_JOINTS:
        m.ribbon(fus_surf, line((y, -math.pi), (y, math.pi), count(48, d, 8)), 0.007, 0.004,
                 LINE, skip=skin_hidden)
    y0, y1 = FUS_Y_JOINTS[0], FUS_Y_JOINTS[-1]
    for t in FUS_T_JOINTS:
        m.ribbon(fus_surf, line((y0, t), (y1, t), count(38, d, 8)), 0.006, 0.004, LINE,
                 skip=skin_hidden)
    for y0, y1, t0, t1 in LEFT_RECTS:
        fus_rect(m, y0, y1, t0, t1, d)
        fus_rect(m, y0, y1, math.pi - t1, math.pi - t0, d)
    # boarding grab handle below the left windscreen sill
    ya, yb, za, zb = -3.05, -2.75, 0.62, 0.70
    base = [Vector((skin_x(y, z, LEFT) - 0.004, y, z)) for y, z in ((ya, za), (yb, za))]
    grip = [Vector((skin_x(y, zb, LEFT) + 0.045, y, zb)) for y in (ya, yb)]
    m.sweep([base[0], grip[0], grip[1], base[1]], 0.010, 6, mat="metal", color=STEEL)


def wing_decals(m, side, d):
    for upper in (True, False):
        s = wing_surf(side, upper)
        for x in WING_X_JOINTS:
            m.ribbon(s, line((x, 0.05), (x, 0.62), count(8, d, 2)), 0.006, 0.004, LINE)
        for v, xa, xb in ((0.14, 0.80, 4.80), (0.36, 0.80, 4.80), (0.56, 0.80, 4.40)):
            m.ribbon(s, line((xa, v), (xb, v), count(14, d, 4)), 0.006, 0.004, LINE)
        for x in [0.9 + 0.15 * i for i in range(26)]:
            m.dot(s, x, 0.14, 0.012, 0.014, 0.005, LINE)
        m.patch(s, 0.80, 4.85, 0.004, 0.05, count(10, d, 2), 1, 0.004, ERODE)  # leading edge
    up = wing_surf(side, True)
    # vortex generators in a row near the leading edge, alternately toed in and out
    for i in range(14):
        x = 1.0 + 0.11 * i
        p, _n = up(x, 0.20)
        psi = math.radians(14.0 if i % 2 else -14.0)
        along = Vector((math.sin(psi), math.cos(psi), 0.0)) * 0.07
        across = Vector((math.cos(psi), -math.sin(psi), 0.0)) * 0.0005
        vane = [p, p + along, p + along * 0.5 + Vector((0, 0, 0.04))]
        for sg in (1.0, -1.0):  # one face per side, 1 mm apart: a vane with no edge faces
            f = m.poly([q + across * sg for q in vane], "paint", PAINT_SURF)
            m.orient([f], lambda c, sg=sg: across * sg)
    # wing fence
    pa, _ = up(2.62, 0.02)
    pb, _ = up(2.62, 0.66)
    m.plate([pa - Vector((0, 0.0, 0.02)), pb - Vector((0, 0.0, 0.02)), pb + Vector((0, -0.05, 0.10)),
             pa + Vector((0, 0.0, 0.14))], (side * 0.012, 0, 0), mat="paint", color=PAINT_SURF)


def fin_decals(m, side, d):
    s = fin_surf(side)
    for z in (0.9, 1.35, 1.8, 2.2):
        m.ribbon(s, line((z, 0.06), (z, 0.60), count(8, d, 2)), 0.006, 0.004, LINE)
    for v in (0.18, 0.38):
        m.ribbon(s, line((0.8, v), (2.3, v), count(8, d, 2)), 0.006, 0.004, LINE)
    for z in [0.8 + 0.15 * i for i in range(11)]:
        m.dot(s, z, 0.18, 0.014, 0.012, 0.005, LINE)
    m.patch(s, 0.8, 2.40, 0.004, 0.04, count(6, d, 2), 1, 0.004, ERODE)


def shape_breaks(m, d):
    # wing-root fillets, upper and lower, both sides
    k = count(8, d, 4)
    for side in (LEFT, RIGHT):
        for sg in (1.0, -1.0):
            def path(y, side=side, sg=sg):
                le, te = wing_le_te(side, 0.72)
                v = (y - le.y) / (te.y - le.y)
                z = wing_z(0.72) + sg * (naca_t(v) * wing_tc(0.72) * (te.y - le.y) + 0.004)
                return Vector((skin_x(y, z, side), y, z + sg * 0.012))
            ys = [lerp(-1.0, 1.4, i / k) for i in range(k + 1)]
            m.sweep([path(y) for y in ys], 0.042, 6, mat="paint", color=PAINT_WING,
                    tangents=[path(y + 1e-4) - path(y - 1e-4) for y in ys])
    # strakes ahead of the tailplane ("SMURFs"), ventral fins, tail bumper
    for side in (LEFT, RIGHT):
        w = fus_section(3.9)[0]
        m.plate([(side * (w - 0.03), 3.35, 0.20), (side * (w + 0.17), 3.85, 0.20),
                 (side * (w - 0.03), 4.30, 0.20)], (0, 0, 0.012), mat="paint", color=PAINT_SURF)
        x = side * 0.24
        m.plate([(x, 4.55, -0.36), (x, 5.45, -0.30), (x, 5.35, -0.64), (x, 4.75, -0.62)],
                (side * 0.014, 0, 0), mat="paint", color=PAINT_SURF)
    m.tube((0, 5.30, -0.30), (0, 5.78, -0.40), 0.04, 0.03, 8, mat="dark", color=RUBBER)
    # fuel vents, whip antenna and beacon on the spine, blade antennas
    m.tube((0.30, 3.2, -0.52), (0.36, 3.45, -0.62), 0.012, 0.010, 6, mat="metal", color=STEEL)
    m.tube((-0.30, 3.2, -0.52), (-0.36, 3.45, -0.62), 0.012, 0.010, 6, mat="metal", color=STEEL)
    zb = _crown(1.90) - 0.01
    m.sweep([Vector(p) for p in ((0, 1.9, zb), (0, 2.0, zb + 0.18), (0, 2.12, zb + 0.32))],
            0.008, 5, mat="metal", color=STEEL)
    zb = _crown(0.60) - 0.015
    m.tube((0, 0.6, zb), (0, 0.6, zb + 0.10), 0.07, 0.05, 10, mat="light", color=RED)
    m.plate([(0.0, 1.05, _crown(1.05) - 0.01), (0.0, 1.40, _crown(1.40) - 0.01),
             (0.0, 1.33, _crown(1.33) + 0.24), (0.0, 1.15, _crown(1.15) + 0.24)],
            (0.012, 0, 0), mat="paint", color=PAINT_SURF)


def build_fuselage(d):
    m = Mesh("fuselage", detail=d)
    fine = d >= 0.5
    ths = stations(-math.pi, math.pi, segs(72, d), FUS_T_JOINTS + (-0.5 * math.pi, 0.0,
                                                                  0.5 * math.pi), cosine=False)[:-1]
    ys = stations(Y_NOSE, Y_SKIN_END, count(180, d, 14), FUS_Y_JOINTS + (RADOME_Y,))
    rings = []
    for y in ys:
        w, top, bot, n = fus_section(y)
        zm, hh = (top + bot) / 2.0, (top - bot) / 2.0
        ring = []
        for th in ths:
            sx, sz = superellipse_point(th, n)
            ring.append(Vector((w * sx, y, zm + hh * sz)))
        rings.append(ring)
    m.rings(rings, mat="paint", color=fus_color, smooth=True)
    build_spine(m, d)

    # --- windscreen glass and its frames (fixed)
    ths = stations(0.0, math.pi, count(24, d, 6), cosine=False)
    ys = stations(DORSAL_Y0, CANOPY_SPLIT, count(12, d, 3))
    m.rings(dorsal_rings(ys, ths), close_ring=False, solid=False, mat="glass", color=GLASS,
            smooth=True)
    dorsal_band(m, CANOPY_SPLIT - 0.020, 0.040, 0.012, d)
    for side in (LEFT, RIGHT):
        sill_band(m, DORSAL_Y0 + 0.02, CANOPY_SPLIT, side, 0.035, 0.010, d)

    # --- D-shaped air intakes
    for side in (LEFT, RIGHT):
        build_intake(m, side, d)

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

    # --- nose pitot probe, ventral blade antenna
    m.tube((0, Y_NOSE + 0.03, -0.04), (0, Y_NOSE - 0.15, -0.04), 0.022, 0.008, 8, mat="metal",
           color=STEEL)
    m.plate([(0.0, -2.5, -0.74), (0.0, -2.0, -0.74), (0.0, -2.05, -0.97), (0.0, -2.3, -0.97)],
            (0.012, 0, 0), mat="paint", color=PAINT_SURF)

    # --- underwing pylons (fixed), two per side
    for side in (LEFT, RIGHT):
        for xp in (1.90, 3.30):
            le, _te = wing_le_te(side, xp)
            ya = le.y + 0.45
            x0 = side * xp - 0.04
            z0 = wing_z(xp) - 0.06
            m.plate([(x0, ya, z0), (x0, ya + 1.05, z0), (x0, ya + 0.95, z0 - 0.26),
                     (x0, ya + 0.12, z0 - 0.30)], (0.08, 0, 0), mat="paint", color=PAINT_SURF)

    build_fixed_wing(m, d)

    build_fin(m, d)
    m.tube((0, 5.74, 2.40), (0, 5.84, 2.40), 0.035, 0.035, 8, mat="light", color=WHITE_LAMP)

    # --- wing-tip pods carrying the navigation lights: left (+x) red, right (-x) green
    for side, col in ((LEFT, RED), (RIGHT, GREEN)):
        y = 1.12
        m.tube((side * 4.78, y, wing_z(4.78)), (side * 4.945, y, wing_z(4.945)), 0.048, 0.040,
               10, mat="paint", color=PAINT_WING)
        m.tube((side * 4.945, y, wing_z(4.945)), (side * 4.97, y, wing_z(4.97)), 0.040, 0.034,
               10, mat="light", color=col)

    if fine:
        shape_breaks(m, d)
        fuselage_decals(m, d)
        for side in (LEFT, RIGHT):
            wing_decals(m, side, d)
            fin_decals(m, side, d)
    return m


FLAP_CUT = (WING["x0"], 2.80)  # spans where the trailing edge behind the hinge is cut away
AIL_CUT = (3.00, 4.55)
WING_TIP_X = 4.90  # the rounded tip starts here


def wing_hinge_y(x):
    """y of the hinge line at span station x: the flap's inboard, the aileron's outboard; the
    full-chord stretch between them blends from one to the other (a station, not a cut)."""
    return lerp(FLAP_HINGE, AIL_HINGE, smoothstep((x - FLAP_CUT[1]) / (AIL_CUT[0] - FLAP_CUT[1])))


def foil_loft(m, stns, ring_at, hinge, is_cut, span_dir, outward, color, root_cap=False):
    """A lifting surface as one loft: `ring_at(s, scale, mid)` gives the aerofoil ring at span
    station s (LE, upper surface, TE, lower surface; the same chord stations on every ring,
    with the hinge line at index `hinge`), so neighbouring stretches share their rings. Where
    `is_cut(sa, sb)` the skin behind the hinge is left out (a flap, aileron or rudder sits
    there), with a hinge face along the cut and a wall where the cut starts or ends.
    `span_dir` points along increasing s, `outward(c)` is the skin's outward direction near
    c. The last ring is capped (the tip); the first only with `root_cap`."""
    rings = [[m.vert(q) for q in ring_at(s, sc, mid)] for s, sc, mid in stns]
    n, h = len(rings[0]), hinge
    front = list(range(n - h, n)) + list(range(0, h))  # edges ahead of the hinge
    back = list(range(h, n - h + 1))  # behind the hinge, from hinge to hinge round the TE
    span_dir = Vector(span_dir)
    skin = []
    for i in range(len(rings) - 1):
        ra, rb = rings[i], rings[i + 1]
        cut = is_cut(stns[i][0], stns[i + 1][0])
        for j in (front if cut else range(n)):
            skin.append(m.face([ra[j], rb[j], rb[(j + 1) % n], ra[(j + 1) % n]], "paint",
                               color, smooth=True))
        if not cut:
            continue
        f = m.face([ra[h], rb[h], rb[n - h], ra[n - h]], "paint", color)  # hinge face
        m.orient([f], lambda c: (0.0, 1.0, 0.0))
        for r, other in ((i, i - 1), (i + 1, i + 2)):  # a wall where the cut starts or ends
            if 0 <= other < len(rings) and not is_cut(*sorted((stns[r][0], stns[other][0]))):
                f = m.face([rings[r][j] for j in back], "paint", color)
                m.orient([f], lambda c, w=span_dir * (1.0 if other < r else -1.0): w)
    m.orient(skin, outward)
    ends = [(rings[-1], span_dir)] + ([(rings[0], -span_dir)] if root_cap else [])
    for ring, w in ends:
        f = m.face(list(ring), "paint", color)
        m.orient([f], lambda c, w=w: w)


def foil_points(le, te, normal, tc, xs, scale=1.0, mid=None):
    """One closed aerofoil ring through the chord fractions xs (0 .. 1): LE, upper surface,
    TE (shared by both surfaces), lower surface."""
    pts = foil_ring(le, te, normal, tc, 0.0, 1.0, len(xs) - 1, scale, mid, xs)
    k = len(xs) - 1
    return pts[:k + 1] + pts[k + 2:-1]


def build_fixed_wing(m, d):
    """Fixed part of each wing (foil_loft); the panel joints and the hinge line are chord
    knots, so the panel tones change exactly at panel lines. The root is open, inside the
    fuselage."""
    nsp = count(9, d, 1)
    cnt = count(22, d, 5)
    xs = stations(WING["x0"], WING_TIP_X, nsp, WING_X_JOINTS + FLAP_CUT[1:] + AIL_CUT,
                  cosine=False)
    nt = count(3, d, 2)
    stns = [(x, 1.0, None) for x in xs] + [
        (WING_TIP_X + 0.045 * s, sc, mid) for s, sc, mid in
        (tip_params(k, nt) for k in range(1, nt + 1))]
    le0, te0 = wing_le_te(LEFT, WING["x0"])
    counts = station_counts(0.0, 1.0, cnt, WING_V_JOINTS + (
        (wing_hinge_y(WING["x0"]) - le0.y) / (te0.y - le0.y),))
    hinge = sum(counts[:len(WING_V_JOINTS) + 1])

    def is_cut(xa, xb):
        return any(lo - 1e-9 <= xa and xb <= hi + 1e-9 for lo, hi in (FLAP_CUT, AIL_CUT))

    for side in (LEFT, RIGHT):
        def ring_at(x, sc, mid, side=side):
            le, te = wing_le_te(side, x)
            vh = (wing_hinge_y(x) - le.y) / (te.y - le.y)
            cs = stations(0.0, 1.0, cnt, WING_V_JOINTS + (vh,), counts=counts)
            return foil_points(le, te, (0, 0, 1), wing_tc(x), cs, sc, mid)
        foil_loft(m, stns, ring_at, hinge, is_cut, (side, 0.0, 0.0),
                  lambda c: (0.0, 0.0, c.z - wing_z(c.x)), wing_color(side))


FIN_CUT = (0.62, 2.42)  # the rudder sits behind the hinge between these heights


def build_fin(m, d):
    """Fixed part of the fin (foil_loft) with a rounded tip; the root is capped because its
    trailing edge stands just clear of the jet pipe."""
    cnt = count(20, d, 5)
    zs = stations(0.45, 2.46, count(5, d, 2), FIN_CUT, cosine=False)
    nt = count(2, d, 2)
    stns = [(z, 1.0, None) for z in zs] + [
        (2.46 + (FIN_TOP - 2.46) * s, sc, mid) for s, sc, mid in
        (tip_params(k, nt) for k in range(1, nt + 1))]
    le0, te0 = fin_le_te(0.45)
    counts = station_counts(0.0, 1.0, cnt, ((RUDDER_HINGE - le0.y) / (te0.y - le0.y),))
    hinge = counts[0]

    def ring_at(z, sc, mid):
        le, te = fin_le_te(z)
        cs = stations(0.0, 1.0, cnt, ((RUDDER_HINGE - le.y) / (te.y - le.y),), counts=counts)
        return foil_points(le, te, (1, 0, 0), fin_tc(z), cs, sc, mid)
    foil_loft(m, stns, ring_at, hinge,
              lambda za, zb: FIN_CUT[0] - 1e-9 <= za and zb <= FIN_CUT[1] + 1e-9,
              (0.0, 0.0, 1.0), lambda c: (c.x, 0.0, 0.0), PAINT_WING, root_cap=True)


def wick(m, p, direction, length=0.15):
    d_ = Vector(direction).normalized()
    m.tube(p, Vector(p) + d_ * length, 0.005, 0.003, 5, mat="dark", color=BLACK)


def _surf_tone(name):
    """One tone per control surface (it is a single panel)."""
    return tone(PAINT_SURF, 1.0 + 0.06 * (hash01(sum(ord(ch) for ch in name)) - 0.5))


def build_flap(side, d):
    name = "flap_l" if side == LEFT else "flap_r"
    xm = sum(FLAP_X) / 2
    m = Mesh(name, origin=(side * xm, FLAP_HINGE, wing_z(xm)), detail=d)
    nsp = count(2, d, 1)
    cnt = count(14, d, 4)
    rings = [wing_station(side, lerp(FLAP_X[0], FLAP_X[1], i / nsp), FLAP_HINGE + GAP, None,
                          cnt=cnt, knots=()) for i in range(nsp + 1)]
    m.rings(rings, mat="paint", color=_surf_tone(name), smooth=True)
    return m


def build_aileron(side, d):
    name = "aileron_l" if side == LEFT else "aileron_r"
    xm = sum(AIL_X) / 2
    m = Mesh(name, origin=(side * xm, AIL_HINGE, wing_z(xm)), detail=d)
    nsp = count(2, d, 1)
    cnt = count(14, d, 4)
    rings = [wing_station(side, lerp(AIL_X[0], AIL_X[1], i / nsp), AIL_HINGE + GAP, None,
                          cnt=cnt, knots=()) for i in range(nsp + 1)]
    m.rings(rings, mat="paint", color=_surf_tone(name), smooth=True)
    if d >= 0.5:
        x = AIL_X[1] * 0.98
        wick(m, (side * x, WING["te"] - 0.01, wing_z(x)), (0, 1, 0.1))
    return m


def build_elevator(d):
    m = Mesh("elevator", origin=(0, TAIL_PIVOT_Y, TAIL_Z), detail=d)
    nsp = count(3, d, 1)
    cnt = count(18, d, 5)
    for side in (LEFT, RIGHT):
        x_tip = TAIL_X1 - 0.07
        rings = [tail_ring(side, x, cnt=cnt) for x in stations(0.25, x_tip, nsp, cosine=False)]
        nt = count(2, d, 2)
        for k in range(1, nt + 1):
            s, sc, mid = tip_params(k, nt)
            rings.append(tail_ring(side, x_tip + (TAIL_X1 - x_tip) * s, sc, mid, cnt))
        m.rings(rings, mat="paint", color=_surf_tone("elevator"), smooth=True,
                caps=(False, True))  # the root stays inside the fuselage in every pose
        if d >= 0.5:
            _le, te = tail_le_te(side, TAIL_X1 - 0.05)
            wick(m, te, (0, 1, 0.05), 0.12)
    return m


def build_rudder(d):
    m = Mesh("rudder", origin=(0, RUDDER_HINGE, 0.9), detail=d)
    k = count(3, d, 1)
    rings = [fin_ring(z, RUDDER_HINGE + GAP, None, cnt=count(14, d, 4))
             for z in stations(0.64, 2.40, k, cosine=False)]
    m.rings(rings, mat="paint", color=_surf_tone("rudder"), smooth=True)
    if d >= 0.5:
        wick(m, (0, 5.83, 2.39), (0, 1, 0.1), 0.14)
    return m


def build_airbrake(d):
    y0, y1 = 3.15, 4.05  # ventral airbrake under the rear fuselage
    zb = fus_point(y0, -math.pi / 2)[2]
    m = Mesh("airbrake", origin=(0, y0, zb), detail=d)
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
    col = _surf_tone("airbrake")
    m.rings([outer[0], outer[1]], close_ring=False, solid=False, mat="paint", color=col,
            smooth=True)
    m.rings([inner[0], inner[1]], close_ring=False, solid=False, mat="paint", color=col,
            smooth=True, flip=True)
    for a, b in ((outer[0], inner[0]), (outer[1], inner[1])):
        m.rings([a, b], close_ring=False, solid=False, mat="paint", color=col)
    for idx in (0, n):
        m.rings([[outer[0][idx], inner[0][idx]], [outer[1][idx], inner[1][idx]]],
                close_ring=False, solid=False, mat="paint", color=col)
    m.recalc(list(m.bm.faces))
    return m


CANOPY_HINGE = Vector((-skin_half_width(-1.6, SILL) * 0.985, (CANOPY_SPLIT + CANOPY_Y1) / 2,
                       SILL))


def build_canopy(d):
    """One-piece stepped-tandem bubble; the Hawk canopy opens sideways to starboard (-X), so
    the hinge is along the right sill (axis parallel to Y)."""
    m = Mesh("canopy", origin=CANOPY_HINGE, detail=d)
    ths = stations(0.0, math.pi, count(24, d, 6), cosine=False)
    ys = stations(CANOPY_SPLIT + 0.002, CANOPY_Y1, count(30, d, 4), (CANOPY_MID,))
    m.rings(dorsal_rings(ys, ths), close_ring=False, solid=False, mat="glass", color=GLASS,
            smooth=True)
    dorsal_band(m, CANOPY_SPLIT + 0.022, 0.040, 0.012, d)
    dorsal_band(m, CANOPY_MID, 0.030, 0.008, d)
    dorsal_band(m, CANOPY_Y1 - 0.022, 0.045, 0.012, d)
    for side in (LEFT, RIGHT):
        sill_band(m, CANOPY_SPLIT, CANOPY_Y1, side, 0.040, 0.012, d)
    return m


def _wheel(m, centre, ro, width, d):
    """Tyre (rounded section), rim and hub about an axle parallel to X."""
    cx, cy, cz = centre
    rr = ro * 0.58
    h = ro - rr
    w = width
    ns = segs(30, d, 8)
    nt = count(16, d, 8)
    prof = []
    for k in range(nt):
        sx, sz = superellipse_point(2 * math.pi * k / nt, 3.0)
        prof.append((0.5 * w * sx, rr + h * (0.5 + 0.5 * sz)))
    m.revolve_x((cx, cy, cz), prof, ns, mat="dark", color=RUBBER, smooth=True,
                closed_profile=True)
    rim = [(-.47 * w, 0.0), (-.47 * w, .55 * rr), (-.40 * w, .92 * rr), (.40 * w, .92 * rr),
           (.47 * w, .55 * rr), (.47 * w, 0.0)]
    m.revolve_x((cx, cy, cz), rim, ns, mat="metal", color=STEEL, smooth=True)
    m.tube((cx - .62 * w, cy, cz), (cx + .62 * w, cy, cz), .05, .05, 8, mat="metal",
           color=STEEL)


def build_gear(d):
    m = Mesh("gear", detail=d)
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
        zw = wing_z(1.70) - 0.06  # leg tops start inside the wing
        m.tube((x, y, zw), (x, y + 0.10, -0.80), 0.065, 0.065, 10, mat="metal", color=STEEL)
        m.tube((x, y + 0.10, -0.80), (x, y + 0.10, zm), 0.045, 0.045, 10, mat="metal",
               color=STEEL)
        m.tube((x, y - 0.55, zw), (x, y + 0.10, -0.78), 0.03, 0.03, 8, mat="metal",
               color=STEEL)
        m.tube((x - side * 0.30, y + 0.40, wing_z(1.40) - 0.03), (x, y + 0.10, -0.70), 0.028,
               0.028, 8, mat="metal", color=STEEL)
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
