#include "support.hpp"

#include <gtest/gtest.h>

namespace {

using namespace cigi;
using namespace cigi::test;

constexpr std::byte operator""_b(unsigned long long v) {
    return static_cast<std::byte>(v);
}

// ---- IG Control written by begin_frame ----------------------------------------------------------

TEST(HostSession, BeginFrameWritesIgControlGolden) {
    const FakeClock clock{0x0102};
    auto config = config_with(clock);
    config.initial_frame_number = 5;
    HostSession host{config};
    ASSERT_TRUE(host.request_mode(IgMode::Operate));
    ASSERT_TRUE(host.request_database(7));
    host.set_extrapolation_enabled(true);

    ASSERT_TRUE(host.begin_frame());
    const auto message = host.finish_frame();

    // ICD 4.1.1: opcode 1, size 24, major 3, database 7, minor 3 | extrapolation | timestamp valid
    // | Operate, magic 0x8000, host frame 5, timestamp 0x0102, last IG frame 0.
    const auto little = bytes_of(1, 24, 3, 7, 0x3D, 0, 0x00, 0x80, 5, 0, 0, 0, 0x02, 0x01, 0, 0, 0,
                                 0, 0, 0, 0, 0, 0, 0);
    const auto big = bytes_of(1, 24, 3, 7, 0x3D, 0, 0x80, 0x00, 0, 0, 0, 5, 0, 0, 0x01, 0x02, 0, 0,
                              0, 0, 0, 0, 0, 0);
    const auto& expected = native_of(little, big);
    ASSERT_EQ(message.size(), expected.size());
    EXPECT_TRUE(std::equal(message.begin(), message.end(), expected.begin()));
    EXPECT_EQ(host.host_frame_number(), 5U);
    EXPECT_EQ(host.stats().messages_sent, 1U);
}

TEST(HostSession, FrameNumbersCountUpAndWrap) {
    const FakeClock clock;
    auto config = config_with(clock);
    config.initial_frame_number = 0xFFFFFFFEU;
    HostSession host{config};
    std::vector<std::uint32_t> seen;
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(host.begin_frame());
        seen.push_back(first_as<IgControl>(host.finish_frame()).host_frame_number);
    }
    EXPECT_EQ(seen, (std::vector<std::uint32_t>{0xFFFFFFFEU, 0xFFFFFFFFU, 0U, 1U}));
}

TEST(HostSession, TimestampWrapsWithTheClock) {
    const FakeClock clock{0xFFFFFFFFU};
    HostSession host{config_with(clock)};
    ASSERT_TRUE(host.begin_frame());
    const auto first = first_as<IgControl>(host.finish_frame());
    clock.advance(2);
    ASSERT_TRUE(host.begin_frame());
    const auto second = first_as<IgControl>(host.finish_frame());
    EXPECT_TRUE(first.timestamp_valid);
    EXPECT_EQ(first.timestamp.count(), 0xFFFFFFFFU);
    EXPECT_EQ(second.timestamp.count(), 1U);
}

TEST(HostSession, DefaultClockIsTheSteadyClock) {
    HostSession host;
    ASSERT_TRUE(host.begin_frame());
    EXPECT_TRUE(first_as<IgControl>(host.finish_frame()).timestamp_valid);
    const auto a = steady_timestamp();
    const auto b = steady_timestamp();
    // Wrapping time: the difference (mod 2^32) is small and non-negative.
    EXPECT_LT(static_cast<std::uint32_t>(b.count() - a.count()), 1'000'000U);
}

TEST(HostSession, FinishWithoutBeginAndBeginTwice) {
    const FakeClock clock;
    HostSession host{config_with(clock)};
    EXPECT_TRUE(host.finish_frame().empty());
    EXPECT_FALSE(host.frame_open());
    ASSERT_TRUE(host.begin_frame());
    EXPECT_TRUE(host.frame_open());
    ASSERT_TRUE(host.begin_frame()); // discards the first; its number is not reused
    EXPECT_EQ(first_as<IgControl>(host.finish_frame()).host_frame_number, 1U);
    EXPECT_EQ(host.stats().messages_sent, 1U);
    EXPECT_TRUE(host.finish_frame().empty());
}

