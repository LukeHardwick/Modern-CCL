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


from mathutils import Matrix, Vector

from mm_common import (LEFT, RIGHT, Mesh, count, empty, lerp, make_ao, make_materials, naca_t,
                       pchip, smoothstep, srgb, stations, superellipse_point)

Y_NOSE = -2.53     # spinner tip
Y_TAIL_END = 5.75  # rudder trailing edge
GROUND = -1.20     # tyres touch here (static, gear uncompressed)
PROP_Y = -2.30     # propeller plane
PROP_Z = 0.03      # thrust line
PROP_R = 0.955     # 1.91 m diameter

EXTRAS = [True]  # small parts (lights, antennas, handles, pitot, inlets): off for the LODs

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
    (-0.40, 0.560, 1.005, -0.630, 0.300, 5.0, 3.2),
    (0.60, 0.560, 1.000, -0.605, 0.300, 5.0, 3.2),
    (1.15, 0.540, 0.965, -0.550, 0.300, 4.6, 3.0),
    (1.70, 0.475, 0.830, -0.460, 0.275, 3.8, 2.8),
    (2.60, 0.370, 0.560, -0.280, 0.220, 2.8, 2.6),
    (3.50, 0.270, 0.380, -0.110, 0.170, 2.4, 2.4),
    (4.40, 0.180, 0.290, 0.010, 0.160, 2.2, 2.2),
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


def fus_param_normal(c):
    """Analytic outward skin normal at the section angle of the point c (on or near a
    surface parallel to the skin)."""
    w, top, bot, zw, nt, nb = fus_section(c.y)
    if c.z >= zw:
        n, sz = nt, (c.z - zw) / max(top - zw, 1e-6)
    else:
        n, sz = nb, (c.z - zw) / max(zw - bot, 1e-6)
    sx = c.x / max(w, 1e-6)
    th = math.atan2(math.copysign(min(1.0, abs(sz)) ** (n / 2.0), sz),
                    math.copysign(min(1.0, abs(sx)) ** (n / 2.0), sx))
    return fus_normal(c.y, th)


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
STRIPE_Z = (0.05, 0.17)         # cheat line on the sides (behind the cowl)
BULKHEAD_Y = 2.15               # rear wall of the baggage area
FIREWALL_Y = -1.55              # between the engine compartment and the cabin

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
               + (FIREWALL_Y, Y_FUS_END - 0.6))


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
    if y > FIREWALL_Y and _in(t, TH_STRIPE):
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


# --------------------------------------------------------------------------- tessellation


_CHORD_CACHE = {}


def chord_stations(tc, m, p, chord, d, knots=(), err=0.0004, min_step=0.012, max_step=0.10,
                   a=0.0, b=1.0, counts=None, want_counts=False):
    key = (tc, m, p, chord, d, tuple(knots), err, min_step, max_step, a, b,
           None if counts is None else tuple(counts), want_counts)
    if key not in _CHORD_CACHE:
        _CHORD_CACHE[key] = _chord_stations(tc, m, p, chord, d, knots, err, min_step, max_step,
                                            a, b, counts, want_counts)
    return _CHORD_CACHE[key]


def _chord_stations(tc, m, p, chord, d, knots, err, min_step, max_step, a, b, counts,
                    want_counts):
    """Chord fractions a..b for an aerofoil (t/c `tc`, camber m at p) of length `chord`:
    spacing from the upper and lower surfaces' curvature for a chordal error `err` (at detail
    1), no step shorter than `min_step` (the leading edge: keeps the long spanwise strips from
    becoming slivers) or longer than `max_step`; knots (hinge lines) exact."""
    def surf(v, sg):
        return Vector((v * chord, 0.0, (camber(v, m, p) + sg * naca_t(v) * tc) * chord))

    def density(u0, u1):
        best = 0.0
        for sg in (1.0, -1.0):
            q0, q1 = surf(u0, sg), surf(u1, sg)
            qm = surf(0.5 * (u0 + u1), sg)
            seg = (q1 - q0).length
            if seg < 1e-12:
                continue
            ang = (qm - q0).angle(q1 - qm, 0.0)
            kappa = ang / max(seg, 1e-9)
            dens = math.sqrt(kappa / (8.0 * err)) * seg
            best = max(best, min(dens, seg / min_step), seg / max_step)
        return best
    # sample densely in a warped parameter (the leading edge needs it), then map back
    return adaptive(a, b, knots, density, d, fine=2000, counts=counts,
                    want_counts=want_counts)


def zip_chains(m, A, B, mat, color, want):
    """Triangulate the strip between two vertex chains with the same first and last vertex
    (a cap split into two halves): advance along whichever chain gives the shorter diagonal,
    so no triangle is a long fan blade. Faces are oriented towards `want(centre)`."""
    i, j, faces = 0, 0, []
    while i < len(A) - 1 or j < len(B) - 1:
        if j == len(B) - 1 or (i < len(A) - 1 and
                               (A[i + 1].co - B[j].co).length <= (B[j + 1].co - A[i].co).length):
            faces.append(m.face([A[i], A[i + 1], B[j]], mat, color))
            i += 1
        else:
            faces.append(m.face([A[i], B[j + 1], B[j]], mat, color))
            j += 1
    m.orient(faces, want)
    return faces


def ring_cap(m, ring, j0, jpi, mat, color, want):
    """Close a section ring (vertex list) by zipping its two halves: from index j0 round to
    jpi one way, and the other way."""
    n = len(ring)
    A = [ring[(j0 + k) % n] for k in range((jpi - j0) % n + 1)]
    B = [ring[(j0 - k) % n] for k in range((j0 - jpi) % n + 1)]
    return zip_chains(m, A, B, mat, color, want)


def tip_rings(n):
    """Rounded tip, ring k of n (1..n): (fraction of the tip length, thickness scale, chord
    scale towards mid-chord); an elliptic cap that closes to a line at the end."""
    out = []
    for k in range(1, n + 1):  # even steps round the ellipse (s = sin phi, scale = cos phi)
        phi = 0.5 * math.pi * k / n
        s_ = math.sin(phi)
        out.append((s_, math.cos(phi) if k < n else 0.0, 1.0 - 0.30 * s_ ** 3))
    return out


# --------------------------------------------------------------------------- foil loft
# Same construction as hawk.foil_loft (copied so that the T22a jet and this model can change
# independently).


def foil_loft(m, stns, ring_at, hinge, is_cut, span_dir, outward, color, root_cap=False,
              mat="paint"):
    """A lifting surface as one loft: `ring_at(s, scale, mid)` gives the aerofoil ring at span
    station s (LE, upper surface, TE, lower surface; the same chord stations on every ring,
    with the hinge line at index `hinge`). Where `is_cut(sa, sb)` the skin behind the hinge is
    left out (a control surface sits there), with a hinge face along the cut and a wall where
    the cut starts or ends. The last ring is capped (the tip); the first only with
    `root_cap`."""
    rings = []
    for s_, sc, mid in stns:
        pts = ring_at(s_, sc, mid)
        ring = []
        for k, q in enumerate(pts):  # a closed tip: the lower point merges with the upper one
            twin = len(pts) - k
            if 0 < twin < k and (pts[twin] - q).length < 1e-7:
                ring.append(ring[twin])
            else:
                ring.append(m.vert(q))
        rings.append(ring)
    n, h = len(rings[0]), hinge
    front = list(range(n - h, n)) + list(range(0, h))
    back = list(range(h, n - h + 1))
    span_dir = Vector(span_dir)
    skin = []
    for i in range(len(rings) - 1):
        ra, rb = rings[i], rings[i + 1]
        cut = is_cut(stns[i][0], stns[i + 1][0])
        for j in (front if cut else range(n)):
            skin.append(m.face([ra[j], rb[j], rb[(j + 1) % n], ra[(j + 1) % n]], mat,
                               color, smooth=True))
        if not cut:
            continue
        f = m.face([ra[h], rb[h], rb[n - h], ra[n - h]], mat, color)
        m.orient([f], lambda c: (0.0, 1.0, 0.0))
        for r, other in ((i, i - 1), (i + 1, i + 2)):
            if 0 <= other < len(rings) and not is_cut(*sorted((stns[r][0], stns[other][0]))):
                f = m.face([rings[r][j] for j in back], mat, color)
                m.orient([f], lambda c, w=span_dir * (1.0 if other < r else -1.0): w)
    m.orient(skin, outward)
    if len(set(map(id, rings[-1]))) < n:  # closed tip: flat-shade the last band so the
        last = set(map(id, rings[-1]))     # closing line does not average both skins
        for f in skin:
            if f is not None and any(id(v) in last for v in f.verts):
                f.smooth = False
    ends = ([(rings[-1], span_dir)] if len(set(map(id, rings[-1]))) == n else []) + (
        [(rings[0], -span_dir)] if root_cap else [])
    for ring, w in ends:
        K = (n + 1) // 2  # zip the upper and lower surfaces across the end
        ring_cap(m, ring, 0, K, mat, color, lambda c, w=w: w)


