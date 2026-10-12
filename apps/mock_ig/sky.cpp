#include "mock_ig/sky.hpp"

#include "mock_ig/geo.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cigi::mock {

int day_of_year(std::chrono::year_month_day date) noexcept {
    const std::chrono::sys_days d{date};
    const std::chrono::sys_days jan1{date.year() / std::chrono::January / 1};
    return static_cast<int>((d - jan1).count()) + 1;
}

double solar_declination_deg(int day_of_year) noexcept {
    // Cooper: 23.44 sin(360 (284 + N) / 365).
    return 23.44 * std::sin(2.0 * std::numbers::pi * (284.0 + day_of_year) / 365.0);
}

SunPosition sun_position(std::chrono::year_month_day date, double utc_hours, double lat_deg,
                         double lon_deg) noexcept {
    const double decl = solar_declination_deg(day_of_year(date)) * deg_to_rad;
    const double lat = lat_deg * deg_to_rad;
    // Mean solar time at this longitude; the hour angle is 15 degrees per hour from solar noon,
    // positive in the afternoon (west of the meridian).
    const double solar_hours = utc_hours + (lon_deg / 15.0);
    const double hour_angle = (solar_hours - 12.0) * 15.0 * deg_to_rad;
    const double sin_el =
        (std::sin(lat) * std::sin(decl)) + (std::cos(lat) * std::cos(decl) * std::cos(hour_angle));
    const double elevation = std::asin(std::clamp(sin_el, -1.0, 1.0));
    const double azimuth = std::atan2(std::sin(hour_angle), (std::cos(hour_angle) * std::sin(lat)) -
                                                                (std::tan(decl) * std::cos(lat))) +
                           std::numbers::pi;
    return {elevation * rad_to_deg, wrap_360(azimuth * rad_to_deg)};
}

double daylight_factor(double sun_elevation_deg) noexcept {
    const double t = std::clamp((sun_elevation_deg + 6.0) / 12.0, 0.0, 1.0);
    return t * t * (3.0 - (2.0 * t)); // smoothstep
}

} // namespace cigi::mock
