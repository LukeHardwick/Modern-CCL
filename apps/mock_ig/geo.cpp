#include "mock_ig/geo.hpp"

#include <algorithm>
#include <cmath>

namespace cigi::mock {

namespace {

// WGS-84 (ICD 3.4.1: the default Earth Reference Model).
constexpr double wgs84_a = 6378137.0;
constexpr double wgs84_f = 1.0 / 298.257223563;
constexpr double wgs84_e2 = wgs84_f * (2.0 - wgs84_f);

/// 1 - e^2 sin^2(lat), the common denominator of both radii of curvature.
double w2(double lat_deg) noexcept {
    const double s = std::sin(lat_deg * deg_to_rad);
    return 1.0 - (wgs84_e2 * s * s);
}

Mat3 rot_z(double deg) noexcept {
    const double c = std::cos(deg * deg_to_rad);
    const double s = std::sin(deg * deg_to_rad);
    return {{c, s, 0}, {-s, c, 0}, {0, 0, 1}};
}
Mat3 rot_y(double deg) noexcept {
    const double c = std::cos(deg * deg_to_rad);
    const double s = std::sin(deg * deg_to_rad);
    return {{c, 0, -s}, {0, 1, 0}, {s, 0, c}};
}
Mat3 rot_x(double deg) noexcept {
    const double c = std::cos(deg * deg_to_rad);
    const double s = std::sin(deg * deg_to_rad);
    return {{1, 0, 0}, {0, c, s}, {0, -s, c}};
}

} // namespace

double meters_per_degree_lat(double lat_deg) noexcept {
    const double w = w2(lat_deg);
    // Meridian radius of curvature M = a (1 - e^2) / W^3 with W = sqrt(w2).
    return wgs84_a * (1.0 - wgs84_e2) / (w * std::sqrt(w)) * deg_to_rad;
}

double meters_per_degree_lon(double lat_deg) noexcept {
    // Prime-vertical radius N = a / W; the parallel has radius N cos(lat).
    return wgs84_a / std::sqrt(w2(lat_deg)) * std::cos(lat_deg * deg_to_rad) * deg_to_rad;
}

LocalFrame::LocalFrame(const Geodetic& origin) noexcept
    : origin_(origin), m_per_deg_lat_(meters_per_degree_lat(origin.lat_deg)),
      m_per_deg_lon_(meters_per_degree_lon(origin.lat_deg)) {}

Vec3d LocalFrame::to_enu(const Geodetic& g) const noexcept {
    return {(g.lon_deg - origin_.lon_deg) * m_per_deg_lon_,
            (g.lat_deg - origin_.lat_deg) * m_per_deg_lat_, g.alt_m - origin_.alt_m};
}

Geodetic LocalFrame::to_geodetic(const Vec3d& enu) const noexcept {
    // m_per_deg_lon_ is zero only at the poles; keep the longitude finite there.
    const double lon_scale = std::max(m_per_deg_lon_, 1e-9);
    return {origin_.lat_deg + (enu.y / m_per_deg_lat_), origin_.lon_deg + (enu.x / lon_scale),
            origin_.alt_m + enu.z};
}

Mat3 euler_zyx(const Attitude& a) noexcept {
    return rot_z(a.yaw_deg) * rot_y(a.pitch_deg) * rot_x(a.roll_deg);
}

Mat3 body_to_enu(const Attitude& a) noexcept {
    return ned_to_enu() * euler_zyx(a);
}

Mat3 conformal_body_to_enu(const Attitude& a, const Vec3d& up_normal_enu) noexcept {
    const Vec3d n = normalized(up_normal_enu);
    const Vec3d heading{std::sin(a.yaw_deg * deg_to_rad), std::cos(a.yaw_deg * deg_to_rad), 0.0};
    const Vec3d x_surface = normalized(heading - (n * dot(heading, n)));
    const Vec3d z_surface = -n;
    const Vec3d y_surface = cross(z_surface, x_surface);
    const Mat3 surface{x_surface, y_surface, z_surface};
    return surface * rot_y(a.pitch_deg) * rot_x(a.roll_deg);
}

Quat to_quat(const Mat3& m) noexcept {
    // Rows r_ij = column j component i; the usual trace-based branch selection.
    const double r00 = m.c0.x;
    const double r11 = m.c1.y;
    const double r22 = m.c2.z;
    const double trace = r00 + r11 + r22;
    Quat q;
    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        q = {0.25 * s, (m.c1.z - m.c2.y) / s, (m.c2.x - m.c0.z) / s, (m.c0.y - m.c1.x) / s};
    } else if (r00 > r11 && r00 > r22) {
        const double s = std::sqrt(1.0 + r00 - r11 - r22) * 2.0;
        q = {(m.c1.z - m.c2.y) / s, 0.25 * s, (m.c1.x + m.c0.y) / s, (m.c2.x + m.c0.z) / s};
    } else if (r11 > r22) {
        const double s = std::sqrt(1.0 + r11 - r00 - r22) * 2.0;
        q = {(m.c2.x - m.c0.z) / s, (m.c1.x + m.c0.y) / s, 0.25 * s, (m.c2.y + m.c1.z) / s};
    } else {
        const double s = std::sqrt(1.0 + r22 - r00 - r11) * 2.0;
        q = {(m.c0.y - m.c1.x) / s, (m.c2.x + m.c0.z) / s, (m.c2.y + m.c1.z) / s, 0.25 * s};
    }
    const double n = std::sqrt((q.w * q.w) + (q.x * q.x) + (q.y * q.y) + (q.z * q.z));
    return {q.w / n, q.x / n, q.y / n, q.z / n};
}

