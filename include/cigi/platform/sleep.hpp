#pragma once

#include <chrono>
#include <cstdint>

namespace cigi::platform {

/// Sleep until `deadline` (returns at once if it has passed).
///
/// This is the library's only timing abstraction: on Windows it first raises the system timer
/// resolution to 1 ms (once, for the life of the process) so sleeps are not rounded to ~15.6 ms.
void sleep_until(std::chrono::steady_clock::time_point deadline);

/// Fixed-rate loop helper: call `wait_next()` once per iteration.
///
/// Ticks are scheduled on a fixed grid from the start time, so a late iteration does not shift the
/// following ones. If the loop falls more than one period behind, the missed ticks are skipped (and
/// counted) rather than replayed in a burst.
class FramePacer {
public:
    using Clock = std::chrono::steady_clock;

    /// Outcome of one `wait_next()`.
    struct Tick {
        Clock::time_point scheduled;        ///< When this tick was due.
        Clock::time_point woke;             ///< When the wait returned.
        std::chrono::nanoseconds overrun{}; ///< `woke - scheduled`, never negative.
        std::uint64_t skipped = 0;          ///< Whole periods missed and dropped.
    };

    /// `period` is clamped to at least 1 ns. The first tick is due one period after `start`.
    explicit FramePacer(std::chrono::nanoseconds period, Clock::time_point start = Clock::now());

    /// Pacer for a rate in Hz. The period is `round(1 s / hz)` clamped to [1 ns, 1 h]; a rate that
    /// is NaN, zero or negative gives a 1 s period, and an infinite rate the 1 ns minimum.
    [[nodiscard]] static FramePacer from_hz(double hz, Clock::time_point start = Clock::now());

    /// Sleep until the next tick is due and account for it.
    Tick wait_next();

    /// Bookkeeping half of `wait_next()` for a wake-up observed at `now`; exposed so the schedule
    /// logic can be tested without sleeping.
    Tick complete(Clock::time_point now);

    /// Restart the grid: the next tick is due one period after `start`.
    void reset(Clock::time_point start = Clock::now());

    [[nodiscard]] std::chrono::nanoseconds period() const { return period_; }

private:
    std::chrono::nanoseconds period_;
    Clock::time_point next_;
};

} // namespace cigi::platform
