#include "mock_ig/mock_ig.hpp"
#include "mock_ig/scripted_host.hpp"

#include <array>
#include <cigi/message/builder.hpp>
#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <type_traits>
#include <vector>

namespace {

using namespace cigi;
using namespace cigi::mock;

constexpr double dt = 1.0 / 60.0;

SceneConfig scene_config(const ScriptedHostConfig& host) {
    SceneConfig c;
    c.origin = host.origin;
    return c;
}

/// Drives a ScriptedHost through a MockIg for N frames, collecting every reply.
struct Session {
    explicit Session(const ScriptedHostConfig& cfg = {}) : host(cfg), ig(scene_config(cfg)) {}

    void frames(std::uint32_t n) {
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto msg = host.next_message();
            ASSERT_TRUE(msg) << to_string(msg.error().code);
            const auto stats = ig.ingest(*msg);
            ASSERT_TRUE(stats);
            EXPECT_EQ(stats->errors, 0U);
            EXPECT_EQ(stats->unknown, 0U);
            EXPECT_EQ(stats->ignored, 0U);
            for (const auto& reply : ig.end_frame(dt)) {
                replies.emplace_back(ig.ig_frame() - 1, reply);
            }
        }
    }

    ScriptedHost host;
    MockIg ig;
    std::vector<std::pair<std::uint32_t, HatHotResponder::Reply>> replies;
};

void expect_vec(const Vec3d& a, const Vec3d& b, double eps) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

// ---- the first message ------------------------------------------------------------------------

TEST(ScriptedHost, FirstFrameContainsTheExpectedPackets) {
    ScriptedHost host;
    std::vector<std::byte> storage(4096);
    MessageBuilder builder(Direction::HostToIg, storage);
    ASSERT_TRUE(host.build_frame(builder));
    EXPECT_EQ(host.frame(), 1U);

    const auto view = MessageView::parse(builder.bytes());
    ASSERT_TRUE(view);
    ASSERT_TRUE(view->validate_all());
    std::map<std::uint8_t, int> by_opcode;
    for (const auto& item : *view) {
        ASSERT_TRUE(item);
        ++by_opcode[item->opcode];
    }
    EXPECT_EQ(by_opcode[IgControl::opcode], 1);
    EXPECT_EQ(by_opcode[CelestialSphereControl::opcode], 1);
    EXPECT_EQ(by_opcode[EntityControl::opcode],
              1 + 3 + 1 + 1); // ownship, 3 vehicles, trailer, cargo
    EXPECT_EQ(by_opcode[ArticulatedPartControl::opcode], 1 + 2); // propeller, turret, barrel
    EXPECT_EQ(by_opcode[RateControl::opcode], 1);                // propeller spin
    EXPECT_EQ(by_opcode[ViewDefinition::opcode], 1);
    EXPECT_EQ(by_opcode[ViewControl::opcode], 1);
    EXPECT_EQ(by_opcode[HatHotRequest::opcode], 2); // the two periodic requests
    EXPECT_EQ(by_opcode.size(), 8U);

    // Everything the host sends is also valid by the ICD's ranges.
    for (const auto& item : *view) {
        const auto decoded = decode_any<HostToIgPackets>(*item, view->direction());
        ASSERT_TRUE(decoded);
        std::visit(
            [](const auto& p) {
                if constexpr (!std::is_same_v<std::remove_cvref_t<decltype(p)>, UnknownPacket>) {
                    EXPECT_TRUE(validate(p)) << static_cast<int>(p.opcode);
                } else {
                    ADD_FAILURE() << "unknown packet";
                }
            },
            *decoded);
    }
}

TEST(ScriptedHost, LaterFramesAreSmaller) {
    ScriptedHost host;
    const auto first = host.next_message();
    ASSERT_TRUE(first);
    const std::size_t first_size = first->size();
    const auto second = host.next_message();
    ASSERT_TRUE(second);
    EXPECT_LT(second->size(), first_size); // one-time packets are not repeated
    EXPECT_EQ(host.frame(), 2U);
}

