#include "mock_ig/geo.hpp"

#include <cmath>
#include <gtest/gtest.h>

namespace {

using namespace cigi::mock;

constexpr double tol = 1e-9;

void expect_vec(const Vec3d& a, const Vec3d& b, double eps = 1e-9) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

void expect_mat(const Mat3& a, const Mat3& b, double eps = 1e-9) {
    expect_vec(a.c0, b.c0, eps);
    expect_vec(a.c1, b.c1, eps);
    expect_vec(a.c2, b.c2, eps);
}

// ---- independent reference: exact WGS-84 geodetic -> ECEF -> ENU (for the accuracy claim) -----
struct Ecef {
    double x, y, z;
};
Ecef ecef(double lat_deg, double lon_deg, double h) {
    constexpr double a = 6378137.0;
    constexpr double f = 1.0 / 298.257223563;
    constexpr double e2 = f * (2.0 - f);
    const double p = lat_deg * deg_to_rad;
    const double l = lon_deg * deg_to_rad;
    const double n = a / std::sqrt(1.0 - (e2 * std::sin(p) * std::sin(p)));
    return {(n + h) * std::cos(p) * std::cos(l), (n + h) * std::cos(p) * std::sin(l),
            ((n * (1.0 - e2)) + h) * std::sin(p)};
}
Vec3d exact_enu(const Geodetic& o, const Geodetic& g) {
    const Ecef a = ecef(o.lat_deg, o.lon_deg, o.alt_m);
    const Ecef b = ecef(g.lat_deg, g.lon_deg, g.alt_m);
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double dz = b.z - a.z;
    const double la = o.lat_deg * deg_to_rad;
    const double lo = o.lon_deg * deg_to_rad;
    return {(-std::sin(lo) * dx) + (std::cos(lo) * dy),
            (-std::sin(la) * std::cos(lo) * dx) - (std::sin(la) * std::sin(lo) * dy) +
                (std::cos(la) * dz),
            (std::cos(la) * std::cos(lo) * dx) + (std::cos(la) * std::sin(lo) * dy) +
                (std::sin(la) * dz)};
}

// ---- vectors -----------------------------------------------------------------------------------

TEST(Vec3, Arithmetic) {
    constexpr Vec3d a{1, 2, 3};
    constexpr Vec3d b{4, -5, 6};
    EXPECT_EQ(a + b, (Vec3d{5, -3, 9}));
    EXPECT_EQ(a - b, (Vec3d{-3, 7, -3}));
    EXPECT_EQ(-a, (Vec3d{-1, -2, -3}));
    EXPECT_EQ(a * 2.0, (Vec3d{2, 4, 6}));
    EXPECT_EQ(2.0 * a, (Vec3d{2, 4, 6}));
    EXPECT_DOUBLE_EQ(dot(a, b), 12.0); // 4 - 10 + 18
    EXPECT_EQ(cross(Vec3d{1, 0, 0}, Vec3d{0, 1, 0}), (Vec3d{0, 0, 1}));
    EXPECT_EQ(cross(a, b), (Vec3d{27, 6, -13})); // (2*6-3*-5, 3*4-1*6, 1*-5-2*4)
    EXPECT_DOUBLE_EQ(length(Vec3d{3, 4, 12}), 13.0);
    expect_vec(normalized(Vec3d{0, 0, 5}), {0, 0, 1});
    EXPECT_EQ(normalized(Vec3d{}), Vec3d{});
}

TEST(Angles, Wrap) {
    EXPECT_DOUBLE_EQ(wrap_360(0.0), 0.0);
    EXPECT_DOUBLE_EQ(wrap_360(360.0), 0.0);
    EXPECT_DOUBLE_EQ(wrap_360(-90.0), 270.0);
    EXPECT_DOUBLE_EQ(wrap_360(725.0), 5.0);
    EXPECT_DOUBLE_EQ(wrap_180(190.0), -170.0);
    EXPECT_DOUBLE_EQ(wrap_180(180.0), 180.0);
    EXPECT_DOUBLE_EQ(wrap_180(-190.0), 170.0);
    EXPECT_DOUBLE_EQ(wrap_180(10.0), 10.0);
}

// ---- metres per degree: values computed by hand from the WGS-84 radii of curvature -------------

TEST(MetersPerDegree, KnownLatitudes) {
    EXPECT_NEAR(meters_per_degree_lat(0.0), 110574.2758, 1e-3);
    EXPECT_NEAR(meters_per_degree_lon(0.0), 111319.4908, 1e-3);
    EXPECT_NEAR(meters_per_degree_lat(45.0), 111131.7774, 1e-3);
    EXPECT_NEAR(meters_per_degree_lon(45.0), 78846.8351, 1e-3);
    EXPECT_NEAR(meters_per_degree_lat(51.5), 111257.8336, 1e-3);
    EXPECT_NEAR(meters_per_degree_lon(51.5), 69440.5166, 1e-3);
    EXPECT_NEAR(meters_per_degree_lat(90.0), 111693.9796, 1e-3);
    EXPECT_NEAR(meters_per_degree_lon(90.0), 0.0, 1e-6);
}

// ---- local frame ------------------------------------------------------------------------------

TEST(LocalFrame, KnownPoint) {
    const LocalFrame frame({51.5, -0.12, 10.0});
    const Vec3d enu = frame.to_enu({51.51, -0.11, 60.0});
    EXPECT_NEAR(enu.x, 694.4051663973532, 1e-6); // 0.01 deg of lon at 51.5 N
    EXPECT_NEAR(enu.y, 1112.578336449093, 1e-6); // 0.01 deg of lat
    EXPECT_NEAR(enu.z, 50.0, tol);
    expect_vec(frame.to_enu(frame.origin()), {0, 0, 0});
}

TEST(LocalFrame, RoundTrip) {
    const LocalFrame frame({-33.9, 151.2, 25.0});
    for (const Vec3d v : {Vec3d{0, 0, 0}, Vec3d{1234.5, -987.6, 321.0}, Vec3d{-40000, 25000, -5}}) {
        expect_vec(frame.to_enu(frame.to_geodetic(v)), v, 1e-6);
    }
    const Geodetic g{-33.8, 151.3, 100.0};
    const Geodetic back = frame.to_geodetic(frame.to_enu(g));
    EXPECT_NEAR(back.lat_deg, g.lat_deg, 1e-12);
    EXPECT_NEAR(back.lon_deg, g.lon_deg, 1e-12);
    EXPECT_NEAR(back.alt_m, g.alt_m, 1e-9);
}

TEST(LocalFrame, PoleStaysFinite) {
    const LocalFrame frame({90.0, 0.0, 0.0});
    const Geodetic g = frame.to_geodetic({100.0, 0.0, 0.0});
    EXPECT_TRUE(std::isfinite(g.lon_deg));
}

// The documented accuracy of the flat-earth frame against an exact ECEF-derived ENU frame.
TEST(LocalFrame, AccuracyAgainstExactFrame) {
    const Geodetic origin{51.5, -0.12, 0.0};
    const LocalFrame frame(origin);
    const double mlat = meters_per_degree_lat(origin.lat_deg);
    const double mlon = meters_per_degree_lon(origin.lat_deg);
    struct Case {
        double distance_m;
        double max_horizontal_m;
        double max_vertical_m;
    };
    for (const Case c : {Case{10'000.0, 12.0, 9.0}, Case{50'000.0, 300.0, 210.0}}) {
        for (const Vec3d dir : {Vec3d{1, 0, 0}, Vec3d{0, 1, 0}, Vec3d{0.7071, 0.7071, 0}}) {
            const Vec3d flat = dir * c.distance_m;
            const Geodetic g{origin.lat_deg + (flat.y / mlat), origin.lon_deg + (flat.x / mlon),
                             0.0};
            const Vec3d exact = exact_enu(origin, g);
            expect_vec(frame.to_enu(g), flat, 1e-6);
            EXPECT_LT(std::hypot(exact.x - flat.x, exact.y - flat.y), c.max_horizontal_m);
            EXPECT_LT(std::abs(exact.z), c.max_vertical_m);
        }
    }
}

// ---- attitude ---------------------------------------------------------------------------------

TEST(Attitude, CardinalHeadings) {
    // Body x (forward), y (right), z (down) in ENU for level flight at each heading.
    expect_mat(body_to_enu({0, 0, 0}), {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}});     // x north, y east
    expect_mat(body_to_enu({90, 0, 0}), {{1, 0, 0}, {0, -1, 0}, {0, 0, -1}});   // x east, y south
    expect_mat(body_to_enu({180, 0, 0}), {{0, -1, 0}, {-1, 0, 0}, {0, 0, -1}}); // x south
    expect_mat(body_to_enu({270, 0, 0}), {{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}});  // x west
}

