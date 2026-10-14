# Hero model: a generic high-wing four-seat light aircraft, proportions from the published
# dimensions of the Cessna 172R/S (length 8.28 m, span 11.00 m, height 2.72 m, wing area 16.2 m2,
# NACA 2412 section, 1.91 m two-blade propeller). No markings, no traced drawings: every shape
# below is a parametric loft or primitive (task T22d).
#
# Blender frame as hawk.py: nose -Y, up +Z, origin ~ centre of gravity, ground contact at
# z = GROUND. Vehicle LEFT is +X, RIGHT is -X.
#
# Tessellation: every count scales with the detail factor `d` (see mm_common). Window, door and
# panel boundaries are knots of the stations, so materials and colours change exactly on
# vertex rings and vertices are shared everywhere else.

import bisect
import math

from mathutils import Vector

from mm_common import (LEFT, RIGHT, Mesh, count, empty, hash01, lerp, make_ao, make_materials,
                       mix, naca_t, pchip, smoothstep, srgb, station_counts, stations,
                       superellipse_point, tip_params, tone)

Y_NOSE = -2.53     # spinner tip
Y_TAIL_END = 5.75  # rudder trailing edge
GROUND = -1.20     # tyres touch here (static, gear uncompressed)
PROP_Y = -2.30     # propeller plane
PROP_Z = 0.03      # thrust line
PROP_R = 0.955     # 1.91 m diameter

# --------------------------------------------------------------------------- colours
PAINT = srgb(0.93, 0.93, 0.92)
PAINT_WING = srgb(0.91, 0.91, 0.90)
PAINT_SURF = srgb(0.88, 0.88, 0.87)
STRIPE = srgb(0.10, 0.22, 0.45)   # generic cheat line (no livery of any operator)
STRIPE2 = srgb(0.70, 0.55, 0.20)
FRAME = srgb(0.09, 0.09, 0.10)
BLACK = srgb(0.04, 0.04, 0.045)
RUBBER = srgb(0.05, 0.05, 0.05)
STEEL = srgb(0.62, 0.64, 0.66)
PROP_GREY = srgb(0.20, 0.20, 0.21)
PROP_TIP = srgb(0.85, 0.85, 0.85)
GLASS = srgb(0.20, 0.26, 0.30)
LINE = srgb(0.62, 0.63, 0.64)
EXHAUST = srgb(0.30, 0.27, 0.24)
RED = srgb(0.9, 0.05, 0.05)
GREEN = srgb(0.05, 0.8, 0.15)
WHITE_LAMP = srgb(0.95, 0.95, 0.9)
INTERIOR = srgb(0.30, 0.29, 0.28)

# --------------------------------------------------------------------------- fuselage
# (y, half width, top z, bottom z, z of the widest line, top squareness, bottom squareness).
# Upper half: superellipse from the widest line up to the top; lower half down to the bottom.
FUS_KEYS = [
    (-2.24, 0.330, 0.270, -0.300, 0.020, 2.4, 2.4),
    (-2.10, 0.400, 0.330, -0.400, 0.030, 2.8, 2.6),
    (-1.85, 0.460, 0.400, -0.500, 0.050, 3.2, 2.8),
    (-1.45, 0.520, 0.500, -0.580, 0.120, 3.8, 3.0),
    (-1.05, 0.550, 0.740, -0.620, 0.220, 4.5, 3.2),
    (-0.62, 0.560, 0.955, -0.630, 0.300, 5.0, 3.2),
    (-0.40, 0.560, 1.015, -0.630, 0.300, 5.0, 3.2),
    (0.60, 0.560, 1.000, -0.600, 0.300, 5.0, 3.2),
    (1.15, 0.540, 0.965, -0.520, 0.300, 4.6, 3.0),
    (1.70, 0.475, 0.830, -0.400, 0.275, 3.8, 2.8),
    (2.60, 0.370, 0.560, -0.240, 0.220, 2.8, 2.6),
    (3.50, 0.270, 0.380, -0.080, 0.170, 2.4, 2.4),
    (4.40, 0.180, 0.290, 0.020, 0.160, 2.2, 2.2),
    (5.15, 0.080, 0.240, 0.100, 0.170, 2.0, 2.0),
]
Y_COWL = FUS_KEYS[0][0]
Y_FUS_END = FUS_KEYS[-1][0]
_ky = [k[0] for k in FUS_KEYS]
_fk = [pchip(_ky, [k[i] for k in FUS_KEYS]) for i in range(1, 7)]


def fus_section(y):
    """(half width, top, bottom, widest-line z, top n, bottom n) at y."""
    return tuple(f(y) for f in _fk)


def fus_point(y, th, grow=0.0):
    """Point on the skin; th: 0 = +x side (on the widest line), pi/2 = top, -pi/2 = bottom."""
    w, top, bot, zw, nt, nb = fus_section(y)
    sx, sz = superellipse_point(th, nt if math.sin(th) >= 0.0 else nb)
    h = (top - zw) if sz >= 0.0 else (zw - bot)
    return Vector(((w + grow) * sx, y, zw + (h + grow) * sz))


def fus_normal(y, th):
    w, top, bot, zw, nt, nb = fus_section(y)
    n = nt if math.sin(th) >= 0.0 else nb
    h = (top - zw) if math.sin(th) >= 0.0 else (zw - bot)
    sx, sz = superellipse_point(th, n)
    gx = math.copysign(abs(sx) ** (n - 1), sx) / w
    gz = math.copysign(abs(sz) ** (n - 1), sz) / h
    # the y component from the change of section along y (finite difference)
    e = 1e-3
    p0, p1 = fus_point(y - e, th), fus_point(y + e, th)
    t_y = (p1 - p0) / (2 * e)
    radial = Vector((gx, 0.0, gz)).normalized()
    nrm = radial - t_y * (radial.dot(t_y) / max(t_y.length_squared, 1e-12))
    return nrm.normalized()


def fus_theta_side(y, z):
    """th of the skin point at height z on the left side (inverse of fus_point)."""
    w, top, bot, zw, nt, nb = fus_section(y)
    if z >= zw:
        n, sz = nt, min(0.9999, (z - zw) / (top - zw))
    else:
        n, sz = nb, -min(0.9999, (zw - z) / (zw - bot))
    sx = (1.0 - abs(sz) ** n) ** (1.0 / n)
    return math.atan2(math.copysign(abs(sz) ** (n / 2.0), sz), sx ** (n / 2.0))


