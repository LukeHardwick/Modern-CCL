#include "mock_ig/sky.hpp"

#include <gtest/gtest.h>

namespace {

using namespace cigi::mock;
using namespace std::chrono;

constexpr year_month_day equinox{year{2026}, March, day{21}}; // day 80
constexpr year_month_day solstice{year{2026}, June, day{21}}; // day 172

TEST(Sky, DayOfYear) {
    EXPECT_EQ(day_of_year({year{2026}, January, day{1}}), 1);
    EXPECT_EQ(day_of_year(equinox), 80);
    EXPECT_EQ(day_of_year(solstice), 172);
    EXPECT_EQ(day_of_year({year{2026}, December, day{31}}), 365);
    EXPECT_EQ(day_of_year({year{2024}, December, day{31}}), 366); // leap year
}

TEST(Sky, Declination) {
    EXPECT_NEAR(solar_declination_deg(172), 23.44, 0.01);
    EXPECT_NEAR(solar_declination_deg(355), -23.44, 0.01);
    EXPECT_NEAR(solar_declination_deg(80), -0.40, 0.01);
}

// Values worked out by hand: sin(el) = sin(lat) sin(decl) + cos(lat) cos(decl) cos(H), with H = 15
// degrees per hour from local solar noon.
TEST(Sky, EquatorAtEquinox) {
    const SunPosition noon = sun_position(equinox, 12.0, 0.0, 0.0);
    EXPECT_NEAR(noon.elevation_deg, 89.60, 0.01); // declination -0.40
    const SunPosition morning = sun_position(equinox, 9.0, 0.0, 0.0);
    EXPECT_NEAR(morning.elevation_deg, 45.0, 0.01); // 90 - 3 h * 15
    EXPECT_NEAR(morning.azimuth_deg, 90.57, 0.01);  // east, a little south
    const SunPosition afternoon = sun_position(equinox, 15.0, 0.0, 0.0);
    EXPECT_NEAR(afternoon.elevation_deg, 45.0, 0.01);
    EXPECT_NEAR(afternoon.azimuth_deg, 269.43, 0.01);
    const SunPosition sunrise = sun_position(equinox, 6.0, 0.0, 0.0);
    EXPECT_NEAR(sunrise.elevation_deg, 0.0, 1e-9);
}

TEST(Sky, MidLatitudes) {
    const SunPosition noon = sun_position(solstice, 12.0, 51.5, 0.0);
    EXPECT_NEAR(noon.elevation_deg, 61.94, 0.01); // 90 - 51.5 + 23.44
    EXPECT_NEAR(noon.azimuth_deg, 180.0, 1e-6);
    const SunPosition midnight = sun_position(solstice, 0.0, 51.5, 0.0);
    EXPECT_NEAR(midnight.elevation_deg, -15.06, 0.01);
    EXPECT_NEAR(midnight.azimuth_deg, 0.0, 1e-6);
    const SunPosition ten = sun_position(solstice, 10.0, 51.5, -0.12);
    EXPECT_NEAR(ten.elevation_deg, 53.64, 0.01);
    EXPECT_NEAR(ten.azimuth_deg, 129.05, 0.01);
}

TEST(Sky, LongitudeShiftsSolarTime) {
    // 90 degrees east is six hours ahead: UTC noon there is solar 18:00 (sunset at the equinox).
    const SunPosition p = sun_position(equinox, 12.0, 0.0, 90.0);
    EXPECT_NEAR(p.elevation_deg, 0.0, 1e-9);
    EXPECT_NEAR(p.azimuth_deg, 269.60, 0.01);
}

TEST(Sky, DaylightFactor) {
    EXPECT_DOUBLE_EQ(daylight_factor(-20.0), 0.0);
    EXPECT_DOUBLE_EQ(daylight_factor(-6.0), 0.0);
    EXPECT_DOUBLE_EQ(daylight_factor(0.0), 0.5);
    EXPECT_DOUBLE_EQ(daylight_factor(6.0), 1.0);
    EXPECT_DOUBLE_EQ(daylight_factor(60.0), 1.0);
    EXPECT_GT(daylight_factor(2.0), daylight_factor(-2.0));
}

} // namespace