TEST(Attitude, PitchAndRollSigns) {
    // Positive pitch raises the nose; positive roll lowers the right wing (ICD 3.4.1.2).
    const Mat3 pitched = body_to_enu({0, 90, 0});
    expect_vec(pitched.c0, {0, 0, 1}); // nose straight up
    const Mat3 rolled = body_to_enu({0, 0, 90});
    expect_vec(rolled.c1, {0, 0, -1}); // right wing points down
    expect_vec(rolled.c2, {-1, 0, 0}); // body-down swings to the west
    const Mat3 pitch30 = body_to_enu({0, 30, 0});
    expect_vec(pitch30.c0, {0, std::cos(30 * deg_to_rad), std::sin(30 * deg_to_rad)});
}

TEST(Attitude, YawPitchRollOrder) {
    // Yaw first: heading east, then 90 degrees nose up -> nose up; roll then turns about nose.
    const Mat3 m = body_to_enu({90, 90, 0});
    expect_vec(m.c0, {0, 0, 1});
    const Mat3 r = body_to_enu({90, 0, 90}); // heading east, right wing down
    expect_vec(r.c0, {1, 0, 0});
    expect_vec(r.c1, {0, 0, -1});
}

TEST(Attitude, MatrixIsOrthonormal) {
    const Mat3 m = body_to_enu({123.0, -33.0, 57.0});
    EXPECT_NEAR(length(m.c0), 1.0, tol);
    EXPECT_NEAR(dot(m.c0, m.c1), 0.0, tol);
    expect_vec(cross(m.c0, m.c1), m.c2); // right-handed: z (down) = x (forward) cross y (right)
    expect_mat(transpose(transpose(m)), m);
    expect_mat(transpose(m) * m, Mat3{});
}

