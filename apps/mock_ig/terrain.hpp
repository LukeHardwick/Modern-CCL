#pragma once

// Synthetic terrain of the mock IG: deterministic, cheap and hilly enough that ground clamping
// and HAT/HOT answers can be judged by eye.
//
//   h(lat, lon) = 100 + 40 sin(2 pi lat / 0.02) cos(2 pi lon / 0.02)      (metres above MSL,
//                                                                          lat/lon in degrees)
//
// Heights lie between 60 and 140 m. Material codes (ICD 4.2.3 Material Code, here our own
// convention): 0 water below 70 m, 1 grass, 2 rock above 130 m.

#include "mock_ig/geo.hpp"

#include <cstdint>

namespace cigi::mock {

inline constexpr double terrain_base_m = 100.0;
inline constexpr double terrain_amplitude_m = 40.0;
inline constexpr double terrain_wavelength_deg = 0.02;
inline constexpr double terrain_water_below_m = 70.0;
inline constexpr double terrain_rock_above_m = 130.0;

enum class Material : std::uint32_t {
    Water = 0,
    Grass = 1,
    Rock = 2,
};

/// Surface normal at a point: unit vector in local ENU (x east, y north, z up) and the same
/// vector as the ICD's azimuth / elevation pair (ICD 4.2.3: azimuth is the horizontal angle from
/// true north to the normal, elevation is its angle above the horizontal plane).
struct SurfaceNormal {
    Vec3d enu;
    double azimuth_deg = 0;   ///< -180..180.
    double elevation_deg = 0; ///< 0..90 (90: flat ground).
};

// The (lat, lon) pairs below are always in that order.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)

/// Terrain height above MSL at a point, metres.
[[nodiscard]] double terrain_height(double lat_deg, double lon_deg) noexcept;

/// Analytic slope converted to metres per metre: (d h / d east, d h / d north) at a point.
[[nodiscard]] Vec3d terrain_gradient(double lat_deg, double lon_deg) noexcept;

[[nodiscard]] SurfaceNormal terrain_normal(double lat_deg, double lon_deg) noexcept;

/// Height band of the point: Water, Grass or Rock.
[[nodiscard]] Material terrain_material(double lat_deg, double lon_deg) noexcept;

// NOLINTEND(bugprone-easily-swappable-parameters)

/// Rectangular extent of the mock database in degrees. HAT/HOT test points outside it are
/// answered "invalid" (ICD 4.1.24, 4.2.2).
struct DatabaseBounds {
    double lat_min_deg = -90.0;
    double lat_max_deg = 90.0;
    double lon_min_deg = -180.0;
    double lon_max_deg = 180.0;

    [[nodiscard]] constexpr bool contains(double lat_deg, double lon_deg) const noexcept {
        return lat_deg >= lat_min_deg && lat_deg <= lat_max_deg && lon_deg >= lon_min_deg &&
               lon_deg <= lon_max_deg;
    }

    /// A square of half-width `half_deg` around `centre`.
    [[nodiscard]] static constexpr DatabaseBounds around(const Geodetic& centre,
                                                         double half_deg) noexcept {
        return {centre.lat_deg - half_deg, centre.lat_deg + half_deg, centre.lon_deg - half_deg,
                centre.lon_deg + half_deg};
    }
};

} // namespace cigi::mock
