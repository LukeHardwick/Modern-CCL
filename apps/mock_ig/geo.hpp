#pragma once

// Geodetic <-> local tangent plane conversions, CIGI attitude -> rotation, and child-entity offset
// composition for the mock IG. Plain structs only (no graphics types).
//
// Local frame: ENU, x = east, y = north, z = up, centred on an origin (lat, lon, alt). It is a
// *flat-earth* frame: east and north are the lat/lon differences scaled by the WGS-84 metres per
// degree at the origin, and up is the plain altitude difference (ICD 3.4.1.1, altitude above the
// ellipsoid). Measured against an exact ECEF-derived ENU frame at an origin of 51.5 N:
//   10 km away: about 10 m horizontal (east-west worst case; parallels converge) and 8 m vertical
//               (the exact frame curves away, so exact "up" is lower than the altitude difference);
//   50 km away: about 250 m horizontal and about 196 m vertical.
// That is fine for a mock IG whose database is a few kilometres wide; it is not a survey tool.
//
// Attitude (ICD 3.4.1.2, 3.4.2.2): yaw is clockwise from true north looking down, pitch is nose up,
// roll is right wing down, applied in the order yaw, pitch, roll about the body Z (down), Y
// (right) and X (forward) axes. Body axes are x forward, y right, z down (NED-style).

#include <array>
#include <cmath>
#include <numbers>

namespace cigi::mock {

inline constexpr double deg_to_rad = std::numbers::pi / 180.0;
inline constexpr double rad_to_deg = 180.0 / std::numbers::pi;

struct Vec3d {
    double x = 0;
    double y = 0;
    double z = 0;

    friend constexpr bool operator==(const Vec3d&, const Vec3d&) noexcept = default;
};

[[nodiscard]] constexpr Vec3d operator+(const Vec3d& a, const Vec3d& b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
[[nodiscard]] constexpr Vec3d operator-(const Vec3d& a, const Vec3d& b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]] constexpr Vec3d operator-(const Vec3d& a) noexcept {
    return {-a.x, -a.y, -a.z};
}
[[nodiscard]] constexpr Vec3d operator*(const Vec3d& a, double s) noexcept {
    return {a.x * s, a.y * s, a.z * s};
}
[[nodiscard]] constexpr Vec3d operator*(double s, const Vec3d& a) noexcept {
    return a * s;
}
[[nodiscard]] constexpr double dot(const Vec3d& a, const Vec3d& b) noexcept {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}
[[nodiscard]] constexpr Vec3d cross(const Vec3d& a, const Vec3d& b) noexcept {
    return {(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x)};
}
[[nodiscard]] inline double length(const Vec3d& a) noexcept {
    return std::sqrt(dot(a, a));
}
/// `a` scaled to unit length; the zero vector stays zero.
[[nodiscard]] inline Vec3d normalized(const Vec3d& a) noexcept {
    const double n = length(a);
    return n > 0.0 ? a * (1.0 / n) : Vec3d{};
}

/// 3x3 rotation matrix stored as columns: `c0`, `c1`, `c2` are the images of the x, y and z unit
/// vectors, i.e. the source frame's axes written in the destination frame.
struct Mat3 {
    Vec3d c0{1, 0, 0};
    Vec3d c1{0, 1, 0};
    Vec3d c2{0, 0, 1};

    friend constexpr bool operator==(const Mat3&, const Mat3&) noexcept = default;
};

[[nodiscard]] constexpr Vec3d operator*(const Mat3& m, const Vec3d& v) noexcept {
    return (m.c0 * v.x) + (m.c1 * v.y) + (m.c2 * v.z);
}
[[nodiscard]] constexpr Mat3 operator*(const Mat3& a, const Mat3& b) noexcept {
    return {a * b.c0, a * b.c1, a * b.c2};
}
[[nodiscard]] constexpr Mat3 transpose(const Mat3& m) noexcept {
    return {{m.c0.x, m.c1.x, m.c2.x}, {m.c0.y, m.c1.y, m.c2.y}, {m.c0.z, m.c1.z, m.c2.z}};
}

/// Unit quaternion (w + xi + yj + zk).
struct Quat {
    double w = 1;
    double x = 0;
    double y = 0;
    double z = 0;

