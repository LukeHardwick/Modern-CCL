#include "mock_ig/hat_hot.hpp"

#include <gtest/gtest.h>
#include <variant>

namespace {

using namespace cigi;
using namespace cigi::mock;

// The hand-computed terrain values at (51.5025, -0.1175): height 120 m, material 1 (grass), normal
// azimuth 121.968537 and elevation 83.911576 degrees (see terrain_test.cpp).
constexpr double test_lat = 51.5025;
constexpr double test_lon = -0.1175;

SceneConfig config() {
    SceneConfig c;
    c.origin = {51.5, -0.12, 0.0};
    c.bounds = DatabaseBounds::around(c.origin, 0.25);
    return c;
}

HatHotRequest geodetic(std::uint16_t id, HatHotRequestType type, std::uint8_t period = 0) {
    return {
        .hat_hot_id = id,
        .request_type = type,
        .update_period = period,
        .latitude_or_x = test_lat,
        .longitude_or_y = test_lon,
        .altitude_or_z = 500.0,
    };
}

const HatHotResponse& as_response(const HatHotResponder::Reply& r) {
    return std::get<HatHotResponse>(r);
}
const HatHotExtendedResponse& as_extended(const HatHotResponder::Reply& r) {
    return std::get<HatHotExtendedResponse>(r);
}

TEST(HatHot, OneShotHatIsAnsweredOnce) {
    SceneModel scene(config());
    HatHotResponder responder;
    responder.submit(geodetic(11, HatHotRequestType::Hat));
    EXPECT_EQ(responder.pending_count(), 1U);
    const auto replies = responder.collect(scene, 5);
    ASSERT_EQ(replies.size(), 1U);
    const auto& r = as_response(replies[0]);
    EXPECT_EQ(r.hat_hot_id, 11);
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.response_type, HatHotResponseType::Hat);
    EXPECT_NEAR(r.height, 380.0, 1e-6); // 500 m altitude - 120 m terrain
    EXPECT_EQ(r.host_frame_lsn, 0);     // ignored for one-shots
    EXPECT_EQ(responder.pending_count(), 0U);
    EXPECT_EQ(responder.periodic_count(), 0U);
    EXPECT_TRUE(responder.collect(scene, 6).empty());
}

TEST(HatHot, HotIgnoresTheRequestedAltitude) {
    SceneModel scene(config());
    HatHotResponder responder;
    auto req = geodetic(12, HatHotRequestType::Hot);
    responder.submit(req);
    req.hat_hot_id = 13;
    req.altitude_or_z = -9999.0;
    responder.submit(req);
    const auto replies = responder.collect(scene, 0);
    ASSERT_EQ(replies.size(), 2U);
    EXPECT_EQ(as_response(replies[0]).response_type, HatHotResponseType::Hot);
    EXPECT_NEAR(as_response(replies[0]).height, 120.0, 1e-6);
    EXPECT_NEAR(as_response(replies[1]).height, 120.0, 1e-6);
}

TEST(HatHot, ExtendedCarriesMaterialAndNormal) {
    SceneModel scene(config());
    HatHotResponder responder;
    responder.submit(geodetic(14, HatHotRequestType::Extended));
    const auto replies = responder.collect(scene, 0);
    ASSERT_EQ(replies.size(), 1U);
    const auto& r = as_extended(replies[0]);
    EXPECT_EQ(r.hat_hot_id, 14);
    EXPECT_TRUE(r.valid);
    EXPECT_NEAR(r.hat, 380.0, 1e-6);
    EXPECT_NEAR(r.hot, 120.0, 1e-6);
    EXPECT_EQ(r.material_code, 1U); // grass
    EXPECT_NEAR(static_cast<double>(r.normal_azimuth), 121.968537, 1e-3);
    EXPECT_NEAR(static_cast<double>(r.normal_elevation), 83.911576, 1e-3);
    EXPECT_TRUE(validate(r));
}

TEST(HatHot, OutsideTheDatabaseIsInvalid) {
    SceneModel scene(config());
    HatHotResponder responder;
    auto req = geodetic(15, HatHotRequestType::Hat);
    req.latitude_or_x = 53.0;
    responder.submit(req);
    req.hat_hot_id = 16;
    req.request_type = HatHotRequestType::Extended;
    responder.submit(req);
    const auto replies = responder.collect(scene, 0);
    ASSERT_EQ(replies.size(), 2U);
    EXPECT_FALSE(as_response(replies[0]).valid);
    EXPECT_DOUBLE_EQ(as_response(replies[0]).height, 0.0);
    EXPECT_FALSE(as_extended(replies[1]).valid);
    EXPECT_EQ(as_extended(replies[1]).material_code, 0U);
    EXPECT_FALSE(scene.probes().back().valid);
    EXPECT_DOUBLE_EQ(scene.probes().back().point.lat_deg, 53.0);
}

