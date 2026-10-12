// cigi::Host over the in-memory transport and a fake clock (ICD 2.2, 2.3, 2.8, 4.1.1, 4.2.1).

#include "support.hpp"

#include <atomic>
#include <gtest/gtest.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>
#include <stop_token>
#include <thread>

namespace {

using namespace cigi;
using namespace cigi::test;

using Transition = std::pair<LinkState, LinkState>;

/// Records every state change of a Host.
std::vector<Transition>& watch(Host& host, std::vector<Transition>& out) {
    host.on_state_change([&out](LinkState from, LinkState to) { out.emplace_back(from, to); });
    return out;
}

Bytes with_unknown_packet(Bytes message) {
    const auto unknown = bytes_of(220, 8, 0, 0, 0, 0, 0, 0);
    message.insert(message.end(), unknown.begin(), unknown.end());
    return message;
}

// ---- link state machine ----------------------------------------------------------------------

TEST(HostLink, StartsWaitingForTheIgAndReachesOperateThroughStandby) {
    Rig rig;
    std::vector<Transition> changes;
    watch(rig.host(), changes);
    EXPECT_EQ(rig.host().state(), LinkState::WaitingForIg);

    std::vector<LinkState> frame_states;
    rig.host().on_frame([&](HostFrame& f) { frame_states.push_back(f.link_state()); });
    ASSERT_TRUE(rig.run_until(LinkState::Operate));

    const std::vector<Transition> expected{{LinkState::WaitingForIg, LinkState::Standby},
                                           {LinkState::Standby, LinkState::Operate}};
    EXPECT_EQ(changes, expected);
    rig.steps(3);
    // on_frame ran only in Operate, and at least once.
    ASSERT_FALSE(frame_states.empty());
    for (const auto s : frame_states) {
        EXPECT_EQ(s, LinkState::Operate);
    }
    EXPECT_EQ(rig.ig().mode(), IgMode::Operate);
}

TEST(HostLink, OnFrameIsNeverCalledBeforeOperate) {
    Rig rig;
    int calls_before_operate = 0;
    rig.host().on_frame([&](HostFrame&) {
        if (rig.host().state() != LinkState::Operate) {
            ++calls_before_operate;
        }
    });
    // The IG sits in Standby: it refuses Operate.
    rig.ig().on_mode_request([](IgMode) { return false; });
    rig.steps(30);
    EXPECT_EQ(rig.host().state(), LinkState::Standby);
    EXPECT_EQ(calls_before_operate, 0);
    EXPECT_EQ(rig.host().stats().frame_callbacks, 0U);
    EXPECT_GT(rig.ig().stats().session.mode_requests_refused, 0U);
    // Standby still got the IG Control start-up messages.
    EXPECT_GT(rig.host().stats().frames_sent, 20U);
}

TEST(HostLink, DatabaseLoadGoesThroughLoadingDatabase) {
    HostConfig config;
    config.database = 5;
    Rig rig{config, IgConfig{}};
    std::vector<Transition> changes;
    watch(rig.host(), changes);
    int requested = 0;
    rig.ig().on_database_request([&](std::int8_t n) {
        requested = n;
        rig.ig().set_database(static_cast<std::int8_t>(-n)); // loading started (ICD 4.1.1)
    });
    ASSERT_TRUE(rig.run_until(LinkState::LoadingDatabase));
    EXPECT_EQ(requested, 5);
    int frames_while_loading = 0;
    rig.host().on_frame([&](HostFrame&) {
        frames_while_loading += rig.host().state() == LinkState::LoadingDatabase ? 1 : 0;
    });
    rig.steps(3);
    EXPECT_EQ(frames_while_loading, 0); // the IG is busy: only IG Control is sent
    rig.ig().set_database(5);           // loaded
    ASSERT_TRUE(rig.run_until(LinkState::Operate));
    const std::vector<Transition> expected{{LinkState::WaitingForIg, LinkState::Standby},
                                           {LinkState::Standby, LinkState::LoadingDatabase},
                                           {LinkState::LoadingDatabase, LinkState::Operate}};
    EXPECT_EQ(changes, expected);
    EXPECT_EQ(rig.host().session().pending_database(), 0); // the request ended with the -n
}

TEST(HostLink, DebugModeIsFlowingAndOfflineMaintenanceReadsAsStandby) {
    RawPeer peer;
    FakeClock clock;
    auto host = Host::over(host_config_with(clock), peer.take_other()).value();
    int frames = 0;
    host.on_frame([&](HostFrame& f) {
        EXPECT_EQ(f.link_state(), LinkState::Debug);
        ++frames;
    });
    peer.send(sof_message(1, IgMode::OfflineMaintenance));
    ASSERT_TRUE(host.poll(0ms));
    EXPECT_EQ(host.state(), LinkState::Standby);
    peer.send(sof_message(2, IgMode::Debug));
    ASSERT_TRUE(host.poll(0ms));
    EXPECT_EQ(host.state(), LinkState::Debug);
    EXPECT_EQ(frames, 1);
}

TEST(HostLink, PeerTimeoutLosesTheLinkAndRecovers) {
    Rig rig;
    std::vector<Transition> changes;
    watch(rig.host(), changes);
    ASSERT_TRUE(rig.run_until(LinkState::Operate));
    changes.clear();

    rig.ig().set_paused(true);
    rig.steps(30);
    EXPECT_EQ(rig.host().state(), LinkState::Operate); // 0.5 s: not yet
    rig.steps(40);
    EXPECT_EQ(rig.host().state(), LinkState::Lost); // 1.17 s
    EXPECT_FALSE(rig.host().session().operational());
    EXPECT_EQ(rig.host().stats().link_losses, 1U);

    rig.ig().set_paused(false);
    ASSERT_TRUE(rig.run_until(LinkState::Operate));
    const std::vector<Transition> expected{{LinkState::Operate, LinkState::Lost},
                                           {LinkState::Lost, LinkState::Operate}};
    EXPECT_EQ(changes, expected);
    EXPECT_GT(rig.host().stats().time_in(LinkState::Lost), 0us);
}

TEST(HostLink, AHostThatNeverHearsAnIgStaysWaitingAndKeepsProbing) {
    RawPeer peer;
    FakeClock clock;
    auto host = Host::over(host_config_with(clock), peer.take_other()).value();
    ASSERT_TRUE(host.poll(0ms)); // probes at once
    EXPECT_EQ(peer.received().size(), 1U);
    clock.advance(5000); // 50 ms
    ASSERT_TRUE(host.poll(0ms));
    EXPECT_TRUE(peer.received().empty());
    clock.advance(5100); // past 100 ms since the first probe
    ASSERT_TRUE(host.poll(0ms));
    EXPECT_EQ(peer.received().size(), 1U);
    clock.advance(500'000); // 5 s: never heard, so never "lost"
    ASSERT_TRUE(host.poll(0ms));
    EXPECT_EQ(host.state(), LinkState::WaitingForIg);
}

TEST(HostLink, AnIgRestartedFarBackIsDetectedAndStartUpIsReplayed) {
    Rig rig;
    std::vector<Transition> changes;
    watch(rig.host(), changes);
    ASSERT_TRUE(rig.run_until(LinkState::Operate));
    rig.steps(20);
    changes.clear();

    ASSERT_TRUE(rig.ig().poll(0ms));
    rig.ig().restart();
    rig.steps(2);
    ASSERT_TRUE(rig.run_until(LinkState::Operate));
    EXPECT_EQ(rig.host().stats().ig_restarts, 1U);
    ASSERT_GE(changes.size(), 2U);
    EXPECT_EQ(changes.front(), (Transition{LinkState::Operate, LinkState::WaitingForIg}));
    EXPECT_EQ(changes.back().second, LinkState::Operate); // the start-up probe re-asked for Operate
    EXPECT_EQ(rig.host().session().stats().frame_regressions, 1U);
    EXPECT_EQ(rig.ig().stats().restarts, 1U);
}

TEST(HostLink, AnIgRestartedAfterThreeFramesRecoversThroughTheStaleFrames) {
    Rig rig;
    std::vector<Transition> changes;
    watch(rig.host(), changes);
    rig.steps(3);
    ASSERT_EQ(rig.host().state(), LinkState::Operate);
    changes.clear();

    ASSERT_TRUE(rig.ig().poll(0ms)); // take in the Host's last message, then restart the IG
    rig.ig().restart();              // frame numbers 0, 1, 2 are now stale or duplicates
    rig.steps(3);
    EXPECT_EQ(rig.host().state(), LinkState::Operate); // nothing it said was current
    EXPECT_GE(rig.host().session().stats().stale_frames, 2U);
    ASSERT_TRUE(rig.run_until(LinkState::Standby, 20)); // the first fresh SOF says Standby
    ASSERT_TRUE(rig.run_until(LinkState::Operate, 20)); // and the Host asks for Operate again
    EXPECT_EQ(rig.host().stats().ig_restarts, 0U);      // not a regression: within the window
    const std::vector<Transition> expected{{LinkState::Operate, LinkState::Standby},
                                           {LinkState::Standby, LinkState::Operate}};
    EXPECT_EQ(changes, expected);
}

TEST(HostLink, ALongRunOfStaleFramesCountsAsARestart) {
    RawPeer peer;
    FakeClock clock;
    auto host = Host::over(host_config_with(clock), peer.take_other()).value();
    peer.send(sof_message(100, IgMode::Operate));
    ASSERT_TRUE(host.poll(0ms));
    ASSERT_EQ(host.state(), LinkState::Operate);

    const std::uint32_t window = host.session().config().stale_window;
    for (std::uint32_t i = 0; i < window; ++i) {
        peer.send(sof_message(99, IgMode::Operate)); // 1 behind: reordered
    }
    ASSERT_TRUE(host.poll(0ms));
    EXPECT_EQ(host.state(), LinkState::Operate);
    EXPECT_EQ(host.stats().ig_restarts, 0U);
    peer.send(sof_message(99, IgMode::Operate)); // one more than the window
    ASSERT_TRUE(host.poll(0ms));
    EXPECT_EQ(host.stats().ig_restarts, 1U);
    EXPECT_EQ(host.state(), LinkState::WaitingForIg);
    EXPECT_FALSE(host.session().operational());
    // The next Start of Frame starts the sequence again, whatever its number.
    peer.send(sof_message(99, IgMode::Operate));
    ASSERT_TRUE(host.poll(0ms));
    EXPECT_EQ(host.state(), LinkState::Operate);
}

TEST(HostLink, DuplicateStartOfFrameChangesNothingAndSendsNothing) {
    RawPeer peer;
    FakeClock clock;
    auto host = Host::over(host_config_with(clock), peer.take_other()).value();
    peer.send(sof_message(5, IgMode::Operate));
    ASSERT_TRUE(host.poll(0ms));
    static_cast<void>(peer.received());
    peer.send(sof_message(5, IgMode::Operate));
    const auto status = host.poll(0ms);
    ASSERT_TRUE(status);
    EXPECT_FALSE(status->frame_sent);
    EXPECT_EQ(status->datagrams_received, 1U);
    EXPECT_EQ(host.session().stats().duplicate_frames, 1U);
}

// ---- sending: sync gating and async pacing ----------------------------------------------------

TEST(HostSend, SynchronousSendsOneMessagePerStartOfFrame) {
    RawPeer peer;
    FakeClock clock;
    auto host = Host::over(host_config_with(clock), peer.take_other()).value();
    static_cast<void>(host.poll(0ms)); // the start-up probe
    static_cast<void>(peer.received());
    for (std::uint32_t frame = 1; frame <= 3; ++frame) {
        peer.send(sof_message(frame, IgMode::Operate));
        const auto first = host.poll(0ms);
        ASSERT_TRUE(first);
        EXPECT_TRUE(first->frame_sent);
        const auto again = host.poll(0ms);
        ASSERT_TRUE(again);
        EXPECT_FALSE(again->frame_sent); // answered already: wait for the next Start of Frame
        EXPECT_EQ(peer.received().size(), 1U);
    }
}

TEST(HostSend, SynchronousMessageCarriesTheRequestedModeAndAcknowledgesTheIgFrame) {
    RawPeer peer;
    FakeClock clock;
    auto host = Host::over(host_config_with(clock), peer.take_other()).value();
    peer.send(sof_message(41, IgMode::ResetStandby));
    ASSERT_TRUE(host.poll(0ms));
    const auto sent = peer.received();
    ASSERT_EQ(sent.size(), 1U);
    const IgControl control = control_of(sent.front());
    EXPECT_EQ(control.ig_mode, IgMode::Operate);
    EXPECT_EQ(control.last_ig_frame_number, 41U);
    EXPECT_TRUE(control.timestamp_valid);
}

TEST(HostSend, AsynchronousSendsAtTheConfiguredRateAndCountsMissedTicks) {
    RawPeer peer;
    FakeClock clock;
    HostConfig config = host_config_with(clock);
    config.synchronous = false;
    config.async_rate_hz = 100.0; // 10 ms = 1000 ticks
    auto host = Host::over(config, peer.take_other()).value();

    ASSERT_TRUE(host.poll(0ms));
    EXPECT_TRUE(peer.received().empty()); // first tick is one period after the start
    clock.advance(1000);
    EXPECT_TRUE(host.poll(0ms)->frame_sent);
    EXPECT_EQ(peer.received().size(), 1U);
    clock.advance(500);
    EXPECT_FALSE(host.poll(0ms)->frame_sent);
    clock.advance(500);
    EXPECT_TRUE(host.poll(0ms)->frame_sent);
    EXPECT_EQ(host.stats().late_frames, 0U);
    clock.advance(5000); // poll ran 40 ms late: four ticks are dropped, not replayed
    EXPECT_TRUE(host.poll(0ms)->frame_sent);
    EXPECT_FALSE(host.poll(0ms)->frame_sent);
    EXPECT_EQ(host.stats().late_frames, 4U);
    EXPECT_EQ(peer.received().size(), 2U);
}

TEST(HostSend, AsynchronousFramesCallOnFrameOnlyOnceTheIgOperates) {
    RawPeer peer;
    FakeClock clock;
    HostConfig config = host_config_with(clock);
    config.synchronous = false;
    config.async_rate_hz = 100.0;
    auto host = Host::over(config, peer.take_other()).value();
    int calls = 0;
    host.on_frame([&](HostFrame&) { ++calls; });
    clock.advance(1000);
    static_cast<void>(host.poll(0ms));
    EXPECT_EQ(calls, 0);
    peer.send(sof_message(1, IgMode::Operate));
    clock.advance(1000);
    static_cast<void>(host.poll(0ms));
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(host.stats().frame_callbacks, 1U);
}

TEST(HostSend, AnOversizeAddFailsButTheFrameIsStillSent) {
    HostConfig config;
    config.max_message_bytes = 24 + 48; // IG Control + exactly one Entity Control
    Rig rig{config, IgConfig{}};
    int received = 0;
    rig.ig().on<EntityControl>([&](const EntityControl&) { ++received; });
    std::vector<Error> errors;
    rig.host().on_error([&](const Error& e) { errors.push_back(e); });
    std::vector<Result<void>> results;
    rig.host().on_frame([&](HostFrame& f) {
        if (results.empty()) {
            results.push_back(f.add(EntityControl{}));
            results.push_back(f.add(EntityControl{}));
            results.push_back(f.add(EntityControl{}));
        }
    });
    ASSERT_TRUE(rig.run_until(LinkState::Operate));
    rig.steps(3);
    ASSERT_EQ(results.size(), 3U);
    EXPECT_TRUE(results[0]);
    ASSERT_FALSE(results[1]);
    EXPECT_EQ(results[1].error().code, ErrorCode::BufferTooSmall);
    EXPECT_FALSE(results[2]);
    EXPECT_EQ(rig.host().stats().oversize_adds, 2U);
    ASSERT_EQ(errors.size(), 2U);
    EXPECT_EQ(errors[0].code, ErrorCode::BufferTooSmall);
    EXPECT_GE(received, 1); // the Entity Control that fit arrived
}

TEST(HostSend, LargeMessagesWarnOnceAboutFragmentation) {
    std::ostringstream out;
    const auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(out);
    sink->set_pattern("%l: %v");
    HostConfig config;
    config.logger = std::make_shared<spdlog::logger>("host-test", sink);
    config.logger->set_level(spdlog::level::warn);
    Rig rig{config, IgConfig{}};
    rig.host().on_frame([](HostFrame& f) {
        for (int i = 0; i < 40; ++i) { // 24 + 40 * 48 = 1944 bytes
            static_cast<void>(f.add(EntityControl{}));
        }
    });
    ASSERT_TRUE(rig.run_until(LinkState::Operate));
    rig.steps(4);
    const std::string log = out.str();
    const auto first = log.find("IP-fragmented");
    ASSERT_NE(first, std::string::npos) << log;
    EXPECT_EQ(log.find("IP-fragmented", first + 1), std::string::npos) << log;
}

TEST(HostSend, HostFrameDescribesTheFrameBeingBuilt) {
    Rig rig;
    struct Seen {
        std::uint32_t ig_frame = 0;
        std::uint32_t host_frame = 0;
        std::chrono::microseconds since{};
        Timestamp ig_timestamp{};
    };
    std::vector<Seen> seen;
    rig.host().on_frame([&](HostFrame& f) {
        seen.push_back(
            {f.ig_frame_number(), f.host_frame_number(), f.since_last_frame(), f.ig_timestamp()});
    });
    ASSERT_TRUE(rig.run_until(LinkState::Operate));
    rig.steps(2);
    ASSERT_GE(seen.size(), 2U);
    EXPECT_EQ(seen[1].ig_frame, seen[0].ig_frame + 1);
    EXPECT_EQ(seen[1].host_frame, seen[0].host_frame + 1);
    EXPECT_NEAR(static_cast<double>(seen[1].since.count()), 16670.0, 100.0);
    EXPECT_GT(seen[1].ig_timestamp, seen[0].ig_timestamp);
}

// ---- receiving --------------------------------------------------------------------------------

TEST(HostReceive, PacketsReachTheirHandlersAndUnknownOnesTheUnknownHandler) {
    RawPeer peer;
    FakeClock clock;
    auto host = Host::over(host_config_with(clock), peer.take_other()).value();
    HatHotResponse response;
    response.hat_hot_id = 7;
    response.valid = true;
    response.height = 12.5;
    std::optional<HatHotResponse> got;
    int unknown = 0;
    host.on<HatHotResponse>([&](const HatHotResponse& r) { got = r; });
    host.on_unknown([&](const UnknownPacket&) { ++unknown; });
    Bytes message = sof_message(1, IgMode::Operate);
    const Bytes packet = encoded(response);
    message.insert(message.end(), packet.begin(), packet.end());
    peer.send(with_unknown_packet(message));
    ASSERT_TRUE(host.poll(0ms));
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->hat_hot_id, 7);
    EXPECT_DOUBLE_EQ(got->height, 12.5);
    EXPECT_EQ(unknown, 1);
    EXPECT_EQ(host.stats().session.unknown_packets, 1U);
}

TEST(HostReceive, BadDatagramsGoToOnErrorAndTheCounters) {
    RawPeer peer;
    FakeClock clock;
    auto host = Host::over(host_config_with(clock), peer.take_other()).value();
    std::vector<ErrorCode> codes;
    host.on_error([&](const Error& e) { codes.push_back(e.code); });
    peer.send(Bytes(3));                            // not a message
    peer.send(control_message(1, IgMode::Operate)); // Host -> IG, sent to a Host
    peer.send(Bytes(70'000));                       // larger than any datagram the Host accepts
    ASSERT_TRUE(host.poll(0ms));
    ASSERT_EQ(codes.size(), 3U);
    EXPECT_EQ(codes[1], ErrorCode::WrongDirection);
    EXPECT_EQ(codes[2], ErrorCode::BufferTooSmall);
    EXPECT_EQ(host.stats().receive_errors, 3U);
    EXPECT_EQ(host.stats().datagrams_received, 2U); // the oversize one was discarded unread
    EXPECT_EQ(host.state(), LinkState::WaitingForIg);
}

TEST(HostReceive, DecodeErrorsInsideAMessageReachOnError) {
    RawPeer peer;
    FakeClock clock;
    auto host = Host::over(host_config_with(clock), peer.take_other()).value();
    std::vector<ErrorCode> codes;
    host.on_error([&](const Error& e) { codes.push_back(e.code); });
    Bytes message = sof_message(1, IgMode::Operate);
    const auto bad = bytes_of(102, 8, 0, 0, 0, 0, 0, 0); // HAT/HOT Response with a size of 8
    message.insert(message.end(), bad.begin(), bad.end());
    peer.send(message);
    ASSERT_TRUE(host.poll(0ms));
    EXPECT_EQ(codes.size(), 1U);
    EXPECT_EQ(host.state(), LinkState::Operate); // the Start of Frame itself was fine
}

// ---- control and lifecycle ---------------------------------------------------------------------

TEST(HostControl, ModeAndDatabaseRequestsPassThroughToTheSession) {
    RawPeer peer;
    FakeClock clock;
    auto host = Host::over(host_config_with(clock), peer.take_other()).value();
    EXPECT_FALSE(host.request_mode(IgMode::OfflineMaintenance));
    EXPECT_FALSE(host.request_database(200));
    EXPECT_TRUE(host.request_mode(IgMode::Debug));
    EXPECT_TRUE(host.request_database(9));
    peer.send(sof_message(1, IgMode::ResetStandby));
    ASSERT_TRUE(host.poll(0ms));
    const auto sent = peer.received();
    ASSERT_FALSE(sent.empty());
    const IgControl control = control_of(sent.back());
    EXPECT_EQ(control.ig_mode, IgMode::Debug);
    EXPECT_EQ(control.database_number, 9);
    const Host& view = host;
    EXPECT_EQ(view.session().requested_mode(), IgMode::Debug);
}

TEST(HostControl, CloseRequestsStandbyFirstAndTheStateBecomesClosed) {
    Rig rig;
    std::vector<Transition> changes;
    watch(rig.host(), changes);
    ASSERT_TRUE(rig.run_until(LinkState::Operate));
    rig.host().close();
    EXPECT_EQ(rig.host().state(), LinkState::Closed);
    EXPECT_EQ(changes.back(), (Transition{LinkState::Operate, LinkState::Closed}));
    EXPECT_TRUE(rig.ig().poll(0ms));
    EXPECT_EQ(rig.ig().mode(), IgMode::ResetStandby); // the IG obeyed the Standby request

    const auto status = rig.host().poll(0ms); // nothing happens any more
    ASSERT_TRUE(status);
    EXPECT_EQ(*status, PollStatus{});
    const auto count = changes.size();
    rig.host().close(); // idempotent
    EXPECT_EQ(changes.size(), count);
}

TEST(HostControl, CloseWithoutStandbyOnCloseSendsNothing) {
    RawPeer peer;
    FakeClock clock;
    HostConfig config = host_config_with(clock);
    config.standby_on_close = false;
    auto host = Host::over(config, peer.take_other()).value();
    peer.send(sof_message(1, IgMode::Operate));
    ASSERT_TRUE(host.poll(0ms));
    static_cast<void>(peer.received());
    host.close();
    EXPECT_TRUE(peer.received().empty());
}

TEST(HostControl, TheDestructorClosesWithoutCallingBack) {
    RawPeer peer;
    FakeClock clock;
    int callbacks = 0;
    {
        auto host = Host::over(host_config_with(clock), peer.take_other()).value();
        host.on_state_change([&](LinkState, LinkState) { ++callbacks; });
        peer.send(sof_message(1, IgMode::Operate));
        ASSERT_TRUE(host.poll(0ms));
        ASSERT_EQ(callbacks, 1);
        static_cast<void>(peer.received());
    }
    EXPECT_EQ(callbacks, 1);
    const auto sent = peer.received();
    ASSERT_EQ(sent.size(), 1U); // the Standby request
    EXPECT_EQ(control_of(sent.front()).ig_mode, IgMode::ResetStandby);
}

TEST(HostControl, AHostIsMovable) {
    RawPeer peer;
    FakeClock clock;
    auto first = Host::over(host_config_with(clock), peer.take_other()).value();
    peer.send(sof_message(1, IgMode::Operate));
    ASSERT_TRUE(first.poll(0ms));
    Host second = std::move(first);
    EXPECT_EQ(second.state(), LinkState::Operate);
    RawPeer other_peer;
    auto third = Host::over(host_config_with(clock), other_peer.take_other()).value();
    third = std::move(second);
    EXPECT_EQ(third.state(), LinkState::Operate);
}

TEST(HostControl, StatsAccountForTimeInEachState) {
    Rig rig;
    rig.host().on_frame([](HostFrame&) {});
    ASSERT_TRUE(rig.run_until(LinkState::Operate));
    rig.steps(60);
    const HostStats stats = rig.host().stats();
    EXPECT_GT(stats.time_in(LinkState::WaitingForIg), 0us);
    EXPECT_NEAR(static_cast<double>(stats.time_in(LinkState::Operate).count()), 1.0e6, 0.1e6);
    EXPECT_EQ(stats.session.messages_sent, stats.frames_sent);
    EXPECT_GT(stats.datagrams_received, 50U);
    EXPECT_GT(stats.frame_callbacks, 0U);
}

TEST(HostControl, RunReturnsOnStopAndOnClose) {
    RawPeer peer;
    auto host = Host::over(HostConfig{}, peer.take_other()).value(); // real clock
    std::stop_source stop;
    Result<void> result = std::unexpected(Error{.code = ErrorCode::Timeout});
    std::thread thread([&] { result = host.run(stop.get_token(), 5ms); });
    std::this_thread::sleep_for(60ms);
    stop.request_stop();
    thread.join();
    EXPECT_TRUE(result);
    EXPECT_FALSE(peer.received().empty()); // it probed while waiting

    host.close();
    EXPECT_TRUE(host.run(std::stop_token{}, 5ms)); // closed: returns at once
}

// ---- failures -------------------------------------------------------------------------------

TEST(HostFailure, AFatalTransportErrorEndsPollAndRun) {
    auto receive_broken = std::make_unique<BrokenTransport>();
    receive_broken->fail_receive = true;
    auto host = Host::over(HostConfig{}, std::move(receive_broken)).value();
    const auto polled = host.poll(0ms);
    ASSERT_FALSE(polled);
    EXPECT_EQ(polled.error().code, ErrorCode::TransportError);
    EXPECT_EQ(polled.error().os_error, 7);
    const auto run = host.run(std::stop_token{}, 1ms);
    ASSERT_FALSE(run);
    EXPECT_EQ(run.error().os_error, 7);

    auto send_broken = std::make_unique<BrokenTransport>();
    send_broken->fail_send = true;
    auto sender = Host::over(HostConfig{}, std::move(send_broken)).value();
    const auto sent = sender.poll(0ms); // the first probe fails to leave
    ASSERT_FALSE(sent);
    EXPECT_EQ(sent.error().os_error, 5);
}

TEST(HostFailure, ABrokenTransportDoesNotMakeTheDestructorThrowOrHang) {
    auto broken = std::make_unique<BrokenTransport>();
    broken->fail_send = true;
    auto host = Host::over(HostConfig{}, std::move(broken)).value();
    host.close(); // the Standby request cannot be sent: logged, then closed anyway
    EXPECT_EQ(host.state(), LinkState::Closed);
}

TEST(HostFailure, InvalidConfigurationIsRejected) {
    const auto reject = [](auto edit) {
        HostConfig config;
        edit(config);
        auto [a, b] = make_memory_transport_pair();
        const auto host = Host::over(config, std::move(a));
        return !host && host.error().code == ErrorCode::InvalidFieldValue;
    };
    EXPECT_FALSE(reject([](HostConfig&) {})); // the defaults are fine
    EXPECT_TRUE(reject([](HostConfig& c) { c.requested_mode = IgMode::OfflineMaintenance; }));
    EXPECT_TRUE(reject([](HostConfig& c) { c.requested_mode = static_cast<IgMode>(9); }));
    EXPECT_TRUE(reject([](HostConfig& c) { c.database = 128; }));
    EXPECT_TRUE(reject([](HostConfig& c) { c.database = -1; }));
    EXPECT_TRUE(reject([](HostConfig& c) {
        c.synchronous = false;
        c.async_rate_hz = 0.0;
    }));
    EXPECT_TRUE(reject([](HostConfig& c) { c.peer_timeout = 0ms; }));
    EXPECT_TRUE(reject([](HostConfig& c) { c.probe_interval = 0ms; }));
    EXPECT_TRUE(reject([](HostConfig& c) { c.max_message_bytes = 16; }));

    EXPECT_FALSE(Host::over(HostConfig{}, nullptr));
    const auto no_address = Host::connect(HostConfig{});
    ASSERT_FALSE(no_address);
    EXPECT_EQ(no_address.error().code, ErrorCode::InvalidFieldValue);
    HostConfig bad_address;
    bad_address.ig_address = "not-an-address";
    const auto unresolved = Host::connect(bad_address);
    ASSERT_FALSE(unresolved);
    EXPECT_EQ(unresolved.error().code, ErrorCode::InvalidFieldValue);
    HostConfig bad_mode;
    bad_mode.ig_address = "127.0.0.1";
    bad_mode.database = 500;
    EXPECT_FALSE(Host::connect(bad_mode));
}

TEST(HostFailure, LinkStateHasNamesAndTraits) {
    for (std::size_t i = 0; i < link_state_count; ++i) {
        const auto s = static_cast<LinkState>(i);
        EXPECT_TRUE(EnumTraits<LinkState>::is_defined(s));
        EXPECT_NE(to_string(s), "?");
    }
    EXPECT_FALSE(EnumTraits<LinkState>::is_defined(static_cast<LinkState>(link_state_count)));
    EXPECT_EQ(to_string(static_cast<LinkState>(200)), "?");
    EXPECT_EQ(to_string(LinkState::LoadingDatabase), "LoadingDatabase");
}

} // namespace