TEST(HostSession, AddWithoutOpenFrameFails) {
    const FakeClock clock;
    HostSession host{config_with(clock)};
    const auto r = host.add(EntityControl{});
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, ErrorCode::UnexpectedFirstPacket);
    EXPECT_EQ(host.stats().rejected_packets, 1U);
}

// ---- synchronous / asynchronous gating
// -----------------------------------------------------------

TEST(HostSession, SynchronousModeSendsOncePerStartOfFrame) {
    const FakeClock clock;
    HostSession host{config_with(clock)};
    EXPECT_FALSE(host.ready_to_send());
    ASSERT_TRUE(host.receive(sof_message(1)));
    EXPECT_TRUE(host.ready_to_send());
    ASSERT_TRUE(host.begin_frame());
    EXPECT_TRUE(host.ready_to_send()); // still answering that Start of Frame
    static_cast<void>(host.finish_frame());
    EXPECT_FALSE(host.ready_to_send());
    ASSERT_TRUE(host.receive(sof_message(2)));
    EXPECT_TRUE(host.ready_to_send());
}

TEST(HostSession, AsynchronousModeIsAlwaysReady) {
    const FakeClock clock;
    HostSession host{config_with(clock, false)};
    EXPECT_TRUE(host.ready_to_send());
    ASSERT_TRUE(host.begin_frame());
    static_cast<void>(host.finish_frame());
    EXPECT_TRUE(host.ready_to_send());
}

// ---- start-up sequence
// ---------------------------------------------------------------------------

TEST(HostSession, EnforcesTheStartupSequence) {
    const FakeClock clock;
    HostSession host{config_with(clock)};
    ASSERT_TRUE(host.begin_frame());
    auto r = host.add(EntityControl{});
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, ErrorCode::NotOperational);
    EXPECT_EQ(r.error().opcode, EntityControl::opcode);
    EXPECT_FALSE(host.operational());

    ASSERT_TRUE(host.receive(sof_message(1, IgMode::ResetStandby)));
    ASSERT_TRUE(host.begin_frame());
    EXPECT_EQ(host.add(HatHotRequest{}).error().code, ErrorCode::NotOperational);
    EXPECT_EQ(host.ig_mode(), IgMode::ResetStandby);

    ASSERT_TRUE(host.receive(sof_message(2, IgMode::Operate)));
    EXPECT_TRUE(host.operational());
    ASSERT_TRUE(host.begin_frame());
    EXPECT_TRUE(host.add(EntityControl{}));
    EXPECT_EQ(host.stats().rejected_packets, 2U);

    ASSERT_TRUE(host.receive(sof_message(3, IgMode::Debug)));
    EXPECT_TRUE(host.operational());
    ASSERT_TRUE(host.receive(sof_message(4, IgMode::OfflineMaintenance)));
    EXPECT_FALSE(host.operational());
    ASSERT_TRUE(host.begin_frame());
    EXPECT_EQ(host.add(EntityControl{}).error().code, ErrorCode::NotOperational);
}

TEST(HostSession, StartupEnforcementCanBeSwitchedOff) {
    const FakeClock clock;
    auto config = config_with(clock);
    config.enforce_startup_sequence = false;
    HostSession host{config};
    ASSERT_TRUE(host.begin_frame());
    EXPECT_TRUE(host.add(EntityControl{}));
    EXPECT_EQ(host.stats().rejected_packets, 0U);
}

TEST(HostSession, SecondIgControlIsRejectedEvenWhenOperational) {
    const FakeClock clock;
    HostSession host{config_with(clock)};
    make_operational(host);
    ASSERT_TRUE(host.begin_frame());
    EXPECT_EQ(host.add(IgControl{}).error().code, ErrorCode::DuplicateFramePacket);
}

TEST(HostSession, OversizePacketIsBufferTooSmallAndLeavesTheMessageUnchanged) {
    const FakeClock clock;
    auto config = config_with(clock);
    config.max_message_bytes = 1; // clamped up to one frame packet
    HostSession host{config};
    make_operational(host);
    ASSERT_TRUE(host.begin_frame());
    EXPECT_EQ(host.add(EntityControl{}).error().code, ErrorCode::BufferTooSmall);
    EXPECT_EQ(host.finish_frame().size(), 24U);
}