TEST(HatHot, PeriodicRequestsAnswerEveryNthFrameWithTheHostFrameLsn) {
    SceneModel scene(config());
    HatHotResponder responder;
    responder.submit(geodetic(20, HatHotRequestType::Hat, 3));
    EXPECT_EQ(responder.periodic_count(), 1U);
    std::vector<int> answered;
    for (std::uint32_t frame = 0; frame < 10; ++frame) {
        scene.apply(IgControl{.host_frame_number = 0x1F0 + frame}); // LSN = frame
        const auto replies = responder.collect(scene, frame);
        if (!replies.empty()) {
            answered.push_back(static_cast<int>(frame));
            ASSERT_EQ(replies.size(), 1U);
            EXPECT_EQ(as_response(replies[0]).host_frame_lsn, frame);
            EXPECT_TRUE(scene.probes().back().periodic);
        }
    }
    EXPECT_EQ(answered, (std::vector<int>{0, 3, 6, 9}));
    EXPECT_EQ(responder.periodic_count(), 1U); // keeps going
}

TEST(HatHot, PeriodOneAnswersEveryFrameAndLargePeriodsWork) {
    SceneModel scene(config());
    HatHotResponder responder;
    responder.submit(geodetic(21, HatHotRequestType::Hat, 1));
    responder.submit(geodetic(22, HatHotRequestType::Hot, 255));
    std::size_t first = 0;
    std::size_t second = 0;
    for (std::uint32_t frame = 0; frame < 300; ++frame) {
        for (const auto& r : responder.collect(scene, frame)) {
            (as_response(r).hat_hot_id == 21 ? first : second) += 1;
        }
    }
    EXPECT_EQ(first, 300U);
    EXPECT_EQ(second, 2U); // frames 0 and 255
}

TEST(HatHot, AnUpdatePeriodOfZeroCancelsAPeriodicRequestAndIsAnsweredOnce) {
    SceneModel scene(config());
    HatHotResponder responder;
    responder.submit(geodetic(23, HatHotRequestType::Hat, 2));
    EXPECT_EQ(responder.collect(scene, 0).size(), 1U);
    responder.submit(geodetic(23, HatHotRequestType::Hat, 0));
    EXPECT_EQ(responder.periodic_count(), 0U);
    EXPECT_EQ(responder.collect(scene, 1).size(), 1U); // the one-shot answer
    EXPECT_TRUE(responder.collect(scene, 2).empty());
    EXPECT_TRUE(responder.collect(scene, 3).empty());
}

TEST(HatHot, ResendingAPeriodicRequestReplacesAndRestartsIt) {
    SceneModel scene(config());
    HatHotResponder responder;
    responder.submit(geodetic(24, HatHotRequestType::Hat, 5));
    EXPECT_EQ(responder.collect(scene, 0).size(), 1U);
    EXPECT_TRUE(responder.collect(scene, 1).empty());
    auto again = geodetic(24, HatHotRequestType::Hot, 2); // new type and period, same ID
    responder.submit(again);
    EXPECT_EQ(responder.periodic_count(), 1U);
    const auto replies = responder.collect(scene, 2);
    ASSERT_EQ(replies.size(), 1U); // restarted: due immediately
    EXPECT_EQ(as_response(replies[0]).response_type, HatHotResponseType::Hot);
    EXPECT_TRUE(responder.collect(scene, 3).empty());
    EXPECT_EQ(responder.collect(scene, 4).size(), 1U);
}

TEST(HatHot, RepliesComeOneShotsFirstThenPeriodicById) {
    SceneModel scene(config());
    HatHotResponder responder;
    responder.submit(geodetic(40, HatHotRequestType::Hat, 1));
    responder.submit(geodetic(30, HatHotRequestType::Hat, 1));
    responder.submit(geodetic(99, HatHotRequestType::Hat));
    responder.submit(geodetic(98, HatHotRequestType::Hat));
    const auto replies = responder.collect(scene, 0);
    ASSERT_EQ(replies.size(), 4U);
    EXPECT_EQ(as_response(replies[0]).hat_hot_id, 99);
    EXPECT_EQ(as_response(replies[1]).hat_hot_id, 98);
    EXPECT_EQ(as_response(replies[2]).hat_hot_id, 30);
    EXPECT_EQ(as_response(replies[3]).hat_hot_id, 40);
}

