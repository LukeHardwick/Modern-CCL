#include "support.hpp"

#include <gtest/gtest.h>

namespace {

using namespace cigi;
using namespace cigi::test;

/// An IG already in `mode` (no callback: requests are applied at once).
IgSession ig_in(IgMode mode, const FakeClock& clock) {
    IgSession ig{config_with(clock)};
    ig.set_mode(mode);
    return ig;
}

// ---- Start of Frame written by begin_frame ------------------------------------------------------

TEST(IgSession, BeginFrameWritesStartOfFrameGolden) {
    const FakeClock clock{0x0203};
    auto config = config_with(clock);
    config.initial_frame_number = 10;
    IgSession ig{config};
    ig.set_mode(IgMode::Operate);
    ig.set_database(-3);
    ig.set_status(9);
    ig.set_earth_reference_model(EarthReferenceModel::HostDefined);
    ASSERT_TRUE(ig.receive(control_message(7, IgMode::Operate)));

    ASSERT_TRUE(ig.begin_frame());
    const auto message = ig.finish_frame();

    // ICD 4.2.1: opcode 101, size 24, major 3, database -3, status 9, minor 3 | ERM | timestamp
    // valid | Operate, magic, IG frame 10, timestamp 0x0203, last Host frame 7.
    const auto little = bytes_of(101, 24, 3, 0xFD, 9, 0x3D, 0x00, 0x80, 10, 0, 0, 0, 0x03, 0x02, 0,
                                 0, 7, 0, 0, 0, 0, 0, 0, 0);
    const auto big = bytes_of(101, 24, 3, 0xFD, 9, 0x3D, 0x80, 0x00, 0, 0, 0, 10, 0, 0, 0x02, 0x03,
                              0, 0, 0, 7, 0, 0, 0, 0);
    const auto& expected = native_of(little, big);
    ASSERT_EQ(message.size(), expected.size());
    EXPECT_TRUE(std::equal(message.begin(), message.end(), expected.begin()));
    EXPECT_EQ(ig.ig_frame_number(), 10U);
    EXPECT_EQ(ig.stats().messages_sent, 1U);
}

TEST(IgSession, FrameNumbersCountUpAndWrap) {
    const FakeClock clock;
    auto config = config_with(clock);
    config.initial_frame_number = 0xFFFFFFFFU;
    IgSession ig{config};
    std::vector<std::uint32_t> seen;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(ig.begin_frame());
        seen.push_back(first_as<StartOfFrame>(ig.finish_frame()).ig_frame_number);
    }
    EXPECT_EQ(seen, (std::vector<std::uint32_t>{0xFFFFFFFFU, 0U, 1U}));
}

TEST(IgSession, FinishWithoutBeginAndBeginTwice) {
    const FakeClock clock;
    IgSession ig{config_with(clock)};
    EXPECT_TRUE(ig.finish_frame().empty());
    ASSERT_TRUE(ig.begin_frame());
    EXPECT_TRUE(ig.frame_open());
    ASSERT_TRUE(ig.begin_frame());
    EXPECT_EQ(first_as<StartOfFrame>(ig.finish_frame()).ig_frame_number, 1U);
}

// ---- sending rules -----------------------------------------------------------------------------

TEST(IgSession, SendsOnlyStartOfFrameUntilOperating) {
    const FakeClock clock;
    IgSession ig{config_with(clock)};
    EXPECT_EQ(ig.add(HatHotResponse{}).error().code, ErrorCode::UnexpectedFirstPacket);
    ASSERT_TRUE(ig.begin_frame());
    EXPECT_EQ(ig.add(HatHotResponse{}).error().code, ErrorCode::NotOperational);
    ig.set_mode(IgMode::OfflineMaintenance);
    EXPECT_EQ(ig.add(HatHotResponse{}).error().code, ErrorCode::NotOperational);
    ig.set_mode(IgMode::Operate);
    EXPECT_TRUE(ig.add(HatHotResponse{}));
    ig.set_mode(IgMode::Debug);
    EXPECT_TRUE(ig.add(HatHotExtendedResponse{}));
    EXPECT_EQ(ig.add(StartOfFrame{}).error().code, ErrorCode::DuplicateFramePacket);
    EXPECT_EQ(ig.finish_frame().size(),
              24U + HatHotResponse::wire_size + HatHotExtendedResponse::wire_size);
    EXPECT_EQ(ig.stats().rejected_packets, 4U);
}

