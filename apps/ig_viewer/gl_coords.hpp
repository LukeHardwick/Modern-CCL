#pragma once

// Coordinate mapping from the mock IG's local ENU frame into raylib's world.
//
//   ENU  (east, north, up)      raylib  (x, y, z), Y up, right handed, default view along -Z
//   x = east                    x =  east
//   y = north                   z = -north      (north is the default "forward", -Z)
//   z = up                      y =  up
//
// The map ENU -> raylib is a proper rotation (determinant +1), so handedness and winding are
// preserved. Entity meshes are authored in the CIGI body frame (x forward, y right, z down);
// pose_matrix() turns a body-to-ENU rotation into a raylib Matrix, so a mesh drawn with it points
// the way the entity does.

#include "mock_ig/geo.hpp"

#include <cmath>
#include <cstdint>
#include <raylib.h>

namespace cigi::viewer {

[[nodiscard]] inline Vector3 to_gl(const mock::Vec3d& enu) noexcept {
    return {static_cast<float>(enu.x), static_cast<float>(enu.z), static_cast<float>(-enu.y)};
}

[[nodiscard]] inline mock::Vec3d from_gl(const Vector3& v) noexcept {
    return {static_cast<double>(v.x), static_cast<double>(-v.z), static_cast<double>(v.y)};
}

/// raylib Matrix (column-major m0..m15: m0-m2 are the image of the x axis, m12-m14 the
/// translation) of a pose: columns are the body axes, scaled by `scale`.
[[nodiscard]] inline Matrix pose_matrix(const mock::Vec3d& position_enu,
                                        const mock::Mat3& body_to_enu,
                                        float scale = 1.0F) noexcept {
    const Vector3 x = to_gl(body_to_enu.c0 * static_cast<double>(scale));
    const Vector3 y = to_gl(body_to_enu.c1 * static_cast<double>(scale));
    const Vector3 z = to_gl(body_to_enu.c2 * static_cast<double>(scale));
    const Vector3 p = to_gl(position_enu);
    Matrix m{};
    m.m0 = x.x;
    m.m1 = x.y;
    m.m2 = x.z;
    m.m4 = y.x;
    m.m5 = y.y;
    m.m6 = y.z;
    m.m8 = z.x;
    m.m9 = z.y;
    m.m10 = z.z;
    m.m12 = p.x;
    m.m13 = p.y;
    m.m14 = p.z;
    m.m15 = 1.0F;
    return m;
}

/// A colour that is stable per Entity ID and well separated between neighbouring IDs (golden
/// ratio hue steps).
[[nodiscard]] inline Color id_color(std::uint16_t id, float saturation = 0.65F,
                                    float value = 0.95F) noexcept {
    const float hue = std::fmod(static_cast<float>(id) * 0.61803398875F, 1.0F) * 360.0F;
    return ColorFromHSV(hue, saturation, value);
}

} // namespace cigi::viewer