def skin_x(y, z):
    """Half-width of the skin at height z (0 above the top or below the bottom)."""
    w, top, bot, zw, nt, nb = fus_section(y)
    if z >= zw:
        r, n = (z - zw) / (top - zw), nt
    else:
        r, n = (zw - z) / (zw - bot), nb
    return w * max(0.0, 1.0 - min(1.0, abs(r)) ** n) ** (1.0 / n)


def skin_top(y, x):
    """Height of the upper skin at half-width |x| (the section top for |x| = 0)."""
    w, top, bot, zw, nt, nb = fus_section(y)
    r = min(1.0, abs(x) / w)
    return zw + (top - zw) * (1.0 - r ** nt) ** (1.0 / nt)


def side_th(z):
    """th of height z on the left cabin side (cabin sections are constant there)."""
    return fus_theta_side(0.0, z)


# Openings (y range, height range) on the left side, mirrored on the right.
DOOR_Y = (-0.96, 0.10)          # door: front edge (hinge) and rear edge
DOOR_Z = (-0.38, 0.93)
DOOR_WIN_Z = (0.40, 0.89)       # window in the door
DOOR_WIN_Y = (-0.86, 0.02)
REAR_WIN_Y = (0.22, 1.02)       # fixed side window behind the door
REAR_WIN_Z = (0.42, 0.89)
WS_Y = (-1.40, -0.40)           # windscreen: base line to the wing leading edge
WS_TH = 0.30                    # windscreen side edge (A-pillar) in th, above the door top
BACK_WIN_Y = (1.24, 1.95)       # rear (overhead) window behind the wing
BACK_WIN_TH = 0.55
ROOF_CUT_Y = (-0.25, 0.80)      # the cabin roof inside the wing centre section is left out
ROOF_CUT_TH = 1.30
STRIPE_Z = (0.05, 0.17)
BULKHEAD_Y = 2.15               # rear wall of the baggage area         # cheat line on the sides (behind the cowl)

TH_DOOR = (side_th(DOOR_Z[0]), side_th(DOOR_Z[1]))
TH_DOOR_WIN = (side_th(DOOR_WIN_Z[0]), side_th(DOOR_WIN_Z[1]))
TH_REAR_WIN = (side_th(REAR_WIN_Z[0]), side_th(REAR_WIN_Z[1]))
TH_STRIPE = (side_th(STRIPE_Z[0]), side_th(STRIPE_Z[1]))
DOOR_GAP = 0.004


def _mirror_th(th):
    """The same place on the right side: th -> pi - th (wrapped to -pi..pi)."""
    t = math.pi - th
    return t - 2 * math.pi if t > math.pi else t


def fus_th_knots():
    left = [TH_DOOR[0], TH_DOOR[1], TH_DOOR_WIN[0], TH_DOOR_WIN[1], TH_REAR_WIN[0],
            TH_REAR_WIN[1], TH_STRIPE[0], TH_STRIPE[1], WS_TH, BACK_WIN_TH, ROOF_CUT_TH]
    ks = set()
    for t in left:
        ks.add(round(t, 9))
        ks.add(round(_mirror_th(t), 9))
    ks |= {round(-0.5 * math.pi, 9), 0.0, round(0.5 * math.pi, 9)}
    return tuple(sorted(ks))


FUS_Y_KNOTS = (DOOR_Y + DOOR_WIN_Y + REAR_WIN_Y + WS_Y + BACK_WIN_Y + ROOF_CUT_Y
               + (-1.55, Y_FUS_END - 0.6))


def _in(v, lo_hi):
    return lo_hi[0] < v < lo_hi[1]


def side_of(th):
    """(side, th mapped to the left side)."""
    return (LEFT, th) if abs(th) <= 0.5 * math.pi else (RIGHT, _mirror_th(th))


def door_region(y, th):
    _side, t = side_of(th)
    return _in(y, DOOR_Y) and _in(t, TH_DOOR)


def fus_material(y, th):
    """Material of the skin face centred at (y, th)."""
    _side, t = side_of(th)
    if _in(y, WS_Y) and WS_TH < th < math.pi - WS_TH:
        return "glass"
    if _in(y, BACK_WIN_Y) and BACK_WIN_TH < th < math.pi - BACK_WIN_TH:
        return "glass"
    if _in(y, REAR_WIN_Y) and _in(t, TH_REAR_WIN):
        return "glass"
    return "paint"


def paint_color(y, t, base):
    if y > -1.55 and _in(t, TH_STRIPE):
        return STRIPE
    return base


# --------------------------------------------------------------------------- wing
# Constant-chord inner panel, tapered outer panel (the leading edge sweeps back, the trailing
# edge is straight), dihedral, 3 degrees of washout, NACA 2412.
WING = dict(z0=1.02, dihedral=1.73, le0=-0.45, te=1.18, x_taper=2.50, x_tip=5.40, x_end=5.50,
            tip_chord=1.13, tc=0.12, camber=0.02, camber_pos=0.4, inc_root=1.5, inc_tip=-1.5)
FLAP_X = (0.62, 3.25)
AIL_X = (3.36, 5.26)
FLAP_HINGE = 0.74
AIL_HINGE = 0.84
GAP = 0.012
STRUT_X = 2.85


def wing_le_y(x):
    x = abs(x)
    if x <= WING["x_taper"]:
        return WING["le0"]
    root_c = WING["te"] - WING["le0"]
    t = (x - WING["x_taper"]) / (WING["x_tip"] - WING["x_taper"])
    return WING["le0"] + t * (root_c - WING["tip_chord"])


def wing_inc(x):
    t = max(0.0, (abs(x) - WING["x_taper"]) / (WING["x_tip"] - WING["x_taper"]))
    return math.radians(lerp(WING["inc_root"], WING["inc_tip"], min(1.0, t)))


def wing_zq(x):
    """Height of the quarter-chord line at span station x (dihedral)."""
    return WING["z0"] + abs(x) * math.tan(math.radians(WING["dihedral"]))


def wing_frame(x):
    """(le, te, up) of the chord at span station |x| (twist about the quarter chord)."""
    le_y, te_y = wing_le_y(x), WING["te"]
    c = te_y - le_y
    a = wing_inc(x)
    yq = le_y + 0.25 * c
    zq = wing_zq(x)
    d = Vector((0.0, math.cos(a), -math.sin(a)))  # along the chord, aft and (nose up) down
    q = Vector((x, yq, zq))
    le, te = q - d * (0.25 * c), q + d * (0.75 * c)
    up = Vector((0.0, math.sin(a), math.cos(a)))
    return le, te, up


