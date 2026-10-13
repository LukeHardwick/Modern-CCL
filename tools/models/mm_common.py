# Shared geometry helpers for tools/models/make_models.py (Blender Python).
#
# Frame used while modelling: Blender axes, nose towards -Y, +Z up, metres. The glTF exporter
# (+Y up) maps this to glTF's front +Z / up +Y. Vehicle LEFT is +X, vehicle RIGHT is -X (in
# both Blender and glTF; facing the nose with +Y... see docs/research/03 A6).
#
# Every glTF node is one `Mesh` (a bmesh with per-face material index and vertex colour); an
# object's origin is the node pivot, so a moving part is built around its hinge.
#
# Detail: a Mesh multiplies the segment counts it is given (tubes, sweeps, bands, revolves) by
# its `detail`; builders scale their own ring and profile counts the same way. A higher detail
# only refines the tessellation of the same analytic shapes, so `--detail 4` is a dense
# reference for the `--detail 1` model.
#
# Colour: a colour is an RGBA tuple or a field `f(p, c)` evaluated per face corner, where `p` is
# the corner's vertex position and `c` the face centre (parent frame). Continuous terms come from
# `p`, so every corner of a vertex gets the same colour and the glTF exporter shares the vertex;
# a region (panel, radome) may come from `c`, which splits vertices only along its boundary.

import math

import bmesh
import bpy
from mathutils import Matrix, Vector

LEFT = 1.0  # vehicle left is +X
RIGHT = -1.0  # vehicle right is -X

MATERIALS = [
    # name, roughness, metallic, alpha
    ("paint", 0.55, 0.0, 1.0),
    ("glass", 0.08, 0.0, 0.50),
    ("dark", 0.85, 0.0, 1.0),
    ("metal", 0.35, 0.8, 1.0),
    ("light", 0.4, 0.0, 1.0),
    ("hud", 0.05, 0.0, 0.22),
]
MAT = {name: i for i, (name, *_rest) in enumerate(MATERIALS)}