TEST(IgSession, EnforcementAndValidationAreConfigurable) {
    const FakeClock clock;
    auto config = config_with(clock);
    config.enforce_startup_sequence = false;
    config.validate_outgoing = true;
    IgSession ig{config};
    ASSERT_TRUE(ig.begin_frame());
    EXPECT_TRUE(ig.add(HatHotResponse{}));
    HatHotResponse bad;
    bad.response_type = static_cast<HatHotResponseType>(3);
    EXPECT_EQ(ig.add(bad).error().code, ErrorCode::InvalidFieldValue);
}

TEST(IgSession, OversizePacketIsBufferTooSmall) {
    const FakeClock clock;
    auto config = config_with(clock);
    config.max_message_bytes = 24;
    IgSession ig{config};
    ig.set_mode(IgMode::Operate);
    ASSERT_TRUE(ig.begin_frame());
    EXPECT_EQ(ig.add(HatHotResponse{}).error().code, ErrorCode::BufferTooSmall);
}

// ---- mode requests -----------------------------------------------------------------------------

TEST(IgSession, WithoutACallbackModeRequestsAreAppliedAtOnce) {
    const FakeClock clock;
    IgSession ig{config_with(clock)};
    EXPECT_EQ(ig.mode(), IgMode::ResetStandby);
    const auto r = ig.receive(control_message(1, IgMode::Operate));
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->mode_changed);
    EXPECT_EQ(ig.mode(), IgMode::Operate);
    const auto again = ig.receive(control_message(2, IgMode::Operate));
    EXPECT_FALSE(again->mode_changed); // nothing new asked for
    const auto back = ig.receive(control_message(3, IgMode::ResetStandby));
    EXPECT_TRUE(back->mode_changed);
    EXPECT_EQ(ig.mode(), IgMode::ResetStandby);
}

TEST(IgSession, ModeRequestCallbackAcceptsAndRefuses) {
    const FakeClock clock;
    IgSession ig{config_with(clock)};
    std::vector<IgMode> asked;
    bool accept = false;
    ig.on_mode_request([&](IgMode m) {
        asked.push_back(m);
        return accept;
    });

    ASSERT_TRUE(ig.receive(control_message(1, IgMode::Operate)));
    EXPECT_EQ(ig.mode(), IgMode::ResetStandby); // refused
    EXPECT_EQ(ig.stats().mode_requests_refused, 1U);
    ASSERT_TRUE(ig.receive(control_message(2, IgMode::Operate)));
    EXPECT_EQ(asked.size(), 1U); // the same request is not asked again

    accept = true;
    ASSERT_TRUE(ig.receive(control_message(3, IgMode::Debug)));
    EXPECT_EQ(asked.size(), 2U);
    EXPECT_EQ(ig.mode(), IgMode::ResetStandby); // accepted, but the transition is the app's
    ig.set_mode(IgMode::Debug);
    EXPECT_EQ(ig.mode(), IgMode::Debug);

    // After set_mode the Host's standing request is judged afresh: Operate differs from Debug.
    ASSERT_TRUE(ig.receive(control_message(4, IgMode::Operate)));
    EXPECT_EQ(asked.size(), 3U);
}