TEST(HostSession, ValidationOnSendIsOptIn) {
    const FakeClock clock;
    EntityControl bad;
    bad.yaw = -90.0F; // outside the ICD's 0..360, but a common host convention
    {
        HostSession host{config_with(clock)};
        make_operational(host);
        ASSERT_TRUE(host.begin_frame());
        EXPECT_TRUE(host.add(bad));
    }
    auto config = config_with(clock);
    config.validate_outgoing = true;
    HostSession strict{config};
    make_operational(strict);
    ASSERT_TRUE(strict.begin_frame());
    const auto r = strict.add(bad);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, ErrorCode::InvalidFieldValue);
    EXPECT_TRUE(strict.add(EntityControl{}));
    EXPECT_EQ(strict.finish_frame().size(), 24U + 48U);
}

// ---- control requests
// ----------------------------------------------------------------------------

TEST(HostSession, RequestModeRejectsWhatTheHostCannotCommand) {
    HostSession host;
    EXPECT_EQ(host.requested_mode(), IgMode::ResetStandby);
    EXPECT_EQ(host.request_mode(IgMode::OfflineMaintenance).error().code,
              ErrorCode::InvalidFieldValue);
    EXPECT_EQ(host.request_mode(static_cast<IgMode>(7)).error().code, ErrorCode::InvalidFieldValue);
    EXPECT_TRUE(host.request_mode(IgMode::Debug));
    EXPECT_EQ(host.requested_mode(), IgMode::Debug);
}

TEST(HostSession, RequestDatabaseRange) {
    HostSession host;
    EXPECT_FALSE(host.request_database(-1));
    EXPECT_FALSE(host.request_database(128));
    EXPECT_TRUE(host.request_database(127));
    EXPECT_EQ(host.pending_database(), 127);
    EXPECT_TRUE(host.request_database(0)); // cancel
    EXPECT_EQ(host.pending_database(), 0);
}

TEST(HostSession, DatabaseHandshakeAcknowledgesLoadingAndStopsRequesting) {
    const FakeClock clock;
    HostSession host{config_with(clock)};
    ASSERT_TRUE(host.request_database(5));
    ASSERT_TRUE(host.receive(sof_message(1, IgMode::Operate, 2)));
    ASSERT_TRUE(host.begin_frame());
    EXPECT_EQ(first_as<IgControl>(host.finish_frame()).database_number, 5);

    // The IG has not reacted yet (still database 2): keep asking.
    ASSERT_TRUE(host.receive(sof_message(2, IgMode::Operate, 2)));
    ASSERT_TRUE(host.begin_frame());
    EXPECT_EQ(first_as<IgControl>(host.finish_frame()).database_number, 5);

    // -5: loading. Acknowledge with 0.
    const auto report = host.receive(sof_message(3, IgMode::Operate, -5));
    ASSERT_TRUE(report);
    EXPECT_TRUE(report->database_changed);
    EXPECT_TRUE(host.database_loading());
    EXPECT_EQ(host.pending_database(), 0);
    ASSERT_TRUE(host.begin_frame());
    EXPECT_EQ(first_as<IgControl>(host.finish_frame()).database_number, 0);

    ASSERT_TRUE(host.receive(sof_message(4, IgMode::Operate, 5)));
    EXPECT_FALSE(host.database_loading());
    EXPECT_EQ(host.ig_database(), 5);
}

TEST(HostSession, DatabaseUnavailableEndsTheRequest) {
    HostSession host;
    ASSERT_TRUE(host.request_database(9));
    ASSERT_TRUE(host.receive(sof_message(1, IgMode::Operate, -128)));
    EXPECT_EQ(host.pending_database(), 0);
    EXPECT_FALSE(host.database_loading()); // -128 is "unavailable", not "loading"
}

// ---- receiving
// -----------------------------------------------------------------------------------