def camber(v, m, p):
    if m <= 0.0:
        return 0.0
    if v < p:
        return m / (p * p) * (2 * p * v - v * v)
    return m / ((1 - p) ** 2) * ((1 - 2 * p) + 2 * p * v - v * v)


def foil_pts(le, te, up, tc, xs, m=0.0, p=0.4, scale=1.0, mid=None):
    """Closed cambered aerofoil ring through the chord fractions xs (0..1): LE, upper surface,
    TE (shared), lower surface back towards the LE. `scale` shrinks the thickness and camber
    (tips), `mid` pulls the chord towards its mid point."""
    le, te, up = Vector(le), Vector(te), Vector(up)
    chord = te - le
    c = chord.length
    upper, lower = [], []
    for v in xs:
        base = le + chord * v
        if mid is not None:
            centre = le + chord * 0.5
            base = centre + (base - centre) * mid
        yc = camber(v, m, p) * c * scale
        yt = naca_t(v) * tc * c * scale
        upper.append(base + up * (yc + yt))
        lower.append(base + up * (yc - yt))
    k = len(xs) - 1
    return upper + lower[k - 1:0:-1]


def foil_part(le, te, up, tc, xs, m=0.0, p=0.4):
    """Open-front ring of the part of a foil between xs[0] and xs[-1] (a control surface):
    upper surface front to back, then the lower surface back to the front."""
    le, te, up = Vector(le), Vector(te), Vector(up)
    chord = te - le
    c = chord.length
    upper, lower = [], []
    for v in xs:
        base = le + chord * v
        yc = camber(v, m, p) * c
        yt = naca_t(v) * tc * c
        upper.append(base + up * (yc + yt))
        lower.append(base + up * (yc - yt))
    return upper + lower[-2::-1]


def wing_surface_z(x, y, upper):
    """z of the wing's upper or lower surface over (x, y), or None outside the chord."""
    le, te, up = wing_frame(abs(x))
    v = (y - le.y) / (te.y - le.y)
    if not 0.0 <= v <= 1.0:
        return None
    c = (te - le).length
    sg = 1.0 if upper else -1.0
    off = (camber(v, WING["camber"], WING["camber_pos"]) + sg * naca_t(v) * WING["tc"]) * c
    return le.z + (te.z - le.z) * v + off * up.z


def chord_outward(frame, x_lim, axis):
    """Outward direction of a lifting surface's skin near c: from the nearest point of the
    chord line at span station min(|span|, x_lim) (`axis` = index of the span coordinate)."""
    def f(c):
        c = Vector(c)
        sp = c[axis]
        s_ = math.copysign(min(abs(sp), x_lim), sp)
        le, te, _up = frame(abs(s_))
        le, te = Vector(le), Vector(te)
        le[axis] = math.copysign(abs(le[axis]), sp)
        te[axis] = math.copysign(abs(te[axis]), sp)
        ch = te - le
        t = max(0.0, min(1.0, (c - le).dot(ch) / ch.length_squared))
        return c - (le + ch * t)
    return f


def wing_outward(c):
    return chord_outward(wing_frame, WING["x_tip"], 0)(c)


def wing_hinge_y(x):
    return lerp(FLAP_HINGE, AIL_HINGE, smoothstep((abs(x) - FLAP_X[1]) / (AIL_X[0] - FLAP_X[1])))


def wing_color(side):
    def f(p, c):
        return PAINT_WING
    return f


# --------------------------------------------------------------------------- foil loft
# Same construction as hawk.foil_loft (copied so that the T22a jet and this model can change
# independently).


def foil_loft(m, stns, ring_at, hinge, is_cut, span_dir, outward, color, root_cap=False):
    """A lifting surface as one loft: `ring_at(s, scale, mid)` gives the aerofoil ring at span
    station s (LE, upper surface, TE, lower surface; the same chord stations on every ring,
    with the hinge line at index `hinge`). Where `is_cut(sa, sb)` the skin behind the hinge is
    left out (a control surface sits there), with a hinge face along the cut and a wall where
    the cut starts or ends. The last ring is capped (the tip); the first only with
    `root_cap`."""
    rings = [[m.vert(q) for q in ring_at(s, sc, mid)] for s, sc, mid in stns]
    n, h = len(rings[0]), hinge
    front = list(range(n - h, n)) + list(range(0, h))
    back = list(range(h, n - h + 1))
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
        f = m.face([ra[h], rb[h], rb[n - h], ra[n - h]], "paint", color)
        m.orient([f], lambda c: (0.0, 1.0, 0.0))
        for r, other in ((i, i - 1), (i + 1, i + 2)):
            if 0 <= other < len(rings) and not is_cut(*sorted((stns[r][0], stns[other][0]))):
                f = m.face([rings[r][j] for j in back], "paint", color)
                m.orient([f], lambda c, w=span_dir * (1.0 if other < r else -1.0): w)
    m.orient(skin, outward)
    ends = [(rings[-1], span_dir)] + ([(rings[0], -span_dir)] if root_cap else [])
    for ring, w in ends:
        f = m.face(list(ring), "paint", color)
        m.orient([f], lambda c, w=w: w)


def build_wing(side, d):
    name = "wing_l" if side == LEFT else "wing_r"
    m = Mesh(name, origin=(side * 1.0, 0.3, wing_zq(1.0)), detail=d)
    cnt = count(26, d, 6)
    knots = (WING["x_taper"], STRUT_X) + FLAP_X + AIL_X
    xs = stations(0.0, WING["x_tip"], count(12, d, 3), knots, cosine=False)
    nt = count(4, d, 2)
    stns = [(x, 1.0, None) for x in xs] + [
        (WING["x_tip"] + (WING["x_end"] - WING["x_tip"]) * s, sc, mid)
        for s, sc, mid in (tip_params(k, nt) for k in range(1, nt + 1))]
    le0, te0, _ = wing_frame(0.0)
    v_h0 = (FLAP_HINGE - le0.y) / (te0.y - le0.y)
    counts = station_counts(0.0, 1.0, cnt, (v_h0,))
    hinge = counts[0]

    def ring_at(x, sc, mid):
        le, te, up = wing_frame(x)
        le, te = Vector((side * le.x, le.y, le.z)), Vector((side * te.x, te.y, te.z))
        vh = (wing_hinge_y(x) - le.y) / (te.y - le.y)
        cs = stations(0.0, 1.0, cnt, (vh,), counts=counts)
        return foil_pts(le, te, up, WING["tc"], cs, WING["camber"], WING["camber_pos"], sc, mid)

    def is_cut(xa, xb):
        return any(lo - 1e-9 <= xa and xb <= hi + 1e-9 for lo, hi in (FLAP_X, AIL_X))

    foil_loft(m, stns, ring_at, hinge, is_cut, (side, 0.0, 0.0), wing_outward,
              wing_color(side))
    # navigation light on the tip fairing: left red, right green
    le, te, up = wing_frame(WING["x_end"] - 0.03)
    y = le.y + 0.12
    m.tube((side * (WING["x_end"] - 0.06), y, le.z), (side * (WING["x_end"] + 0.005), y, le.z),
           0.025, 0.020, 8, mat="light", color=RED if side == LEFT else GREEN)
    return m