Mat3 to_mat3(const Quat& q) noexcept {
    const double xx = q.x * q.x;
    const double yy = q.y * q.y;
    const double zz = q.z * q.z;
    const double xy = q.x * q.y;
    const double xz = q.x * q.z;
    const double yz = q.y * q.z;
    const double wx = q.w * q.x;
    const double wy = q.w * q.y;
    const double wz = q.w * q.z;
    return {{1 - (2 * (yy + zz)), 2 * (xy + wz), 2 * (xz - wy)},
            {2 * (xy - wz), 1 - (2 * (xx + zz)), 2 * (yz + wx)},
            {2 * (xz + wy), 2 * (yz - wx), 1 - (2 * (xx + yy))}};
}

Vec3d rotate(const Quat& q, const Vec3d& v) noexcept {
    return to_mat3(q) * v;
}

double wrap_360(double deg) noexcept {
    double r = std::fmod(deg, 360.0);
    if (r < 0.0) {
        r += 360.0;
    }
    return r;
}

double wrap_180(double deg) noexcept {
    const double r = wrap_360(deg);
    return r > 180.0 ? r - 360.0 : r;
}

Attitude attitude_from_body_to_enu(const Mat3& body_to_enu_rotation) noexcept {
    // ned_to_enu() is its own inverse, so R_ned = S * R_enu.
    const Mat3 r = ned_to_enu() * body_to_enu_rotation;
    const double sin_pitch = std::clamp(-r.c0.z, -1.0, 1.0);
    Attitude a;
    a.pitch_deg = std::asin(sin_pitch) * rad_to_deg;
    if (std::abs(sin_pitch) > 1.0 - 1e-12) {
        // Gimbal lock: attribute everything to yaw.
        a.yaw_deg = wrap_360(std::atan2(-r.c1.x, r.c1.y) * rad_to_deg);
        a.roll_deg = 0.0;
    } else {
        a.yaw_deg = wrap_360(std::atan2(r.c0.y, r.c0.x) * rad_to_deg);
        a.roll_deg = std::atan2(r.c1.z, r.c2.z) * rad_to_deg;
    }
    return a;
}

ChildPose compose_child(const Vec3d& parent_position_enu, const Mat3& parent_body_to_enu,
                        const Vec3d& offset_body, const Attitude& relative) noexcept {
    return {parent_position_enu + (parent_body_to_enu * offset_body),
            parent_body_to_enu * euler_zyx(relative)};
}

} // namespace cigi::mock