TEST(Attitude, QuaternionRoundTrip) {
    for (const Attitude a :
         {Attitude{0, 0, 0}, Attitude{45, 10, -20}, Attitude{200, -60, 170}, Attitude{359, 89, 179},
          Attitude{90, 0, 180}, Attitude{10, -45, -90}}) {
        const Mat3 m = body_to_enu(a);
        const Quat q = to_quat(m);
        expect_mat(to_mat3(q), m, 1e-9);
        EXPECT_NEAR((q.w * q.w) + (q.x * q.x) + (q.y * q.y) + (q.z * q.z), 1.0, 1e-12);
        const Vec3d v{1.5, -2.0, 0.5};
        expect_vec(rotate(q, v), m * v, 1e-9);
    }
}

TEST(Attitude, QuaternionBranches) {
    // Rotations of 180 degrees about each axis exercise every branch of the extraction.
    const Mat3 about_x{{1, 0, 0}, {0, -1, 0}, {0, 0, -1}};
    const Mat3 about_y{{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}};
    const Mat3 about_z{{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}};
    expect_mat(to_mat3(to_quat(about_x)), about_x);
    expect_mat(to_mat3(to_quat(about_y)), about_y);
    expect_mat(to_mat3(to_quat(about_z)), about_z);
    expect_mat(to_mat3(to_quat(Mat3{})), Mat3{});
}

TEST(Attitude, QuaternionProduct) {
    // 90 degrees about z then 90 about x: compare with the matrix product.
    const Quat qz = to_quat(euler_zyx({90, 0, 0}));
    const Quat qx = to_quat(euler_zyx({0, 0, 90}));
    expect_mat(to_mat3(qz * qx), euler_zyx({90, 0, 0}) * euler_zyx({0, 0, 90}));
}

TEST(Attitude, ExtractionRoundTrip) {
    for (const Attitude a :
         {Attitude{0, 0, 0}, Attitude{45, 10, -20}, Attitude{200, -60, 170}, Attitude{359, 89, 179},
          Attitude{90, 0, 180}, Attitude{10, -45, -90}}) {
        const Attitude back = attitude_from_body_to_enu(body_to_enu(a));
        EXPECT_NEAR(back.yaw_deg, a.yaw_deg, 1e-6);
        EXPECT_NEAR(back.pitch_deg, a.pitch_deg, 1e-6);
        EXPECT_NEAR(back.roll_deg, a.roll_deg, 1e-6);
    }
}