def control_surface(name, side, x0, x1, hinge_y, d, pivot_drop=0.0):
    """Flap or aileron: the wing section behind the hinge between span stations x0..x1. The
    node pivot is on the hinge line (dropped below it by `pivot_drop` for a flap whose tracks
    move it aft as well as down)."""
    xm = (x0 + x1) / 2
    le, te, up = wing_frame(xm)
    zc = wing_surface_z(xm, hinge_y, True)
    zl = wing_surface_z(xm, hinge_y, False)
    m = Mesh(name, origin=(side * xm, hinge_y, (zc + zl) / 2 - pivot_drop), detail=d)
    nsp = count(3, d, 1)
    cnt = count(12, d, 4)
    rings = []
    for i in range(nsp + 1):
        x = lerp(x0 + 0.004, x1 - 0.004, i / nsp)
        le, te, up = wing_frame(x)
        le, te = Vector((side * le.x, le.y, le.z)), Vector((side * te.x, te.y, te.z))
        v0 = (hinge_y + GAP - le.y) / (te.y - le.y)
        xs = stations(v0, 1.0, cnt, cosine=False)
        rings.append(foil_part(le, te, up, WING["tc"], xs, WING["camber"], WING["camber_pos"]))
    m.rings(rings, mat="paint", color=PAINT_SURF, smooth=True)
    return m


# --------------------------------------------------------------------------- tail
TAIL_Z = 0.16
STAB = dict(x0=0.0, x1=1.64, x_end=1.725, le0=4.18, le1=4.42, te0=5.50, te1=5.28, tc=0.09)
ELEV_HINGE = 4.92
ELEV_X0 = 0.10   # the elevator halves start outside the rudder's lower end
STAB_ROOT = 0.22  # first tailplane station, outside the tail cone (the root ring is moved in
                  # onto the skin), so no station of any detail falls inside the fuselage
FIN = dict(z0=0.30, z1=1.46, z_end=1.52, le0=3.85, le1=4.86, tc=0.10)
RUDDER_HINGE = 5.15
RUDDER_Z = (0.10, 1.46)


def stab_frame(x):
    t = min(1.0, abs(x) / STAB["x1"])
    le = Vector((x, lerp(STAB["le0"], STAB["le1"], t), TAIL_Z))
    te = Vector((x, lerp(STAB["te0"], STAB["te1"], t), TAIL_Z))
    return le, te, Vector((0.0, 0.0, 1.0))


def fin_frame(z):
    t = (z - FIN["z0"]) / (FIN["z1"] - FIN["z0"])
    le = Vector((0.0, lerp(FIN["le0"], FIN["le1"], t), z))
    te = Vector((0.0, rudder_te_y(z), z))
    return le, te, Vector((1.0, 0.0, 0.0))


def rudder_te_y(z):
    return lerp(5.62, Y_TAIL_END, smoothstep((z - RUDDER_Z[0]) / (1.30 - RUDDER_Z[0])))


def build_stab(m, d):
    """Fixed tailplane (part of the fuselage node): both halves, elevator cut-out behind the
    hinge."""
    cnt = count(18, d, 5)
    for side in (LEFT, RIGHT):
        xs = stations(STAB_ROOT, STAB["x1"], count(5, d, 2), cosine=False)
        nt = count(3, d, 2)
        stns = [(x, 1.0, None) for x in xs] + [
            (STAB["x1"] + (STAB["x_end"] - STAB["x1"]) * s, sc, mid)
            for s, sc, mid in (tip_params(k, nt) for k in range(1, nt + 1))]
        le0, te0, _ = stab_frame(0.0)
        counts = station_counts(0.0, 1.0, cnt, ((ELEV_HINGE - le0.y) / (te0.y - le0.y),))

        def ring_at(x, sc, mid, side=side):
            le, te, up = stab_frame(x)
            le.x, te.x = side * le.x, side * te.x
            vh = (ELEV_HINGE - le.y) / (te.y - le.y)
            cs = stations(0.0, 1.0, cnt, (vh,), counts=counts)
            pts = foil_pts(le, te, up, STAB["tc"], cs, scale=sc, mid=mid)
            if x == STAB_ROOT:  # the root follows the tail cone's side, just inside it
                pts = [Vector((side * max(0.0, skin_x(p.y, p.z) - 0.004), p.y, p.z))
                       for p in pts]
            return pts
        foil_loft(m, stns, ring_at, counts[0],
                  lambda xa, xb: xb <= STAB["x1"] + 1e-9,  # elevator, or the rudder's gap
                  (side, 0.0, 0.0), chord_outward(stab_frame, STAB["x1"], 0), PAINT_WING)


def build_elevator(d):
    m = Mesh("elevator", origin=(0.0, ELEV_HINGE, TAIL_Z), detail=d)
    cnt = count(12, d, 4)
    for side in (LEFT, RIGHT):
        rings = []
        for x in stations(ELEV_X0, STAB["x1"] - 0.01, count(4, d, 1), cosine=False):
            le, te, up = stab_frame(x)
            le.x, te.x = side * le.x, side * te.x
            v0 = (ELEV_HINGE + GAP - le.y) / (te.y - le.y)
            rings.append(foil_part(le, te, up, STAB["tc"], stations(v0, 1.0, cnt, cosine=False)))
        m.rings(rings, mat="paint", color=PAINT_SURF, smooth=True)
    return m