def build_wing(side, d):
    name = "wing_l" if side == LEFT else "wing_r"
    m = Mesh(name, origin=(side * 1.0, 0.3, wing_zq(1.0)), detail=d)
    knots = (WING["x_taper"], STRUT_X) + FLAP_X + AIL_X
    xs = stations(0.0, WING["x_tip"], count(18, d, 3), knots, cosine=False)
    nt = count(8, d, 2)
    stns = [(x, 1.0, None) for x in xs] + [
        (WING["x_tip"] + (WING["x_end"] - WING["x_tip"]) * s, sc, mid)
        for s, sc, mid in tip_rings(nt)]
    le0, te0, _ = wing_frame(0.0)
    c0 = te0.y - le0.y
    _cs, counts = chord_stations(WING["tc"], WING["camber"], WING["camber_pos"], c0, d,
                                 ((FLAP_HINGE - le0.y) / c0,), want_counts=True)
    hinge = counts[0]

    def ring_at(x, sc, mid):
        le, te, up = wing_frame(x)
        le, te = Vector((side * le.x, le.y, le.z)), Vector((side * te.x, te.y, te.z))
        c = te.y - le.y
        vh = (wing_hinge_y(x) - le.y) / c
        cs = chord_stations(WING["tc"], WING["camber"], WING["camber_pos"], c0, d, (vh,),
                            counts=counts)
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
    if not EXTRAS[0]:  # lower levels of detail: no small parts
        return m
    m.tube((side * (WING["x_end"] - 0.06), te.y - 0.25, te.z + 0.01),
           (side * (WING["x_end"] + 0.004), te.y - 0.25, te.z + 0.01), 0.016, 0.014, 8,
           mat="light", color=WHITE_LAMP)  # strobe
    # fuel filler cap on the upper surface near the root
    xc, yc = side * 1.15, 0.12
    zc = wing_surface_z(1.15, yc, True)
    disc(m, (xc, yc, zc + 0.002), (0.0, 0.0, 1.0), 0.035, 0.035, count(12, d, 8), "metal",
         STEEL, up=(0.0, 1.0, 0.0))
    if side == LEFT:
        # landing/taxi light lens in the leading edge, pitot tube under the wing
        x = 2.05
        le, te, up = wing_frame(x)
        disc(m, (x, le.y - 0.002, le.z), (0.0, -1.0, 0.0), 0.11, 0.025, count(16, d, 8),
             "light", WHITE_LAMP)
        zl = wing_surface_z(3.6, -0.10, False)
        path = ([Vector((3.6, -0.10 - 0.02 * t, zl + 0.01 - 0.11 * t)) for t in (0, .5, 1)]
                + [Vector((3.6, -0.12 - 0.23 * t, zl - 0.10)) for t in (.25, .5, .75, 1.0)])
        m.sweep(path, 0.008, 8, mat="metal", color=STEEL)
    return m


def control_surface(name, side, x0, x1, hinge_y, d, pivot_drop=0.0):
    """Flap or aileron: the wing section behind the hinge between span stations x0..x1. The
    node pivot is on the hinge line (dropped below it by `pivot_drop` for a flap whose tracks
    move it aft as well as down)."""
    xm = (x0 + x1) / 2
    zc = wing_surface_z(xm, hinge_y, True)
    zl = wing_surface_z(xm, hinge_y, False)
    m = Mesh(name, origin=(side * xm, hinge_y, (zc + zl) / 2 - pivot_drop), detail=d)
    rings = []
    counts = None
    for x in stations(x0 + 0.004, x1 - 0.004, count(6, d, 1), (WING["x_taper"],),
                      cosine=False):
        le, te, up = wing_frame(x)
        le, te = Vector((side * le.x, le.y, le.z)), Vector((side * te.x, te.y, te.z))
        c = te.y - le.y
        v0 = (hinge_y + GAP - le.y) / c
        xs, counts = chord_stations(WING["tc"], WING["camber"], WING["camber_pos"], c, d,
                                    a=v0, counts=counts, want_counts=True)
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
FIN = dict(z0=0.40, z1=1.46, z_end=1.52, le0=3.85, le1=4.86, tc=0.10)
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


def root_on_skin(ring_fn, signed, s_out, s_in, iters=40):
    """The root ring of a lifting surface where it meets the fuselage skin: for each ring
    index, bisection along the span between `s_out` (outside the skin) and `s_in` (inside) for
    the point where `signed(p)` changes sign (> 0 outside). The points lie on the surface's own
    analytic shape, so the root does not depend on the station spacing."""
    out_ring, in_ring = ring_fn(s_out), ring_fn(s_in)
    res = []
    for k in range(len(out_ring)):
        if signed(in_ring[k]) > 0.0:  # never inside (behind the fuselage end): keep s_out
            res.append(out_ring[k])
            continue
        a, b = s_out, s_in
        for _ in range(iters):
            mid = 0.5 * (a + b)
            if signed(ring_fn(mid)[k]) > 0.0:
                a = mid
            else:
                b = mid
        res.append(ring_fn(0.5 * (a + b))[k])
    return res


def build_stab(m, d):
    """Fixed tailplane (part of the fuselage node): both halves, elevator cut-out behind the
    hinge."""
    le0, te0, _ = stab_frame(0.0)
    c0 = te0.y - le0.y
    _cs, counts = chord_stations(STAB["tc"], 0.0, 0.4, c0, d,
                                 ((ELEV_HINGE - le0.y) / c0,), want_counts=True)
    for side in (LEFT, RIGHT):
        xs = stations(STAB_ROOT, STAB["x1"], count(5, d, 2), cosine=False)
        nt = count(8, d, 2)
        stns = [(x, 1.0, None) for x in xs] + [
            (STAB["x1"] + (STAB["x_end"] - STAB["x1"]) * s, sc, mid)
            for s, sc, mid in tip_rings(nt)]

        def plain(x, sc=1.0, mid=None, side=side):
            le, te, up = stab_frame(x)
            le.x, te.x = side * le.x, side * te.x
            vh = (ELEV_HINGE - le.y) / (te.y - le.y)
            cs = chord_stations(STAB["tc"], 0.0, 0.4, c0, d, (vh,), counts=counts)
            return foil_pts(le, te, up, STAB["tc"], cs, scale=sc, mid=mid)

        def ring_at(x, sc, mid, side=side, plain=plain):
            if x == STAB_ROOT:  # the root where the surface meets the tail cone, just inside
                return root_on_skin(plain, lambda p: abs(p.x) - (skin_x(p.y, p.z) - 0.004),
                                    STAB_ROOT, 0.0)
            return plain(x, sc, mid)
        foil_loft(m, stns, ring_at, counts[0],
                  lambda xa, xb: xb <= STAB["x1"] + 1e-9,  # elevator, or the rudder's gap
                  (side, 0.0, 0.0), chord_outward(stab_frame, STAB["x1"], 0), PAINT_WING)


def build_elevator(d):
    m = Mesh("elevator", origin=(0.0, ELEV_HINGE, TAIL_Z), detail=d)
    for side in (LEFT, RIGHT):
        rings = []
        counts = None
        for x in stations(ELEV_X0, STAB["x1"] - 0.01, count(4, d, 1), cosine=False):
            le, te, up = stab_frame(x)
            le.x, te.x = side * le.x, side * te.x
            c = te.y - le.y
            v0 = (ELEV_HINGE + GAP - le.y) / c
            xs, counts = chord_stations(STAB["tc"], 0.0, 0.4, c, d, a=v0, counts=counts,
                                        want_counts=True)
            rings.append(foil_part(le, te, up, STAB["tc"], xs))
        m.rings(rings, mat="paint", color=PAINT_SURF, smooth=True)
    return m