TEST(ScriptedHost, IsDeterministic) {
    ScriptedHostConfig cfg;
    cfg.extra_entities = 7;
    ScriptedHost a(cfg);
    ScriptedHost b(cfg);
    for (int i = 0; i < 70; ++i) {
        const auto ma = a.next_message();
        const auto mb = b.next_message();
        ASSERT_TRUE(ma);
        ASSERT_TRUE(mb);
        ASSERT_EQ(ma->size(), mb->size());
        EXPECT_TRUE(std::equal(ma->begin(), ma->end(), mb->begin()));
    }
}

TEST(ScriptedHost, ReportsBufferTooSmallAndDoesNotAdvance) {
    ScriptedHost host;
    std::vector<std::byte> tiny(100);
    MessageBuilder builder(Direction::HostToIg, tiny);
    const auto r = host.build_frame(builder);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, ErrorCode::BufferTooSmall);
    EXPECT_EQ(host.frame(), 0U);
}

// ---- ground truth after 600 frames ------------------------------------------------------------

TEST(ScriptedHost, SixHundredFramesProduceTheExpectedWorld) {
    Session run;
    run.frames(600);
    const SceneModel& scene = run.ig.scene();
    EXPECT_EQ(scene.stats().decode_errors, 0U);
    EXPECT_EQ(scene.stats().unknown_entity, 0U);
    EXPECT_EQ(scene.stats().invalid_parent, 0U);
    EXPECT_EQ(scene.entities().size(), run.host.entity_count());
    EXPECT_EQ(scene.entities().size(), 6U);
    EXPECT_EQ(scene.frame_count(), 600U);
    EXPECT_EQ(run.ig.ig_frame(), 600U);
    EXPECT_EQ(scene.host_frame_number(), 599U);

    // The last frame sent was number 599, at t = 599 / 60 s. Expected positions come from the
    // closed-form circles (independently worked out: see the numbers in the comments).
    const double t = 599 * dt;

    const Entity& ownship = *scene.find(0);
    // East 761.4614 m, north 1292.3531 m, heading 120.5068 degrees.
    expect_vec(ownship.enu - Vec3d{0, 0, ownship.enu.z}, {761.4613690, 1292.3531187, 0.0}, 1e-5);
    expect_vec(run.host.ownship_enu(t), {761.4613690, 1292.3531187, 0.0}, 1e-5);
    EXPECT_NEAR(ownship.world_attitude.yaw_deg, 120.5068195, 1e-4);
    EXPECT_NEAR(run.host.ownship_heading_deg(t), 120.5068195, 1e-6);
    EXPECT_NEAR(ownship.world_attitude.roll_deg, 23.5128423, 1e-4); // atan(v^2 / g r)
    EXPECT_NEAR(ownship.agl_m, 300.0, 1e-6);                        // held 300 m above the hills
    EXPECT_NEAR(ownship.position.alt_m - ownship.terrain_m, 300.0, 1e-6);
    EXPECT_TRUE(ownship.visible);

    // Vehicles: east/north/heading from the loop's closed form.
    const Vec3d veh[3] = {
        {798.4850770, 35.8581391, 0}, {757.5705157, 45.2327269, 0}, {715.8279335, 49.6419262, 0}};
    const double heading[3] = {106.3429390, 99.4674455, 92.5919519};
    for (int k = 1; k <= 3; ++k) {
        const Entity& v = *scene.find(static_cast<std::uint16_t>(k));
        expect_vec(v.enu - Vec3d{0, 0, v.enu.z}, veh[k - 1], 1e-5);
        expect_vec(ScriptedHost::vehicle_enu(k, t), veh[k - 1], 1e-5);
        EXPECT_NEAR(v.world_attitude.yaw_deg, heading[k - 1], 1.0); // conformal: slope nudges it
        EXPECT_NEAR(v.agl_m, 0.0, 1e-6);                            // on the ground
        EXPECT_NEAR(ScriptedHost::vehicle_heading_deg(k, t), heading[k - 1], 1e-6);
    }

    // The trailer rides 8 m behind vehicle 3; the cargo is still on vehicle 2.
    const Entity& trailer = *scene.find(4);
    EXPECT_TRUE(trailer.attached);
    EXPECT_EQ(trailer.parent_id, 3);
    EXPECT_NEAR(length(trailer.enu - scene.find(3)->enu), 8.0, 1e-9);
    const Entity& cargo = *scene.find(5);
    EXPECT_TRUE(cargo.attached);
    EXPECT_EQ(cargo.parent_id, 2);
    EXPECT_NEAR(length(cargo.enu - scene.find(2)->enu), 2.5, 1e-9);
    EXPECT_GT(cargo.enu.z, scene.find(2)->enu.z); // above it

    // Turret and barrel on vehicle 1: yaw 36 deg/s * t, barrel pitch 15 + 15 sin(2 pi t / 5).
    const auto& parts = scene.find(1)->parts;
    EXPECT_NEAR(parts.at(1).attitude.yaw_deg, 359.4, 1e-3);
    EXPECT_NEAR(parts.at(2).attitude.yaw_deg, 359.4, 1e-3);
    EXPECT_NEAR(parts.at(2).attitude.pitch_deg, 14.6858637, 1e-4);
    EXPECT_NEAR(parts.at(1).offset.z, -1.4, 1e-6);

    // Celestial: started 10:00 UTC, one minute per second: frame 599 -> 10:09 on 21 June 2026.
    EXPECT_TRUE(scene.celestial().received);
    EXPECT_EQ(scene.celestial().hour, 10);
    EXPECT_EQ(scene.celestial().minute, 9);
    ASSERT_TRUE(scene.celestial().date.has_value());
    EXPECT_EQ(static_cast<unsigned>(scene.celestial().date->month()), 6U);
    EXPECT_EQ(static_cast<unsigned>(scene.celestial().date->day()), 21U);
    EXPECT_TRUE(scene.celestial().sun);
    EXPECT_FLOAT_EQ(scene.celestial().star_field_intensity, 80.0F);

    // The view is attached to the ownship at the pilot's eye.
    const ViewState* view = scene.find_view(0);
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(view->controlled);
    EXPECT_EQ(view->entity_id, 0);
    EXPECT_DOUBLE_EQ(view->offset.x, 1.0);
    EXPECT_DOUBLE_EQ(view->offset.z, -1.0);
    EXPECT_DOUBLE_EQ(view->horizontal_fov_deg(), 70.0);
    EXPECT_DOUBLE_EQ(view->vertical_fov_deg(), 42.0);
    ASSERT_TRUE(scene.view_pose(0).has_value());
    expect_vec(scene.view_pose(0)->position_enu,
               ownship.enu + (ownship.body_to_enu * Vec3d{1, 0, -1}), 1e-9);

    EXPECT_FALSE(scene.find(0)->trail.empty());
}

