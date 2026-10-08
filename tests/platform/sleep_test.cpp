#include "cigi/platform/sleep.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <limits>

namespace {

using cigi::platform::FramePacer;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

TEST(Sleep, SleepsAtLeastUntilDeadline) {
    const auto deadline = Clock::now() + 20ms;
    cigi::platform::sleep_until(deadline);
    EXPECT_GE(Clock::now(), deadline);
}

TEST(Sleep, PastDeadlineReturnsPromptly) {
    const auto t0 = Clock::now();
    cigi::platform::sleep_until(t0 - 1s);
    EXPECT_LT(Clock::now() - t0, 500ms);
}

TEST(FramePacer, OnTimeTickHasNoOverrun) {
    const auto start = Clock::now();
    FramePacer p{10ms, start};
    const auto t = p.complete(start + 10ms);
    EXPECT_EQ(t.scheduled, start + 10ms);
    EXPECT_EQ(t.overrun, 0ns);
    EXPECT_EQ(t.skipped, 0U);
}

TEST(FramePacer, EarlyWakeClampsOverrunToZero) {
    const auto start = Clock::now();
    FramePacer p{10ms, start};
    EXPECT_EQ(p.complete(start + 9ms).overrun, 0ns);
}

TEST(FramePacer, LateTickReportsOverrunAndKeepsGrid) {
    const auto start = Clock::now();
    FramePacer p{10ms, start};
    const auto t = p.complete(start + 14ms);
    EXPECT_EQ(t.overrun, 4ms);
    EXPECT_EQ(t.skipped, 0U);
    // Next tick stays on the original grid (20 ms), not 24 ms.
    EXPECT_EQ(p.complete(start + 20ms).scheduled, start + 20ms);
}

TEST(FramePacer, VeryLateTickSkipsMissedPeriods) {
    const auto start = Clock::now();
    FramePacer p{10ms, start};
    const auto t = p.complete(start + 35ms); // due 10; periods 20 and 30 are missed
    EXPECT_EQ(t.overrun, 25ms);
    EXPECT_EQ(t.skipped, 2U);
    EXPECT_EQ(p.complete(start + 40ms).scheduled, start + 40ms);
}

TEST(FramePacer, ResetRestartsGrid) {
    const auto start = Clock::now();
    FramePacer p{10ms, start};
    p.complete(start + 100ms);
    p.reset(start + 200ms);
    EXPECT_EQ(p.complete(start + 210ms).scheduled, start + 210ms);
}

TEST(FramePacer, PeriodClampedAndFromHz) {
    EXPECT_EQ(FramePacer{0ns}.period(), 1ns);
    EXPECT_EQ(FramePacer{-5ns}.period(), 1ns);
    EXPECT_EQ(FramePacer::from_hz(100.0).period(), 10ms);
    EXPECT_EQ(FramePacer::from_hz(0.0).period(), 1s);
    EXPECT_EQ(FramePacer::from_hz(-3.0).period(), 1s);
    EXPECT_EQ(FramePacer::from_hz(std::numeric_limits<double>::quiet_NaN()).period(), 1s);
    EXPECT_EQ(FramePacer::from_hz(std::numeric_limits<double>::infinity()).period(), 1ns);
    EXPECT_EQ(FramePacer::from_hz(1e-300).period(), 1h);
    EXPECT_EQ(FramePacer::from_hz(60.0).period(), 16666667ns); // rounded, not truncated
}

TEST(FramePacer, SixtyHertzMeanPeriod) {
    FramePacer p = FramePacer::from_hz(60.0);
    const auto t0 = Clock::now();
    constexpr int frames = 30; // ~0.5 s
    for (int i = 0; i < frames; ++i) {
        p.wait_next();
    }
    const auto elapsed = Clock::now() - t0;
    const auto mean = elapsed / frames;
    // Lenient: CI machines are noisy, but a fixed grid keeps the mean close to 16.67 ms.
    EXPECT_GE(mean, 14ms);
    EXPECT_LE(mean, 20ms);
}

} // namespace
