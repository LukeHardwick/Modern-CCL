// Peer learning, sender filtering hooks and the small facade additions of T09d: the IG learns its
// Host through DatagramTransport::has_peer / latch_peer / forget_peer (ICD 2.8), a message is
// delivered only after its sender was adopted, on_ig_control, local_port, foreign-datagram stats.

#include "support.hpp"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <utility>

namespace {

using namespace cigi;
using namespace cigi::test;

struct LearnerFixture {
    RawPeer peer;
    FakeClock clock;
    LearningTransport* transport = nullptr;
    Ig ig;

    explicit LearnerFixture(IgConfig config = {}) : ig(make(std::move(config), *this)) {}

    static Ig make(IgConfig config, LearnerFixture& f) {
        config.session.clock = f.clock.fn();
        auto transport = std::make_unique<LearningTransport>(f.peer.take_other());
        f.transport = transport.get();
        return Ig::over(std::move(config), std::move(transport)).value();
    }
    void tick(std::uint32_t ticks = frame_ticks) {
        clock.advance(ticks);
        EXPECT_TRUE(ig.poll(0ms).has_value());
    }
};

/// A Host message in Operate with one Entity Control after the IG Control.
Bytes operate_with_entity(std::uint32_t frame) {
    Bytes m = control_message(frame, IgMode::Operate);
    const Bytes entity = encoded(EntityControl{});
    m.insert(m.end(), entity.begin(), entity.end());
    return m;
}

TEST(PeerLearning, AnIgSendsNothingUntilItsTransportHasAPeer) {
    LearnerFixture f;
    f.tick();
    f.tick();
    EXPECT_TRUE(f.peer.received().empty());
    EXPECT_EQ(f.transport->sends_without_peer, 0);
    EXPECT_EQ(f.ig.stats().frames_unrouted, 2U);
    EXPECT_EQ(f.ig.stats().frames_sent, 0U);
}

TEST(PeerLearning, OnlyAHostMessageNominatesAPeer) {
    LearnerFixture f;
    f.peer.send(Bytes(3));
    f.peer.send(sof_message(1)); // IG -> Host: not a Host message
    f.tick();
    EXPECT_EQ(f.transport->latch_calls, 0);
    EXPECT_FALSE(f.transport->has_peer());
    EXPECT_FALSE(f.ig.host_connected());

    f.peer.send(control_message(1));
    f.tick();
    EXPECT_EQ(f.transport->latch_calls, 1);
    EXPECT_TRUE(f.transport->has_peer());
    EXPECT_TRUE(f.ig.host_connected());
    EXPECT_EQ(f.peer.received().size(), 1U); // the Start of Frame of that tick now goes out
}

/// A logger that writes into `out`.
std::shared_ptr<spdlog::logger> capture_logger(std::ostringstream& out) {
    const auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(out);
    auto logger = std::make_shared<spdlog::logger>("peer_test", sink);
    logger->set_level(spdlog::level::info);
    return logger;
}

std::size_t count_of(const std::string& text, const std::string& needle) {
    std::size_t n = 0;
    for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        ++n;
    }
    return n;
}

TEST(PeerLearning, AMessageWhoseSenderCannotBeAdoptedReachesNoHandler) {
    std::ostringstream log;
    IgConfig config;
    config.logger = capture_logger(log);
    LearnerFixture f{config};
    int entities = 0;
    f.ig.on<EntityControl>([&](const EntityControl&) { ++entities; });
    int controls = 0;
    f.ig.on_ig_control([&](const IgControl&) { ++controls; });
    f.transport->latch_failures = 2;

    f.peer.send(operate_with_entity(1));
    f.tick();
    EXPECT_EQ(entities, 0);
    EXPECT_EQ(controls, 0);
    EXPECT_EQ(f.ig.session().stats().messages_received, 0U);
    EXPECT_EQ(f.ig.mode(), IgMode::ResetStandby); // its mode request was not applied either
    EXPECT_EQ(f.ig.stats().receive_errors, 1U);
    EXPECT_FALSE(f.ig.host_connected());
    EXPECT_FALSE(f.transport->has_peer());

    // The next one fails again (logged once only); the one after that succeeds and is delivered.
    f.peer.send(operate_with_entity(2));
    f.tick();
    EXPECT_EQ(f.transport->latch_calls, 2);
    EXPECT_EQ(f.ig.stats().receive_errors, 2U);
    EXPECT_EQ(count_of(log.str(), "cannot adopt"), 1U);
    EXPECT_EQ(count_of(log.str(), "Host connected"), 0U);
    f.peer.send(operate_with_entity(3));
    f.tick();
    EXPECT_EQ(f.transport->latch_calls, 3);
    EXPECT_EQ(entities, 1);
    EXPECT_EQ(controls, 1);
    EXPECT_EQ(f.ig.mode(), IgMode::Operate);
    EXPECT_TRUE(f.ig.host_connected());
    f.peer.send(operate_with_entity(4));
    f.tick();
    EXPECT_EQ(count_of(log.str(), "Host connected"), 1U); // once, on the first successful latch
    EXPECT_EQ(count_of(log.str(), "cannot adopt"), 1U);
}