TEST(IgSession, TheHostCannotRequestOfflineMaintenance) {
    const FakeClock clock;
    IgSession ig{config_with(clock)};
    int calls = 0;
    ig.on_mode_request([&](IgMode) {
        ++calls;
        return true;
    });
    ASSERT_TRUE(ig.receive(control_message(1, IgMode::OfflineMaintenance)));
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(ig.mode(), IgMode::ResetStandby);
    EXPECT_EQ(ig.stats().mode_requests_refused, 1U);
}

// ---- mode rules for received packets
// -------------------------------------------------------------

Bytes host_message(std::uint32_t frame, IgMode mode) {
    Bytes m = control_message(frame, mode);
    append(m, encoded(EntityControl{.entity_id = 3}));
    append(m, encoded(ViewControl{}));
    append(m, bytes_of(130, 8, 0, 0, 0, 0, 0, 0)); // unknown opcode
    return m;
}

TEST(IgSession, StandbyDeliversOnlyIgControl) {
    const FakeClock clock;
    IgSession ig{config_with(clock)};
    ig.on_mode_request([](IgMode) { return false; });
    std::vector<int> seen;
    ig.handlers().on<IgControl>([&](const IgControl&) { seen.push_back(1); });
    ig.handlers().on<EntityControl>([&](const EntityControl&) { seen.push_back(2); });
    ig.handlers().on_unknown([&](const UnknownPacket&) { seen.push_back(130); });
    ASSERT_TRUE(ig.receive(host_message(1, IgMode::Operate)));
    EXPECT_EQ(seen, (std::vector<int>{1}));
    EXPECT_EQ(ig.stats().ignored_packets, 3U);
    EXPECT_EQ(ig.stats().unknown_packets, 0U);
}

TEST(IgSession, OperateDeliversEverything) {
    const FakeClock clock;
    IgSession ig = ig_in(IgMode::Operate, clock);
    std::vector<int> seen;
    std::uint16_t entity = 0;
    ig.handlers().on<IgControl>([&](const IgControl&) { seen.push_back(1); });
    ig.handlers().on<EntityControl>([&](const EntityControl& e) {
        seen.push_back(2);
        entity = e.entity_id;
    });
    ig.handlers().on<ViewControl>([&](const ViewControl&) { seen.push_back(16); });
    ig.handlers().on_unknown([&](const UnknownPacket& u) { seen.push_back(u.opcode); });
    ASSERT_TRUE(ig.receive(host_message(1, IgMode::Operate)));
    EXPECT_EQ(seen, (std::vector<int>{1, 2, 16, 130}));
    EXPECT_EQ(entity, 3);
    EXPECT_EQ(ig.stats().ignored_packets, 0U);
    EXPECT_EQ(ig.stats().unknown_packets, 1U);

    ig.set_mode(IgMode::Debug);
    seen.clear();
    ASSERT_TRUE(ig.receive(host_message(2, IgMode::Debug)));
    EXPECT_EQ(seen.size(), 4U);
}

TEST(IgSession, ARequestedModeTakesEffectForTheSameMessage) {
    const FakeClock clock;
    IgSession ig{config_with(clock)}; // no callback: Operate is applied at once
    int entities = 0;
    ig.handlers().on<EntityControl>([&](const EntityControl&) { ++entities; });
    ASSERT_TRUE(ig.receive(host_message(1, IgMode::Operate)));
    EXPECT_EQ(entities, 1);
}

TEST(IgSession, OfflineMaintenanceIgnoresTheHostEntirely) {
    const FakeClock clock;
    IgSession ig = ig_in(IgMode::OfflineMaintenance, clock);
    int calls = 0;
    ig.on_mode_request([&](IgMode) {
        ++calls;
        return true;
    });
    ig.handlers().on<IgControl>([&](const IgControl&) { ++calls; });
    const auto r = ig.receive(host_message(5, IgMode::Operate));
    ASSERT_TRUE(r);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(ig.stats().ignored_packets, 4U);
    EXPECT_EQ(ig.mode(), IgMode::OfflineMaintenance);
    EXPECT_EQ(ig.last_host_frame_number(), 5U); // the message was still received
}