TEST(ScriptedHost, PropellerSpinsFromAnIGSideRate) {
    Session run;
    run.frames(100);
    // 720 deg/s for 100 frames of 1/60 s = 1200 degrees = 120 modulo a full turn (roll is
    // reported in -180..180).
    EXPECT_NEAR(run.ig.scene().find(0)->parts.at(1).attitude.roll_deg, 120.0, 1e-6);
    EXPECT_NEAR(run.ig.scene().find(0)->parts.at(1).offset.x, 2.2, 1e-6);
}

TEST(ScriptedHost, CargoDetachesAndIsCarriedByItsRate) {
    ScriptedHostConfig cfg;
    cfg.detach_s = 5.0;
    Session run(cfg);
    EXPECT_EQ(run.host.detach_frame(), 300U);
    run.frames(300); // frames 0..299: still attached
    EXPECT_TRUE(run.ig.scene().find(5)->attached);
    run.frames(1); // frame 300: the detach
    const Entity& cargo = *run.ig.scene().find(5);
    EXPECT_FALSE(cargo.attached);
    EXPECT_EQ(cargo.ground_clamp, GroundClamp::NonConformal);
    EXPECT_NEAR(cargo.agl_m, 30.0, 1e-6);
    // It starts where vehicle 2 was at t = 5 s (ground position) ...
    const Vec3d expected_start = ScriptedHost::vehicle_enu(2, 5.0);
    // ... and has already been extrapolated by one frame (8 m/s north, 4 m/s east).
    expect_vec(cargo.enu - Vec3d{0, 0, cargo.enu.z},
               expected_start + Vec3d{4.0 * dt, 8.0 * dt, 0.0}, 1e-3);

    const Vec3d before = cargo.enu;
    run.frames(120); // two seconds with no Entity Control for the cargo at all
    const Vec3d moved = run.ig.scene().find(5)->enu - before;
    expect_vec(moved - Vec3d{0, 0, moved.z}, {8.0, 16.0, 0.0}, 5e-3);
    EXPECT_EQ(run.ig.scene().stats().unknown_entity, 0U);
}

