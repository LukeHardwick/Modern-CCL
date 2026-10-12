// The in-memory transport and the fault-injecting decorator.

#include "support.hpp"

#include <gtest/gtest.h>
#include <thread>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,bugprone-unused-return-value):
// tests index vectors after asserting their size and drive poll() for its effects.

namespace {

using namespace cigi;
using namespace cigi::test;

Bytes numbered(std::uint8_t n) {
    return {8, static_cast<std::byte>(n)};
}

/// Drain what `t` has to receive.
std::vector<std::uint8_t> drain(DatagramTransport& t) {
    std::vector<std::uint8_t> got;
    Bytes buffer(64);
    while (const auto n = t.receive(buffer, 0ms)) {
        EXPECT_EQ(*n, 8U);
        got.push_back(std::to_integer<std::uint8_t>(buffer[0]));
    }
    return got;
}

// ---- memory transport ---------------------------------------------------------------------------

TEST(MemoryTransport, DeliversInOrderBothWays) {
    auto [a, b] = make_memory_transport_pair();
    ASSERT_TRUE(a->send(numbered(1)));
    ASSERT_TRUE(a->send(numbered(2)));
    ASSERT_TRUE(b->send(numbered(9)));
    EXPECT_EQ(drain(*b), (std::vector<std::uint8_t>{1, 2}));
    EXPECT_EQ(drain(*a), (std::vector<std::uint8_t>{9}));
    Bytes buffer(8);
    const auto none = a->receive(buffer, 0ms);
    ASSERT_FALSE(none);
    EXPECT_EQ(none.error().code, ErrorCode::Timeout);
}

TEST(MemoryTransport, AnOversizeDatagramIsDiscarded) {
    auto [a, b] = make_memory_transport_pair();
    ASSERT_TRUE(a->send(Bytes(100)));
    ASSERT_TRUE(a->send(numbered(5)));
    Bytes small(16);
    const auto first = b->receive(small, 0ms);
    ASSERT_FALSE(first);
    EXPECT_EQ(first.error().code, ErrorCode::BufferTooSmall);
    EXPECT_EQ(drain(*b), (std::vector<std::uint8_t>{5}));
}

TEST(MemoryTransport, ReceiveWaitsForTheTimeout) {
    auto [a, b] = make_memory_transport_pair();
    std::thread sender([&] {
        std::this_thread::sleep_for(30ms);
        EXPECT_TRUE(a->send(numbered(7)));
    });
    Bytes buffer(8);
    const auto n = b->receive(buffer, 2000ms);
    sender.join();
    ASSERT_TRUE(n);
    EXPECT_EQ(*n, 8U);
    const auto timed_out = b->receive(buffer, 10ms);
    ASSERT_FALSE(timed_out);
    EXPECT_EQ(timed_out.error().code, ErrorCode::Timeout);
}

// ---- fault injection ----------------------------------------------------------------------------

struct FaultPair {
    FakeClock clock;
    std::unique_ptr<FaultInjectingTransport> faulty;
    std::unique_ptr<DatagramTransport> far;

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    explicit FaultPair(std::uint64_t seed = 1, std::uint32_t start = 0) : clock(start) {
        auto [a, b] = make_memory_transport_pair();
        faulty = std::make_unique<FaultInjectingTransport>(std::move(a), clock.fn(), seed);
        far = std::move(b);
    }
};

TEST(FaultTransport, PassesEverythingWhenNoFaultIsSet) {
    FaultPair p;
    for (std::uint8_t i = 0; i < 5; ++i) {
        ASSERT_TRUE(p.faulty->send(numbered(i)));
    }
    EXPECT_EQ(drain(*p.far), (std::vector<std::uint8_t>{0, 1, 2, 3, 4}));
    EXPECT_EQ(p.faulty->stats().sent, 5U);
    EXPECT_EQ(p.faulty->stats().dropped, 0U);
    // The receive side is untouched.
    ASSERT_TRUE(p.far->send(numbered(8)));
    EXPECT_EQ(drain(*p.faulty), (std::vector<std::uint8_t>{8}));
}

TEST(FaultTransport, DropsEverythingAtProbabilityOne) {
    FaultPair p;
    p.faulty->set_drop_probability(1.0);
    for (std::uint8_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(p.faulty->send(numbered(i))); // a lost datagram is not a send failure
    }
    EXPECT_TRUE(drain(*p.far).empty());
    EXPECT_EQ(p.faulty->stats().dropped, 4U);
    p.faulty->set_drop_probability(0.0);
    ASSERT_TRUE(p.faulty->send(numbered(9)));
    EXPECT_EQ(drain(*p.far), (std::vector<std::uint8_t>{9}));
}

TEST(FaultTransport, DropsAboutTheRequestedShareAndIsReproducible) {
    const auto run = [](std::uint64_t seed) {
        FaultPair p{seed};
        p.faulty->set_drop_probability(0.25);
        std::vector<std::uint8_t> const survived;
        for (int i = 0; i < 2000; ++i) {
            static_cast<void>(p.faulty->send(numbered(static_cast<std::uint8_t>(i))));
        }
        return drain(*p.far).size();
    };
    const std::size_t first = run(42);
    EXPECT_EQ(first, run(42)); // same seed, same losses
    EXPECT_GT(first, 1350U);   // 25% of 2000 dropped: about 1500 survive
    EXPECT_LT(first, 1650U);
    EXPECT_NE(first, run(43)); // a different seed gives a different run
}

TEST(FaultTransport, DuplicatesDatagrams) {
    FaultPair p;
    p.faulty->set_duplicate_probability(1.0);
    ASSERT_TRUE(p.faulty->send(numbered(3)));
    EXPECT_EQ(drain(*p.far), (std::vector<std::uint8_t>{3, 3}));
    EXPECT_EQ(p.faulty->stats().duplicated, 1U);
}

TEST(FaultTransport, DelaysDatagramsUntilTheClockReleasesThem) {
    FaultPair p;
    p.faulty->set_added_latency(20ms); // 2000 ticks
    ASSERT_TRUE(p.faulty->send(numbered(1)));
    p.clock.advance(1000);
    ASSERT_TRUE(p.faulty->send(numbered(2)));
    EXPECT_TRUE(drain(*p.far).empty());
    EXPECT_EQ(p.faulty->pending(), 2U);

    p.clock.advance(1000); // 20 ms after the first
    Bytes buffer(8);
    EXPECT_FALSE(p.faulty->receive(buffer, 0ms)); // releases from receive() too
    EXPECT_EQ(drain(*p.far), (std::vector<std::uint8_t>{1}));
    p.clock.advance(1000);
    ASSERT_TRUE(p.faulty->send(numbered(3))); // and from send(), before the new one is queued
    EXPECT_EQ(drain(*p.far), (std::vector<std::uint8_t>{2}));
    EXPECT_EQ(p.faulty->stats().delayed, 3U);
    EXPECT_EQ(p.faulty->stats().released, 2U);

    p.faulty->set_added_latency(0us);
    p.clock.advance(5000);
    ASSERT_TRUE(p.faulty->send(numbered(4)));
    EXPECT_EQ(drain(*p.far), (std::vector<std::uint8_t>{3, 4}));
}

TEST(FaultTransport, DelayHandlesTheClockWrappingAround) {
    FaultPair p{1, 0xFFFF'FF00U};
    p.faulty->set_added_latency(5ms); // 500 ticks: the release time wraps past zero
    ASSERT_TRUE(p.faulty->send(numbered(1)));
    p.clock.advance(300);
    Bytes buffer(8);
    static_cast<void>(p.faulty->receive(buffer, 0ms));
    EXPECT_TRUE(drain(*p.far).empty());
    p.clock.advance(300);
    static_cast<void>(p.faulty->receive(buffer, 0ms));
    EXPECT_EQ(drain(*p.far), (std::vector<std::uint8_t>{1}));
}

TEST(FaultTransport, FaultsCombine) {
    FaultPair p;
    p.faulty->set_duplicate_probability(1.0);
    p.faulty->set_added_latency(10ms);
    ASSERT_TRUE(p.faulty->send(numbered(1)));
    p.clock.advance(1000);
    ASSERT_TRUE(p.faulty->send(numbered(2)));
    EXPECT_EQ(drain(*p.far), (std::vector<std::uint8_t>{1, 1}));
}

TEST(FaultTransport, SettersClampAndGettersReport) {
    FaultPair p;
    p.faulty->set_drop_probability(7.0);
    EXPECT_EQ(p.faulty->drop_probability(), 1.0);
    p.faulty->set_drop_probability(-1.0);
    EXPECT_EQ(p.faulty->drop_probability(), 0.0);
    p.faulty->set_drop_probability(std::nan(""));
    EXPECT_EQ(p.faulty->drop_probability(), 0.0);
    p.faulty->set_duplicate_probability(0.5);
    EXPECT_EQ(p.faulty->duplicate_probability(), 0.5);
    p.faulty->set_added_latency(-5ms);
    EXPECT_EQ(p.faulty->added_latency(), 0us);
    p.faulty->set_added_latency(3ms);
    EXPECT_EQ(p.faulty->added_latency(), 3ms);
}

TEST(FaultTransport, ReportsTheInnerTransportsFailure) {
    auto broken = std::make_unique<BrokenTransport>();
    broken->fail_send = true;
    FaultInjectingTransport faulty{std::move(broken)}; // default clock and seed
    faulty.set_duplicate_probability(1.0);
    const auto sent = faulty.send(numbered(1));
    ASSERT_FALSE(sent);
    EXPECT_EQ(sent.error().os_error, 5);
}

TEST(FaultTransport, ReceiveWakesForTheNextReleaseInsteadOfSleepingThrough) {
    FaultPair p;
    p.faulty->set_added_latency(30ms); // 3000 ticks
    ASSERT_TRUE(p.faulty->send(numbered(1)));
    p.clock.advance(3000); // due now, but nothing has released it yet
    Bytes buffer(8);
    EXPECT_FALSE(p.faulty->receive(buffer, 0ms)); // releases it
    EXPECT_EQ(drain(*p.far), (std::vector<std::uint8_t>{1}));

    // A release due in the future caps the wait of a long receive().
    ASSERT_TRUE(p.faulty->send(numbered(2)));
    const auto capped_start = std::chrono::steady_clock::now();
    EXPECT_FALSE(p.faulty->receive(buffer, 2s));
    EXPECT_LT(std::chrono::steady_clock::now() - capped_start, 1500ms); // woke after ~30 ms
    EXPECT_EQ(p.faulty->pending(), 1U); // the fake clock has not moved: still waiting
}

TEST(FaultTransport, AFailingInnerSendOfAReleasedDatagramIsReported) {
    auto broken = std::make_unique<BrokenTransport>();
    BrokenTransport* inner = broken.get();
    FakeClock clock;
    FaultInjectingTransport faulty{std::move(broken), clock.fn()};
    faulty.set_added_latency(10ms);
    ASSERT_TRUE(faulty.send(numbered(1))); // queued
    inner->fail_send = true;
    clock.advance(1000);
    Bytes buffer(8);
    const auto released = faulty.receive(buffer, 0ms);
    ASSERT_FALSE(released);
    EXPECT_EQ(released.error().os_error, 5);

    faulty.set_added_latency(10ms);
    inner->fail_send = false;
    ASSERT_TRUE(faulty.send(numbered(2)));
    inner->fail_send = true;
    clock.advance(1000);
    faulty.set_added_latency(0us);
    const auto sent = faulty.send(numbered(3)); // releases 2 (fails) and sends 3 (fails)
    EXPECT_FALSE(sent);
}

TEST(FaultTransport, WorksUnderAHostAndAnIg) {
    // Dropping every Start of Frame the IG sends makes a connected Host lose the link.
    Rig rig{/*faults=*/true};
    ASSERT_TRUE(rig.run_until(LinkState::Operate));
    rig.ig_faults()->set_drop_probability(1.0);
    rig.steps(80);
    EXPECT_EQ(rig.host().state(), LinkState::Lost);
    rig.ig_faults()->set_drop_probability(0.0);
    EXPECT_TRUE(rig.run_until(LinkState::Operate));
    EXPECT_GT(rig.ig_faults()->stats().dropped, 50U);
    EXPECT_EQ(rig.host_faults()->stats().dropped, 0U);
}

TEST(FaultTransport, DuplicatedAndDelayedFramesAreAbsorbedByTheSessions) {
    Rig rig{/*faults=*/true};
    rig.ig_faults()->set_duplicate_probability(1.0);
    rig.ig_faults()->set_added_latency(30ms);
    ASSERT_TRUE(rig.run_until(LinkState::Operate));
    rig.steps(30);
    EXPECT_EQ(rig.host().state(), LinkState::Operate);
    EXPECT_GT(rig.host().session().stats().duplicate_frames, 20U);
}

} // namespace
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,bugprone-unused-return-value)