TEST(IgSession, PacketErrorsInTheBodyAreCounted) {
    const FakeClock clock;
    IgSession ig = ig_in(IgMode::Operate, clock);
    std::vector<Error> errors;
    ig.handlers().on_error([&](const Error& e) { errors.push_back(e); });
    Bytes m = control_message(1, IgMode::Operate);
    append(m, bytes_of(2, 8, 0, 0, 0, 0, 0, 0)); // Entity Control is longer than 8 bytes
    ASSERT_TRUE(ig.receive(m));
    EXPECT_EQ(errors.size(), 1U);
    EXPECT_EQ(ig.stats().decode_errors, 1U);
}

// ---- database requests
// ---------------------------------------------------------------------------

TEST(IgSession, DatabaseRequestsAreForwardedOncePerRequestWhenOperating) {
    const FakeClock clock;
    IgSession ig{config_with(clock)};
    std::vector<int> requested;
    ig.on_database_request([&](std::int8_t n) { requested.push_back(n); });

    // Reset/Standby ignores the Database Number (ICD 4.1.1).
    ASSERT_TRUE(ig.receive(control_message(1, IgMode::ResetStandby, 4)));
    EXPECT_TRUE(requested.empty());

    const auto r = ig.receive(control_message(2, IgMode::Operate, 4));
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->database_changed);
    EXPECT_EQ(requested, (std::vector<int>{4})); // the held request fires once the IG operates

    ASSERT_TRUE(ig.receive(control_message(3, IgMode::Operate, 4)));
    EXPECT_EQ(requested.size(), 1U); // not again while the Host keeps asking
    ASSERT_TRUE(ig.receive(control_message(4, IgMode::Operate, 0))); // acknowledged
    ASSERT_TRUE(ig.receive(control_message(5, IgMode::Operate, 4))); // new request
    EXPECT_EQ(requested, (std::vector<int>{4, 4}));
}

TEST(IgSession, DatabaseRequestsWithoutACallbackAreStillReported) {
    const FakeClock clock;
    IgSession ig = ig_in(IgMode::Operate, clock);
    const auto r = ig.receive(control_message(1, IgMode::Operate, 2));
    EXPECT_TRUE(r->database_changed);
    EXPECT_EQ(unwrap(ig.last_ig_control()).database_number, 2);
}

// ---- receiving ---------------------------------------------------------------------------------

TEST(IgSession, ReceiveRecordsTheHostAndTracksFrames) {
    const FakeClock clock;
    IgSession ig = ig_in(IgMode::Operate, clock);
    EXPECT_FALSE(ig.peer());
    EXPECT_FALSE(ig.last_ig_control());
    ASSERT_TRUE(ig.receive(control_message(20)));
    EXPECT_EQ(ig.peer(),
              (PeerInfo{.version = protocol_version, .byte_order = wire::ByteOrder::Native}));
    EXPECT_EQ(ig.host_frame_lsn(), 4); // 20 & 0xF

    auto r = ig.receive(control_message(24));
    EXPECT_EQ(r->lost_frames, 3U);
    r = ig.receive(control_message(24));
    EXPECT_TRUE(r->frame_regressed);
    r = ig.receive(control_message(0xFFFFFFFFU));
    EXPECT_TRUE(r->frame_regressed); // far behind
    r = ig.receive(control_message(0));
    EXPECT_EQ(r->lost_frames, 0U);
    EXPECT_EQ(ig.stats().lost_frames, 3U);
    EXPECT_EQ(ig.stats().frame_regressions, 2U);
    EXPECT_EQ(ig.host_frame_lsn(), 0);
}

