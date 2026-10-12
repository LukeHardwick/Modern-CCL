#include "../support/bytes.hpp"
#include "mock_ig/hat_hot.hpp"
#include "mock_ig/scene.hpp"

#include <array>
#include <cigi/message/builder.hpp>
#include <cmath>
#include <gtest/gtest.h>
#include <string>

namespace {

using namespace cigi;
using namespace cigi::mock;

constexpr Geodetic origin{51.5, -0.12, 0.0};

SceneConfig config() {
    SceneConfig c;
    c.origin = origin;
    return c;
}

EntityControl top_level(std::uint16_t id, double lat, double lon, double alt) {
    EntityControl e;
    e.entity_id = id;
    e.entity_state = EntityState::Active;
    e.alpha = 255;
    e.latitude_or_x = lat;
    e.longitude_or_y = lon;
    e.altitude_or_z = alt;
    return e;
}

EntityControl child(std::uint16_t id, std::uint16_t parent, double x, double y, double z) {
    EntityControl e;
    e.entity_id = id;
    e.entity_state = EntityState::Active;
    e.attach_state = AttachState::Attach;
    e.parent_id = parent;
    e.alpha = 255;
    e.latitude_or_x = x;
    e.longitude_or_y = y;
    e.altitude_or_z = z;
    return e;
}

void expect_vec(const Vec3d& a, const Vec3d& b, double eps = 1e-6) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

// ---- entity classification ---------------------------------------------------------------------

TEST(Classify, EntityTypeRanges) {
    EXPECT_EQ(classify_entity_type(0), ShapeClass::None);
    EXPECT_EQ(classify_entity_type(1), ShapeClass::Other);
    EXPECT_EQ(classify_entity_type(99), ShapeClass::Other);
    EXPECT_EQ(classify_entity_type(100), ShapeClass::Aircraft);
    EXPECT_EQ(classify_entity_type(199), ShapeClass::Aircraft);
    EXPECT_EQ(classify_entity_type(200), ShapeClass::GroundVehicle);
    EXPECT_EQ(classify_entity_type(299), ShapeClass::GroundVehicle);
    EXPECT_EQ(classify_entity_type(300), ShapeClass::Other);
    EXPECT_EQ(classify_entity_type(65535), ShapeClass::Other);
}

// ---- Entity Control ---------------------------------------------------------------------------

TEST(Scene, CreatesATopLevelEntity) {
    SceneModel scene(config());
    auto e = top_level(7, 51.5025, -0.1175, 500.0);
    e.entity_type = 101;
    e.yaw = 90.0F;
    e.pitch = 5.0F;
    e.roll = -10.0F;
    scene.apply(e);
    scene.resolve();
    const Entity* got = scene.find(7);
    ASSERT_NE(got, nullptr);
    EXPECT_TRUE(got->visible);
    EXPECT_EQ(got->entity_type, 101);
    EXPECT_EQ(got->last_control, e);
    EXPECT_FALSE(got->attached);
    EXPECT_DOUBLE_EQ(got->position.alt_m, 500.0);
    expect_vec(got->enu, scene.frame().to_enu({51.5025, -0.1175, 500.0}));
    // Terrain here is 120 m (hand-computed in the terrain tests): 380 m above ground.
    EXPECT_NEAR(got->terrain_m, 120.0, 1e-6);
    EXPECT_NEAR(got->agl_m, 380.0, 1e-6);
    EXPECT_NEAR(got->world_attitude.yaw_deg, 90.0, 1e-4);
    EXPECT_NEAR(got->world_attitude.pitch_deg, 5.0, 1e-4);
    EXPECT_NEAR(got->world_attitude.roll_deg, -10.0, 1e-4);
    EXPECT_EQ(scene.stats().entities_created, 1U);
    EXPECT_EQ(scene.entities().size(), 1U);
}

TEST(Scene, UpdatesAnExistingEntityWithoutRecreating) {
    SceneModel scene(config());
    scene.apply(top_level(1, 51.5, -0.12, 100.0));
    scene.apply(top_level(1, 51.51, -0.12, 200.0));
    scene.resolve();
    EXPECT_EQ(scene.stats().entities_created, 1U);
    EXPECT_DOUBLE_EQ(scene.find(1)->position.alt_m, 200.0);
    EXPECT_DOUBLE_EQ(scene.find(1)->position.lat_deg, 51.51);
}

TEST(Scene, StateControlsVisibility) {
    SceneModel scene(config());
    auto e = top_level(1, 51.5, -0.12, 10.0);
    e.entity_state = EntityState::InactiveStandby;
    scene.apply(e);
    scene.resolve();
    ASSERT_NE(scene.find(1), nullptr);
    EXPECT_FALSE(scene.find(1)->visible);
    e.entity_state = EntityState::Active;
    scene.apply(e);
    scene.resolve();
    EXPECT_TRUE(scene.find(1)->visible);
}

TEST(Scene, DestroyRemovesEntityAndItsChildren) {
    SceneModel scene(config());
    scene.apply(top_level(1, 51.5, -0.12, 10.0));
    scene.apply(child(2, 1, 5, 0, 0));
    scene.apply(child(3, 2, 1, 0, 0)); // grandchild
    scene.apply(top_level(4, 51.5, -0.12, 10.0));
    ASSERT_EQ(scene.entities().size(), 4U);
    EXPECT_EQ(scene.children_of(1), std::vector<std::uint16_t>{2});

    auto d = top_level(1, 0, 0, 0);
    d.entity_state = EntityState::Destroyed;
    scene.apply(d);
    scene.resolve();
    EXPECT_EQ(scene.entities().size(), 1U);
    EXPECT_NE(scene.find(4), nullptr);
    EXPECT_EQ(scene.stats().entities_destroyed, 3U);
    EXPECT_TRUE(scene.children_of(1).empty());

    // Destroying something that does not exist is counted, not fatal.
    scene.apply(d);
    EXPECT_EQ(scene.stats().destroy_unknown, 1U);
}

TEST(Scene, DestroyAnnulsRates) {
    SceneModel scene(config());
    scene.apply(top_level(1, 51.5, -0.12, 10.0));
    scene.apply(RateControl{.entity_id = 1, .x_linear_rate = 10.0F});
    auto d = top_level(1, 0, 0, 0);
    d.entity_state = EntityState::Destroyed;
    scene.apply(d);
    scene.apply(top_level(1, 51.5, -0.12, 10.0)); // same ID again
    scene.advance(1.0);
    EXPECT_NEAR(scene.find(1)->position.lat_deg, 51.5, 1e-12); // the old rate is gone
}

// ---- ground clamp -----------------------------------------------------------------------------

TEST(Scene, NonConformalClampAddsTerrainHeight) {
    SceneModel scene(config());
    auto e = top_level(1, 51.5025, -0.1175, 300.0); // terrain 120 m
    e.ground_clamp = GroundClamp::NonConformal;
    e.yaw = 45.0F;
    scene.apply(e);
    scene.resolve();
    const Entity& got = *scene.find(1);
    EXPECT_NEAR(got.position.alt_m, 420.0, 1e-6);
    EXPECT_NEAR(got.agl_m, 300.0, 1e-6);
    // Non-conformal: attitude is relative to the geodetic plane, so the body stays level.
    EXPECT_NEAR(got.world_attitude.pitch_deg, 0.0, 1e-9);
    EXPECT_NEAR(got.world_attitude.yaw_deg, 45.0, 1e-4);
}

TEST(Scene, ConformalClampFollowsTheSlope) {
    SceneModel scene(config());
    auto e = top_level(1, 51.5025, -0.1175, 1.0); // slope: normal elevation 83.911576 degrees
    e.ground_clamp = GroundClamp::Conformal;
    scene.apply(e);
    scene.resolve();
    const Entity& got = *scene.find(1);
    EXPECT_NEAR(got.agl_m, 1.0, 1e-6);
    // The body-down axis is minus the terrain normal (hand-computed in the terrain tests).
    expect_vec(got.body_to_enu.c2, {-0.08997753, 0.056155521, -0.994359393}, 1e-6);
    // Facing north on that slope (worked out independently): nose up 3.21, right wing down 5.18,
    // heading nudged by the slope to 0.29 degrees.
    EXPECT_NEAR(got.world_attitude.pitch_deg, 3.206055, 1e-4);
    EXPECT_NEAR(got.world_attitude.roll_deg, 5.178623, 1e-4);
    EXPECT_NEAR(got.world_attitude.yaw_deg, 0.290414, 1e-4);
}

TEST(Scene, ClampIsIgnoredWhileAttached) {
    SceneModel scene(config());
    scene.apply(top_level(1, 51.5025, -0.1175, 300.0));
    auto c = child(2, 1, 10, 0, 0);
    c.ground_clamp = GroundClamp::Conformal;
    scene.apply(c);
    scene.resolve();
    // The child sits at its offset from the parent regardless of the terrain below it.
    const Vec3d expected = scene.find(1)->enu + (scene.find(1)->body_to_enu * Vec3d{10, 0, 0});
    expect_vec(scene.find(2)->enu, expected);
}

// ---- attach / detach --------------------------------------------------------------------------

TEST(Scene, AttachedEntityFollowsItsParent) {
    SceneModel scene(config());
    auto p = top_level(1, 51.5, -0.12, 100.0);
    p.yaw = 90.0F; // facing east
    scene.apply(p);
    scene.apply(child(2, 1, 10, 2, 1)); // 10 m ahead, 2 m right, 1 m below
    scene.resolve();
    const Vec3d parent_enu = scene.find(1)->enu;
    expect_vec(scene.find(2)->enu, parent_enu + Vec3d{10, -2, -1});
    EXPECT_TRUE(scene.find(2)->attached);
    EXPECT_EQ(scene.find(2)->parent_id, 1);
    EXPECT_NEAR(scene.find(2)->position.alt_m, 99.0, 1e-9);

    // Move the parent; the child comes along without its own packet.
    scene.apply(top_level(1, 51.5, -0.11, 100.0));
    p.longitude_or_y = -0.11;
    p.yaw = 0.0F; // facing north now
    scene.apply(p);
    scene.resolve();
    expect_vec(scene.find(2)->enu, scene.find(1)->enu + Vec3d{2, 10, -1});
}

TEST(Scene, ChildAttitudeIsRelativeToParent) {
    SceneModel scene(config());
    auto p = top_level(1, 51.5, -0.12, 100.0);
    p.yaw = 90.0F;
    scene.apply(p);
    auto c = child(2, 1, 0, 0, 0);
    c.yaw = 90.0F; // 90 degrees to the right of the parent
    scene.apply(c);
    scene.resolve();
    EXPECT_NEAR(scene.find(2)->world_attitude.yaw_deg, 180.0, 1e-4);
}

TEST(Scene, DetachMakesTheEntityTopLevel) {
    SceneModel scene(config());
    scene.apply(top_level(1, 51.5, -0.12, 100.0));
    scene.apply(child(2, 1, 10, 0, 0));
    scene.apply(top_level(2, 51.6, -0.12, 50.0)); // Attach State = Detach
    scene.resolve();
    EXPECT_FALSE(scene.find(2)->attached);
    EXPECT_DOUBLE_EQ(scene.find(2)->position.lat_deg, 51.6);
    EXPECT_TRUE(scene.children_of(1).empty());
}

TEST(Scene, ReparentingNeedsNoDetach) {
    SceneModel scene(config());
    scene.apply(top_level(1, 51.5, -0.12, 100.0));
    scene.apply(top_level(3, 51.5, -0.11, 100.0));
    scene.apply(child(2, 1, 1, 0, 0));
    scene.apply(child(2, 3, 1, 0, 0));
    EXPECT_EQ(scene.find(2)->parent_id, 3);
}

TEST(Scene, InvalidParentIsIgnoredAndCounted) {
    SceneModel scene(config());
    scene.apply(top_level(1, 51.5, -0.12, 100.0));
    scene.apply(child(2, 99, 1, 0, 0)); // no such parent
    EXPECT_EQ(scene.find(2), nullptr);
    EXPECT_EQ(scene.stats().invalid_parent, 1U);

    scene.apply(child(1, 1, 0, 0, 0)); // its own parent
    EXPECT_FALSE(scene.find(1)->attached);
    EXPECT_EQ(scene.stats().invalid_parent, 2U);

    // A cycle: 2 under 1 is fine, then 1 under 2 would loop.
    scene.apply(child(2, 1, 1, 0, 0));
    scene.apply(child(1, 2, 0, 0, 0));
    EXPECT_FALSE(scene.find(1)->attached);
    EXPECT_EQ(scene.stats().invalid_parent, 3U);
    ASSERT_FALSE(scene.log().empty());
    EXPECT_NE(scene.log().back().find("invalid parent"), std::string::npos);

    // An existing attachment survives a refused re-attach to a bad parent.
    scene.apply(child(2, 77, 5, 5, 5));
    EXPECT_EQ(scene.find(2)->parent_id, 1);
}

TEST(Scene, ChildOfAnInactiveParentIsInvisible) {
    SceneModel scene(config());
    auto p = top_level(1, 51.5, -0.12, 100.0);
    p.entity_state = EntityState::InactiveStandby;
    scene.apply(p);
    scene.apply(child(2, 1, 1, 0, 0));
    scene.resolve();
    EXPECT_FALSE(scene.find(2)->visible);
    p.entity_state = EntityState::Active;
    scene.apply(p);
    scene.resolve();
    EXPECT_TRUE(scene.find(2)->visible);
}

TEST(Scene, AlphaIsInheritedWhenAsked) {
    SceneModel scene(config());
    auto p = top_level(1, 51.5, -0.12, 100.0);
    p.alpha = 128;
    scene.apply(p);
    auto c = child(2, 1, 1, 0, 0);
    c.alpha = 200;
    c.inherit_alpha = true;
    scene.apply(c);
    auto n = child(3, 1, 2, 0, 0);
    n.alpha = 200;
    scene.apply(n);
    scene.resolve();
    EXPECT_EQ(scene.find(1)->apparent_alpha, 128);
    EXPECT_EQ(scene.find(2)->apparent_alpha, 100); // 200 * 128 / 255
    EXPECT_EQ(scene.find(3)->apparent_alpha, 200);
}

// ---- Articulated Part Control -----------------------------------------------------------------

TEST(Scene, ArticulatedPartAppliesOnlyEnabledFields) {
    SceneModel scene(config());
    scene.apply(top_level(1, 51.5, -0.12, 100.0));
    ArticulatedPartControl a;
    a.entity_id = 1;
    a.articulated_part_id = 3;
    a.part_enabled = true;
    a.x_offset_enabled = true;
    a.yaw_enabled = true;
    a.x_offset = 1.5F;
    a.y_offset = 99.0F; // not enabled: ignored
    a.yaw = 45.0F;
    a.pitch = 30.0F; // not enabled: ignored
    scene.apply(a);
    const PartState& part = scene.find(1)->parts.at(3);
    EXPECT_TRUE(part.enabled);
    EXPECT_DOUBLE_EQ(part.offset.x, 1.5);
    EXPECT_DOUBLE_EQ(part.offset.y, 0.0);
    EXPECT_DOUBLE_EQ(part.attitude.yaw_deg, 45.0);
    EXPECT_DOUBLE_EQ(part.attitude.pitch_deg, 0.0);

    // Values are absolute, and a disabled field keeps what it had.
    a.x_offset_enabled = false;
    a.yaw = 10.0F;
    a.y_offset_enabled = true;
    a.z_offset_enabled = true;
    a.roll_enabled = true;
    a.pitch_enabled = true;
    a.y_offset = -2.0F;
    a.z_offset = 0.5F;
    a.roll = 20.0F;
    a.pitch = 30.0F;
    scene.apply(a);
    EXPECT_DOUBLE_EQ(part.offset.x, 1.5);
    EXPECT_DOUBLE_EQ(part.offset.y, -2.0);
    EXPECT_DOUBLE_EQ(part.offset.z, 0.5);
    EXPECT_DOUBLE_EQ(part.attitude.yaw_deg, 10.0);
    EXPECT_DOUBLE_EQ(part.attitude.roll_deg, 20.0);
    EXPECT_DOUBLE_EQ(part.attitude.pitch_deg, 30.0);

    a.part_enabled = false; // removed from the scene
    scene.apply(a);
    EXPECT_FALSE(part.enabled);
}

TEST(Scene, ArticulatedPartOfUnknownEntityIsCounted) {
    SceneModel scene(config());
    scene.apply(ArticulatedPartControl{.entity_id = 42, .articulated_part_id = 1});
    EXPECT_EQ(scene.stats().unknown_entity, 1U);
    EXPECT_TRUE(scene.entities().empty());
    ASSERT_FALSE(scene.log().empty());
    EXPECT_NE(scene.log().back().find("unknown entity 42"), std::string::npos);
}

// ---- Rate Control -----------------------------------------------------------------------------

TEST(Scene, WorldRatesMoveATopLevelEntityAlongNorthEastDown) {
    SceneModel scene(config());
    scene.apply(top_level(1, 51.5, -0.12, 100.0));
    // 10 m/s north, 5 m/s east, 2 m/s down for 10 s.
    scene.apply(RateControl{
        .entity_id = 1, .x_linear_rate = 10.0F, .y_linear_rate = 5.0F, .z_linear_rate = 2.0F});
    scene.resolve();
    const Vec3d start = scene.find(1)->enu;
    for (int i = 0; i < 100; ++i) {
        scene.advance(0.1);
    }
    expect_vec(scene.find(1)->enu - start, {50.0, 100.0, -20.0}, 5e-3);
    EXPECT_NEAR(scene.find(1)->position.alt_m, 80.0, 1e-9);
}

TEST(Scene, WorldRatesOfAClampedEntityChangeItsOffset) {
    SceneModel scene(config());
    auto e = top_level(1, 51.5025, -0.1175, 100.0);
    e.ground_clamp = GroundClamp::NonConformal;
    scene.apply(e);
    scene.apply(RateControl{.entity_id = 1, .z_linear_rate = -5.0F}); // climb 5 m/s
    scene.advance(2.0);
    EXPECT_NEAR(scene.find(1)->agl_m, 110.0, 1e-6);
}

TEST(Scene, LocalRatesFollowTheEntityAttitude) {
    SceneModel scene(config());
    auto e = top_level(1, 51.5, -0.12, 100.0);
    e.yaw = 90.0F; // facing east
    scene.apply(e);
    scene.apply(RateControl{.entity_id = 1,
                            .coordinate_system = RateCoordinateSystem::Local,
                            .x_linear_rate = 10.0F}); // 10 m/s forward
    scene.resolve();
    const Vec3d start = scene.find(1)->enu;
    scene.advance(1.0);
    scene.advance(1.0);
    expect_vec(scene.find(1)->enu - start, {20.0, 0.0, 0.0}, 5e-3);

    // Right-hand body axis points south when facing east.
    scene.apply(RateControl{
        .entity_id = 1, .coordinate_system = RateCoordinateSystem::Local, .y_linear_rate = 4.0F});
    const Vec3d mid = scene.find(1)->enu;
    scene.advance(1.0);
    expect_vec(scene.find(1)->enu - mid, {0.0, -4.0, 0.0}, 5e-3);
}

TEST(Scene, AngularRatesAdvanceTheEulerAnglesAndWrap) {
    SceneModel scene(config());
    auto e = top_level(1, 51.5, -0.12, 100.0);
    e.yaw = 350.0F;
    e.roll = 170.0F;
    e.pitch = 80.0F;
    scene.apply(e);
    scene.apply(
        RateControl{.entity_id = 1, .roll_rate = 20.0F, .pitch_rate = 20.0F, .yaw_rate = 20.0F});
    scene.advance(1.0);
    const Entity& got = *scene.find(1);
    EXPECT_NEAR(got.attitude.yaw_deg, 10.0, 1e-4);    // 350 + 20 wraps
    EXPECT_NEAR(got.attitude.roll_deg, -170.0, 1e-4); // 170 + 20 wraps
    EXPECT_NEAR(got.attitude.pitch_deg, 90.0, 1e-4);  // limited to 90
}

TEST(Scene, RatesOfAChildAreInTheParentFrame) {
    SceneModel scene(config());
    auto p = top_level(1, 51.5, -0.12, 100.0);
    p.yaw = 90.0F; // east
    scene.apply(p);
    scene.apply(child(2, 1, 0, 0, 0));
    scene.apply(RateControl{.entity_id = 2, .x_linear_rate = 3.0F}); // 3 m/s along the parent's x
    scene.advance(2.0);
    EXPECT_DOUBLE_EQ(scene.find(2)->offset.x, 6.0);
    expect_vec(scene.find(2)->enu - scene.find(1)->enu, {6.0, 0.0, 0.0}, 1e-9);
}

TEST(Scene, LocalRatesOfAChildFollowItsOwnAttitude) {
    SceneModel scene(config());
    scene.apply(top_level(1, 51.5, -0.12, 100.0));
    auto c = child(2, 1, 0, 0, 0);
    c.yaw = 90.0F; // turned right relative to the parent
    scene.apply(c);
    scene.apply(RateControl{
        .entity_id = 2, .coordinate_system = RateCoordinateSystem::Local, .x_linear_rate = 2.0F});
    scene.advance(1.0);
    expect_vec(scene.find(2)->offset, {0.0, 2.0, 0.0}, 1e-9); // forward for the child = parent's y
}

TEST(Scene, RatesOfAPartMoveThePart) {
    SceneModel scene(config());
    scene.apply(top_level(1, 51.5, -0.12, 100.0));
    // The part does not need an Articulated Part Control first.
    scene.apply(RateControl{.entity_id = 1,
                            .articulated_part_id = 5,
                            .apply_to_articulated_part = true,
                            .y_linear_rate = 1.0F,
                            .roll_rate = 90.0F,
                            .yaw_rate = 30.0F});
    scene.advance(2.0);
    const PartState& part = scene.find(1)->parts.at(5);
    EXPECT_NEAR(part.offset.y, 2.0, 1e-9);
    EXPECT_NEAR(part.attitude.roll_deg, 180.0, 1e-9);
    EXPECT_NEAR(part.attitude.yaw_deg, 60.0, 1e-9);
    EXPECT_FALSE(scene.find(1)->rates.active()); // the entity itself is not moving
    EXPECT_NEAR(scene.find(1)->position.lat_deg, 51.5, 1e-12);
}

TEST(Scene, ZeroRatesStopTheEntity) {
    SceneModel scene(config());
    scene.apply(top_level(1, 51.5, -0.12, 100.0));
    scene.apply(RateControl{.entity_id = 1, .x_linear_rate = 10.0F});
    scene.advance(1.0);
    const Vec3d at = scene.find(1)->enu;
    scene.apply(RateControl{.entity_id = 1});
    scene.advance(1.0);
    expect_vec(scene.find(1)->enu, at, 1e-9);
}

TEST(Scene, InactiveEntitiesDoNotMove) {
    SceneModel scene(config());
    auto e = top_level(1, 51.5, -0.12, 100.0);
    e.entity_state = EntityState::InactiveStandby;
    scene.apply(e);
    scene.apply(RateControl{.entity_id = 1, .x_linear_rate = 10.0F});
    scene.advance(5.0);
    EXPECT_DOUBLE_EQ(scene.find(1)->position.lat_deg, 51.5);
}

TEST(Scene, ANewEntityControlRestartsExtrapolationFromItsState) {
    SceneModel scene(config());
    scene.apply(top_level(1, 51.5, -0.12, 100.0));
    scene.apply(RateControl{.entity_id = 1, .y_linear_rate = 10.0F});
    scene.advance(1.0);
    scene.apply(top_level(1, 51.5, -0.12, 100.0)); // the Host re-states the position
    scene.resolve();
    EXPECT_DOUBLE_EQ(scene.find(1)->position.lon_deg, -0.12);
    scene.advance(1.0); // the rate is still in force
    EXPECT_GT(scene.find(1)->position.lon_deg, -0.12);
}

TEST(Scene, RateOfUnknownEntityIsCounted) {
    SceneModel scene(config());
    scene.apply(RateControl{.entity_id = 9, .x_linear_rate = 1.0F});
    EXPECT_EQ(scene.stats().unknown_entity, 1U);
    ASSERT_FALSE(scene.log().empty());
}

TEST(Scene, ZeroStepDoesNotMove) {
    SceneModel scene(config());
    scene.apply(top_level(1, 51.5, -0.12, 100.0));
    scene.apply(RateControl{.entity_id = 1, .x_linear_rate = 10.0F});
    scene.advance(0.0);
    EXPECT_DOUBLE_EQ(scene.find(1)->position.lat_deg, 51.5);
    EXPECT_EQ(scene.frame_count(), 1U);
}

// ---- Component Control ------------------------------------------------------------------------

TEST(Scene, ComponentControlIsStoredAndLoggedOnChange) {
    SceneModel scene(config());
    ComponentControl c;
    c.component_class = ComponentClass::Entity;
    c.component_id = 7;
    c.instance_id = 2;
    c.component_state = 3;
    c.data_1 = 0xDEADBEEF;
    scene.apply(c);
    scene.apply(c); // unchanged: counted but not logged again
    EXPECT_EQ(scene.stats().component_controls, 2U);
    EXPECT_EQ(scene.log().size(), 1U);
    const auto& stored = scene.components().at({0, 7, 2});
    EXPECT_EQ(stored.state, 3);
    EXPECT_EQ(stored.data[0], 0xDEADBEEFU);
    c.component_state = 4;
    scene.apply(c);
    EXPECT_EQ(scene.components().at({0, 7, 2}).state, 4);
    EXPECT_EQ(scene.log().size(), 2U);
    c.data_6 = 1;
    scene.apply(c);
    EXPECT_EQ(scene.log().size(), 3U);
    EXPECT_EQ(scene.components().size(), 1U);
}

// ---- IG Control -------------------------------------------------------------------------------

TEST(Scene, IgControlIsRemembered) {
    SceneModel scene(config());
    scene.apply(IgControl{.ig_mode = IgMode::Operate, .host_frame_number = 77});
    EXPECT_EQ(scene.host_frame_number(), 77U);
    EXPECT_EQ(scene.ig_control().ig_mode, IgMode::Operate);
}

// ---- Celestial Sphere -------------------------------------------------------------------------

TEST(Scene, CelestialDefaultsBeforeAnyPacket) {
    const SceneModel scene(config());
    EXPECT_FALSE(scene.celestial().received);
    EXPECT_EQ(scene.celestial().hour, 12);
    EXPECT_DOUBLE_EQ(scene.celestial().utc_hours(), 12.0);
}

TEST(Scene, CelestialTimeAppliesOnlyWhenValid) {
    SceneModel scene(config());
    CelestialSphereControl c;
    c.hour = 18;
    c.minute = 30;
    c.sun_enabled = true;
    c.moon_enabled = true;
    c.star_field_enabled = true;
    c.star_field_intensity = 60.0F;
    c.ephemeris_model_enabled = false;
    c.set_date(std::chrono::year_month_day{std::chrono::year{2027}, std::chrono::March,
                                           std::chrono::day{5}});
    c.date_time_valid = false;
    scene.apply(c);
    EXPECT_TRUE(scene.celestial().received);
    EXPECT_EQ(scene.celestial().hour, 12); // the IG keeps its own time
    EXPECT_TRUE(scene.celestial().sun);
    EXPECT_TRUE(scene.celestial().moon);
    EXPECT_TRUE(scene.celestial().star_field);
    EXPECT_FALSE(scene.celestial().ephemeris_model);
    EXPECT_FLOAT_EQ(scene.celestial().star_field_intensity, 60.0F);

    c.date_time_valid = true;
    scene.apply(c);
    EXPECT_EQ(scene.celestial().hour, 18);
    EXPECT_EQ(scene.celestial().minute, 30);
    EXPECT_DOUBLE_EQ(scene.celestial().utc_hours(), 18.5);
    ASSERT_TRUE(scene.celestial().date.has_value());
    EXPECT_EQ(static_cast<int>(scene.celestial().date->year()), 2027);
    EXPECT_EQ(static_cast<unsigned>(scene.celestial().date->month()), 3U);

    c.date = 99999999; // not a calendar date: the previous date stays
    scene.apply(c);
    EXPECT_EQ(static_cast<int>(scene.celestial().date->year()), 2027);
}

// ---- views ------------------------------------------------------------------------------------

TEST(Scene, ViewControlSetsEntityAndEnabledOffsets) {
    SceneModel scene(config());
    ViewControl v;
    v.view_id = 3;
    v.entity_id = 5;
    v.x_offset_enabled = true;
    v.z_offset_enabled = true;
    v.yaw_enabled = true;
    v.x_offset = 2.0F;
    v.y_offset = 9.0F; // not enabled
    v.z_offset = -1.0F;
    v.yaw = 30.0F;
    v.pitch = 15.0F; // not enabled
    scene.apply(v);
    const ViewState* got = scene.find_view(3);
    ASSERT_NE(got, nullptr);
    EXPECT_TRUE(got->controlled);
    EXPECT_EQ(got->entity_id, 5);
    EXPECT_DOUBLE_EQ(got->offset.x, 2.0);
    EXPECT_DOUBLE_EQ(got->offset.y, 0.0);
    EXPECT_DOUBLE_EQ(got->offset.z, -1.0);
    EXPECT_DOUBLE_EQ(got->attitude.yaw_deg, 30.0);
    EXPECT_DOUBLE_EQ(got->attitude.pitch_deg, 0.0);

    v.y_offset_enabled = true;
    v.roll_enabled = true;
    v.pitch_enabled = true;
    v.roll = 5.0F;
    scene.apply(v);
    EXPECT_DOUBLE_EQ(scene.find_view(3)->offset.y, 9.0);
    EXPECT_DOUBLE_EQ(scene.find_view(3)->attitude.roll_deg, 5.0);
    EXPECT_DOUBLE_EQ(scene.find_view(3)->attitude.pitch_deg, 15.0);
    EXPECT_EQ(scene.find_view(99), nullptr);
}

TEST(Scene, ViewDefinitionAppliesEnabledVolumeValues) {
    SceneModel scene(config());
    ViewDefinition d;
    d.view_id = 1;
    d.group_id = 4;
    d.near_enabled = true;
    d.far_enabled = true;
    d.left_enabled = true;
    d.right_enabled = true;
    d.top_enabled = true;
    d.bottom_enabled = true;
    d.near_plane = 0.5F;
    d.far_plane = 5000.0F;
    d.left = -20.0F;
    d.right = 25.0F;
    d.top = 15.0F;
    d.bottom = -10.0F;
    d.mirror_mode = MirrorMode::Horizontal;
    d.projection_type = ProjectionType::OrthographicParallel;
    d.pixel_replication_mode = PixelReplicationMode::TwoByTwo;
    d.view_type = 2;
    d.reorder = true;
    scene.apply(d);
    const ViewState& v = *scene.find_view(1);
    EXPECT_EQ(v.group_id, 4);
    EXPECT_FLOAT_EQ(v.near_plane, 0.5F);
    EXPECT_FLOAT_EQ(v.far_plane, 5000.0F);
    EXPECT_DOUBLE_EQ(v.horizontal_fov_deg(), 45.0);
    EXPECT_DOUBLE_EQ(v.vertical_fov_deg(), 25.0);
    EXPECT_EQ(v.mirror, MirrorMode::Horizontal);
    EXPECT_EQ(v.projection, ProjectionType::OrthographicParallel);
    EXPECT_EQ(v.pixel_replication, PixelReplicationMode::TwoByTwo);
    EXPECT_EQ(v.view_type, 2);
    EXPECT_TRUE(v.reorder);

    // Disabled values are left alone.
    ViewDefinition keep;
    keep.view_id = 1;
    keep.group_id = 4;
    keep.near_plane = 99.0F;
    keep.left = -1.0F;
    scene.apply(keep);
    EXPECT_FLOAT_EQ(scene.find_view(1)->near_plane, 0.5F);
    EXPECT_FLOAT_EQ(scene.find_view(1)->left, -20.0F);
}

TEST(Scene, ViewGroupControlReachesEveryMember) {
    SceneModel scene(config());
    scene.apply(ViewDefinition{.view_id = 1, .group_id = 7});
    scene.apply(ViewDefinition{.view_id = 2, .group_id = 7});
    scene.apply(ViewDefinition{.view_id = 3, .group_id = 8});
    scene.apply(ViewControl{.view_id = 1234, // ignored: Group ID is non-zero
                            .group_id = 7,
                            .x_offset_enabled = true,
                            .entity_id = 6,
                            .x_offset = 4.0F});
    EXPECT_EQ(scene.find_view(1)->entity_id, 6);
    EXPECT_EQ(scene.find_view(2)->entity_id, 6);
    EXPECT_DOUBLE_EQ(scene.find_view(2)->offset.x, 4.0);
    EXPECT_FALSE(scene.find_view(3)->controlled);
    EXPECT_EQ(scene.find_view(1234), nullptr);
}

TEST(Scene, ViewPoseIsEntityPlusOffsetThenRotation) {
    SceneModel scene(config());
    auto e = top_level(0, 51.5, -0.12, 100.0);
    e.yaw = 90.0F; // east
    scene.apply(e);
    scene.apply(ViewControl{.view_id = 0,
                            .x_offset_enabled = true,
                            .z_offset_enabled = true,
                            .yaw_enabled = true,
                            .entity_id = 0,
                            .x_offset = 2.0F,
                            .z_offset = -1.0F, // 1 m above the reference point
                            .yaw = 90.0F});    // looking 90 degrees to the right of the nose
    scene.resolve();
    const auto pose = scene.view_pose(0);
    ASSERT_TRUE(pose.has_value());
    expect_vec(pose->position_enu, scene.find(0)->enu + Vec3d{2.0, 0.0, 1.0});
    expect_vec(pose->body_to_enu.c0, {0.0, -1.0, 0.0}, 1e-9); // looking south
    EXPECT_FALSE(scene.view_pose(5).has_value());             // unknown view
    scene.apply(ViewControl{.view_id = 6, .entity_id = 99});
    EXPECT_FALSE(scene.view_pose(6).has_value()); // its entity does not exist
}

// ---- trails -----------------------------------------------------------------------------------

TEST(Scene, TrailsRecordMovementAndAreBounded) {
    auto cfg = config();
    cfg.trail_length = 5;
    cfg.trail_step_m = 1.5;
    SceneModel scene(cfg);
    scene.apply(top_level(1, 51.5, -0.12, 100.0));
    scene.apply(RateControl{.entity_id = 1, .x_linear_rate = 1.0F}); // 1 m/s north
    scene.advance(1.0);
    ASSERT_EQ(scene.find(1)->trail.size(), 1U);
    scene.advance(1.0); // only 1 m since the last point: not recorded
    EXPECT_EQ(scene.find(1)->trail.size(), 1U);
    scene.advance(1.0); // 2 m: recorded (step is 1.5 m)
    EXPECT_EQ(scene.find(1)->trail.size(), 2U);
    for (int i = 0; i < 40; ++i) {
        scene.advance(1.0);
    }
    EXPECT_EQ(scene.find(1)->trail.size(), 5U); // capped
    EXPECT_NEAR(length(scene.find(1)->trail.back() - scene.find(1)->enu), 0.0, 2.0);
}

TEST(Scene, InvisibleEntitiesLeaveNoTrail) {
    SceneModel scene(config());
    auto e = top_level(1, 51.5, -0.12, 100.0);
    e.entity_state = EntityState::InactiveStandby;
    scene.apply(e);
    scene.advance(1.0);
    EXPECT_TRUE(scene.find(1)->trail.empty());
}

// ---- HAT/HOT request log and probes -----------------------------------------------------------

TEST(Scene, RemembersRequestsAndProbesWithinTheirLimits) {
    auto cfg = config();
    cfg.probe_history = 3;
    SceneModel scene(cfg);
    for (std::uint16_t i = 0; i < 5; ++i) {
        scene.apply(HatHotRequest{.hat_hot_id = i});
        HatHotProbe probe;
        probe.request.hat_hot_id = i;
        scene.add_probe(probe);
    }
    ASSERT_EQ(scene.recent_requests().size(), 3U);
    EXPECT_EQ(scene.recent_requests().front().hat_hot_id, 2);
    EXPECT_EQ(scene.recent_requests().back().hat_hot_id, 4);
    ASSERT_EQ(scene.probes().size(), 3U);
    EXPECT_EQ(scene.probes().front().request.hat_hot_id, 2);
}

TEST(Scene, LogIsBoundedAndCanBeTapped) {
    auto cfg = config();
    cfg.log_history = 3;
    SceneModel scene(cfg);
    std::vector<std::string> sunk;
    scene.set_log_sink([&sunk](std::string_view line) { sunk.emplace_back(line); });
    for (std::uint16_t i = 0; i < 6; ++i) {
        scene.apply(RateControl{.entity_id = i}); // unknown entity: one line each
    }
    EXPECT_EQ(scene.log().size(), 3U);
    EXPECT_EQ(sunk.size(), 6U);
    EXPECT_NE(scene.log().back().find("entity 5"), std::string::npos);
}

// ---- through the dispatcher -------------------------------------------------------------------

TEST(SceneDispatch, EveryHostPacketReachesTheScene) {
    SceneModel scene(config());
    HatHotResponder responder;
    SceneModel::Handlers dispatcher;
    scene.register_handlers(dispatcher, &responder);

    StaticMessageBuilder<1024> msg(Direction::HostToIg);
    ASSERT_TRUE(msg.add(IgControl{.ig_mode = IgMode::Operate, .host_frame_number = 5}));
    ASSERT_TRUE(msg.add(top_level(1, 51.5, -0.12, 100.0)));
    ASSERT_TRUE(msg.add(ArticulatedPartControl{.entity_id = 1,
                                               .articulated_part_id = 2,
                                               .part_enabled = true,
                                               .yaw_enabled = true,
                                               .yaw = 20.0F}));
    ASSERT_TRUE(msg.add(RateControl{.entity_id = 1, .x_linear_rate = 1.0F}));
    ASSERT_TRUE(msg.add(ComponentControl{.component_id = 4}));
    ASSERT_TRUE(msg.add(CelestialSphereControl{.hour = 9, .date_time_valid = true}));
    ASSERT_TRUE(msg.add(ViewControl{.view_id = 0, .entity_id = 1}));
    ASSERT_TRUE(msg.add(ViewDefinition{.view_id = 0, .near_enabled = true, .near_plane = 2.0F}));
    ASSERT_TRUE(msg.add(HatHotRequest{.hat_hot_id = 8, .update_period = 0}));
    const auto view = MessageView::parse(msg.bytes());
    ASSERT_TRUE(view);
    const DispatchStats stats = dispatcher.dispatch(*view);
    EXPECT_EQ(stats.handled, 9U);
    EXPECT_EQ(stats.errors, 0U);
    EXPECT_EQ(scene.stats().packets, 9U);
    EXPECT_EQ(scene.host_frame_number(), 5U);
    EXPECT_NE(scene.find(1), nullptr);
    EXPECT_TRUE(scene.find(1)->parts.contains(2));
    EXPECT_EQ(scene.components().size(), 1U);
    EXPECT_EQ(scene.celestial().hour, 9);
    EXPECT_FLOAT_EQ(scene.find_view(0)->near_plane, 2.0F);
    EXPECT_EQ(scene.recent_requests().size(), 1U);
    EXPECT_EQ(responder.pending_count(), 1U); // forwarded to the responder
}

TEST(SceneDispatch, WithoutAResponderRequestsAreOnlyRecorded) {
    SceneModel scene(config());
    SceneModel::Handlers dispatcher;
    scene.register_handlers(dispatcher);
    StaticMessageBuilder<256> msg(Direction::HostToIg);
    ASSERT_TRUE(msg.add(IgControl{}));
    ASSERT_TRUE(msg.add(HatHotRequest{.hat_hot_id = 8}));
    const auto view = MessageView::parse(msg.bytes());
    ASSERT_TRUE(view);
    EXPECT_EQ(dispatcher.dispatch(*view).handled, 2U);
    EXPECT_EQ(scene.recent_requests().size(), 1U);
}

TEST(SceneDispatch, UnknownOpcodesAndBadPacketsAreCountedNotFatal) {
    SceneModel scene(config());
    SceneModel::Handlers dispatcher;
    scene.register_handlers(dispatcher);

    StaticMessageBuilder<256> msg(Direction::HostToIg);
    ASSERT_TRUE(msg.add(IgControl{}));
    ASSERT_TRUE(msg.add(test::UserPacket{1})); // opcode 201: not a Host -> IG packet we know
    ASSERT_TRUE(msg.add(test::UserPacket{2}));
    test::Bytes bytes = test::to_vec(msg.bytes());
    // An Entity Control (opcode 2) that claims to be 8 bytes long: a body decode error.
    for (const std::uint8_t b : std::array<std::uint8_t, 8>{2, 8, 0, 0, 0, 0, 0, 0}) {
        bytes.push_back(static_cast<std::byte>(b));
    }
    const auto view = MessageView::parse(bytes);
    ASSERT_TRUE(view);
    const DispatchStats stats = dispatcher.dispatch(*view);
    EXPECT_EQ(stats.unknown, 2U);
    EXPECT_EQ(stats.errors, 1U);
    EXPECT_EQ(scene.stats().unknown_packets, 2U);
    EXPECT_EQ(scene.stats().decode_errors, 1U);
    EXPECT_TRUE(scene.entities().empty());
    // The unknown opcode is logged once, the error each time.
    std::size_t unknown_lines = 0;
    for (const auto& line : scene.log()) {
        unknown_lines += line.contains("unknown opcode 201") ? 1 : 0;
    }
    EXPECT_EQ(unknown_lines, 1U);
    EXPECT_NE(scene.log().back().find("decode error"), std::string::npos);
}

} // namespace