TEST(PeerLearning, AHostThatWentQuietIsForgottenSoANewOneCanBeLearnt) {
    LearnerFixture f;
    f.peer.send(control_message(1));
    f.tick();
    ASSERT_TRUE(f.transport->has_peer());
    EXPECT_EQ(f.transport->forget_calls, 0);

    f.tick(110'000); // 1.1 s of silence: past peer_timeout
    EXPECT_EQ(f.transport->forget_calls, 1);
    EXPECT_FALSE(f.transport->has_peer());
    EXPECT_FALSE(f.ig.host_connected());
    const auto unrouted = f.ig.stats().frames_unrouted;
    f.tick();
    EXPECT_EQ(f.ig.stats().frames_unrouted, unrouted + 1);

    f.peer.send(control_message(1));
    f.tick();
    EXPECT_EQ(f.transport->latch_calls, 2);
    EXPECT_TRUE(f.transport->has_peer());
    EXPECT_TRUE(f.ig.host_connected());
}

TEST(PeerLearning, ARestartForgetsTheHost) {
    LearnerFixture f;
    f.peer.send(control_message(1));
    f.tick();
    ASSERT_TRUE(f.transport->has_peer());
    f.ig.restart();
    EXPECT_EQ(f.transport->forget_calls, 1);
    EXPECT_FALSE(f.transport->has_peer());
    f.tick();
    EXPECT_GE(f.ig.stats().frames_unrouted, 1U);
}

TEST(PeerLearning, AClosedIgKeepsItsForeignCountAndHasNoPort) {
    LearnerFixture f;
    f.transport->foreign = 7;
    f.transport->port = 4321;
    EXPECT_EQ(f.ig.local_port(), 4321);
    EXPECT_EQ(f.ig.stats().foreign_datagrams, 7U);
    f.ig.close();
    f.ig.restart(); // does not touch the dropped transport
    EXPECT_EQ(f.ig.local_port(), 0);
    EXPECT_EQ(f.ig.stats().foreign_datagrams, 7U);
}

TEST(PeerLearning, DecoratorsForwardTheHooks) {
    RawPeer peer;
    const FakeClock clock;
    auto learning = std::make_unique<LearningTransport>(peer.take_other());
    LearningTransport* inner = learning.get();
    inner->foreign = 3;
    inner->port = 99;
    auto faults = std::make_unique<FaultInjectingTransport>(std::move(learning), clock.fn(), 1);
    EXPECT_FALSE(faults->has_peer());
    EXPECT_TRUE(faults->latch_peer());
    EXPECT_TRUE(faults->has_peer());
    EXPECT_EQ(faults->foreign_datagrams(), 3U);
    EXPECT_EQ(faults->local_port(), 99);
    faults->forget_peer();
    EXPECT_FALSE(inner->has_peer());
    EXPECT_EQ(inner->forget_calls, 1);

    // And an IG over the decorator learns through it.
    IgConfig config;
    config.session.clock = clock.fn();
    auto ig = Ig::over(config, std::move(faults)).value();
    peer.send(control_message(1));
    clock.advance(frame_ticks);
    ASSERT_TRUE(ig.poll(0ms).has_value());
    EXPECT_TRUE(inner->has_peer());
    EXPECT_EQ(ig.local_port(), 99);
}

TEST(PeerLearning, ATransportWithoutHooksIsAlwaysKnown) {
    auto [a, b] = make_memory_transport_pair();
    EXPECT_TRUE(a->has_peer());
    EXPECT_TRUE(a->latch_peer());
    a->forget_peer();
    EXPECT_TRUE(a->has_peer());
    EXPECT_EQ(a->foreign_datagrams(), 0U);
    EXPECT_EQ(a->local_port(), 0);
}

TEST(PeerLearning, TheHostCountsForeignDatagramsAndKnowsItsPort) {
    RawPeer peer;
    const FakeClock clock;
    auto transport = std::make_unique<LearningTransport>(peer.take_other());
    transport->foreign = 5;
    transport->port = 8005;
    HostConfig config;
    config.session.clock = clock.fn();
    auto host = Host::over(config, std::move(transport)).value();
    EXPECT_EQ(host.local_port(), 8005);
    EXPECT_EQ(host.stats().foreign_datagrams, 5U);
    host.close();
    EXPECT_EQ(host.local_port(), 0);
    EXPECT_EQ(host.stats().foreign_datagrams, 5U);
}

TEST(PeerLearning, TheHostWarnsOnceAboutDroppedSenders) {
    std::ostringstream log;
    RawPeer peer;
    const FakeClock clock;
    auto transport = std::make_unique<LearningTransport>(peer.take_other());
    auto* raw = transport.get();
    HostConfig config;
    config.session.clock = clock.fn();
    config.logger = capture_logger(log);
    auto host = Host::over(config, std::move(transport)).value();
    EXPECT_TRUE(host.poll(0ms).has_value());
    EXPECT_EQ(count_of(log.str(), "filter_senders"), 0U);
    raw->foreign = 2;
    EXPECT_TRUE(host.poll(0ms).has_value());
    raw->foreign = 4;
    EXPECT_TRUE(host.poll(0ms).has_value());
    EXPECT_EQ(count_of(log.str(), "filter_senders"), 1U);
}

TEST(OnIgControl, FiresOncePerCurrentIgControlAfterTheOtherPackets) {
    RawPeer peer;
    FakeClock clock;
    IgConfig config;
    config.session.clock = clock.fn();
    auto ig = Ig::over(config, peer.take_other()).value();
    std::vector<std::string> order;
    std::vector<std::uint32_t> frames;
    ig.on<EntityControl>([&](const EntityControl&) { order.emplace_back("entity"); });
    ig.on_ig_control([&](const IgControl& c) {
        order.emplace_back("control");
        frames.push_back(c.host_frame_number);
    });
    const auto poll = [&] {
        clock.advance(frame_ticks);
        EXPECT_TRUE(ig.poll(0ms).has_value());
    };

    peer.send(operate_with_entity(10));
    poll();
    EXPECT_EQ(order, (std::vector<std::string>{"entity", "control"}));

    peer.send(operate_with_entity(10)); // duplicate: nothing changed, no control
    peer.send(control_message(8));      // stale: older than the newest
    poll();
    EXPECT_EQ(frames, (std::vector<std::uint32_t>{10}));

    peer.send(control_message(11, IgMode::ResetStandby)); // also in Reset/Standby
    peer.send(control_message(12, IgMode::ResetStandby));
    poll();
    EXPECT_EQ(frames, (std::vector<std::uint32_t>{10, 11, 12}));
}

TEST(OnIgControl, AHandlerThatClosesTheIgEndsThePoll) {
    RawPeer peer;
    const FakeClock clock;
    IgConfig config;
    config.session.clock = clock.fn();
    auto ig = Ig::over(config, peer.take_other()).value();
    int calls = 0;
    ig.on_ig_control([&](const IgControl&) {
        ++calls;
        ig.close();
    });
    peer.send(control_message(1));
    peer.send(control_message(2));
    clock.advance(frame_ticks);
    const auto polled = ig.poll(0ms);
    ASSERT_TRUE(polled.has_value());
    EXPECT_FALSE(polled->frame_sent);
    EXPECT_EQ(calls, 1);
}

TEST(PeerLearning, TheFirstDroppedSenderIsWarnedAboutOnce) {
    std::ostringstream log;
    IgConfig config;
    config.logger = capture_logger(log);
    LearnerFixture f{config};
    f.transport->foreign = 0;
    f.tick();
    EXPECT_EQ(count_of(log.str(), "filter_senders"), 0U);
    f.transport->foreign = 1;
    f.tick();
    f.transport->foreign = 3;
    f.tick();
    EXPECT_EQ(count_of(log.str(), "filter_senders"), 1U);
}

TEST(OnIgControl, IsNotCalledInOfflineMaintenance) {
    RawPeer peer;
    const FakeClock clock;
    IgConfig config;
    config.session.clock = clock.fn();
    auto ig = Ig::over(config, peer.take_other()).value();
    int calls = 0;
    ig.on_ig_control([&](const IgControl&) { ++calls; });
    ig.set_mode(IgMode::OfflineMaintenance); // the IG ignores all Host data (ICD 4.2.1)
    peer.send(control_message(1, IgMode::Operate));
    clock.advance(frame_ticks);
    EXPECT_TRUE(ig.poll(0ms).has_value());
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(ig.mode(), IgMode::OfflineMaintenance);
}

} // namespace
