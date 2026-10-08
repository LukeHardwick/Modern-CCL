#include "cigi/platform/sleep.hpp"

#include <algorithm>
#include <thread>

// PLATFORM: the one place with timing-related #ifdef _WIN32 (plan area P3).
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// timeBeginPeriod / timeEndPeriod live in winmm (linked by CMake on Windows).
#include <mmsystem.h>
#endif

namespace cigi::platform {

namespace {

#ifdef _WIN32
// PLATFORM: Windows sleeps are quantised to ~15.6 ms unless the timer resolution is raised.
// A function-local static keeps it raised for the process lifetime and restores it at exit.
// Unverified: only compiled on Windows/MSVC.
class TimerResolutionGuard {
public:
    TimerResolutionGuard() : active_(timeBeginPeriod(1) == TIMERR_NOERROR) {}
    ~TimerResolutionGuard() {
        if (active_) {
            timeEndPeriod(1);
        }
    }
    TimerResolutionGuard(const TimerResolutionGuard&) = delete;
    TimerResolutionGuard& operator=(const TimerResolutionGuard&) = delete;
    TimerResolutionGuard(TimerResolutionGuard&&) = delete;
    TimerResolutionGuard& operator=(TimerResolutionGuard&&) = delete;

private:
    bool active_;
};
#endif

} // namespace

void sleep_until(std::chrono::steady_clock::time_point deadline) {
#ifdef _WIN32
    static const TimerResolutionGuard guard; // PLATFORM: see above
    (void)guard;
#endif
    std::this_thread::sleep_until(deadline);
}

FramePacer::FramePacer(std::chrono::nanoseconds period, Clock::time_point start)
    : period_(std::max(period, std::chrono::nanoseconds{1})), next_(start + period_) {}

FramePacer FramePacer::from_hz(double hz, Clock::time_point start) {
    constexpr double min_ns = 1.0;
    constexpr double max_ns = 3.6e12; // 1 hour
    double ns = 1e9;                  // NaN, zero and negative rates fall back to 1 s
    if (hz > 0.0) {
        ns = std::clamp(1e9 / hz, min_ns, max_ns);
    }
    return FramePacer{
        std::chrono::round<std::chrono::nanoseconds>(std::chrono::duration<double, std::nano>(ns)),
        start};
}

FramePacer::Tick FramePacer::wait_next() {
    sleep_until(next_);
    return complete(Clock::now());
}

FramePacer::Tick FramePacer::complete(Clock::time_point now) {
    Tick tick{
        .scheduled = next_,
        .woke = now,
        .overrun = std::max(now - next_, std::chrono::nanoseconds{0}),
        .skipped = 0,
    };
    tick.skipped = static_cast<std::uint64_t>(tick.overrun / period_);
    next_ += period_ * static_cast<std::chrono::nanoseconds::rep>(tick.skipped + 1);
    return tick;
}

void FramePacer::reset(Clock::time_point start) {
    next_ = start + period_;
}

} // namespace cigi::platform