TEST(ScriptedHost, CelestialTimeRollsOverMidnight) {
    ScriptedHostConfig cfg;
    cfg.start_utc_hours = 23.9; // 23:54; 599 frames later it is 00:03 the next day
    Session run(cfg);
    run.frames(600);
    EXPECT_EQ(run.ig.scene().celestial().hour, 0);
    EXPECT_EQ(run.ig.scene().celestial().minute, 3);
    EXPECT_EQ(static_cast<unsigned>(run.ig.scene().celestial().date->day()), 22U);
}

// ---- HAT/HOT traffic ---------------------------------------------------------------------------

TEST(ScriptedHost, HatHotRequestsAreAnsweredAsScheduled) {
    Session run;
    run.frames(120);
    std::map<std::uint16_t, std::vector<std::uint32_t>> frames_by_id;
    for (const auto& [frame, reply] : run.replies) {
        std::visit([&](const auto& r) { frames_by_id[r.hat_hot_id].push_back(frame); }, reply);
    }
    EXPECT_EQ(frames_by_id[1], std::vector<std::uint32_t>{1});
    EXPECT_EQ(frames_by_id[2], std::vector<std::uint32_t>{30});
    EXPECT_EQ(frames_by_id[3], (std::vector<std::uint32_t>{0, 30, 60, 90}));
    EXPECT_EQ(frames_by_id[4], (std::vector<std::uint32_t>{0, 15, 30, 45, 60, 75, 90, 105}));
    EXPECT_EQ(frames_by_id[5], std::vector<std::uint32_t>{90});
    EXPECT_EQ(run.ig.scene().probes().size(), 15U); // 1 + 1 + 4 + 8 + 1

    for (const auto& [frame, reply] : run.replies) {
        if (const auto* r = std::get_if<HatHotResponse>(&reply)) {
            switch (r->hat_hot_id) {
            case 1: // 500 m over the origin, terrain 100 m there
                EXPECT_TRUE(r->valid);
                EXPECT_EQ(r->response_type, HatHotResponseType::Hat);
                EXPECT_NEAR(r->height, 400.0, 1e-6);
                EXPECT_EQ(r->host_frame_lsn, 0); // one-shot
                break;
            case 2: // HOT 300 m east, 120 m north of the origin: 102.8178 m
                EXPECT_TRUE(r->valid);
                EXPECT_EQ(r->response_type, HatHotResponseType::Hot);
                EXPECT_NEAR(r->height, 102.8178285, 1e-5);
                break;
            case 4: // 2 m above vehicle 1 (on the ground)
                EXPECT_TRUE(r->valid);
                EXPECT_NEAR(r->height, 2.0, 0.05);
                EXPECT_EQ(r->host_frame_lsn, frame & 0xFU);
                break;
            case 5: // 2 degrees north: outside the database
                EXPECT_FALSE(r->valid);
                break;
            default:
                ADD_FAILURE() << "unexpected response id " << r->hat_hot_id;
            }
        } else {
            const auto& x = std::get<HatHotExtendedResponse>(reply);
            EXPECT_EQ(x.hat_hot_id, 3);
            EXPECT_TRUE(x.valid);
            EXPECT_NEAR(x.hat, 300.0, 1e-4); // the ownship flies 300 m above the ground
            EXPECT_EQ(x.host_frame_lsn, frame & 0xFU);
            EXPECT_TRUE(validate(x));
        }
    }
}