TEST(HostSession, ReceiveRecordsTheIgState) {
    HostSession host;
    EXPECT_FALSE(host.last_start_of_frame());
    EXPECT_FALSE(host.peer());
    EXPECT_EQ(host.ig_database(), 0);

    StartOfFrame sof;
    sof.ig_frame_number = 41;
    sof.ig_mode = IgMode::Operate;
    sof.ig_status = 3;
    sof.database_number = 4;
    const auto report = host.receive(encoded(sof));
    ASSERT_TRUE(report);
    EXPECT_FALSE(report->mode_changed); // the first message is not a change
    EXPECT_EQ(report->lost_frames, 0U);
    ASSERT_TRUE(host.last_start_of_frame());
    EXPECT_EQ(unwrap(host.last_start_of_frame()).ig_status, 3);
    EXPECT_EQ(host.last_ig_frame_number(), 41U);
    EXPECT_EQ(host.peer(),
              (PeerInfo{.version = protocol_version, .byte_order = wire::ByteOrder::Native}));
    EXPECT_EQ(host.stats().messages_received, 1U);

    const auto next = host.receive(sof_message(42, IgMode::Debug, 4));
    ASSERT_TRUE(next);
    EXPECT_TRUE(next->mode_changed);
    EXPECT_FALSE(next->database_changed);
}

TEST(HostSession, DetectsLostRegressedAndWrappedIgFrames) {
    HostSession host;
    ASSERT_TRUE(host.receive(sof_message(10)));
    auto r = host.receive(sof_message(13));
    EXPECT_EQ(r->lost_frames, 2U);
    EXPECT_FALSE(r->frame_regressed);
    r = host.receive(sof_message(14));
    EXPECT_EQ(r->lost_frames, 0U);
    r = host.receive(sof_message(14)); // duplicate
    EXPECT_TRUE(r->frame_regressed);
    r = host.receive(sof_message(3)); // IG restarted
    EXPECT_TRUE(r->frame_regressed);
    EXPECT_EQ(host.stats().frame_regressions, 2U);
    EXPECT_EQ(host.stats().lost_frames, 2U);

    ASSERT_TRUE(host.receive(sof_message(0xFFFFFFFFU)));
    r = host.receive(sof_message(0)); // wrap is not a gap
    EXPECT_EQ(r->lost_frames, 0U);
    EXPECT_FALSE(r->frame_regressed);
    r = host.receive(sof_message(3)); // wrap with a gap
    EXPECT_EQ(r->lost_frames, 2U);
}

TEST(HostSession, DropsWhatItCannotUse) {
    HostSession host;
    EXPECT_EQ(host.receive(bytes_of(1, 2, 3)).error().code, ErrorCode::Truncated);
    const auto wrong_way = host.receive(control_message(1));
    ASSERT_FALSE(wrong_way);
    EXPECT_EQ(wrong_way.error().code, ErrorCode::WrongDirection);

    Bytes bad_magic = sof_message(1);
    bad_magic.at(6) = 0_b;
    bad_magic.at(7) = 0_b;
    EXPECT_EQ(host.receive(bad_magic).error().code, ErrorCode::InvalidByteSwapMagic);
    EXPECT_EQ(host.stats().decode_errors, 3U);
    EXPECT_EQ(host.stats().messages_received, 0U);
    EXPECT_FALSE(host.ready_to_send());
}

TEST(HostSession, HandlersReceiveEveryPacketIncludingStartOfFrame) {
    HostSession host;
    std::vector<int> order;
    std::uint32_t frame = 0;
    std::uint16_t hat_id = 0;
    host.handlers().on<StartOfFrame>([&](const StartOfFrame& s) {
        order.push_back(101);
        frame = s.ig_frame_number;
    });
    host.handlers().on<HatHotResponse>([&](const HatHotResponse& r) {
        order.push_back(106);
        hat_id = r.hat_hot_id;
    });
    int unknown = 0;
    host.handlers().on_unknown([&](const UnknownPacket& u) { unknown += u.opcode; });

    Bytes m = sof_message(77);
    append(m, encoded(HatHotResponse{.hat_hot_id = 9}));
    append(m, bytes_of(130, 8, 0, 0, 0, 0, 0, 0)); // an opcode outside the set
    ASSERT_TRUE(host.receive(m));
    EXPECT_EQ(order, (std::vector<int>{101, 106}));
    EXPECT_EQ(frame, 77U);
    EXPECT_EQ(hat_id, 9);
    EXPECT_EQ(unknown, 130);
    EXPECT_EQ(host.stats().unknown_packets, 1U);
}