def build_fin(m, d):
    zs = stations(FIN["z0"], FIN["z1"], count(5, d, 2), RUDDER_Z, cosine=False)
    nt = count(8, d, 2)
    stns = [(z, 1.0, None) for z in zs] + [
        (FIN["z1"] + (FIN["z_end"] - FIN["z1"]) * s, sc, mid) for s, sc, mid in tip_rings(nt)]
    le0, te0, _ = fin_frame(FIN["z0"])
    c0 = te0.y - le0.y
    _cs, counts = chord_stations(FIN["tc"], 0.0, 0.4, c0, d,
                                 ((RUDDER_HINGE - le0.y) / c0,), want_counts=True)

    def plain(z, sc=1.0, mid=None):
        le, te, up = fin_frame(z)
        vh = (RUDDER_HINGE - le.y) / (te.y - le.y)
        cs = chord_stations(FIN["tc"], 0.0, 0.4, c0, d, (vh,), counts=counts)
        return foil_pts(le, te, up, FIN["tc"], cs, scale=sc, mid=mid)

    def above_skin(p):  # behind the tail cone's end its end section continues
        return p.z - (skin_top(min(p.y, Y_FUS_END), p.x) - 0.004)

    def ring_at(z, sc, mid):
        if z == FIN["z0"]:  # the root where the fin meets the tail cone, just inside it
            return root_on_skin(plain, above_skin, FIN["z0"], 0.0)
        return plain(z, sc, mid)
    foil_loft(m, stns, ring_at, counts[0],
              lambda za, zb: RUDDER_Z[0] - 1e-9 <= za and zb <= RUDDER_Z[1] + 1e-9,
              (0.0, 0.0, 1.0), chord_outward(fin_frame, FIN["z1"], 2), PAINT_WING,
              root_cap=False)


def build_rudder(d):
    m = Mesh("rudder", origin=(0.0, RUDDER_HINGE, 0.7), detail=d)
    rings = []
    counts = None
    for z in stations(RUDDER_Z[0] + 0.005, RUDDER_Z[1] - 0.005, count(12, d, 2), cosine=False):
        le, te, up = fin_frame(z)
        c = te.y - le.y
        v0 = (RUDDER_HINGE + GAP - le.y) / c
        xs, counts = chord_stations(FIN["tc"], 0.0, 0.4, c, d, a=v0, counts=counts,
                                    want_counts=True)
        rings.append(foil_part(le, te, up, FIN["tc"], xs))
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


def adaptive(a, b, knots, density, d, fine=1200, counts=None, want_counts=False):
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
    out, used = [], []
    for i, (u0, u1) in enumerate(zip(edges, edges[1:])):
        f0, f1 = F(u0), F(u1)
        k = counts[i] if counts is not None else max(1, int(round(d * (f1 - f0))))
        used.append(k)
        out.append(u0)
        out += [Finv(f0 + (f1 - f0) * j / k) for j in range(1, k)]
    out.append(b)
    return (out, used) if want_counts else out


def adaptive_grid(us, inc, knots, d, counts=None):
    """As adaptive(), on a given (sorted, possibly non-uniform) fine grid `us` with `inc[i]` =
    segments wanted between us[i - 1] and us[i] at detail 1."""
    cum = [0.0]
    for v in inc[1:]:
        cum.append(cum[-1] + v)
    a, b = us[0], us[-1]

    def F(u):
        i = max(0, min(len(us) - 2, bisect.bisect_right(us, u) - 1))
        span = us[i + 1] - us[i]
        t = 0.0 if span <= 0 else (u - us[i]) / span
        return cum[i] + (cum[i + 1] - cum[i]) * t

    def Finv(f):
        i = max(0, min(len(us) - 2, bisect.bisect_right(cum, f) - 1))
        span = cum[i + 1] - cum[i]
        t = 0.0 if span <= 0 else (f - cum[i]) / span
        return us[i] + (us[i + 1] - us[i]) * max(0.0, min(1.0, t))
    edges = [a] + sorted(k for k in set(knots) if a < k < b) + [b]
    out = []
    for i, (u0, u1) in enumerate(zip(edges, edges[1:])):
        f0, f1 = F(u0), F(u1)
        k = counts[i] if counts is not None else max(1, int(round(d * (f1 - f0))))
        out.append(u0)
        out += [Finv(f0 + (f1 - f0) * j / k) for j in range(1, k)]
    out.append(b)
    return out


def section_fn(y):
    """Fast point function of the section at y (the section evaluated once)."""
    w, top, bot, zw, nt, nb = fus_section(y)

    def f(th):
        up = math.sin(th) >= 0.0
        sx, sz = superellipse_point(th, nt if up else nb)
        h = (top - zw) if up else (zw - bot)
        return Vector((w * sx, y, zw + h * sz))
    return f


def th_grid(per_quadrant=1500):
    """Fine th samples, dense at the ends of each quadrant (the superellipse parameter crowds
    the flat sides, top and bottom into a tiny range of th there)."""
    out = []
    for q in range(4):
        base = -math.pi + q * 0.5 * math.pi
        for i in range(per_quadrant if q < 3 else per_quadrant + 1):
            f = i / per_quadrant
            out.append(base + 0.5 * math.pi * f * f * f * (10 - 15 * f + 6 * f * f))
    return out


TH_GRID = th_grid()


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


_FUS_ST = []


def fus_stations():
    if not _FUS_ST:
        _FUS_ST.append(_fus_stations())
    return _FUS_ST[0]


def _fus_stations(err=None):
    """Curvature-adaptive y stations of the skin (the largest need over th lines sampled on
    TH_GRID, so the flat sides are seen) and, for the doors, th stations of the cabin."""
    ny = 200
    ysmp = [Y_COWL + (Y_FUS_END - Y_COWL) * i / ny for i in range(ny + 1)]
    lines = TH_GRID[::40]
    secs = [section_fn(y) for y in ysmp]
    y_d = [0.0] * (ny + 1)
    err = err or FUS_ERR
    for t in lines:
        pts = [f(t) for f in secs]
        dens = _turn_density(pts, err)
        for i in range(ny + 1):
            seg = (pts[i] - pts[i - 1]).length if i else 0.0
            y_d[i] = max(y_d[i], dens[i], seg / FUS_MIN_STEP)
    f = section_fn(0.0)
    cab = [f(t) for t in TH_GRID]
    dens = _turn_density(cab, err)
    th_inc = [0.0] + [max(0.5 * (dens[j] + dens[j - 1]), (cab[j] - cab[j - 1]).length /
                          FUS_MIN_STEP) for j in range(1, len(cab))]

    def y_den(u0, u1):
        t = (0.5 * (u0 + u1) - ysmp[0]) / (ysmp[-1] - ysmp[0]) * ny
        i = min(ny - 1, max(0, int(t)))
        v = y_d[i] + (y_d[i + 1] - y_d[i]) * (t - i)
        return v * (u1 - u0) / ((ysmp[-1] - ysmp[0]) / ny)
    return y_den, th_inc


def ring_stations(y, d, err=None, knots=None):
    """The th stations of the ring at y: from this section's own curvature (a small tail-cone
    section needs fewer than the cabin), every knot included, so neighbouring rings can be
    zipped between knots."""
    f = section_fn(y)
    pts = [f(t) for t in TH_GRID]
    dens = _turn_density(pts, err or FUS_ERR)
    inc = [0.0] + [max(0.5 * (dens[j] + dens[j - 1]),
                       (pts[j] - pts[j - 1]).length / FUS_MIN_STEP)
                   for j in range(1, len(pts))]
    return adaptive_grid(TH_GRID, inc, knots or fus_th_knots(), d)[:-1]