    friend constexpr bool operator==(const Quat&, const Quat&) noexcept = default;
};

[[nodiscard]] constexpr Quat operator*(const Quat& a, const Quat& b) noexcept {
    return {
        (a.w * b.w) - (a.x * b.x) - (a.y * b.y) - (a.z * b.z),
        (a.w * b.x) + (a.x * b.w) + (a.y * b.z) - (a.z * b.y),
        (a.w * b.y) - (a.x * b.z) + (a.y * b.w) + (a.z * b.x),
        (a.w * b.z) + (a.x * b.y) - (a.y * b.x) + (a.z * b.w),
    };
}

/// Yaw / pitch / roll in degrees (CIGI order and signs).
struct Attitude {
    double yaw_deg = 0;
    double pitch_deg = 0;
    double roll_deg = 0;

    friend constexpr bool operator==(const Attitude&, const Attitude&) noexcept = default;
};

/// Geodetic position: degrees, degrees, metres above the WGS-84 ellipsoid.
struct Geodetic {
    double lat_deg = 0;
    double lon_deg = 0;
    double alt_m = 0;

    friend constexpr bool operator==(const Geodetic&, const Geodetic&) noexcept = default;
};

/// Metres per degree of latitude / longitude at `lat_deg` on the WGS-84 ellipsoid.
[[nodiscard]] double meters_per_degree_lat(double lat_deg) noexcept;
[[nodiscard]] double meters_per_degree_lon(double lat_deg) noexcept;

/// Flat-earth ENU frame around `origin` (see the file comment for its accuracy).
class LocalFrame {
public:
    explicit LocalFrame(const Geodetic& origin) noexcept;

    [[nodiscard]] const Geodetic& origin() const noexcept { return origin_; }
    [[nodiscard]] Vec3d to_enu(const Geodetic& g) const noexcept;
    [[nodiscard]] Geodetic to_geodetic(const Vec3d& enu) const noexcept;

private:
    Geodetic origin_;
    double m_per_deg_lat_;
    double m_per_deg_lon_;
};

/// Rotation of a body frame with the given attitude, written in the NED-style reference axes
/// (x north, y east, z down) it is measured against: R = Rz(yaw) * Ry(pitch) * Rx(roll).
/// Maps body-frame vectors (x fwd, y right, z down) into the reference frame. Also the rotation
/// of a child relative to its parent body frame (ICD 3.4.2.2).
[[nodiscard]] Mat3 euler_zyx(const Attitude& a) noexcept;

/// The fixed change of axes NED -> ENU: (n, e, d) -> (e, n, -d).
[[nodiscard]] constexpr Mat3 ned_to_enu() noexcept {
    return {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}};
}

/// Body-to-ENU rotation of a top-level entity with attitude `a` (ICD 3.4.1.2): body vectors in,
/// east/north/up vectors out.
[[nodiscard]] Mat3 body_to_enu(const Attitude& a) noexcept;

/// Body-to-ENU rotation of a terrain-conformal entity (ICD 4.1.2, Ground/Ocean Clamp =
/// Conformal): the body's down axis is the negative terrain normal `up_normal_enu`, its forward
/// axis is the heading `a.yaw_deg` projected onto the slope, and pitch / roll are offsets from
/// that surface frame.
[[nodiscard]] Mat3 conformal_body_to_enu(const Attitude& a, const Vec3d& up_normal_enu) noexcept;

[[nodiscard]] Quat to_quat(const Mat3& rotation) noexcept;
[[nodiscard]] Mat3 to_mat3(const Quat& q) noexcept;
[[nodiscard]] Vec3d rotate(const Quat& q, const Vec3d& v) noexcept;

/// Attitude (yaw 0..360, pitch -90..90, roll -180..180) of a body-to-ENU rotation. At pitch
/// +-90 degrees yaw and roll are not separable; roll is reported as 0 there.
[[nodiscard]] Attitude attitude_from_body_to_enu(const Mat3& body_to_enu_rotation) noexcept;

/// A child entity placed at `offset_body` (parent body axes, metres) with relative attitude
/// `relative` (ICD 4.1.2, 3.4.2): its ENU position and body-to-ENU rotation.
struct ChildPose {
    Vec3d position_enu;
    Mat3 body_to_enu;
};
[[nodiscard]] ChildPose compose_child(const Vec3d& parent_position_enu,
                                      const Mat3& parent_body_to_enu, const Vec3d& offset_body,
                                      const Attitude& relative) noexcept;

/// Wrap an angle to [0, 360) / (-180, 180].
[[nodiscard]] double wrap_360(double deg) noexcept;
[[nodiscard]] double wrap_180(double deg) noexcept;

} // namespace cigi::mock
