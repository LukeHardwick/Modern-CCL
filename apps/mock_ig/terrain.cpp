#include "mock_ig/terrain.hpp"

#include <cmath>
#include <numbers>

namespace cigi::mock {

namespace {

constexpr double k = 2.0 * std::numbers::pi / terrain_wavelength_deg; // radians per degree

} // namespace

double terrain_height(double lat_deg, double lon_deg) noexcept {
    return terrain_base_m + (terrain_amplitude_m * std::sin(k * lat_deg) * std::cos(k * lon_deg));
}

Vec3d terrain_gradient(double lat_deg, double lon_deg) noexcept {
    // d/dlat = A k cos(k lat) cos(k lon) per degree; d/dlon = -A k sin(k lat) sin(k lon).
    const double per_deg_lat =
        terrain_amplitude_m * k * std::cos(k * lat_deg) * std::cos(k * lon_deg);
    const double per_deg_lon =
        -terrain_amplitude_m * k * std::sin(k * lat_deg) * std::sin(k * lon_deg);
    return {per_deg_lon / meters_per_degree_lon(lat_deg),
            per_deg_lat / meters_per_degree_lat(lat_deg), 0.0};
}

SurfaceNormal terrain_normal(double lat_deg, double lon_deg) noexcept {
    const Vec3d g = terrain_gradient(lat_deg, lon_deg);
    const Vec3d n = normalized(Vec3d{-g.x, -g.y, 1.0});
    return {
        .enu = n,
        .azimuth_deg = std::atan2(n.x, n.y) * rad_to_deg,
        .elevation_deg = std::asin(n.z) * rad_to_deg,
    };
}

Material terrain_material(double lat_deg, double lon_deg) noexcept {
    const double h = terrain_height(lat_deg, lon_deg);
    if (h < terrain_water_below_m) {
        return Material::Water;
    }
    return h > terrain_rock_above_m ? Material::Rock : Material::Grass;
}

} // namespace cigi::mock