def build_fuselage(d):
    m = Mesh("fuselage", detail=d)
    y_den, _th_inc = fus_stations()
    ys = adaptive(Y_COWL, Y_FUS_END, FUS_Y_KNOTS, y_den, d)
    knots = fus_th_knots()
    rings = []  # per y: (th list, vertex list)
    for y in ys:
        ths = ring_stations(y, d)
        rings.append((ths, [m.vert(fus_point(y, th)) for th in ths]))

    def segments(ths):
        """Index runs between consecutive knots (the last run wraps to -pi + 2 pi)."""
        cut = [j for j, t in enumerate(ths) if any(abs(t - k) < 1e-9 for k in knots)
               or j == 0]
        runs = []
        for a_, b_ in zip(cut, cut[1:] + [len(ths)]):
            runs.append(list(range(a_, b_ + 1)))
        return runs
    faces = []
    for i in range(len(ys) - 1):
        (ta, va), (tb, vb) = rings[i], rings[i + 1]
        yc = (ys[i] + ys[i + 1]) / 2
        for ra, rb in zip(segments(ta), segments(tb)):
            n_a, n_b = len(ta), len(tb)
            th_a = [ta[j % n_a] + (2 * math.pi if j >= n_a else 0.0) for j in ra]
            th_b = [tb[j % n_b] + (2 * math.pi if j >= n_b else 0.0) for j in rb]
            A = [va[j % n_a] for j in ra]
            B = [vb[j % n_b] for j in rb]
            tc = 0.25 * (th_a[0] + th_a[-1] + th_b[0] + th_b[-1])
            if tc > math.pi:
                tc -= 2 * math.pi
            if door_region(yc, tc):
                continue
            if _in(yc, ROOF_CUT_Y) and ROOF_CUT_TH < tc < math.pi - ROOF_CUT_TH:
                continue
            mat = fus_material(yc, tc)
            col = GLASS if mat == "glass" else paint_color(yc, side_of(tc)[1], PAINT)
            if len(A) == len(B):
                for k in range(len(A) - 1):
                    faces.append(m.face([A[k], B[k], B[k + 1], A[k + 1]], mat, col,
                                        smooth=True))
            else:
                faces += zip_chains(m, A, B, mat, col, lambda c: Vector((0.0, 0.0, 0.0)))
    for f in faces:
        if f is not None:
            f.smooth = True
    m.orient(faces, fus_param_normal)
    ths, grid_last = rings[-1]
    # tail cone end cap (zipped: no fan of slivers)
    j0 = min(range(len(ths)), key=lambda j: abs(ths[j]))
    ring_cap(m, grid_last, j0, 0, "paint", PAINT, lambda c: (0.0, 1.0, 0.0))
    ths0, ring0 = rings[0]
    # nose bowl: annulus from the cowl front to the spinner base
    k = count(2, d, 1)
    rings = [[v.co + m.origin for v in ring0]]
    for i in range(1, k + 1):
        t = i / k
        rings.append([Vector((lerp(p.x, 0.15 * math.cos(th), t), Y_COWL + 0.03 * t,
                              lerp(p.z, PROP_Z + 0.15 * math.sin(th), t)))
                      for p, th in zip(rings[0], ths0)])
    nose = m.rings(rings, close_ring=True, solid=False, mat="paint", color=PAINT, smooth=True,
                   ends=(ring0, None))
    m.orient(nose, lambda c: (0.0, -1.0, 0.0))
    if EXTRAS[0]:
        build_bulkheads(m, d)
    build_stab(m, d)
    build_fin(m, d)
    build_struts(m, d)
    if EXTRAS[0]:
        build_details(m, d)
    else:
        build_dorsal(m, d)
    return m


def build_bulkheads(m, d):
    """Firewall (closes the engine compartment off from the cabin) and the rear wall of the
    baggage area (closes the view down the tail cone through the windows)."""
    yf = FIREWALL_Y
    thf = ring_stations(yf, d)
    ring = [m.vert(fus_point(yf, th, -0.004)) for th in thf]
    ring_cap(m, ring, min(range(len(thf)), key=lambda j: abs(thf[j])), 0, "dark", INTERIOR,
             lambda c: (0.0, 1.0, 0.0))
    yb = BULKHEAD_Y
    thb = ring_stations(yb, d)
    ring = [m.vert(fus_point(yb, th, -0.004)) for th in thb]
    ring_cap(m, ring, min(range(len(thb)), key=lambda j: abs(thb[j])), 0, "dark", INTERIOR,
             lambda c: (0.0, -1.0, 0.0))


def strut(m, a, b, chord, tc, d, color=None):
    """A streamlined strut from a to b: a symmetric aerofoil section (chord along the flight
    direction, square to the strut) lofted with enough stations that no quad is a needle.
    The ends sit inside the parts they join, so they are left open."""
    axis = (b - a).normalized()
    fwd = Vector((0.0, -1.0, 0.0))
    fwd = (fwd - axis * fwd.dot(axis)).normalized()
    up = axis.cross(fwd).normalized()
    xs = chord_stations(tc, 0.0, 0.4, chord, d, err=0.0003, min_step=0.008, max_step=0.03)
    n = count(max(4, int((b - a).length / 0.15)), d, 2)
    rings = []
    for i in range(n + 1):
        c = a.lerp(b, i / n)
        le = c + fwd * (0.4 * chord)
        te = c - fwd * (0.6 * chord)
        rings.append(foil_pts(le, te, up, tc, xs))
    f = m.rings(rings, mat="paint", color=color or PAINT_SURF, smooth=True, solid=False)
    m.orient(f, lambda q: (q - a) - axis * (q - a).dot(axis))
    return f


def fin_le_y(z):
    return lerp(FIN["le0"], FIN["le1"], (z - FIN["z0"]) / (FIN["z1"] - FIN["z0"]))


DORSAL = dict(y0=2.80, z_top=0.80, half=0.024)  # dorsal fin: LE start, top, half-thickness


def build_dorsal(m, d):
    """Dorsal fin: a thin blade on the tail-cone top whose rounded crest rises in a straight
    line from y0 into the fin's leading edge; built from cross-sections along y (so its
    triangles do not narrow into needles), ending just inside the fin."""
    z0 = skin_top(DORSAL["y0"], 0.0)
    # the crest line meets the fin's leading edge at z_top
    y_end = fin_le_y(DORSAL["z_top"]) + 0.015
    half = DORSAL["half"]
    k = count(10, d, 4)  # points over the crest from one side to the other
    rings = []
    for y in stations(DORSAL["y0"], y_end, count(12, d, 3))[1:]:
        t = (y - DORSAL["y0"]) / (y_end - DORSAL["y0"])
        crest = lerp(z0, DORSAL["z_top"], t)
        hw = half * min(1.0, 4.0 * t) ** 0.5  # the blade thickens behind its front point
        base = skin_top(y, hw) - 0.004
        ring = []
        for j in range(k + 1):
            phi = math.pi * j / k
            ring.append(Vector((hw * math.cos(phi), y,
                                base + (crest - base) * math.sin(phi) ** 0.35)))
        rings.append(ring)
    nose = Vector((0.0, DORSAL["y0"], z0 - 0.004))
    rings.insert(0, [nose] * (k + 1))
    f = m.rings(rings, close_ring=False, mat="paint", color=PAINT_WING, smooth=True,
                solid=False)
    m.orient(f, lambda c: (c.x, 0.0, 0.0))