TEST(Attitude, ExtractionAtGimbalLock) {
    // Pitch +-90: only yaw - roll (or yaw + roll) is observable; roll is reported as 0.
    const Attitude up = attitude_from_body_to_enu(body_to_enu({30, 90, 0}));
    EXPECT_NEAR(up.pitch_deg, 90.0, 1e-6);
    EXPECT_NEAR(up.yaw_deg, 30.0, 1e-6);
    EXPECT_NEAR(up.roll_deg, 0.0, 1e-9);
    const Attitude down = attitude_from_body_to_enu(body_to_enu({30, -90, 0}));
    EXPECT_NEAR(down.pitch_deg, -90.0, 1e-6);
    EXPECT_NEAR(down.yaw_deg, 30.0, 1e-6);
    // The same orientation reached with a roll must give the same matrix back.
    const Attitude mixed = attitude_from_body_to_enu(body_to_enu({30, 90, 20}));
    expect_mat(body_to_enu(mixed), body_to_enu({30, 90, 20}), 1e-9);
}

// ---- conformal attitude -----------------------------------------------------------------------

TEST(Attitude, ConformalOnFlatGroundEqualsPlainAttitude) {
    const Vec3d up{0, 0, 1};
    for (const Attitude a : {Attitude{0, 0, 0}, Attitude{77, 5, -3}, Attitude{300, -10, 20}}) {
        expect_mat(conformal_body_to_enu(a, up), body_to_enu(a), 1e-9);
    }
}

TEST(Attitude, ConformalFollowsSlope) {
    // A 45 degree slope rising to the north: the normal leans to the south. Heading north with
    // zero offsets, the body-down axis is minus the normal and the nose climbs the slope.
    const double s = std::sqrt(0.5);
    const Mat3 m = conformal_body_to_enu({0, 0, 0}, {0, -s, s});
    expect_vec(m.c2, {0, s, -s});
    expect_vec(m.c0, {0, s, s});
    expect_vec(m.c1, {1, 0, 0}); // still pointing east
    // Heading east on the same slope stays horizontal (no climb across the slope).
    const Mat3 across = conformal_body_to_enu({90, 0, 0}, {0, -s, s});
    expect_vec(across.c0, {1, 0, 0});
    // A pitch offset is applied relative to the slope: 10 degrees more nose-up than the surface.
    const Mat3 off = conformal_body_to_enu({0, 10, 0}, {0, -s, s});
    expect_vec(off.c0,
               {0, std::cos((45.0 + 10.0) * deg_to_rad), std::sin((45.0 + 10.0) * deg_to_rad)});
}

// ---- child composition ------------------------------------------------------------------------

TEST(Compose, OffsetIsInParentBodyFrame) {
    // Parent heading east at (100, 200, 50); child 10 m ahead, 2 m to the right, 1 m below.
    const Mat3 parent = body_to_enu({90, 0, 0});
    const ChildPose c = compose_child({100, 200, 50}, parent, {10, 2, 1}, {0, 0, 0});
    expect_vec(c.position_enu, {110, 198, 49}); // ahead = east, right = south, below = down
    expect_mat(c.body_to_enu, parent);
}

TEST(Compose, RelativeAttitudeAppliesInParentFrame) {
    // Parent heading north; child yawed 90 degrees relative to it faces east.
    const ChildPose c = compose_child({0, 0, 0}, body_to_enu({0, 0, 0}), {0, 0, 0}, {90, 0, 0});
    expect_vec(c.body_to_enu.c0, {1, 0, 0});
    // Parent pitched up 90 (nose up), child pitched up 90 more relative to it: nose points back.
    const ChildPose d = compose_child({0, 0, 0}, body_to_enu({0, 90, 0}), {0, 0, 0}, {0, 90, 0});
    expect_vec(d.body_to_enu.c0, {0, -1, 0});
}

TEST(Compose, OffsetFollowsPitchedParent) {
    // Parent pitched 90 degrees (nose up): the child's body-forward offset goes straight up.
    const ChildPose c = compose_child({0, 0, 0}, body_to_enu({0, 90, 0}), {5, 0, 0}, {0, 0, 0});
    expect_vec(c.position_enu, {0, 0, 5});
}

} // namespace