def build_fin(m, d):
    cnt = count(18, d, 5)
    zs = stations(FIN["z0"], FIN["z1"], count(5, d, 2), RUDDER_Z, cosine=False)
    nt = count(3, d, 2)
    stns = [(z, 1.0, None) for z in zs] + [
        (FIN["z1"] + (FIN["z_end"] - FIN["z1"]) * s, sc, mid)
        for s, sc, mid in (tip_params(k, nt) for k in range(1, nt + 1))]
    le0, te0, _ = fin_frame(FIN["z0"])
    counts = station_counts(0.0, 1.0, cnt, ((RUDDER_HINGE - le0.y) / (te0.y - le0.y),))

    def ring_at(z, sc, mid):
        le, te, up = fin_frame(z)
        vh = (RUDDER_HINGE - le.y) / (te.y - le.y)
        cs = stations(0.0, 1.0, cnt, (vh,), counts=counts)
        pts = foil_pts(le, te, up, FIN["tc"], cs, scale=sc, mid=mid)
        if z == FIN["z0"]:  # the root follows the tail cone's top, just inside it
            pts = [Vector((p.x, p.y, skin_top(p.y, p.x) - 0.004)) if p.y < Y_FUS_END
                   else p for p in pts]
        return pts
    foil_loft(m, stns, ring_at, counts[0],
              lambda za, zb: RUDDER_Z[0] - 1e-9 <= za and zb <= RUDDER_Z[1] + 1e-9,
              (0.0, 0.0, 1.0), chord_outward(fin_frame, FIN["z1"], 2), PAINT_WING,
              root_cap=False)


def build_rudder(d):
    m = Mesh("rudder", origin=(0.0, RUDDER_HINGE, 0.7), detail=d)
    cnt = count(12, d, 4)
    rings = []
    for z in stations(RUDDER_Z[0] + 0.005, RUDDER_Z[1] - 0.005, count(5, d, 2), cosine=False):
        le, te, up = fin_frame(z)
        v0 = (RUDDER_HINGE + GAP - le.y) / (te.y - le.y)
        rings.append(foil_part(le, te, up, FIN["tc"], stations(v0, 1.0, cnt, cosine=False)))
    m.rings(rings, mat="paint", color=PAINT_SURF, smooth=True)
    return m


# --------------------------------------------------------------------------- fuselage


def wing_covers(p):
    """Skin points inside the wing (the cabin roof under the wing centre section)."""
    if abs(p.x) > WING["x_tip"]:
        return False
    lo = wing_surface_z(p.x, p.y, False)
    hi = wing_surface_z(p.x, p.y, True)
    return lo is not None and lo + 0.002 < p.z < hi


def _foil_covers(frame, tc, p, span, x_lim):
    """Inside a symmetric foil whose span coordinate is `span` (|span| <= x_lim)."""
    if abs(span) > x_lim:
        return False
    le, te, up = frame(abs(span))
    v = (p.y - le.y) / (te.y - le.y)
    if not 0.0 < v < 1.0:
        return False
    half = naca_t(v) * tc * (te.y - le.y)
    off = (p - (le + (te - le) * v)).dot(up)
    return abs(off) < half - 0.001


def stab_covers(p):
    return _foil_covers(stab_frame, STAB["tc"], p, p.x, STAB["x1"])


def fin_covers(p):
    return p.z > FIN["z0"] - 0.2 and _foil_covers(
        lambda z: fin_frame(max(z, FIN["z0"])), FIN["tc"], p, p.z, FIN["z1"])


def adaptive(a, b, knots, density, d, fine=1200):
    """Stations from a to b (with every knot in (a, b) present exactly) whose spacing follows
    `density(u0, u1)` -> segments wanted between u0 and u1 at detail 1: each interval between
    knots gets max(1, round(d * its density integral)) equal parts of that integral. The
    density is a fixed function of the analytic shape, so the stations only refine with d."""
    us = [a + (b - a) * i / fine for i in range(fine + 1)]
    inc = [0.0] + [density(us[i - 1], us[i]) for i in range(1, fine + 1)]
    cum = [0.0]
    for v in inc[1:]:
        cum.append(cum[-1] + v)

    def F(u):
        t = (u - a) / (b - a) * fine
        i = min(fine - 1, max(0, int(t)))
        return cum[i] + (cum[i + 1] - cum[i]) * (t - i)

    def Finv(f):
        i = max(0, min(fine - 1, bisect.bisect_right(cum, f) - 1))
        span = cum[i + 1] - cum[i]
        t = 0.0 if span <= 0 else (f - cum[i]) / span
        return us[i] + (us[i + 1] - us[i]) * max(0.0, min(1.0, t))
    edges = [a] + sorted(k for k in set(knots) if a < k < b) + [b]
    out = []
    for u0, u1 in zip(edges, edges[1:]):
        f0, f1 = F(u0), F(u1)
        k = max(1, int(round(d * (f1 - f0))))
        out.append(u0)
        out += [Finv(f0 + (f1 - f0) * j / k) for j in range(1, k)]
    out.append(b)
    return out


def _turn_density(pts, err):
    """Segments wanted per polyline step for a chordal error `err`: sqrt(curvature / 8 err)
    times the step length (s = sqrt(8 e R) is the chord with sagitta e)."""
    out = [0.0] * len(pts)
    for i in range(1, len(pts) - 1):
        a, b = pts[i] - pts[i - 1], pts[i + 1] - pts[i]
        la, lb = a.length, b.length
        if la < 1e-9 or lb < 1e-9:
            continue
        ang = a.angle(b, 0.0)
        kappa = ang / (0.5 * (la + lb))
        out[i] = math.sqrt(kappa / (8.0 * err)) * 0.5 * (la + lb)
    return out


FUS_ERR = 0.0004      # target chordal error of the skin at detail 1 (m)
FUS_MIN_STEP = 0.30   # longest skin edge even where the skin is flat (m), for shading


def fus_stations():
    """Curvature-adaptive (y, th) stations of the skin: the same lists at every detail before
    scaling, so --detail only refines them."""
    ny, nt = 160, 720
    ysmp = [Y_COWL + (Y_FUS_END - Y_COWL) * i / ny for i in range(ny + 1)]
    tsmp = [-math.pi + 2 * math.pi * j / nt for j in range(nt + 1)]
    th_d = [0.0] * (nt + 1)
    for y in ysmp[::4]:
        pts = [fus_point(y, t) for t in tsmp]
        dens = _turn_density(pts, FUS_ERR)
        for j in range(nt + 1):
            seg = (pts[j] - pts[j - 1]).length if j else 0.0
            th_d[j] = max(th_d[j], dens[j], seg / FUS_MIN_STEP)
    y_d = [0.0] * (ny + 1)
    for t in tsmp[::12]:
        pts = [fus_point(y, t) for y in ysmp]
        dens = _turn_density(pts, FUS_ERR)
        for i in range(ny + 1):
            seg = (pts[i] - pts[i - 1]).length if i else 0.0
            y_d[i] = max(y_d[i], dens[i], seg / FUS_MIN_STEP)

    def table(us, vals):
        def f(u0, u1):
            t = (0.5 * (u0 + u1) - us[0]) / (us[-1] - us[0]) * (len(us) - 1)
            i = min(len(us) - 2, max(0, int(t)))
            v = vals[i] + (vals[i + 1] - vals[i]) * (t - i)
            step = (us[-1] - us[0]) / (len(us) - 1)
            return v * (u1 - u0) / step
        return f
    return table(ysmp, y_d), table(tsmp, th_d)


