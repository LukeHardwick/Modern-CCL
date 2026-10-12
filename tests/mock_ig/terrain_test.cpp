#include "mock_ig/terrain.hpp"

#include <cmath>
#include <gtest/gtest.h>
#include <utility>

namespace {

using namespace cigi::mock;

// Golden values worked out by hand from h = 100 + 40 sin(2 pi lat/0.02) cos(2 pi lon/0.02) and
// checked with an independent script: the sine and cosine arguments are multiples of pi/4 at
// eighth wavelengths (lat or lon = 0.0025 k degrees).

TEST(Terrain, HeightAtCardinalPoints) {
    EXPECT_NEAR(terrain_height(0.0, 0.0), 100.0, 1e-9);            // sin 0 = 0
    EXPECT_NEAR(terrain_height(0.005, 0.0), 140.0, 1e-9);          // sin(pi/2) cos 0 = 1
    EXPECT_NEAR(terrain_height(0.005, 0.01), 60.0, 1e-9);          // cos(pi) = -1
    EXPECT_NEAR(terrain_height(0.015, 0.0), 60.0, 1e-9);           // sin(3pi/2) = -1
    EXPECT_NEAR(terrain_height(0.005, 0.005), 100.0, 1e-9);        // cos(pi/2) = 0
    EXPECT_NEAR(terrain_height(0.0025, 0.0), 128.284271247, 1e-8); // 100 + 40 sin(pi/4)
    EXPECT_NEAR(terrain_height(0.0025, 0.0025), 120.0, 1e-9);      // 40 * 0.5
    EXPECT_NEAR(terrain_height(-0.0025, 0.0025), 80.0, 1e-9);
}

TEST(Terrain, HeightAtRealisticCoordinates) {
    EXPECT_NEAR(terrain_height(51.5025, -0.1175), 120.0, 1e-7);
    EXPECT_NEAR(terrain_height(10.0075, 20.0025), 120.0, 1e-7);
}

TEST(Terrain, IsPeriodicWithTheWavelength) {
    EXPECT_NEAR(terrain_height(0.0031 + 0.02, 0.0047), terrain_height(0.0031, 0.0047), 1e-7);
    EXPECT_NEAR(terrain_height(0.0031, 0.0047 - 0.04), terrain_height(0.0031, 0.0047), 1e-7);
}

TEST(Terrain, HeightRange) {
    for (int i = 0; i < 200; ++i) {
        for (int j = 0; j < 200; ++j) {
            const double h = terrain_height(i * 0.0007, j * 0.0011);
            EXPECT_GE(h, 60.0 - 1e-9);
            EXPECT_LE(h, 140.0 + 1e-9);
        }
    }
}

TEST(Terrain, GradientGolden) {
    // dh/dnorth = 40 k cos(k lat) cos(k lon) / (metres per degree of latitude), k = 2pi/0.02.
    const Vec3d g = terrain_gradient(0.0025, 0.0);
    EXPECT_NEAR(g.x, 0.0, 1e-12);        // sin(k lon) = 0
    EXPECT_NEAR(g.y, 0.080360154, 1e-8); // 8885.77 per degree / 110574.28 m per degree
    const Vec3d e = terrain_gradient(0.0025, 0.0025);
    EXPECT_NEAR(e.x, -0.056442814, 1e-8);
    EXPECT_NEAR(e.y, 0.056823210, 1e-8);
    EXPECT_DOUBLE_EQ(e.z, 0.0);
}

TEST(Terrain, GradientMatchesFiniteDifferences) {
    for (const auto& [lat, lon] :
         {std::pair{0.0013, 0.0021}, std::pair{51.5031, -0.1189}, std::pair{-20.001, 40.0042}}) {
        const double h = 1e-7; // degrees
        const double d_lat =
            (terrain_height(lat + h, lon) - terrain_height(lat - h, lon)) / (2 * h);
        const double d_lon =
            (terrain_height(lat, lon + h) - terrain_height(lat, lon - h)) / (2 * h);
        const Vec3d g = terrain_gradient(lat, lon);
        EXPECT_NEAR(g.y, d_lat / meters_per_degree_lat(lat), 1e-6);
        EXPECT_NEAR(g.x, d_lon / meters_per_degree_lon(lat), 1e-6);
    }
}

TEST(Terrain, NormalGolden) {
    const SurfaceNormal flat = terrain_normal(0.005, 0.0); // a hilltop
    EXPECT_NEAR(flat.enu.z, 1.0, 1e-12);
    EXPECT_NEAR(flat.elevation_deg, 90.0, 1e-9);

    // Slope rising to the north: the normal leans south (azimuth +-180).
    const SurfaceNormal south = terrain_normal(0.0025, 0.0);
    EXPECT_NEAR(south.enu.x, 0.0, 1e-12);
    EXPECT_NEAR(south.enu.y, -0.080101931, 1e-8);
    EXPECT_NEAR(south.enu.z, 0.996786678, 1e-8);
    EXPECT_NEAR(std::abs(south.azimuth_deg), 180.0, 1e-9);
    EXPECT_NEAR(south.elevation_deg, 85.405575, 1e-5);

    // Rising to the north-west: the normal leans south-east, azimuth 135.19 degrees.
    const SurfaceNormal se = terrain_normal(0.0025, 0.0025);
    EXPECT_NEAR(se.enu.x, 0.056262649, 1e-8);
    EXPECT_NEAR(se.enu.y, -0.056641831, 1e-8);
    EXPECT_NEAR(se.azimuth_deg, 135.192423, 1e-5);
    EXPECT_NEAR(se.elevation_deg, 85.420862, 1e-5);

    const SurfaceNormal sw = terrain_normal(-0.0025, 0.0025);
    EXPECT_NEAR(sw.azimuth_deg, -135.192423, 1e-5);

    const SurfaceNormal real = terrain_normal(51.5025, -0.1175);
    EXPECT_NEAR(real.azimuth_deg, 121.968537, 1e-5);
    EXPECT_NEAR(real.elevation_deg, 83.911576, 1e-5);

    const SurfaceNormal origin = terrain_normal(0.0, 0.0);
    EXPECT_NEAR(origin.elevation_deg, 83.516357, 1e-5);
}

TEST(Terrain, NormalIsUnitLength) {
    for (int i = 0; i < 50; ++i) {
        const SurfaceNormal n = terrain_normal(i * 0.0013, i * 0.0007);
        EXPECT_NEAR(length(n.enu), 1.0, 1e-12);
        EXPECT_GE(n.elevation_deg, 80.0); // slopes stay below about 10 degrees
        EXPECT_GE(n.azimuth_deg, -180.0);
        EXPECT_LE(n.azimuth_deg, 180.0);
    }
}

TEST(Terrain, MaterialBands) {
    EXPECT_EQ(terrain_material(0.015, 0.0), Material::Water);  // 60 m
    EXPECT_EQ(terrain_material(0.005, 0.0), Material::Rock);   // 140 m
    EXPECT_EQ(terrain_material(0.0, 0.0), Material::Grass);    // 100 m
    EXPECT_EQ(terrain_material(0.0025, 0.0), Material::Grass); // 128.3 m
    // Either side of the band edges (100 +- 40 * 0.7575 = 130.3 / 69.7, and 129.7 / 70.3).
    EXPECT_EQ(terrain_material(0.005, 0.0022642), Material::Rock);  // 130.3 m
    EXPECT_EQ(terrain_material(0.005, 0.0023364), Material::Grass); // 129.7 m
    EXPECT_EQ(terrain_material(0.015, 0.0022642), Material::Water); // 69.7 m
    EXPECT_EQ(terrain_material(0.015, 0.0023364), Material::Grass); // 70.3 m
    EXPECT_EQ(static_cast<unsigned>(Material::Water), 0U);
    EXPECT_EQ(static_cast<unsigned>(Material::Grass), 1U);
    EXPECT_EQ(static_cast<unsigned>(Material::Rock), 2U);
}

TEST(Terrain, DatabaseBounds) {
    const DatabaseBounds b = DatabaseBounds::around({10.0, 20.0, 0.0}, 0.5);
    EXPECT_TRUE(b.contains(10.0, 20.0));
    EXPECT_TRUE(b.contains(10.5, 20.5)); // the edge is inside
    EXPECT_TRUE(b.contains(9.5, 19.5));
    EXPECT_FALSE(b.contains(10.51, 20.0));
    EXPECT_FALSE(b.contains(9.49, 20.0));
    EXPECT_FALSE(b.contains(10.0, 20.51));
    EXPECT_FALSE(b.contains(10.0, 19.49));
    EXPECT_TRUE(DatabaseBounds{}.contains(-89.0, 179.0));
}

} // namespace