TEST(HostSession, PacketErrorsAreCountedAndPassedOn) {
    HostSession host;
    std::vector<Error> errors;
    host.handlers().on_error([&](const Error& e) { errors.push_back(e); });
    Bytes m = sof_message(1);
    append(m, bytes_of(102, 8, 0, 0, 0, 0, 0, 0)); // HAT/HOT Response is longer than 8 bytes
    append(m, sof_message(2));                     // second frame packet
    const auto r = host.receive(m);
    ASSERT_TRUE(r); // the datagram as a whole is accepted
    EXPECT_EQ(errors.size(), 2U);
    EXPECT_EQ(host.stats().decode_errors, 2U);
}

TEST(HostSession, ForeignByteOrderPeer) {
    HostSession host;
    Bytes m = sof_message(0x01020304U, IgMode::Operate);
    swap_frame_fields(m);
    std::uint32_t frame = 0;
    host.handlers().on<StartOfFrame>([&](const StartOfFrame& s) { frame = s.ig_frame_number; });
    ASSERT_TRUE(host.receive(m));
    EXPECT_EQ(frame, 0x01020304U);
    EXPECT_EQ(unwrap(host.peer()).byte_order, wire::ByteOrder::Swapped);
    EXPECT_TRUE(host.operational());
    EXPECT_EQ(host.last_ig_frame_number(), 0x01020304U);
}

// ---- peer versions
// -------------------------------------------------------------------------------

TEST(HostSession, LegacyPeerFrameCounterIsEchoedByTheHost) {
    // A 3.1 IG: a 16-byte Start of Frame whose bytes 8-11 are its Frame Counter (ICD 2.3 note).
    const FakeClock clock;
    HostSession host{config_with(clock)};
    Bytes legacy = frame_packet(101, 16, 1);
    legacy.at(5) = static_cast<std::byte>((1U << 4U) | 1U); // minor 1, Operate
    wire::store<std::uint32_t>(legacy, 8, 4242);
    ASSERT_TRUE(host.receive(legacy));
    EXPECT_EQ(unwrap(host.peer()).version, version_of(3, 1));
    EXPECT_TRUE(host.operational());

    ASSERT_TRUE(host.begin_frame());
    const auto control = first_as<IgControl>(host.finish_frame());
    EXPECT_EQ(control.host_frame_number, 4242U); // copied from the IG's counter
    EXPECT_EQ(control.last_ig_frame_number, 0U); // the field does not exist before 3.2
    ASSERT_TRUE(host.receive(legacy));
    EXPECT_EQ(host.stats().frame_regressions, 1U); // same counter twice
}

TEST(HostSession, SendingToOlderPeersMasksNewerContent) {
    const FakeClock clock;
    EntityControl entity;
    entity.extrapolation_enabled = true;
    RateControl rate;
    rate.coordinate_system = RateCoordinateSystem::Local;
    HatHotRequest request;
    request.update_period = 4;

    const auto send = [&](std::uint8_t minor) {
        HostSession host{config_with(clock)};
        host.set_extrapolation_enabled(true);
        Bytes sof = frame_packet(101, minor < 2 ? 16 : 24, minor);
        sof.at(5) = static_cast<std::byte>((static_cast<unsigned>(minor) << 4U) | 1U);
        EXPECT_TRUE(host.receive(sof));
        EXPECT_TRUE(host.begin_frame());
        EXPECT_TRUE(host.add(entity));
        EXPECT_TRUE(host.add(rate));
        EXPECT_TRUE(host.add(request));
        return to_vec(host.finish_frame());
    };

    const auto m33 = send(3);
    EXPECT_TRUE(first_as<IgControl>(m33).extrapolation_enabled);
    EXPECT_TRUE(unwrap(find_packet<EntityControl>(m33)).extrapolation_enabled);
    EXPECT_EQ(unwrap(find_packet<RateControl>(m33)).coordinate_system, RateCoordinateSystem::Local);
    EXPECT_EQ(unwrap(find_packet<HatHotRequest>(m33)).update_period, 4);

    const auto m32 = send(2);
    EXPECT_FALSE(first_as<IgControl>(m32).extrapolation_enabled);
    EXPECT_FALSE(unwrap(find_packet<EntityControl>(m32)).extrapolation_enabled);
    EXPECT_EQ(unwrap(find_packet<RateControl>(m32)).coordinate_system, RateCoordinateSystem::Local);
    EXPECT_EQ(unwrap(find_packet<HatHotRequest>(m32)).update_period, 4);

    const auto m31 = send(1);
    EXPECT_FALSE(unwrap(find_packet<EntityControl>(m31)).extrapolation_enabled);
    EXPECT_EQ(unwrap(find_packet<RateControl>(m31)).coordinate_system,
              RateCoordinateSystem::WorldParent);
    EXPECT_EQ(unwrap(find_packet<HatHotRequest>(m31)).update_period, 0);
}