def build_fuselage(d):
    m = Mesh("fuselage", detail=d)
    y_den, th_den = fus_stations()
    ths = adaptive(-math.pi, math.pi, fus_th_knots(), th_den, d)[:-1]
    ys = adaptive(Y_COWL, Y_FUS_END, FUS_Y_KNOTS, y_den, d)
    grid = [[m.vert(fus_point(y, th)) for th in ths] for y in ys]
    n = len(ths)
    faces = []
    for i in range(len(ys) - 1):
        yc = (ys[i] + ys[i + 1]) / 2
        for j in range(n):
            j1 = (j + 1) % n
            t0, t1 = ths[j], ths[j1] if j1 else ths[j1] + 2 * math.pi
            tc = (t0 + t1) / 2
            if tc > math.pi:
                tc -= 2 * math.pi
            if door_region(yc, tc):
                continue
            if _in(yc, ROOF_CUT_Y) and ROOF_CUT_TH < tc < math.pi - ROOF_CUT_TH:
                continue
            quad = [grid[i][j], grid[i + 1][j], grid[i + 1][j1], grid[i][j1]]
            mat = fus_material(yc, tc)
            faces.append(m.face(quad, mat, GLASS if mat == "glass" else
                                paint_color(yc, side_of(tc)[1], PAINT), smooth=True))
    m.orient(faces, lambda c: c - Vector((0.0, c.y, fus_section(c.y)[3])))
    # tail cone end cap
    cap = m.face(list(reversed(grid[-1])), "paint", PAINT)
    m.orient([cap], lambda c: (0.0, 1.0, 0.0))
    # nose bowl: annulus from the cowl front to the spinner base
    ring0 = grid[0]
    k = count(2, d, 1)
    rings = [[v.co + m.origin for v in ring0]]
    for i in range(1, k + 1):
        t = i / k
        rings.append([Vector((lerp(p.x, 0.15 * math.cos(th), t), Y_COWL + 0.03 * t,
                              lerp(p.z, PROP_Z + 0.15 * math.sin(th), t)))
                      for p, th in zip(rings[0], ths)])
    nose = m.rings(rings, close_ring=True, solid=False, mat="paint", color=PAINT, smooth=True,
                   ends=(ring0, None))
    m.orient(nose, lambda c: (0.0, -1.0, 0.0))
    yb = BULKHEAD_Y
    ring = [fus_point(yb, th, -0.004) for th in ths]
    f = m.poly(ring, "dark", INTERIOR)
    m.orient([f], lambda c: (0.0, -1.0, 0.0))
    build_stab(m, d)
    build_fin(m, d)
    build_struts(m, d)
    return m


def build_struts(m, d):
    for side in (LEFT, RIGHT):
        le, te, _ = wing_frame(STRUT_X)
        y_top = le.y + 0.30 * (te.y - le.y)
        z_top = wing_surface_z(STRUT_X, y_top, False) + 0.01
        w = fus_section(0.25)[0]
        a = Vector((side * (w - 0.02), 0.25, -0.30))
        b = Vector((side * STRUT_X, y_top, z_top))
        m.tube(a, b, 0.038, 0.034, 16, mat="paint", color=PAINT_SURF)


# --------------------------------------------------------------------------- doors


def build_door(side, d):
    name = "door_l" if side == LEFT else "door_r"
    y_h = DOOR_Y[0] + DOOR_GAP
    t0, t1 = TH_DOOR
    hinge = fus_point(y_h, side_th(0.25) if side == LEFT else _mirror_th(side_th(0.25)))
    m = Mesh(name, origin=hinge, detail=d)
    ky = count(10, d, 3)
    kt = count(10, d, 3)
    tw = TH_DOOR_WIN
    ys = stations(DOOR_Y[0] + DOOR_GAP, DOOR_Y[1] - DOOR_GAP, ky, DOOR_WIN_Y, cosine=False)
    ts = stations(t0 + 0.003, t1 - 0.003, kt, tw + TH_STRIPE, cosine=False)
    if side == RIGHT:
        ts = [_mirror_th(t) for t in ts]
    outer = [[fus_point(y, t) for t in ts] for y in ys]
    inner = [[fus_point(y, t, -0.045) for t in ts] for y in ys]
    vo = [[m.vert(p) for p in row] for row in outer]
    vi = [[m.vert(p) for p in row] for row in inner]
    faces_out, faces_in = [], []
    glass_cells = set()
    for i in range(len(ys) - 1):
        for j in range(len(ts) - 1):
            yc, tc = (ys[i] + ys[i + 1]) / 2, (ts[j] + ts[j + 1]) / 2
            _s, tl = side_of(tc)
            glass = _in(yc, DOOR_WIN_Y) and _in(tl, tw)
            if glass:
                glass_cells.add((i, j))
            faces_out.append(m.face([vo[i][j], vo[i + 1][j], vo[i + 1][j + 1], vo[i][j + 1]],
                                    "glass" if glass else "paint",
                                    GLASS if glass else paint_color(yc, tl, PAINT), smooth=True))
            if not glass:
                faces_in.append(m.face([vi[i][j], vi[i + 1][j], vi[i + 1][j + 1],
                                        vi[i][j + 1]], "dark", INTERIOR, smooth=True))
    out = Vector((side, 0.0, 0.0))
    m.orient(faces_out, lambda c: out)
    m.orient(faces_in, lambda c: -out)
    # edge walls round the door and round the window opening (the glass sits in the outer skin)
    walls = []
    ny, nt_ = len(ys), len(ts)
    border = ([(0, j) for j in range(nt_)] + [(i, nt_ - 1) for i in range(1, ny)]
              + [(ny - 1, j) for j in range(nt_ - 2, -1, -1)] + [(i, 0) for i in range(ny - 2, 0, -1)])
    for a, b in zip(border, border[1:] + border[:1]):
        walls.append(m.face([vo[a[0]][a[1]], vo[b[0]][b[1]], vi[b[0]][b[1]], vi[a[0]][a[1]]],
                            "paint", PAINT, smooth=False))
    centre = sum((p for row in outer for p in row), Vector()) / (ny * nt_)
    m.orient(walls, lambda c: Vector((0.0, c.y - centre.y, c.z - centre.z)))
    # window reveal: walls from the outer skin to the inner panel round the glass, facing
    # into the opening
    for (i, j) in sorted(glass_cells):
        for di, dj, edge in ((-1, 0, ((i, j), (i, j + 1))), (1, 0, ((i + 1, j), (i + 1, j + 1))),
                             (0, -1, ((i, j), (i + 1, j))), (0, 1, ((i, j + 1), (i + 1, j + 1)))):
            if (i + di, j + dj) in glass_cells:
                continue
            (a0, a1), (b0, b1) = edge
            f = m.face([vo[a0][a1], vo[b0][b1], vi[b0][b1], vi[a0][a1]], "dark", INTERIOR)
            cc = (outer[i][j] + outer[i + 1][j + 1]) / 2
            m.orient([f], lambda c, cc=cc: cc - c)
    return m


