// cigi::Ig over the in-memory transport and a fake clock (ICD 2.2, 2.8, 4.1.1, 4.2.1, 4.2.2).

#include "support.hpp"

#include <cmath>
#include <gtest/gtest.h>
#include <stop_token>
#include <thread>
#include <utility>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,bugprone-unused-return-value):
// tests index vectors after asserting their size and drive poll() for its effects.

namespace {

using namespace cigi;
using namespace cigi::test;

struct IgFixture {
    RawPeer peer;
    FakeClock clock;
    Ig ig;

    explicit IgFixture(IgConfig config = {}) : ig(make(std::move(config), clock, peer)) {}

    static Ig make(IgConfig config, const FakeClock& clock, RawPeer& peer) {
        config.session.clock = clock.fn();
        return Ig::over(std::move(config), peer.take_other()).value();
    }
    void tick(std::uint32_t ticks = frame_ticks) {
        clock.advance(ticks);
        EXPECT_TRUE(ig.poll(0ms).has_value());
    }
};

TEST(IgFacade, SendsStartOfFrameAtTheConfiguredRate) {
    IgConfig config;
    config.rate_hz = 100.0;
    IgFixture f{config};
    ASSERT_TRUE(f.ig.poll(0ms));
    EXPECT_TRUE(f.peer.received().empty()); // the first tick is one period after the start
    f.tick(1000);
    f.tick(1000);
    f.tick(500); // not due
    const auto sent = f.peer.received();
    ASSERT_EQ(sent.size(), 2U);
    const auto first = first_as<StartOfFrame>(sent[0]);
    const auto second = first_as<StartOfFrame>(sent[1]);
    EXPECT_EQ(first.ig_mode, IgMode::ResetStandby);
    EXPECT_EQ(second.ig_frame_number, first.ig_frame_number + 1);
    f.tick(5000); // late by four periods
    EXPECT_EQ(f.ig.stats().late_frames, 4U);
    EXPECT_EQ(f.ig.stats().frames_sent, 3U);
}

TEST(IgFacade, OnFrameRunsOnlyInOperateAndCanAnswerWithTheHostFrameLsn) {
    IgFixture f;
    int calls = 0;
    std::uint8_t lsn = 0xFF;
    f.ig.on_frame([&](IgFrame& frame) {
        ++calls;
        lsn = frame.host_frame_lsn();
        HatHotResponse response;
        response.hat_hot_id = 3;
        response.valid = true;
        response.host_frame_lsn = frame.host_frame_lsn();
        EXPECT_TRUE(frame.add(response));
        EXPECT_EQ(frame.mode(), IgMode::Operate);
        EXPECT_GT(frame.ig_frame_number(), 0U);
    });
    f.tick();
    EXPECT_EQ(calls, 0); // Reset/Standby

    f.peer.send(control_message(6, IgMode::Operate));
    f.tick();
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(lsn, 6);
    EXPECT_EQ(f.ig.mode(), IgMode::Operate);
    const auto sent = f.peer.received();
    ASSERT_EQ(sent.size(), 2U);
    const auto response = find_packet<HatHotResponse>(sent.back());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->hat_hot_id, 3);
    EXPECT_EQ(response->host_frame_lsn, 6);
}

TEST(IgFacade, HandlersSeeHostPacketsOnlyInOperate) {
    IgFixture f;
    int entities = 0;
    int unknown = 0;
    f.ig.on<EntityControl>([&](const EntityControl&) { ++entities; });
    f.ig.on_unknown([&](const UnknownPacket&) { ++unknown; });
    const auto message = [](std::uint32_t frame, IgMode mode) {
        Bytes m = control_message(frame, mode);
        const Bytes entity = encoded(EntityControl{});
        m.insert(m.end(), entity.begin(), entity.end());
        const auto odd = bytes_of(220, 8, 0, 0, 0, 0, 0, 0);
        m.insert(m.end(), odd.begin(), odd.end());
        return m;
    };
    f.peer.send(message(1, IgMode::ResetStandby));
    f.tick();
    EXPECT_EQ(entities, 0); // ICD 2.8: ignored in Reset/Standby
    f.peer.send(message(2, IgMode::Operate));
    f.tick();
    EXPECT_EQ(entities, 1);
    EXPECT_EQ(unknown, 1);
    const auto control = f.ig.session().last_ig_control();
    ASSERT_TRUE(control.has_value());
    EXPECT_EQ(control->host_frame_number, 2U);
}