TEST(HatHot, EntityRelativePointFollowsTheEntity) {
    SceneModel scene(config());
    HatHotResponder responder;
    EntityControl e;
    e.entity_id = 3;
    e.entity_state = EntityState::Active;
    e.latitude_or_x = test_lat;
    e.longitude_or_y = test_lon;
    e.altitude_or_z = 500.0;
    e.yaw = 90.0F; // facing east
    scene.apply(e);

    // 50 m above the reference point (z is down): straight up, same terrain.
    HatHotRequest req;
    req.hat_hot_id = 50;
    req.coordinate_system = HatHotCoordinateSystem::Entity;
    req.entity_id = 3;
    req.altitude_or_z = -50.0;
    responder.submit(req);
    auto replies = responder.collect(scene, 0);
    ASSERT_EQ(replies.size(), 1U);
    EXPECT_NEAR(as_response(replies[0]).height, 430.0, 1e-6); // 550 - 120

    // 100 m ahead of an east-facing entity is 100 m east: the probe point shows it.
    req.hat_hot_id = 51;
    req.latitude_or_x = 100.0;
    req.altitude_or_z = 0.0;
    responder.submit(req);
    replies = responder.collect(scene, 1);
    ASSERT_EQ(replies.size(), 1U);
    const HatHotProbe& probe = scene.probes().back();
    EXPECT_TRUE(probe.valid);
    EXPECT_NEAR(probe.point.lat_deg, test_lat, 1e-12);
    EXPECT_NEAR(probe.point.lon_deg, test_lon + (100.0 / meters_per_degree_lon(51.5)), 1e-9);
    EXPECT_NEAR(probe.point.alt_m, 500.0, 1e-9);
    EXPECT_NEAR(probe.hat_m, 500.0 - terrain_height(probe.point.lat_deg, probe.point.lon_deg),
                1e-9);
}

TEST(HatHot, EntityRelativeRequestForAMissingEntity) {
    SceneModel scene(config());
    HatHotResponder responder;
    HatHotRequest req;
    req.hat_hot_id = 60;
    req.coordinate_system = HatHotCoordinateSystem::Entity;
    req.entity_id = 77;
    responder.submit(req);
    const auto replies = responder.collect(scene, 0);
    ASSERT_EQ(replies.size(), 1U);
    EXPECT_FALSE(as_response(replies[0]).valid); // answered, but invalid
}

TEST(HatHot, PeriodicEntityRequestEndsWhenTheEntityIsDestroyed) {
    SceneModel scene(config());
    HatHotResponder responder;
    EntityControl e;
    e.entity_id = 3;
    e.entity_state = EntityState::Active;
    e.latitude_or_x = test_lat;
    e.longitude_or_y = test_lon;
    scene.apply(e);
    HatHotRequest req;
    req.hat_hot_id = 61;
    req.update_period = 1;
    req.coordinate_system = HatHotCoordinateSystem::Entity;
    req.entity_id = 3;
    responder.submit(req);
    EXPECT_EQ(responder.collect(scene, 0).size(), 1U);
    EXPECT_EQ(responder.collect(scene, 1).size(), 1U);
    e.entity_state = EntityState::Destroyed;
    scene.apply(e);
    EXPECT_TRUE(responder.collect(scene, 2).empty());
    EXPECT_EQ(responder.periodic_count(), 0U);
    // A geodetic periodic request never depends on an entity.
    responder.submit(geodetic(62, HatHotRequestType::Hat, 1));
    EXPECT_EQ(responder.collect(scene, 3).size(), 1U);
    EXPECT_EQ(responder.collect(scene, 4).size(), 1U);
}

TEST(HatHot, ProbesRecordEveryAnswer) {
    SceneModel scene(config());
    HatHotResponder responder;
    responder.submit(geodetic(70, HatHotRequestType::Extended));
    (void)responder.collect(scene, 123);
    ASSERT_EQ(scene.probes().size(), 1U);
    const HatHotProbe& p = scene.probes().front();
    EXPECT_EQ(p.ig_frame, 123U);
    EXPECT_EQ(p.request.hat_hot_id, 70);
    EXPECT_FALSE(p.periodic);
    EXPECT_TRUE(p.valid);
    EXPECT_NEAR(p.hat_m, 380.0, 1e-6);
    EXPECT_NEAR(p.hot_m, 120.0, 1e-6);
    EXPECT_EQ(p.material, Material::Grass);
    EXPECT_NEAR(p.normal.elevation_deg, 83.911576, 1e-5);
}

TEST(HatHot, CollectResolvesTheScene) {
    SceneModel scene(config());
    HatHotResponder responder;
    EntityControl e;
    e.entity_id = 3;
    e.entity_state = EntityState::Active;
    e.latitude_or_x = test_lat;
    e.longitude_or_y = test_lon;
    e.altitude_or_z = 700.0;
    scene.apply(e); // not resolved yet
    HatHotRequest req;
    req.hat_hot_id = 80;
    req.coordinate_system = HatHotCoordinateSystem::Entity;
    req.entity_id = 3;
    responder.submit(req);
    const auto replies = responder.collect(scene, 0);
    ASSERT_EQ(replies.size(), 1U);
    EXPECT_NEAR(as_response(replies[0]).height, 580.0, 1e-6);
}

} // namespace