TEST(HostSession, SymbolComponentClassesCannotBeSentBeforeThreeThree) {
    const FakeClock clock;
    ComponentControl symbol;
    symbol.component_class = ComponentClass::Symbol;
    ComponentControl surface;
    surface.component_class = ComponentClass::SymbolSurface;
    const ComponentControl entity; // class 0 is fine everywhere

    HostSession host{config_with(clock)};
    Bytes sof = frame_packet(101, 24, 2);
    sof.at(5) = static_cast<std::byte>((2U << 4U) | 1U);
    ASSERT_TRUE(host.receive(sof));
    ASSERT_TRUE(host.begin_frame());
    EXPECT_EQ(host.add(symbol).error().code, ErrorCode::UnsupportedVersion);
    EXPECT_EQ(host.add(surface).error().code, ErrorCode::UnsupportedVersion);
    EXPECT_TRUE(host.add(entity));
    EXPECT_EQ(host.finish_frame().size(), 24U + ComponentControl::wire_size);

    HostSession modern{config_with(clock)};
    make_operational(modern);
    ASSERT_TRUE(modern.begin_frame());
    EXPECT_TRUE(modern.add(symbol));
}

TEST(HostSession, ReceivedPacketsAreMaskedByThePeerVersion) {
    HatHotResponse response;
    response.hat_hot_id = 1;
    response.host_frame_lsn = 5;
    HatHotExtendedResponse extended;
    extended.host_frame_lsn = 6;

    const auto lsns = [&](std::uint8_t minor) {
        HostSession host;
        std::vector<int> seen;
        host.handlers().on<HatHotResponse>(
            [&](const HatHotResponse& r) { seen.push_back(r.host_frame_lsn); });
        host.handlers().on<HatHotExtendedResponse>(
            [&](const HatHotExtendedResponse& r) { seen.push_back(r.host_frame_lsn); });
        Bytes m = sof_message(1);
        set_minor(m, minor);
        append(m, encoded(response));
        append(m, encoded(extended));
        EXPECT_TRUE(host.receive(m));
        return seen;
    };
    EXPECT_EQ(lsns(3), (std::vector<int>{5, 6}));
    EXPECT_EQ(lsns(2), (std::vector<int>{5, 6}));
    EXPECT_EQ(lsns(1), (std::vector<int>{0, 0}));
}

// ---- moving a session --------------------------------------------------------------------------

TEST(HostSession, MovedSessionKeepsWorking) {
    const FakeClock clock;
    HostSession a{config_with(clock)};
    make_operational(a);
    ASSERT_TRUE(a.begin_frame());
    HostSession b = std::move(a);
    EXPECT_TRUE(b.add(EntityControl{}));
    EXPECT_EQ(b.finish_frame().size(), 72U);
    HostSession c;
    c = std::move(b);
    ASSERT_TRUE(c.begin_frame());
    EXPECT_EQ(c.finish_frame().size(), 24U);
    EXPECT_EQ(c.config().max_message_bytes, 16384U);
}

} // namespace