# --------------------------------------------------------------------------- propeller


def build_propeller(d):
    m = Mesh("propeller", origin=(0.0, PROP_Y, PROP_Z), detail=d)
    # spinner: profile of revolution about the thrust line
    ns = count(24, d, 8)
    prof = [(Y_NOSE, 0.0)]
    k = count(10, d, 4)
    for i in range(1, k + 1):
        t = i / k
        prof.append((lerp(Y_NOSE, -2.22, t), 0.172 * math.sin(0.5 * math.pi * t) ** 0.75))
    rings = []
    for y, r in prof:
        rings.append([Vector((r * math.cos(2 * math.pi * j / ns), y,
                              PROP_Z + r * math.sin(2 * math.pi * j / ns))) for j in range(ns)])
    sp = m.rings(rings, mat="paint", color=PAINT, smooth=True, solid=False)
    m.orient(sp, lambda c: Vector((c.x, c.y - (Y_NOSE + 0.35), c.z - PROP_Z)))
    # two blades along +-Z at rest; pitch angle from a 1.5 m helix, chord tapering to a round tip
    nr = count(10, d, 3)
    cnt = count(10, d, 4)
    for sg in (1.0, -1.0):
        rings = []
        rs = stations(0.15, PROP_R - 0.06, nr, cosine=False)
        nt = count(3, d, 2)
        rows = [(r, 1.0, None) for r in rs] + [
            (PROP_R - 0.06 + 0.06 * s, sc, mid) for s, sc, mid in
            (tip_params(i, nt) for i in range(1, nt + 1))]
        for r, sc, mid in rows:
            chord = 0.155 - 0.045 * (r - 0.15) / (PROP_R - 0.15) if r > 0.25 else lerp(0.07, 0.15, (r - 0.15) / 0.10)
            beta = math.atan2(1.52, 2 * math.pi * max(r, 0.2))
            # chord direction in the x-y plane, rotated by the pitch angle; the blade turns
            # clockwise seen from the cockpit, the leading edge on +x for the upper blade
            cdir = Vector((-sg * math.cos(beta), math.sin(beta), 0.0))
            up = cdir.cross(Vector((0.0, 0.0, 1.0))).normalized()
            if up.y > 0.0:  # the cambered (suction) face looks forward
                up = -up
            centre = Vector((0.0, PROP_Y, PROP_Z + sg * r))
            le = centre - cdir * (0.35 * chord)
            te = centre + cdir * (0.65 * chord)
            tc = lerp(0.30, 0.08, min(1.0, (r - 0.15) / 0.5))
            xs = stations(0.0, 1.0, cnt)
            rings.append(foil_pts(le, te, up, tc, xs, 0.03, 0.4, sc, mid))
        bl = m.rings(rings, mat="metal", color=PROP_GREY, smooth=True, caps=(False, True))
        m.recalc(bl)
    return m


def build_prop_disc(d):
    m = Mesh("prop_disc", origin=(0.0, PROP_Y, PROP_Z), shade=False, detail=d)
    ns = count(48, d, 12)
    rings = []
    for r in (0.175, PROP_R):
        rings.append([Vector((r * math.cos(2 * math.pi * j / ns), PROP_Y - 0.01,
                              PROP_Z + r * math.sin(2 * math.pi * j / ns))) for j in range(ns)])
    f = m.rings(rings, close_ring=True, solid=False, mat="hud", color=srgb(0.5, 0.5, 0.5, 0.25),
                smooth=False)
    m.orient(f, lambda c: (0.0, -1.0, 0.0))
    return m


# --------------------------------------------------------------------------- gear
MAIN_Y = 0.48
MAIN_X = 1.27
MAIN_R = 0.215
NOSE_Y = -1.80
NOSE_R = 0.180


FAIRING_CUT = 0.07  # the fairing's flat bottom is this far below the axle


def _wheel(m, centre, ro, width, d):
    """The part of the tyre that shows below the fairing (the rest would be hidden)."""
    cx, cy, cz = centre
    rr = ro * 0.55
    h = ro - rr
    ns = count(12, d, 4)
    nt = count(12, d, 6)
    prof = []
    for k in range(nt):
        sx, sz = superellipse_point(2 * math.pi * k / nt, 3.0)
        prof.append((0.5 * width * sx, rr + h * (0.5 + 0.5 * sz)))
    half = math.acos(min(1.0, (FAIRING_CUT - 0.015) / ro))  # 15 mm up into the fairing
    phis = stations(-0.5 * math.pi - half, -0.5 * math.pi + half, ns, cosine=False)
    rings = [[Vector((cx + u, cy + r * math.cos(phi), cz + r * math.sin(phi))) for phi in phis]
             for u, r in prof]
    tyre = m.rings(rings, wrap_rings=True, close_ring=False, mat="dark", color=RUBBER,
                   smooth=True, solid=False, rounded=True)
    core = rr + 0.5 * h

    def out(c):  # away from the tyre's core circle
        radial = Vector((0.0, c.y - cy, c.z - cz))
        q = Vector((cx, cy, cz)) + radial.normalized() * core
        return c - q
    m.orient(tyre, out)