TEST(IgFacade, ModeAndDatabaseRequestsReachTheApplication) {
    IgFixture f;
    std::vector<IgMode> modes;
    std::vector<int> databases;
    f.ig.on_mode_request([&](IgMode m) {
        modes.push_back(m);
        return m != IgMode::Debug; // refuse Debug
    });
    f.ig.on_database_request([&](std::int8_t n) { databases.push_back(n); });
    f.peer.send(control_message(1, IgMode::Debug));
    f.tick();
    EXPECT_EQ(f.ig.mode(), IgMode::ResetStandby);
    EXPECT_EQ(f.ig.stats().session.mode_requests_refused, 1U);
    f.peer.send(control_message(2, IgMode::Operate));
    f.tick();
    EXPECT_EQ(f.ig.mode(), IgMode::ResetStandby); // accepted, but the application sets it
    f.ig.set_mode(IgMode::Operate);
    f.peer.send(control_message(3, IgMode::Operate, 4));
    f.ig.set_database(-4);
    f.tick();
    EXPECT_EQ(databases, std::vector<int>{4});
    const auto sent = f.peer.received();
    const auto sof = first_as<StartOfFrame>(sent.back());
    EXPECT_EQ(sof.ig_mode, IgMode::Operate);
    EXPECT_EQ(sof.database_number, -4);
    EXPECT_EQ((std::vector<IgMode>{IgMode::Debug, IgMode::Operate}), modes);
}

TEST(IgFacade, PausingStopsStartOfFrameButNotReceiving) {
    IgFixture f;
    int entities = 0;
    f.ig.set_mode(IgMode::Operate);
    f.ig.on<EntityControl>([&](const EntityControl&) { ++entities; });
    f.tick();
    static_cast<void>(f.peer.received());

    f.ig.set_paused(true);
    EXPECT_TRUE(f.ig.paused());
    Bytes m = control_message(1, IgMode::Operate);
    const Bytes entity = encoded(EntityControl{});
    m.insert(m.end(), entity.begin(), entity.end());
    f.peer.send(m);
    f.tick();
    f.tick();
    EXPECT_TRUE(f.peer.received().empty());
    EXPECT_EQ(entities, 1);
    EXPECT_EQ(f.ig.stats().frames_skipped, 2U);

    f.ig.set_paused(false);
    f.tick();
    EXPECT_EQ(f.peer.received().size(), 1U);
}

TEST(IgFacade, RestartResetsTheFrameCounterAndTheMode) {
    IgFixture f;
    f.peer.send(control_message(1, IgMode::Operate));
    f.tick();
    f.tick();
    f.tick();
    ASSERT_EQ(f.ig.mode(), IgMode::Operate);
    const auto before = f.peer.received();
    ASSERT_EQ(before.size(), 3U);
    EXPECT_EQ(first_as<StartOfFrame>(before.back()).ig_frame_number, 2U);

    f.ig.restart();
    EXPECT_EQ(f.ig.mode(), IgMode::ResetStandby);
    EXPECT_EQ(f.ig.stats().restarts, 1U);
    EXPECT_FALSE(f.ig.host_connected());
    f.tick();
    const auto after = f.peer.received();
    ASSERT_EQ(after.size(), 1U);
    const auto sof = first_as<StartOfFrame>(after.front());
    EXPECT_EQ(sof.ig_frame_number, 0U);
    EXPECT_EQ(sof.ig_mode, IgMode::ResetStandby);
}