def srgb(r, g, b, a=1.0):
    """sRGB 0..1 -> linear RGBA (glTF COLOR_0 is linear)."""
    def lin(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return (lin(r), lin(g), lin(b), a)


def grey(v):
    return srgb(v, v, v)


def lerp(a, b, t):
    return a + (b - a) * t


def hash01(*vals):
    """Deterministic pseudo-random 0..1 from quantised numbers (FNV-1a)."""
    h = 2166136261
    for v in vals:
        h = ((h ^ (int(round(v * 100)) & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
    return h / 4294967295.0


def tone(c, amount):
    return (c[0] * amount, c[1] * amount, c[2] * amount, c[3])


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(4))


def smoothstep(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


def count(base, d, lo=3):
    """A tessellation count scaled by the detail factor."""
    return max(lo, int(round(base * d)))


def pchip(xs, ys):
    """Monotone cubic Hermite interpolant (Fritsch-Carlson) through (xs, ys)."""
    n = len(xs)
    h = [xs[i + 1] - xs[i] for i in range(n - 1)]
    d = [(ys[i + 1] - ys[i]) / h[i] for i in range(n - 1)]
    m = [0.0] * n
    m[0], m[-1] = d[0], d[-1]
    for i in range(1, n - 1):
        m[i] = 0.0 if d[i - 1] * d[i] <= 0 else 2.0 / (1.0 / d[i - 1] + 1.0 / d[i])

    def f(x):
        if x <= xs[0]:
            return ys[0]
        if x >= xs[-1]:
            return ys[-1]
        lo, hi = 0, n - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if xs[mid] <= x:
                lo = mid
            else:
                hi = mid
        t = (x - xs[lo]) / h[lo]
        t2, t3 = t * t, t * t * t
        return ((2 * t3 - 3 * t2 + 1) * ys[lo] + (t3 - 2 * t2 + t) * h[lo] * m[lo]
                + (-2 * t3 + 3 * t2) * ys[lo + 1] + (t3 - t2) * h[lo] * m[lo + 1])
    return f


def superellipse_point(theta, n):
    c, s = math.cos(theta), math.sin(theta)
    e = 2.0 / n
    return (math.copysign(abs(c) ** e, c), math.copysign(abs(s) ** e, s))


def superellipse_normal(sx, sz, a, b, n):
    """Outward 2-D normal of (x/a)^n + (z/b)^n = 1 at the unit-shape point (sx, sz)."""
    gx = math.copysign(abs(sx) ** (n - 1), sx) / a
    gz = math.copysign(abs(sz) ** (n - 1), sz) / b
    k = math.hypot(gx, gz) or 1.0
    return gx / k, gz / k


# ---------------------------------------------------------------- stations (tessellation)


def _warp(t, cosine):
    return 0.5 - 0.5 * math.cos(math.pi * t) if cosine else t


def _unwarp(s, cosine):
    return math.acos(max(-1.0, min(1.0, 1.0 - 2.0 * s))) / math.pi if cosine else s


def _knot_params(a, b, knots, cosine):
    lo, hi = min(a, b), max(a, b)
    inner = sorted(k for k in set(knots) if lo < k < hi)
    if a > b:
        inner.reverse()
    return [0.0] + [_unwarp((k - a) / (b - a), cosine) for k in inner] + [1.0], inner


def station_counts(a, b, n, knots=(), cosine=True):
    """Segments per interval between consecutive knots for `stations` (at least one each)."""
    ts, _inner = _knot_params(a, b, knots, cosine)
    return [max(1, int(round((t1 - t0) * n))) for t0, t1 in zip(ts, ts[1:])]


def stations(a, b, n, knots=(), cosine=True, counts=None):
    """About n + 1 values from a to b, cosine-spaced (denser at both ends) or uniform, with every
    knot inside (a, b) present exactly, so that feature lines (panel joints, hinge lines) fall on
    vertex rings. `counts` (from station_counts) fixes the segments per interval so that several
    lofts line up. Knots only move tessellation points; the shape stays the same."""
    ts, inner = _knot_params(a, b, knots, cosine)
    counts = counts or station_counts(a, b, n, knots, cosine)
    assert len(counts) == len(ts) - 1, "knots outside the range differ between lofts"
    out = []
    for i, k in enumerate(counts):
        for j in range(k):
            if j == 0 and i > 0:
                out.append(inner[i - 1])
            else:
                out.append(a + (b - a) * _warp(ts[i] + (ts[i + 1] - ts[i]) * j / k, cosine))
    out.append(b)
    return out


# ---------------------------------------------------------------- scene and materials


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def make_materials():
    mats = []
    for name, rough, metal, alpha in MATERIALS:
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        nt = mat.node_tree
        bsdf = nt.nodes["Principled BSDF"]
        attr = nt.nodes.new("ShaderNodeVertexColor")
        attr.layer_name = "Color"
        nt.links.new(attr.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = rough
        bsdf.inputs["Metallic"].default_value = metal
        bsdf.inputs["Alpha"].default_value = alpha
        if alpha < 1.0:
            if hasattr(mat, "surface_render_method"):
                mat.surface_render_method = "BLENDED"
            if hasattr(mat, "blend_method"):
                mat.blend_method = "BLEND"
            mat.use_backface_culling = False
        mats.append(mat)
    return mats


# ---------------------------------------------------------------- Mesh builder


class Mesh:
    """A bmesh being built for one glTF node. `origin` is the node pivot in the parent frame:
    all points passed in are in the PARENT frame and are stored relative to the origin."""

    def __init__(self, name, origin=(0.0, 0.0, 0.0), shade=True, detail=1.0):
        self.name = name
        self.bm = bmesh.new()
        self.origin = Vector(origin)
        self.layer = self.bm.loops.layers.float_color.new("Color")
        self.mat = MAT["paint"]
        self.color = grey(0.7)
        self.shade = shade
        self.detail = detail
        self.last_rings = None

    def n(self, base, lo=3):
        """A segment count scaled by this mesh's detail."""
        return count(base, self.detail, lo)

    # -- low level
    def vert(self, p):
        return self.bm.verts.new(Vector(p) - self.origin)

    def _style(self, mat, color):
        return (self.mat if mat is None else (MAT[mat] if isinstance(mat, str) else mat),
                self.color if color is None else color)

    def face(self, verts, mat=None, color=None, smooth=False):
        uniq = []
        for v in verts:
            if not uniq or uniq[-1] is not v:
                uniq.append(v)
        if len(uniq) > 1 and uniq[0] is uniq[-1]:
            uniq.pop()
        if len(set(id(v) for v in uniq)) < 3 or len(uniq) != len(set(id(v) for v in uniq)):
            return None
        try:
            f = self.bm.faces.new(uniq)
        except ValueError:
            return None
        m, c = self._style(mat, color)
        f.material_index = m
        f.smooth = smooth
        if callable(c):
            centre = sum((v.co for v in uniq), Vector()) / len(uniq) + self.origin
            for loop in f.loops:
                loop[self.layer] = c(loop.vert.co + self.origin, centre)
        else:
            for loop in f.loops:
                loop[self.layer] = c
        return f

    def poly(self, points, mat=None, color=None, smooth=False):
        return self.face([self.vert(p) for p in points], mat, color, smooth)

    def orient(self, faces, want):
        """Flip faces whose normal points against `want(centre) -> Vector` (for open shells,
        where recalc_face_normals cannot know inside from outside)."""
        for f in faces:
            if f is None or not f.is_valid:
                continue
            f.normal_update()
            c = f.calc_center_median() + self.origin
            if f.normal.dot(Vector(want(c))) < 0:
                f.normal_flip()

    def orient_all(self, faces, sample, want):
        """Flip every face in `faces` together when most of the `sample` faces point against
        `want(centre)`: keeps a consistently wound open surface consistent."""
        score = 0.0
        for f in sample:
            if f is None or not f.is_valid:
                continue
            f.normal_update()
            score += f.normal.dot(Vector(want(f.calc_center_median() + self.origin)))
        if score < 0:
            for f in faces:
                if f is not None and f.is_valid:
                    f.normal_flip()

    def recalc(self, faces):
        faces = [f for f in faces if f is not None and f.is_valid]
        if faces:
            bmesh.ops.recalc_face_normals(self.bm, faces=faces)

    # -- surfaces
    def rings(self, rings, wrap_rings=False, close_ring=True, caps=(True, True), mat=None,
              color=None, smooth=True, solid=True, flip=False, skip=None, ends=(None, None)):
        """Loft through equal-length point rings. Coincident points inside a ring merge into
        one vertex (poles). Returns the faces. `skip(centre)` leaves holes. `ends` may give
        existing vertex lists for the first and/or last ring, so lofts can be chained; the
        vertex rings used are kept in `last_rings`."""
        vr = []
        last = len(rings) - 1
        for r, ring in enumerate(rings):
            given = ends[0] if r == 0 else (ends[1] if r == last else None)
            if given is not None:
                vr.append(list(given))
                continue
            verts, cache = [], []
            for p in ring:
                p = Vector(p)
                for q, v in cache:
                    if (q - p).length < 1e-5:
                        verts.append(v)
                        break
                else:
                    v = self.vert(p)
                    cache.append((p, v))
                    verts.append(v)
            vr.append(verts)
        self.last_rings = vr
        faces = []
        pairs = list(zip(vr, vr[1:]))
        if wrap_rings:
            pairs.append((vr[-1], vr[0]))
        for ra, rb in pairs:
            n = len(ra)
            for i in range(n if close_ring else n - 1):
                j = (i + 1) % n
                quad = (ra[i], rb[i], rb[j], ra[j])
                if flip:
                    quad = quad[::-1]
                if skip is not None:
                    c = sum((v.co for v in quad), Vector()) / 4.0 + self.origin
                    if skip(c):
                        continue
                faces.append(self.face(quad, mat, color, smooth))
        if not wrap_rings and solid:
            for ring, first in ((vr[0], True), (vr[-1], False)):
                if caps[0 if first else 1]:
                    uniq = []
                    for v in ring:
                        if v not in uniq:
                            uniq.append(v)
                    if len(uniq) >= 3:
                        faces.append(self.face(uniq if first else uniq[::-1], mat, color, False))
        if solid:
            self.recalc(faces)
        return [f for f in faces if f is not None]

    def tube(self, p0, p1, r0, r1=None, seg=8, mat=None, color=None, smooth=True, caps=True):
        """Cone/cylinder between two points."""
        p0, p1 = Vector(p0), Vector(p1)
        r1 = r0 if r1 is None else r1
        seg = self.n(seg)
        axis = (p1 - p0).normalized()
        ref = Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((1, 0, 0))
        u = axis.cross(ref).normalized()
        v = axis.cross(u)
        rings = []
        for p, r in ((p0, r0), (p1, r1)):
            rings.append([p + r * (math.cos(2 * math.pi * k / seg) * u
                                   + math.sin(2 * math.pi * k / seg) * v) for k in range(seg)])
        return self.rings(rings, caps=(caps, caps), mat=mat, color=color, smooth=smooth)

    def sweep(self, pts, radius, seg=6, mat=None, color=None, smooth=True, closed=False):
        """Circular tube along a polyline (parallel-transport frame)."""
        pts = [Vector(p) for p in pts]
        seg = self.n(seg)
        n = len(pts)
        rings = []
        t0 = (pts[1] - pts[0]).normalized()
        ref = Vector((0, 0, 1)) if abs(t0.z) < 0.9 else Vector((1, 0, 0))
        u = t0.cross(ref).normalized()
        for i in range(n):
            if closed:
                t = (pts[(i + 1) % n] - pts[i - 1]).normalized()
            else:
                t = (pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]).normalized()
            u = (u - t * u.dot(t)).normalized()
            v = t.cross(u)
            rings.append([pts[i] + radius * (math.cos(2 * math.pi * k / seg) * u
                                             + math.sin(2 * math.pi * k / seg) * v)
                          for k in range(seg)])
        return self.rings(rings, wrap_rings=closed, mat=mat, color=color, smooth=smooth)

    def band(self, pts, normals, width, thick, seg=8, mat=None, color=None):
        """A flat frame strip along the polyline `pts` lying on a surface with unit `normals`:
        a rounded-rectangle section `width` across, standing `thick` proud of the surface (its
        base is sunk by thick/2 so no gap shows)."""
        pts = [Vector(p) for p in pts]
        seg = self.n(seg, 4)
        n = len(pts)
        rings = []
        for i in range(n):
            t = (pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]).normalized()
            nr = Vector(normals[i])
            nr = (nr - t * nr.dot(t)).normalized()
            side = t.cross(nr)
            c = pts[i] + nr * (thick * 0.25)
            ring = []
            for k in range(seg):
                sx, sz = superellipse_point(2 * math.pi * (k + 0.5) / seg, 4.0)
                ring.append(c + side * (width * 0.5 * sx) + nr * (thick * 0.75 * sz))
            rings.append(ring)
        return self.rings(rings, mat=mat, color=color, smooth=True)

    def revolve_x(self, centre, profile, seg=24, mat=None, color=None, smooth=True,
                  closed_profile=False):
        """Surface of revolution about an axis parallel to X through `centre`; `profile` is
        (u along the axis, radius) points."""
        c = Vector(centre)
        seg = self.n(seg)
        rings = [[c + Vector((u, r * math.cos(2 * math.pi * k / seg),
                              r * math.sin(2 * math.pi * k / seg))) for k in range(seg)]
                 for u, r in profile]
        return self.rings(rings, wrap_rings=closed_profile, mat=mat, color=color,
                          smooth=smooth, solid=not closed_profile)

    def box(self, centre, size, matrix=None, mat=None, color=None, smooth=False):
        """Flat-shaded box; `matrix` (3x3 or 4x4) rotates about `centre`."""
        cx, cy, cz = centre
        hx, hy, hz = (s / 2.0 for s in size)
        corners = {}
        for sx in (-1, 1):
            for sy in (-1, 1):
                for sz in (-1, 1):
                    p = Vector((sx * hx, sy * hy, sz * hz))
                    if matrix is not None:
                        p = matrix @ p
                    corners[(sx, sy, sz)] = p + Vector((cx, cy, cz))
        faces = []
        for axis, sgn in ((0, 1), (0, -1), (1, 1), (1, -1), (2, 1), (2, -1)):
            ks = [k for k in corners if k[axis] == sgn]
            ks.sort(key=lambda k: math.atan2(k[(axis + 2) % 3], k[(axis + 1) % 3]))
            faces.append(self.poly([corners[k] for k in ks], mat, color, smooth))
        self.recalc(faces)
        return faces

    def plate(self, outline, offset, mat=None, color=None, smooth=False):
        """Extrude a planar polygon (3D points) along the vector `offset` (flat shaded)."""
        offset = Vector(offset)
        a = [Vector(p) for p in outline]
        b = [p + offset for p in a]
        faces = [self.poly(a, mat, color), self.poly(b[::-1], mat, color)]
        for i in range(len(a)):
            j = (i + 1) % len(a)
            faces.append(self.poly([a[i], a[j], b[j], b[i]], mat, color))
        self.recalc(faces)
        return faces

    # -- surface decals (panel lines, fasteners): thin raised geometry, smooth shaded so the
    # strips share their vertices
    def oriented(self, verts, hint, mat=None, color=None, smooth=True):
        f = self.face(verts, mat, color, smooth)
        if f is not None:
            f.normal_update()
            if f.normal.dot(hint) < 0:
                f.normal_flip()
        return f

    def ribbon(self, surf, uvs, width, grow=0.004, color=None, mat=None, skip=None):
        """A narrow strip following the polyline `uvs` of the surface function `surf(u, v) ->
        (point, normal)`. Stations where `skip(point)` is true are left out (hidden parts)."""
        pts = [surf(u, v) for u, v in uvs]
        n = len(pts)
        left, right = [], []
        for i, (p, nr) in enumerate(pts):
            a = pts[max(i - 1, 0)][0]
            b = pts[min(i + 1, n - 1)][0]
            t = (b - a)
            if t.length < 1e-9 or (skip is not None and skip(p)):
                left.append(None)
                right.append(None)
                continue
            side = t.normalized().cross(nr).normalized() * (width / 2.0)
            q = p + nr * grow
            left.append((self.vert(q - side), nr))
            right.append((self.vert(q + side), nr))
        for i in range(len(left) - 1):
            if left[i] is None or left[i + 1] is None:
                continue
            self.oriented([left[i][0], right[i][0], right[i + 1][0], left[i + 1][0]],
                          left[i][1], mat, color)

    def patch(self, surf, u0, u1, v0, v1, nu, nv, grow=0.004, color=None, mat=None):
        grid = [[surf(lerp(u0, u1, i / nu), lerp(v0, v1, j / nv)) for j in range(nv + 1)]
                for i in range(nu + 1)]
        vg = [[self.vert(p + n * grow) for p, n in row] for row in grid]
        for i in range(nu):
            for j in range(nv):
                self.oriented([vg[i][j], vg[i + 1][j], vg[i + 1][j + 1], vg[i][j + 1]],
                              grid[i][j][1], mat, color)

    def dot(self, surf, u, v, du, dv, grow=0.004, color=None):
        """A small square fastener head of size (du, dv) in surface parameters."""
        self.patch(surf, u - du / 2, u + du / 2, v - dv / 2, v + dv / 2, 1, 1, grow, color)

    # -- finish
    def finish(self, mats, parent=None, smooth_angle=40.0, ao=None):
        bm = self.bm
        bm.normal_update()
        bm.verts.index_update()
        limit = math.radians(smooth_angle)
        for e in bm.edges:
            if len(e.link_faces) == 2:
                f0, f1 = e.link_faces
                if (f0.smooth and f1.smooth and f0.normal.angle(f1.normal, 0.0) > limit):
                    e.smooth = False
        if self.shade:
            aocache = {}
            for f in bm.faces:
                for loop in f.loops:
                    n = loop.vert.normal if f.smooth else f.normal
                    k = 0.76 + 0.24 * (0.5 + 0.5 * n.z)  # cheap baked "sky occlusion"
                    if ao is not None and f.material_index != MAT["glass"]:
                        key = loop.vert.index
                        if key not in aocache:
                            aocache[key] = ao(loop.vert.co + self.origin, loop.vert.normal)
                        k *= aocache[key]
                    c = loop[self.layer]
                    loop[self.layer] = (c[0] * k, c[1] * k, c[2] * k, c[3])
        mesh = bpy.data.meshes.new(self.name)
        bm.to_mesh(mesh)
        bm.free()
        for mat in mats:
            mesh.materials.append(mat)
        for poly in mesh.polygons:
            poly.use_smooth = bool(poly.use_smooth)
        obj = bpy.data.objects.new(self.name, mesh)
        obj.location = self.origin
        bpy.context.scene.collection.objects.link(obj)
        if parent is not None:
            obj.parent = parent
        return obj


def make_ao(meshes, radius=0.45):
    """Deterministic baked ambient occlusion over all (non-glass) geometry: 8 fixed rays per
    vertex against a BVH of the whole model. Returns ao(pos, normal) -> multiplier 0.55..1."""
    from mathutils.bvhtree import BVHTree
    verts, polys = [], []
    for m in meshes:
        for f in m.bm.faces:
            if f.material_index == MAT["glass"]:
                continue
            idx = []
            for v in f.verts:
                verts.append(v.co + m.origin)
                idx.append(len(verts) - 1)
            for k in range(1, len(idx) - 1):
                polys.append((idx[0], idx[k], idx[k + 1]))
    tree = BVHTree.FromPolygons(verts, polys)
    dirs = []
    for theta, phi in ((30, 0), (30, 90), (30, 180), (30, 270),
                       (60, 45), (60, 135), (60, 225), (60, 315)):
        t, p = math.radians(theta), math.radians(phi)
        dirs.append((math.sin(t) * math.cos(p), math.sin(t) * math.sin(p), math.cos(t)))

    def ao(pos, normal):
        n = Vector(normal).normalized()
        ref = Vector((0, 0, 1)) if abs(n.z) < 0.9 else Vector((1, 0, 0))
        u = n.cross(ref).normalized()
        v = n.cross(u)
        origin = Vector(pos) + n * 0.012
        occ = 0.0
        for a, b, c in dirs:
            hit = tree.ray_cast(origin, u * a + v * b + n * c, radius)
            if hit[0] is not None:
                occ += 1.0 - hit[3] / radius
        return 0.55 + 0.45 * (1.0 - occ / len(dirs))
    return ao


def smart_uv(objs, angle=66.0, margin=0.002):
    """Non-overlapping UVs (Smart UV Project) for later textures (research A4)."""
    view_layer = bpy.context.view_layer
    for obj in objs:
        if obj.type != "MESH" or not obj.data.polygons:
            continue
        for o in bpy.context.scene.objects:
            o.select_set(o is obj)
        view_layer.objects.active = obj
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(angle_limit=math.radians(angle), island_margin=margin,
                                 scale_to_bounds=False)
        bpy.ops.object.mode_set(mode="OBJECT")
    for o in bpy.context.scene.objects:
        o.select_set(False)


def empty(name, location, parent=None, size=0.1):
    obj = bpy.data.objects.new(name, None)
    obj.empty_display_size = size
    obj.location = location
    bpy.context.scene.collection.objects.link(obj)
    if parent is not None:
        obj.parent = parent
    return obj


# ---------------------------------------------------------------- foil panels (wings, fins)


def naca_t(x):
    """Half-thickness / chord of a symmetric NACA 4-digit section at t/c = 1, closed trailing
    edge. x in 0..1."""
    return 5.0 * (0.2969 * math.sqrt(x) - 0.1260 * x - 0.3516 * x * x + 0.2843 * x ** 3
                  - 0.1036 * x ** 4)


def foil_ring(le, te, normal, tc, a, b, count, scale=1.0, mid_scale=None, xs=None):
    """Closed loop around an aerofoil between chord fractions a..b: upper surface front to
    back, then lower surface back to front. `scale` shrinks thickness (tip rounding);
    `mid_scale` pulls the section toward its mid-chord point. `xs` gives the chord fractions
    explicitly (default: `count` cosine-spaced steps)."""
    le, te, normal = Vector(le), Vector(te), Vector(normal).normalized()
    chord = te - le
    c = chord.length
    if xs is None:
        xs = [a + (b - a) * (0.5 - 0.5 * math.cos(math.pi * k / count)) for k in range(count + 1)]
    upper, lower = [], []
    for x in xs:
        th = naca_t(x) * tc * c * scale
        base = le + chord * x
        if mid_scale is not None:
            centre = le + chord * ((a + b) / 2.0)
            base = centre + (base - centre) * mid_scale
        upper.append(base + normal * th)
        lower.append(base - normal * th)
    return upper + lower[::-1]


def tip_params(k, n):
    """Rounded tip, ring k of n (1..n): (fraction of the tip length, thickness scale, chord
    scale toward mid-chord); an elliptic cap ending in a thin flat edge."""
    s = math.sin(0.5 * math.pi * k / n)
    return s, 0.15 + 0.85 * math.sqrt(max(0.0, 1.0 - s * s)), 1.0 - 0.45 * s ** 3