def disc(m, centre, normal, rx, rz, seg, mat, color, up=(0.0, 0.0, 1.0)):
    """A flat elliptical disc (decal-like part: an inlet, a cap, a lamp lens), zip-closed."""
    n = Vector(normal).normalized()
    u = Vector(up)
    u = (u - n * u.dot(n)).normalized()
    v = n.cross(u)
    ring = [m.vert(Vector(centre) + v * (rx * math.cos(2 * math.pi * k / seg))
                   + u * (rz * math.sin(2 * math.pi * k / seg))) for k in range(seg)]
    return ring_cap(m, ring, 0, seg // 2, mat, color, lambda c: n)


def bowl_y(x, z):
    """y of the nose bowl (the annulus from the cowl front ring in to r = 0.15 round the
    thrust line) at (x, z), from the polar radius between the outer ring and the inner circle."""
    f = section_fn(Y_COWL)
    phi = math.atan2(z - PROP_Z, x)
    rho = math.hypot(x, z - PROP_Z)
    best = None
    for k in range(720):  # the outer ring's radius at this polar angle
        p = f(-math.pi + 2 * math.pi * k / 720)
        a = math.atan2(p.z - PROP_Z, p.x)
        da = abs((a - phi + math.pi) % (2 * math.pi) - math.pi)
        if best is None or da < best[0]:
            best = (da, math.hypot(p.x, p.z - PROP_Z))
    t = max(0.0, min(1.0, (best[1] - rho) / max(best[1] - 0.15, 1e-6)))
    return Y_COWL + 0.03 * t


def inlet(m, xc, zc, rx, rz, seg):
    """A cowl air inlet: a dark ellipse lying 1.5 mm in front of the nose bowl with a skirt
    running back through the bowl, so no gap shows behind it."""
    front, back = [], []
    for k in range(seg):
        a = 2 * math.pi * k / seg
        x, z = xc + rx * math.cos(a), zc + rz * math.sin(a)
        y = bowl_y(x, z)
        front.append(m.vert((x, y - 0.0015, z)))
        back.append(m.vert((x, y + 0.008, z)))
    ring_cap(m, front, 0, seg // 2, "dark", BLACK, lambda c: (0.0, -1.0, 0.0))
    skirt = [m.face([front[k], front[(k + 1) % seg], back[(k + 1) % seg], back[k]], "dark",
                    BLACK) for k in range(seg)]
    m.orient(skirt, lambda c: (c.x - xc, 0.0, c.z - zc))


def build_details(m, d):
    """Fuselage-node detail: dorsal fin, cowl inlets and exhaust, beacon, antennas, door
    handles, tail tie-down."""
    build_dorsal(m, d)
    seg = count(16, d, 8)
    for side in (LEFT, RIGHT):  # cowl air inlets either side of the spinner
        inlet(m, side * 0.215, PROP_Z + 0.03, 0.085, 0.055, seg)
    # exhaust stack under the right side of the cowl
    zb = fus_point(-1.75, -0.5 * math.pi).z
    m.tube((-0.14, -1.80, zb + 0.06), (-0.16, -1.66, zb - 0.07), 0.024, 0.024, 12, mat="dark",
           color=EXHAUST)
    # rotating beacon on the fin tip, white tail light on the rudder trailing edge
    le, te, _ = fin_frame(FIN["z1"])
    yb = le.y + 0.12
    m.tube((0.0, yb, FIN["z_end"] - 0.035), (0.0, yb, FIN["z_end"] - 0.005), 0.022, 0.018,
           12, mat="light", color=RED)
    # VHF antennas on the cabin roof behind the wing, ADF/transponder blade under the belly
    for x, y in ((0.0, 1.45), (0.0, 1.85)):
        zt = skin_top(y, x) - 0.01
        m.plate([(x, y, zt), (x, y + 0.10, zt), (x, y + 0.13, zt + 0.32), (x, y + 0.08, zt + 0.32)],
                (0.006, 0.0, 0.0), mat="dark", color=BLACK)
    zb = fus_point(1.30, -0.5 * math.pi).z
    m.plate([(0.0, 1.20, zb + 0.01), (0.0, 1.32, zb + 0.01), (0.0, 1.36, zb - 0.09),
             (0.0, 1.28, zb - 0.09)], (0.006, 0.0, 0.0), mat="dark", color=BLACK)
    # tail tie-down ring under the tail cone
    zt = fus_point(4.75, -0.5 * math.pi).z
    m.tube((0.0, 4.75, zt + 0.01), (0.0, 4.75, zt - 0.06), 0.012, 0.012, 8, mat="metal",
           color=STEEL)
    # door handles
    for side in (LEFT, RIGHT):
        y0, y1, z = DOOR_Y[1] - 0.20, DOOR_Y[1] - 0.08, 0.30
        pa = Vector((side * (skin_x(y0, z) + 0.004), y0, z))
        pb = Vector((side * (skin_x(y1, z) + 0.004), y1, z))
        m.tube(pa, pb, 0.008, 0.008, 6, mat="metal", color=STEEL)


def build_struts(m, d):
    for side in (LEFT, RIGHT):
        le, te, _ = wing_frame(STRUT_X)
        y_top = le.y + 0.30 * (te.y - le.y)
        z_top = wing_surface_z(STRUT_X, y_top, False) + 0.01
        a = Vector((side * (skin_x(0.25, -0.30) - 0.02), 0.25, -0.30))
        b = Vector((side * STRUT_X, y_top, z_top))
        strut(m, a, b, 0.105, 0.34, d)


# --------------------------------------------------------------------------- doors


def build_door(side, d):
    name = "door_l" if side == LEFT else "door_r"
    y_h = DOOR_Y[0] + DOOR_GAP
    t0, t1 = TH_DOOR
    hinge = fus_point(y_h, side_th(0.25) if side == LEFT else _mirror_th(side_th(0.25)))
    m = Mesh(name, origin=hinge, detail=d)
    tw = TH_DOOR_WIN
    y_den, th_inc = fus_stations()
    ys = adaptive(DOOR_Y[0] + DOOR_GAP, DOOR_Y[1] - DOOR_GAP, DOOR_WIN_Y, y_den, d)
    tsl = [t for t in adaptive_grid(TH_GRID, th_inc, (t0 + 0.003, t1 - 0.003) + tw + TH_STRIPE,
                                    d) if t0 + 0.003 - 1e-12 <= t <= t1 - 0.003 + 1e-12]
    ts = tsl if side == LEFT else [_mirror_th(t) for t in tsl]
    outer = [[fus_point(y, t) for t in ts] for y in ys]
    inner = [[fus_point(y, t, -0.045) for t in ts] for y in ys]
    vo = [[m.vert(p) for p in row] for row in outer]
    vi = [[m.vert(p) for p in row] for row in inner]
    faces_out, faces_in = [], []
    glass_cells = set()
    for i in range(len(ys) - 1):
        for j in range(len(ts) - 1):
            yc, tl = (ys[i] + ys[i + 1]) / 2, (tsl[j] + tsl[j + 1]) / 2
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
    ns = count(36, d, 16)  # at least 16: the spinner must cover the nose bowl's opening
    prof = [(Y_NOSE, 0.0)]
    k = count(16, d, 4)
    for i in range(1, k + 1):
        t = i / k
        prof.append((lerp(Y_NOSE, -2.22, t), 0.172 * math.sin(0.5 * math.pi * t) ** 0.75))
    rings = []
    for y, r in prof:
        rings.append([Vector((r * math.cos(2 * math.pi * j / ns), y,
                              PROP_Z + r * math.sin(2 * math.pi * j / ns))) for j in range(ns)])
    sp = m.rings(rings, mat="paint", color=PAINT, smooth=True, solid=False)
    m.orient(sp, lambda c: Vector((c.x, c.y - (Y_NOSE + 0.35), c.z - PROP_Z)))
    # two blades along +-Z at rest; pitch angle from a 1.52 m helix, chord tapering to a round
    # tip that closes to a line; chord stations from the section's curvature
    _xs, counts = chord_stations(0.12, 0.03, 0.4, 0.15, d, err=0.0002, min_step=0.004,
                                 max_step=0.03, want_counts=True)
    xs = chord_stations(0.12, 0.03, 0.4, 0.15, d, err=0.0002, min_step=0.004, max_step=0.03,
                        counts=counts)
    nt = count(6, d, 2)
    rs = stations(0.15, PROP_R - 0.08, count(30, d, 3), cosine=False)
    rows = [(r, 1.0, None) for r in rs] + [
        (PROP_R - 0.08 + 0.08 * s_, sc, mid) for s_, sc, mid in tip_rings(nt)]
    for sg in (1.0, -1.0):
        rings = []
        for r, sc, mid in rows:
            chord = (0.155 - 0.045 * (r - 0.25) / (PROP_R - 0.25) if r > 0.25
                     else lerp(0.07, 0.155, smoothstep((r - 0.15) / 0.10)))
            beta = math.atan2(1.52, 2 * math.pi * max(r, 0.2))
            # chord direction in the x-y plane, rotated by the pitch angle
            # clockwise seen from the cockpit (as the 172's Lycoming): the upper blade moves
            # towards -x (vehicle right), so its leading edge is on -x
            cdir = Vector((sg * math.cos(beta), math.sin(beta), 0.0))
            up = cdir.cross(Vector((0.0, 0.0, 1.0))).normalized()
            if up.y > 0.0:  # the cambered (suction) face looks forward
                up = -up
            centre = Vector((0.0, PROP_Y, PROP_Z + sg * r))
            le = centre - cdir * (0.35 * chord)
            te = centre + cdir * (0.65 * chord)
            tc = lerp(0.30, 0.08, smoothstep((r - 0.15) / 0.5))
            rings.append(foil_pts(le, te, up, tc, xs, 0.03, 0.4, sc, mid))
        stns = [(i, 1.0, None) for i in range(len(rings))]
        foil_loft(m, stns, lambda i, sc, mid, rings=rings: rings[i], 0,
                  lambda a_, b_: False, (0.0, 0.0, sg),
                  lambda c, sg=sg: blade_out(c, sg), PROP_GREY, mat="metal")
    return m


def blade_out(c, sg):
    """Outward from a blade's pitch axis (the radial line through the hub)."""
    return Vector((c.x, c.y - PROP_Y, 0.0))


def build_prop_disc(d):
    m = Mesh("prop_disc", origin=(0.0, PROP_Y, PROP_Z), shade=False, detail=d)
    ns = count(48, d, 12)
    rings = []
    for r in [lerp(0.175, PROP_R, (i / 5) ** 0.8) for i in range(6)]:
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
FAIRING_ERR = 0.0006  # chordal target of the fairings (they are seen from 8 m at the closest)


def _wheel(m, centre, ro, width, d):
    """The part of the tyre that shows below the fairing (the rest would be hidden)."""
    cx, cy, cz = centre
    rr = ro * 0.55
    h = ro - rr
    ns = count(20, d, 4)
    nt = count(24, d, 6)
    prof = []  # the outer part of the section only: the inside faces the axle in the fairing
    for k in range(nt + 1):
        a = -0.2 * math.pi + 1.4 * math.pi * k / nt
        sx, sz = superellipse_point(a, 3.0)
        prof.append((0.5 * width * sx, rr + h * (0.5 + 0.5 * sz)))
    half = math.acos(min(1.0, (FAIRING_CUT - 0.004) / ro))  # 4 mm up into the fairing
    phis = stations(-0.5 * math.pi - half, -0.5 * math.pi + half, ns, cosine=False)
    rings = [[Vector((cx + u, cy + r * math.cos(phi), cz + r * math.sin(phi))) for phi in phis]
             for u, r in prof]
    tyre = m.rings(rings, close_ring=False, mat="dark", color=RUBBER, smooth=True,
                   solid=False, rounded=True)
    for u, r_in in (prof[0], prof[-1]):  # hub faces closing each sidewall, below the floor
        hh = math.acos(min(1.0, (FAIRING_CUT - 0.004) / r_in))
        hp = stations(-0.5 * math.pi - hh, -0.5 * math.pi + hh, count(10, d, 3), cosine=False)
        hub = m.rings([[Vector((cx + u, cy + rad * math.cos(phi), cz + rad * math.sin(phi)))
                        for phi in hp] for rad in (r_in, FAIRING_CUT + 0.004)], close_ring=False,
                      mat="metal", color=STEEL, solid=False)
        m.orient(hub, lambda c, sx=math.copysign(1.0, u): (sx, 0.0, 0.0))
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
    ns = count(22, d, 8)
    k = count(16, d, 5)
    zc = cz + 0.10
    zcut = cz - FAIRING_CUT
    nf = count(4, d, 2)
    # arc stations from the curvature of the widest section (a tall, narrow ellipse)
    a_w, a_h = 0.5 * width, zc + 0.5 * height - zcut
    grid = [math.pi * i / 2000 for i in range(2001)]
    pts = [Vector((-a_w * math.cos(a), 0.0, a_h * math.sin(a))) for a in grid]
    dens = _turn_density(pts, FAIRING_ERR)
    inc = [0.0] + [max(0.5 * (dens[i] + dens[i - 1]), (pts[i] - pts[i - 1]).length / 0.08)
                   for i in range(1, len(pts))]
    arc = adaptive_grid(grid, inc, (), d)
    ns = len(arc) - 1
    rings = []
    # lengthwise stations in u (t = u^2 resolves the round nose) from the curvature of the
    # crown line and the side line
    ug = [math.sqrt(0.97) * i / 2000 for i in range(2001)]
    crown = [Vector((0.0, length * u * u, a_h * fairing_r(u * u))) for u in ug]
    sidel = [Vector((a_w * fairing_r(u * u), length * u * u, 0.0)) for u in ug]
    dc, ds = _turn_density(crown, FAIRING_ERR), _turn_density(sidel, FAIRING_ERR)
    inc = [0.0] + [max(0.5 * (dc[i] + dc[i - 1]), 0.5 * (ds[i] + ds[i - 1]),
                       (crown[i] - crown[i - 1]).length / 0.08) for i in range(1, len(ug))]
    for u in adaptive_grid(ug, inc, (), d)[1:]:
        t = u * u
        y = cy - 0.36 * length + length * t
        r = fairing_r(t)
        top = zcut + (zc + 0.5 * height - zcut) * r
        ring = []
        for a in arc:  # arc over the top, from the right floor corner to the left
            ring.append(Vector((cx - 0.5 * width * r * math.cos(a), y,
                                zcut + (top - zcut) * math.sin(a))))
        for j in range(1, nf):  # the floor back to the right
            ring.append(Vector((cx + 0.5 * width * r * (1 - 2 * j / nf), y, zcut)))
        rings.append(ring)
    nose = Vector((cx, cy - 0.36 * length, zcut))
    rings.insert(0, [nose] * len(rings[0]))
    f = m.rings(rings, mat="paint", color=PAINT, smooth=True, solid=False)
    f += ring_cap(m, m.last_rings[-1], 0, ns, "paint", PAINT, lambda c: (0.0, 1.0, 0.0))

    def out(c):
        t = max(0.0, min(1.0, (c.y - (cy - 0.36 * length)) / length))
        top = zcut + (zc + 0.5 * height - zcut) * fairing_r(t)
        return Vector((c.x - cx, 0.0, c.z - 0.5 * (zcut + top)))
    m.orient(f, out)


def build_gear(d):
    m = Mesh("gear", detail=d)
    for side in (LEFT, RIGHT):
        # spring-steel leg from the lower fuselage side out to the axle
        top = Vector((side * (skin_x(MAIN_Y, -0.50) - 0.012), MAIN_Y, -0.50))
        end = Vector((side * (MAIN_X - 0.09), MAIN_Y, GROUND + MAIN_R + 0.02))  # in the fairing
        k = count(6, d, 3)
        pts = [top.lerp(end, i / k) for i in range(k + 1)]
        m.sweep(pts, 0.032, 16, mat="paint", color=PAINT)
        _wheel(m, (side * MAIN_X, MAIN_Y, GROUND + MAIN_R), MAIN_R, 0.15, d)
        _fairing(m, (side * MAIN_X, MAIN_Y, GROUND + MAIN_R), 0.80, 0.26, 0.42, d)
    # nose leg (oleo) from under the firewall to the fork
    top = Vector((0.0, -1.62, fus_point(-1.62, -0.5 * math.pi).z + 0.03))
    hub = Vector((0.0, NOSE_Y, GROUND + NOSE_R))
    t_end = (top.z - (hub.z + 0.25)) / (top.z - hub.z)  # 3 cm into the fairing's crown
    axis = (hub - top).normalized()
    u_ = axis.cross(Vector((1.0, 0.0, 0.0))).normalized()
    v_ = axis.cross(u_)
    seg = count(16, d, 8)
    oleo = []
    for t, r in ((0.0, 0.045), (0.55, 0.045), (0.55, 0.032), (t_end, 0.032)):
        c = top.lerp(hub, t)
        oleo.append([c + r * (math.cos(2 * math.pi * k / seg) * u_ + math.sin(2 * math.pi * k / seg)
                              * v_) for k in range(seg)])
    m.rings(oleo, mat="metal", color=STEEL, smooth=True, caps=(False, False), rounded=True)
    _wheel(m, (0.0, NOSE_Y, GROUND + NOSE_R), NOSE_R, 0.13, d)
    _fairing(m, (0.0, NOSE_Y, GROUND + NOSE_R), 0.62, 0.22, 0.36, d)
    return m


# --------------------------------------------------------------------------- cockpit
# Four-seat cabin: lining 2 cm inside the skin (holes at the windows and doors; the headliner
# closes the roof under the wing), carpeted floor, instrument panel with glareshield, six-pack
# and engine dials, radio stack, two yokes, rudder pedals, throttle and mixture, trim wheel,
# two front seats and a rear bench. Eyepoints at a seated pilot's eye height.
COCKPIT_ERR = 0.0002    # the cockpit is seen from 0.6 m: chordal target of its curved parts
LINING = 0.02
FLOOR_Z = -0.37
RISER_Y = 1.62      # the floor steps up to the baggage floor here
BAGGAGE_Z = -0.32
PANEL_Y = -1.22
PANEL_TOP = 0.40
EYE = (0.25, -0.22, 0.62)  # pilot's eye (left seat); the right seat mirrors it
SEAT_X = 0.25
LINING_COL = srgb(0.52, 0.50, 0.47)
HEADLINER = srgb(0.78, 0.76, 0.72)
CARPET = srgb(0.20, 0.20, 0.21)
SEAT_COL = srgb(0.33, 0.31, 0.29)
PANEL_COL = srgb(0.14, 0.14, 0.15)
DIAL = srgb(0.03, 0.03, 0.035)
NEEDLE = srgb(0.92, 0.92, 0.88)
_COCKPIT_ST = []


def badge(m, centre, rx, rz, seg, mat, color, depth=0.0005, lift=0.0015,
          normal=(0.0, -1.0, 0.0)):
    """A flat elliptical part standing `lift` proud of a flat surface whose outward normal is
    `normal`, with a skirt running `depth` back through it (no gap shows behind it)."""
    n = Vector(normal).normalized()
    up = Vector((0.0, 0.0, 1.0)) if abs(n.z) < 0.9 else Vector((0.0, 1.0, 0.0))
    u = (up - n * up.dot(n)).normalized()
    v = n.cross(u)
    c = Vector(centre)
    front, back = [], []
    for k in range(seg):
        a = 2 * math.pi * k / seg
        q = c + v * (rx * math.cos(a)) + u * (rz * math.sin(a))
        front.append(m.vert(q + n * lift))
        back.append(m.vert(q - n * depth))
    ring_cap(m, front, 0, seg // 2, mat, color, lambda _c: n)
    skirt = [m.face([front[k], front[(k + 1) % seg], back[(k + 1) % seg], back[k]], mat, color,
                    smooth=True) for k in range(seg)]
    m.round.update(f for f in skirt if f is not None)
    m.orient(skirt, lambda q: (q - c) - n * (q - c).dot(n))


def build_lining(m, d):
    if not _COCKPIT_ST:
        _COCKPIT_ST.append(_fus_stations(COCKPIT_ERR))
    y_den, _ = _COCKPIT_ST[0]
    knots = [k for k in FUS_Y_KNOTS if PANEL_Y < k < BULKHEAD_Y]
    ys = adaptive(PANEL_Y, BULKHEAD_Y, knots, y_den, d)
    # the floor line is a knot too, so the lining stops exactly at the floor
    fl = side_th(FLOOR_Z)
    th_knots = tuple(sorted(set(fus_th_knots()) | {round(fl, 9), round(_mirror_th(fl), 9)}))
    rings = []
    for y in ys:
        ths = ring_stations(y, d, COCKPIT_ERR, th_knots)
        rings.append((ths, [m.vert(fus_point(y, th, -LINING)) for th in ths]))

    def segments(ths):
        cut = [j for j, t in enumerate(ths) if any(abs(t - k) < 1e-9 for k in th_knots)
               or j == 0]
        return [list(range(a_, b_ + 1)) for a_, b_ in zip(cut, cut[1:] + [len(ths)])]
    faces = []
    for i in range(len(ys) - 1):
        (ta, va), (tb, vb) = rings[i], rings[i + 1]
        yc = (ys[i] + ys[i + 1]) / 2
        for ra, rb in zip(segments(ta), segments(tb)):
            n_a, n_b = len(ta), len(tb)
            th_a = [ta[j % n_a] + (2 * math.pi if j >= n_a else 0.0) for j in ra]
            th_b = [tb[j % n_b] + (2 * math.pi if j >= n_b else 0.0) for j in rb]
            A = [va[j % n_a] for j in ra]
            B = [vb[j % n_b] for j in rb]
            tc = 0.25 * (th_a[0] + th_a[-1] + th_b[0] + th_b[-1])
            if tc > math.pi:
                tc -= 2 * math.pi
            if door_region(yc, tc) or fus_material(yc, tc) == "glass":
                continue
            if _in(yc, ROOF_CUT_Y) and ROOF_CUT_TH < tc < math.pi - ROOF_CUT_TH:
                continue  # the headliner tray closes the roof under the wing
            if side_of(tc)[1] < fl:
                continue  # under the floor
            col = HEADLINER if WS_TH < tc < math.pi - WS_TH else LINING_COL
            if len(A) == len(B):
                new = [m.face([A[k], B[k], B[k + 1], A[k + 1]], "dark", col, smooth=True)
                       for k in range(len(A) - 1)]
            else:
                new = zip_chains(m, A, B, "dark", col, lambda c: Vector((0.0, 0.0, 0.0)))
            for f in new:
                if f is not None:
                    f.smooth = True
            faces += new
    faces = [f for f in faces if f is not None and f.is_valid]
    m.orient(faces, lambda c: -fus_param_normal(c))


HEADLINER_Z = 0.915  # the headliner under the wing centre section (below the wing's skin)


def build_headliner(m, d):
    """The cabin ceiling where the roof is cut out under the wing: a flat panel below the
    wing's lower surface with short walls up into the wing, so no gap shows round it."""
    y0, y1 = ROOF_CUT_Y[0] - 0.02, ROOF_CUT_Y[1] + 0.02
    x1 = max(fus_point(y, ROOF_CUT_TH).x for y in (y0, 0.0, y1)) + 0.03
    nx, ny = count(6, d, 2), count(10, d, 2)
    xs = [lerp(-x1, x1, i / nx) for i in range(nx + 1)]
    ys = [lerp(y0, y1, j / ny) for j in range(ny + 1)]
    grid = [[m.vert((x, y, HEADLINER_Z)) for x in xs] for y in ys]
    faces = [m.face([grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]], "dark",
                    HEADLINER) for j in range(ny) for i in range(nx)]
    m.orient(faces, lambda c: (0.0, 0.0, -1.0))
    # the walls stand above the plate, so only their outer sides can be seen (from outside,
    # through the windows): they face outwards
    for y, out in ((y0, -1.0), (y1, 1.0)):  # front and rear walls, up to the lining
        rows = [[Vector((x, y, HEADLINER_Z)) for x in xs],
                [Vector((x, y, skin_top(y, x) - LINING + 0.003)) for x in xs]]
        f = m.rings(rows, close_ring=False, mat="dark", color=HEADLINER, solid=False)
        m.orient(f, lambda c, o=out: (0.0, o, 0.0))
    for x, out in ((-x1, -1.0), (x1, 1.0)):  # side walls, up to the lining
        rows = [[Vector((x, y, HEADLINER_Z)) for y in ys],
                [Vector((x, y, skin_top(y, x) - LINING + 0.003)) for y in ys]]
        f = m.rings(rows, close_ring=False, mat="dark", color=HEADLINER, solid=False)
        m.orient(f, lambda c, o=out: (o, 0.0, 0.0))


def cabin_half(y, z):
    return skin_x(y, z) - LINING


def build_floor_panel(m, d):
    # carpeted floor (edges tucked under the lining), a riser and the raised baggage floor
    # (the belly rises behind the rear seat)
    for y0, y1, z in ((PANEL_Y, RISER_Y, FLOOR_Z), (RISER_Y, BULKHEAD_Y, BAGGAGE_Z)):
        ys = stations(y0, y1, count(10, d, 2), cosine=False)
        rows = [[Vector((-cabin_half(y, z) - 0.005, y, z)),
                 Vector((cabin_half(y, z) + 0.005, y, z))] for y in ys]
        f = m.rings(rows, close_ring=False, mat="dark", color=CARPET, solid=False)
        m.orient(f, lambda c: (0.0, 0.0, 1.0))
    zs = stations(FLOOR_Z, BAGGAGE_Z, count(3, d, 1), cosine=False)
    rows = [[Vector((-cabin_half(RISER_Y, z) - 0.005, RISER_Y, z)),
             Vector((cabin_half(RISER_Y, z) + 0.005, RISER_Y, z))] for z in zs]
    f = m.rings(rows, close_ring=False, mat="dark", color=CARPET, solid=False)
    m.orient(f, lambda c: (0.0, -1.0, 0.0))
    # instrument panel: a flat face across the cabin, its edges tucked into the lining
    zs = stations(FLOOR_Z - 0.01, PANEL_TOP, count(14, d, 3), cosine=False)
    rows = [[Vector((-cabin_half(PANEL_Y, z) - 0.005, PANEL_Y, z)),
             Vector((cabin_half(PANEL_Y, z) + 0.005, PANEL_Y, z))] for z in zs]
    f = m.rings(rows, close_ring=False, mat="dark", color=PANEL_COL, solid=False)
    m.orient(f, lambda c: (0.0, 1.0, 0.0))
    # glareshield: from a lip over the panel, a rounded hood running forward up to the
    # windscreen base (closing the gap above the panel)
    k = count(8, d, 3)
    y_front = WS_Y[0] - 0.02
    z_front = skin_top(y_front, 0.0) - LINING
    rows = [[Vector((-cabin_half(PANEL_Y + 0.10, PANEL_TOP - 0.03) - 0.01, PANEL_Y + 0.10,
                     PANEL_TOP - 0.03)),
             Vector((cabin_half(PANEL_Y + 0.10, PANEL_TOP - 0.03) + 0.01, PANEL_Y + 0.10,
                     PANEL_TOP - 0.03))]]
    for j in range(k + 1):
        t = j / k
        y = lerp(PANEL_Y + 0.10, y_front, smoothstep(t))
        z = PANEL_TOP + 0.03 + (z_front - PANEL_TOP - 0.03) * t * t
        hw = cabin_half(y, z) + 0.005
        rows.append([Vector((-hw, y, z)), Vector((hw, y, z))])
    f = m.rings(rows, close_ring=False, mat="dark", color=PANEL_COL, solid=False)
    m.orient(f, lambda c: (0.0, 0.6, 0.8))


def dial(m, x, z, r, d, needle_deg=40.0):
    """A round instrument: bezel ring, black face and a light needle, standing proud of the
    panel (each with a skirt into the panel)."""
    seg = count(32, d, 12)
    aft = (0.0, 1.0, 0.0)  # the panel faces aft, towards the pilots
    badge(m, (x, PANEL_Y, z), r + 0.006, r + 0.006, seg, "metal", STEEL, lift=0.003,
          normal=aft)
    badge(m, (x, PANEL_Y + 0.003, z), r, r, seg, "dark", DIAL, lift=0.0025, depth=0.0005,
          normal=aft)
    a = math.radians(needle_deg)
    tip = Vector((x + 0.8 * r * math.sin(a), PANEL_Y + 0.0065, z + 0.8 * r * math.cos(a)))
    base = Vector((x, PANEL_Y + 0.0065, z))
    side = Vector((math.cos(a), 0.0, -math.sin(a))) * 0.003
    f = m.poly([base - side, base + side, tip], "light", NEEDLE)
    m.orient([f], lambda c: (0.0, 1.0, 0.0))


def yoke(m, x, d):
    hub = Vector((x, PANEL_Y + 0.28, 0.13))
    m.tube((x, PANEL_Y - 0.01, 0.13), hub, 0.012, 0.012, count(16, d, 6), mat="metal",
           color=STEEL)
    path = [hub + Vector(p) for p in ((-0.16, 0.0, 0.07), (-0.155, 0.0, 0.0), (-0.13, 0.0, -0.04),
                                      (-0.06, 0.0, -0.055), (0.06, 0.0, -0.055),
                                      (0.13, 0.0, -0.04), (0.155, 0.0, 0.0), (0.16, 0.0, 0.07))]
    pts = []
    for a, b in zip(path, path[1:]):
        for t in (0.0, 0.25, 0.5, 0.75):
            pts.append(a.lerp(b, t))
    pts.append(path[-1])
    m.sweep(pts, 0.016, count(20, d, 6), mat="dark", color=BLACK)
    m.box(hub + Vector((0.0, 0.0, -0.03)), (0.10, 0.04, 0.06), mat="dark", color=BLACK)


def seat(m, x, y, width, d):
    rot = Matrix.Rotation(math.radians(-12.0), 3, "X")  # backrest leans back
    m.box((x, y + 0.24, FLOOR_Z + 0.20), (width, 0.48, 0.10), mat="dark", color=SEAT_COL)
    m.box((x, y + 0.50, FLOOR_Z + 0.55), (width, 0.09, 0.62), matrix=rot, mat="dark",
          color=SEAT_COL)
    for sx in (-1, 1):  # seat legs
        m.box((x + sx * (width / 2 - 0.04), y + 0.24, FLOOR_Z + 0.077), (0.03, 0.40, 0.15),
              mat="metal", color=STEEL)


def build_cockpit(d):
    m = Mesh("cockpit", detail=d)
    build_lining(m, d)
    build_headliner(m, d)
    build_floor_panel(m, d)
    # pilot's six-pack and engine dials, right-seat dials, radio stack
    for col, x in enumerate((0.38, 0.27, 0.16)):
        for row, z in enumerate((0.30, 0.18)):
            dial(m, x, z, 0.038, d, 25.0 + 40.0 * (col + 2 * row))
    for i, x in enumerate((0.43, 0.35, 0.27, 0.19)):
        dial(m, x, 0.06, 0.018, d, 70.0 * i)
    for x in (-0.20, -0.30, -0.40):
        dial(m, x, 0.26, 0.030, d, 120.0)
    for i, z in enumerate((0.31, 0.24, 0.17, 0.10, 0.03)):
        m.box((0.02, PANEL_Y + 0.004, z), (0.16, 0.012, 0.055), mat="dark", color=BLACK)
        m.box((0.02, PANEL_Y + 0.0105, z + 0.01), (0.07, 0.002, 0.016), mat="light",
              color=srgb(0.15, 0.55, 0.25))
    # yokes, throttle and mixture, trim wheel, rudder pedals
    for x in (SEAT_X, -SEAT_X):
        yoke(m, x, d)
    for x, col in ((0.06, BLACK), (0.11, RED)):
        m.tube((x, PANEL_Y - 0.005, -0.02), (x, PANEL_Y + 0.07, -0.02), 0.005, 0.005, 8,
               mat="metal", color=STEEL)
        m.tube((x, PANEL_Y + 0.07, -0.02), (x, PANEL_Y + 0.10, -0.02), 0.015, 0.015,
               count(18, d, 8), mat="dark", color=col)
    m.tube((-0.02, -0.55, FLOOR_Z + 0.072), (0.02, -0.55, FLOOR_Z + 0.072), 0.07, 0.07,
           count(40, d, 12), mat="dark", color=BLACK)
    rot = Matrix.Rotation(math.radians(30.0), 3, "X")
    for x in (SEAT_X, -SEAT_X):
        for dx in (-0.09, 0.09):
            m.box((x + dx, PANEL_Y + 0.12, FLOOR_Z + 0.13), (0.08, 0.012, 0.14), matrix=rot,
                  mat="metal", color=STEEL)
    # seats: two in front, a bench behind
    for x in (SEAT_X, -SEAT_X):
        seat(m, x, -0.50, 0.44, d)
    seat(m, 0.0, 0.45, 0.92, d)
    return m


# --------------------------------------------------------------------------- build


def build(d=1.0, cockpit=True, extras=True):
    """Return (root object, expected node names)."""
    mats = make_materials()
    EXTRAS[0] = extras
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
    # coarse levels of detail: neighbouring faces of a smooth surface meet at larger angles,
    # so the crease threshold opens up with the step size
    crease = 40.0 if d >= 0.5 else 75.0
    fus = fus_m.finish(mats, ao=ao, smooth_angle=crease)
    names = ["fuselage"]
    for p in parts:
        p.finish(mats, parent=fus, ao=ao, smooth_angle=crease)
        names.append(p.name)
    disc.finish(mats, parent=fus)
    names.append("prop_disc")
    if cockpit:
        cock = build_cockpit(d)
        cock.finish(mats, parent=fus)
        names.append("cockpit")
        empty("eyepoint_left", EYE, parent=fus)
        empty("eyepoint_right", (-EYE[0], EYE[1], EYE[2]), parent=fus)
        names += ["eyepoint_left", "eyepoint_right"]
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
    "eyepoint_left": dict(loc=EYE, target=(0.18, -2.5, 0.25), lens=20),
    "eyepoint_right": dict(loc=(-EYE[0], EYE[1], EYE[2]), target=(-0.10, -2.5, 0.25), lens=20),
    "windscreen": dict(loc=(0.9, -3.4, 1.15), target=(0.05, -0.9, 0.25), lens=35),
    "cabin_rear": dict(loc=(0.0, 0.98, 0.80), target=(0.05, -1.2, 0.15), lens=22),
}