TEST(IgFacade, HostConnectedFollowsTheHostsMessages) {
    IgFixture f;
    EXPECT_FALSE(f.ig.host_connected());
    f.peer.send(control_message(1));
    f.tick();
    EXPECT_TRUE(f.ig.host_connected());
    f.tick(50'000); // 0.5 s
    EXPECT_TRUE(f.ig.host_connected());
    f.tick(60'000); // 1.1 s since the last message
    EXPECT_FALSE(f.ig.host_connected());
    f.peer.send(control_message(2));
    f.tick();
    EXPECT_TRUE(f.ig.host_connected());
}

TEST(IgFacade, BadDatagramsGoToOnError) {
    IgFixture f;
    std::vector<ErrorCode> codes;
    f.ig.on_error([&](const Error& e) { codes.push_back(e.code); });
    f.peer.send(Bytes(3));
    f.peer.send(sof_message(1)); // IG -> Host, sent to an IG
    f.peer.send(Bytes(70'000));
    f.tick();
    ASSERT_EQ(codes.size(), 3U);
    EXPECT_EQ(codes[1], ErrorCode::WrongDirection);
    EXPECT_EQ(codes[2], ErrorCode::BufferTooSmall);
    EXPECT_EQ(f.ig.stats().receive_errors, 3U);
    EXPECT_EQ(f.ig.stats().datagrams_received, 2U);
    EXPECT_FALSE(f.ig.host_connected());
}

TEST(IgFacade, CloseEndsPollingAndRun) {
    IgFixture f;
    f.ig.close();
    const auto status = f.ig.poll(0ms);
    ASSERT_TRUE(status);
    EXPECT_EQ(*status, PollStatus{});
    EXPECT_TRUE(f.ig.run(std::stop_token{}, 1ms));
    f.ig.close(); // idempotent
}

TEST(IgFacade, RunStopsOnRequest) {
    RawPeer peer;
    auto ig = Ig::over(IgConfig(), peer.take_other()).value(); // real clock
    std::stop_source stop;
    Result<void> result = std::unexpected(Error{.code = ErrorCode::Timeout});
    std::thread thread([&] { result = ig.run(stop.get_token(), 5ms); });
    std::this_thread::sleep_for(100ms);
    stop.request_stop();
    thread.join();
    EXPECT_TRUE(result);
    EXPECT_FALSE(peer.received().empty());
}

TEST(IgFacade, FatalTransportErrorsEndPoll) {
    auto receive_broken = std::make_unique<BrokenTransport>();
    receive_broken->fail_receive = true;
    auto ig = Ig::over(IgConfig(), std::move(receive_broken)).value();
    EXPECT_FALSE(ig.poll(0ms));
    EXPECT_FALSE(ig.run(std::stop_token{}, 1ms));

    FakeClock const clock;
    IgConfig config;
    config.session.clock = clock.fn();
    auto send_broken = std::make_unique<BrokenTransport>();
    send_broken->fail_send = true;
    auto sender = Ig::over(config, std::move(send_broken)).value();
    clock.advance(frame_ticks);
    const auto sent = sender.poll(0ms);
    ASSERT_FALSE(sent);
    EXPECT_EQ(sent.error().os_error, 5);
}

TEST(IgFacade, InvalidConfigurationIsRejected) {
    const auto reject = [](auto edit) {
        IgConfig config;
        edit(config);
        auto [a, b] = make_memory_transport_pair();
        return !Ig::over(config, std::move(a));
    };
    EXPECT_FALSE(reject([](IgConfig&) {}));
    EXPECT_TRUE(reject([](IgConfig& c) { c.rate_hz = 0.0; }));
    EXPECT_TRUE(reject([](IgConfig& c) { c.rate_hz = std::nan(""); }));
    EXPECT_TRUE(reject([](IgConfig& c) { c.peer_timeout = 0ms; }));
    EXPECT_TRUE(reject([](IgConfig& c) { c.max_message_bytes = 8; }));
    EXPECT_FALSE(Ig::over(IgConfig(), nullptr));
    IgConfig rate;
    rate.rate_hz = -1.0;
    EXPECT_FALSE(Ig::connect(rate));
    IgConfig port;
    port.host_address = "127.0.0.1";
    port.host_port = 0;
    EXPECT_FALSE(Ig::connect(port));
    IgConfig address;
    address.listen_address = "not-an-address";
    EXPECT_FALSE(Ig::connect(address));
}

TEST(IgFacade, IsMovable) {
    IgFixture f;
    Ig moved = std::move(f.ig);
    f.peer.send(control_message(1, IgMode::Operate));
    f.clock.advance(frame_ticks);
    EXPECT_TRUE(moved.poll(0ms));
    EXPECT_EQ(moved.mode(), IgMode::Operate);
    const Ig& view = moved;
    EXPECT_EQ(view.session().mode(), IgMode::Operate);
}

} // namespace
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,bugprone-unused-return-value)