def fairing_r(t):
    """Teardrop radius (0..1) at t = 0 (nose) .. 1 (tail): round nose, tapering tail."""
    return (t ** 0.5) * (1.0 - t) ** 0.9 / 0.5296


def _fairing(m, centre, length, width, height, d):
    """Wheel speed fairing: a closed teardrop with a flat bottom FAIRING_CUT below the axle;
    the tyre shows through the bottom (a slot in the real one)."""
    cx, cy, cz = centre
    ns = count(20, d, 8)
    k = count(14, d, 5)
    zc = cz + 0.10
    zcut = cz - FAIRING_CUT
    nf = count(4, d, 2)
    rings = []
    for t in stations(0.0, 1.0, k)[1:]:
        y = cy - 0.36 * length + length * t
        r = fairing_r(t)
        top = zcut + (zc + 0.5 * height - zcut) * r
        ring = []
        for j in range(ns + 1):  # arc over the top, from the right floor corner to the left
            a = math.pi * j / ns
            ring.append(Vector((cx - 0.5 * width * r * math.cos(a), y,
                                zcut + (top - zcut) * math.sin(a) ** 0.85)))
        for j in range(1, nf):  # the floor back to the right
            ring.append(Vector((cx + 0.5 * width * r * (1 - 2 * j / nf), y, zcut)))
        rings.append(ring)
    nose = Vector((cx, cy - 0.36 * length, zcut))
    rings.insert(0, [nose] * len(rings[0]))
    f = m.rings(rings, mat="paint", color=PAINT, smooth=True, solid=False)

    def out(c):
        t = max(0.0, min(1.0, (c.y - (cy - 0.36 * length)) / length))
        top = zcut + (zc + 0.5 * height - zcut) * fairing_r(t)
        return Vector((c.x - cx, 0.0, c.z - 0.5 * (zcut + top)))
    m.orient(f, out)


def build_gear(d):
    m = Mesh("gear", detail=d)
    for side in (LEFT, RIGHT):
        # spring-steel leg from the lower fuselage side out to the axle
        w = fus_section(MAIN_Y)[0]
        top = Vector((side * (w - 0.05), MAIN_Y, -0.50))
        end = Vector((side * (MAIN_X - 0.09), MAIN_Y, GROUND + MAIN_R + 0.02))  # in the fairing
        k = count(6, d, 3)
        pts = [top.lerp(end, i / k) for i in range(k + 1)]
        m.sweep(pts, 0.032, 8, mat="paint", color=PAINT)
        _wheel(m, (side * MAIN_X, MAIN_Y, GROUND + MAIN_R), MAIN_R, 0.15, d)
        _fairing(m, (side * MAIN_X, MAIN_Y, GROUND + MAIN_R), 0.80, 0.26, 0.42, d)
    # nose leg (oleo) from under the firewall to the fork
    top = Vector((0.0, -1.62, -0.50))
    hub = Vector((0.0, NOSE_Y, GROUND + NOSE_R))
    m.tube(top, top.lerp(hub, 0.55), 0.045, 0.045, 10, mat="metal", color=STEEL)
    m.tube(top.lerp(hub, 0.5), top.lerp(hub, 0.80), 0.032, 0.032, 10, mat="metal",
           color=STEEL)
    _wheel(m, (0.0, NOSE_Y, GROUND + NOSE_R), NOSE_R, 0.13, d)
    _fairing(m, (0.0, NOSE_Y, GROUND + NOSE_R), 0.62, 0.22, 0.36, d)
    return m


# --------------------------------------------------------------------------- build


def build(d=1.0):
    """Return (root object, expected node names)."""
    mats = make_materials()
    fus_m = build_fuselage(d)
    parts = [build_wing(LEFT, d), build_wing(RIGHT, d),
             control_surface("flap_l", LEFT, *FLAP_X, FLAP_HINGE, d, 0.06),
             control_surface("flap_r", RIGHT, *FLAP_X, FLAP_HINGE, d, 0.06),
             control_surface("aileron_l", LEFT, *AIL_X, AIL_HINGE, d),
             control_surface("aileron_r", RIGHT, *AIL_X, AIL_HINGE, d),
             build_elevator(d), build_rudder(d), build_door(LEFT, d), build_door(RIGHT, d),
             build_propeller(d), build_gear(d)]
    disc = build_prop_disc(d)
    ao = make_ao([fus_m] + parts) if d >= 0.4 else None
    fus = fus_m.finish(mats, ao=ao)
    names = ["fuselage"]
    for p in parts:
        p.finish(mats, parent=fus, ao=ao)
        names.append(p.name)
    disc.finish(mats, parent=fus)
    names.append("prop_disc")
    return fus, names


def checks():
    """Model-specific checks after the build (Blender scene): the three tyres touch the ground
    and the propeller clears it. Returns a list of problem strings."""
    import bpy
    problems = []
    gear = bpy.data.objects.get("gear")
    prop = bpy.data.objects.get("propeller")
    if gear is not None:
        mw = gear.matrix_world
        for name, (x, y) in (("left main", (MAIN_X, MAIN_Y)), ("right main", (-MAIN_X, MAIN_Y)),
                             ("nose", (0.0, NOSE_Y))):
            zs = [(mw @ v.co).z for v in gear.data.vertices
                  if abs((mw @ v.co).x - x) < 0.1 and abs((mw @ v.co).y - y) < 0.3]
            low = min(zs) if zs else None
            if low is None or abs(low - GROUND) > 0.005:
                problems.append(f"{name} tyre bottom at {low} (ground {GROUND})")
    if prop is not None:
        mw = prop.matrix_world
        low = min((mw @ v.co).z for v in prop.data.vertices)
        if low - GROUND < 0.20:
            problems.append(f"propeller tip ground clearance {low - GROUND:.3f} m < 0.20 m")
        else:
            print(f"[make_models] cessna: propeller ground clearance {low - GROUND:.3f} m")
    return problems


# Close-up preview views (make_models.render_previews): camera location, target, lens.
PREVIEW_VIEWS = {
    "closeup": dict(loc=(3.2, -5.2, 1.3), target=(0.2, -1.2, 0.3), lens=40),
    "cabin": dict(loc=(4.6, -0.3, 0.6), target=(0.4, -0.3, 0.35), lens=35),
    "tail": dict(loc=(3.4, 7.6, 1.6), target=(0.0, 4.6, 0.6), lens=40),
    "front": dict(loc=(0.0, -14.0, 0.5), target=(0.0, 0.0, 0.3), lens=55),
}