TEST(IgSession, DropsWhatItCannotUse) {
    const FakeClock clock;
    IgSession ig{config_with(clock)};
    EXPECT_EQ(ig.receive(bytes_of(1)).error().code, ErrorCode::Truncated);
    EXPECT_EQ(ig.receive(sof_message(1)).error().code, ErrorCode::WrongDirection);
    Bytes bad_version = control_message(1);
    bad_version.at(2) = static_cast<std::byte>(4);
    EXPECT_EQ(ig.receive(bad_version).error().code, ErrorCode::UnsupportedVersion);
    EXPECT_EQ(ig.stats().decode_errors, 3U);
    EXPECT_EQ(ig.stats().messages_received, 0U);
}

TEST(IgSession, ForeignByteOrderHost) {
    const FakeClock clock;
    IgSession ig{config_with(clock)};
    Bytes m = control_message(0x01020304U, IgMode::Operate);
    swap_frame_fields(m);
    ASSERT_TRUE(ig.receive(m));
    EXPECT_EQ(unwrap(ig.peer()).byte_order, wire::ByteOrder::Swapped);
    EXPECT_EQ(ig.last_host_frame_number(), 0x01020304U);
    EXPECT_EQ(ig.mode(), IgMode::Operate);
}

TEST(IgSession, LegacyHostSendsAFrameCounter) {
    const FakeClock clock;
    IgSession ig{config_with(clock)};
    Bytes legacy = frame_packet(1, 16, 1);
    legacy.at(4) = static_cast<std::byte>((1U << 4U) | 1U); // minor 1, Operate
    wire::store<std::uint32_t>(legacy, 8, 99);
    ASSERT_TRUE(ig.receive(legacy));
    EXPECT_EQ(unwrap(ig.peer()).version, version_of(3, 1));
    EXPECT_EQ(ig.last_host_frame_number(), 99U);
    EXPECT_EQ(ig.mode(), IgMode::Operate);
    ASSERT_TRUE(ig.begin_frame());
    EXPECT_EQ(first_as<StartOfFrame>(ig.finish_frame()).last_host_frame_number, 99U);
}

TEST(IgSession, ReceivedPacketsAreMaskedByTheHostVersion) {
    const FakeClock clock;
    EntityControl entity;
    entity.extrapolation_enabled = true;
    RateControl rate;
    rate.coordinate_system = RateCoordinateSystem::Local;
    HatHotRequest request;
    request.update_period = 8;

    const auto seen_by_app = [&](std::uint8_t minor) {
        IgSession ig = ig_in(IgMode::Operate, clock);
        std::vector<int> seen;
        ig.handlers().on<EntityControl>(
            [&](const EntityControl& e) { seen.push_back(e.extrapolation_enabled ? 1 : 0); });
        ig.handlers().on<RateControl>([&](const RateControl& r) {
            seen.push_back(r.coordinate_system == RateCoordinateSystem::Local ? 1 : 0);
        });
        ig.handlers().on<HatHotRequest>(
            [&](const HatHotRequest& r) { seen.push_back(r.update_period); });
        Bytes m = control_message(1, IgMode::Operate);
        set_minor(m, minor);
        append(m, encoded(entity));
        append(m, encoded(rate));
        append(m, encoded(request));
        EXPECT_TRUE(ig.receive(m));
        return seen;
    };
    EXPECT_EQ(seen_by_app(3), (std::vector<int>{1, 1, 8}));
    EXPECT_EQ(seen_by_app(2), (std::vector<int>{0, 1, 8}));
    EXPECT_EQ(seen_by_app(1), (std::vector<int>{0, 0, 0}));
}

TEST(IgSession, MovedSessionKeepsWorking) {
    const FakeClock clock;
    IgSession a{config_with(clock)};
    ASSERT_TRUE(a.begin_frame());
    IgSession b = std::move(a);
    EXPECT_EQ(b.finish_frame().size(), 24U);
    IgSession c;
    c = std::move(b);
    ASSERT_TRUE(c.begin_frame());
    EXPECT_EQ(c.finish_frame().size(), 24U);
    EXPECT_EQ(c.config().max_message_bytes, 16384U);
}

} // namespace
