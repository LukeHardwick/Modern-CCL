#pragma once

// Sun position for the viewer's sky and light, from the Celestial Sphere Control time (ICD 4.1.9,
// Hour and Minute are UTC in 3.3). A textbook approximation (declination from the day of year,
// no equation of time, no refraction): good to about a degree or two, which is plenty for a mock
// IG that only wants day / dusk / night.

#include <chrono>
#include <optional>

namespace cigi::mock {

struct SunPosition {
    double elevation_deg = 0; ///< Above the horizon (negative: below).
    double azimuth_deg = 0;   ///< Clockwise from true north, 0..360.
};

/// Day of the year (1..366) of a calendar date.
[[nodiscard]] int day_of_year(std::chrono::year_month_day date) noexcept;

/// Solar declination in degrees for a day of the year (Cooper's formula).
[[nodiscard]] double solar_declination_deg(int day_of_year) noexcept;

/// Sun position seen from (lat, lon) at `utc_hours` (0..24) on `date`.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] SunPosition sun_position(std::chrono::year_month_day date, double utc_hours,
                                       double lat_deg, double lon_deg) noexcept;

/// 0 (night) .. 1 (day) brightness for a sun elevation: smooth ramp across civil twilight
/// (-6 to +6 degrees).
[[nodiscard]] double daylight_factor(double sun_elevation_deg) noexcept;

} // namespace cigi::mock