TEST(ScriptedHost, ARepliesMessageCanBeBuiltAndDecoded) {
    Session run;
    run.frames(1);
    const auto msg = run.host.next_message();
    ASSERT_TRUE(msg);
    ASSERT_TRUE(run.ig.ingest(*msg));
    const auto replies = run.ig.end_frame(dt);
    ASSERT_EQ(replies.size(), 1U); // id 1 (frame 1); the periodic ones were answered in frame 0
    std::array<std::byte, 512> storage{};
    MessageBuilder builder(Direction::IgToHost, storage);
    ASSERT_TRUE(run.ig.build_reply(builder, replies));
    const auto view = MessageView::parse(builder.bytes());
    ASSERT_TRUE(view);
    ASSERT_TRUE(view->validate_all());
    std::size_t count = 0;
    const DispatchStats stats =
        dispatch<IgToHostPackets>(*view, overloaded{[&](const StartOfFrame& s) {
                                                        EXPECT_EQ(s.ig_frame_number, 1U);
                                                        EXPECT_EQ(s.last_host_frame_number, 1U);
                                                        EXPECT_EQ(s.ig_mode, IgMode::Operate);
                                                        ++count;
                                                    },
                                                    [&](const HatHotResponse& r) {
                                                        EXPECT_EQ(r.hat_hot_id, 1);
                                                        ++count;
                                                    }});
    EXPECT_EQ(stats.errors, 0U);
    EXPECT_EQ(count, 2U);
}

// ---- stress -----------------------------------------------------------------------------------

TEST(ScriptedHost, TwoHundredExtraEntitiesGoThroughTheCodec) {
    ScriptedHostConfig cfg;
    cfg.extra_entities = 200;
    Session run(cfg);
    run.frames(30);
    const SceneModel& scene = run.ig.scene();
    EXPECT_EQ(scene.entities().size(), 206U);
    EXPECT_EQ(run.host.entity_count(), 206U);
    ASSERT_NE(scene.find(100), nullptr);
    ASSERT_NE(scene.find(299), nullptr);
    EXPECT_EQ(scene.find(300), nullptr);
    EXPECT_EQ(scene.stats().invalid_parent, 0U);
    // Each of the three kinds is present and placed on or above the hills.
    for (std::uint16_t id = 100; id < 300; ++id) {
        const Entity& e = *scene.find(id);
        EXPECT_TRUE(e.visible);
        EXPECT_GE(e.agl_m, -1e-6);
        switch (id % 3) {
        case 1: // 100 + i, i % 3 == 0 -> aircraft
            EXPECT_EQ(e.entity_type, 110);
            EXPECT_GE(e.agl_m, 50.0 - 1e-6);
            break;
        case 2:
            EXPECT_EQ(e.entity_type, 210);
            EXPECT_NEAR(e.agl_m, 0.0, 1e-6);
            break;
        default:
            EXPECT_EQ(e.entity_type, 50);
            EXPECT_NEAR(e.agl_m, 20.0, 1e-6);
        }
    }
}

} // namespace
