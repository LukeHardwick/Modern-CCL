#pragma once

// Helpers shared by the Host and Ig facades (not installed).

#include "cigi/error.hpp"
#include "cigi/platform/sleep.hpp"
#include "cigi/session/session.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace cigi::detail {

using TimePoint = std::chrono::steady_clock::time_point;

/// Size of the buffer a datagram is received into (largest IPv4 UDP payload).
inline constexpr std::size_t receive_buffer_bytes = 65507;
/// Largest UDP payload that fits one standard Ethernet frame (1500 - 20 IP - 8 UDP).
inline constexpr std::size_t ethernet_payload_limit = 1472;
/// Most datagrams one poll() takes, so a flood cannot starve sending.
inline constexpr std::size_t max_drain_per_poll = 1024;

/// Turns the injected 32-bit, wrapping 10 microsecond Timestamp clock of the sessions into a
/// monotonic time point that does not wrap (as long as it is read at least every 11.9 hours), so
/// timeouts, pacing and time accounting all follow the one injected clock. Host::poll and
/// Ig::poll read it on every call, which keeps that guarantee.
class TickClock {
public:
    explicit TickClock(ClockFn source)
        : source_(source ? std::move(source) : ClockFn{steady_timestamp}),
          last_(source_().count()) {}

    [[nodiscard]] TimePoint now() {
        const std::uint32_t raw = source_().count();
        total_ += static_cast<std::uint32_t>(raw - last_); // wraps modulo 2^32
        last_ = raw;
        using Ticks = std::chrono::duration<std::int64_t, Timestamp::period>;
        return TimePoint{std::chrono::duration_cast<TimePoint::duration>(Ticks{total_})};
    }

private:
    ClockFn source_;
    std::uint32_t last_;
    std::int64_t total_ = 0;
};

/// A fixed-rate schedule on the injected clock, built on platform::FramePacer (which owns the
/// "skip missed ticks, no burst" rule). The facades wait with the transport's receive timeout,
/// not with FramePacer::wait_next, so a frame is never late because a datagram is waiting.
class Pacing {
public:
    Pacing(double hz, TimePoint start)
        : pacer_(platform::FramePacer::from_hz(hz, start)), next_(start + pacer_.period()) {}

    [[nodiscard]] bool due(TimePoint now) const noexcept { return now >= next_; }
    [[nodiscard]] TimePoint next() const noexcept { return next_; }

    /// Account for a tick handled at `now` (which must be due); returns the ticks that were
    /// missed and dropped.
    std::uint64_t advance(TimePoint now) {
        const auto tick = pacer_.complete(now);
        next_ = tick.scheduled +
                pacer_.period() * static_cast<std::chrono::nanoseconds::rep>(tick.skipped + 1);
        return tick.skipped;
    }

    void reset(TimePoint start) {
        pacer_.reset(start);
        next_ = start + pacer_.period();
    }

private:
    platform::FramePacer pacer_;
    TimePoint next_;
};

/// How long to wait for a datagram: until `wake` but at most `limit`, rounded up to whole
/// milliseconds (the transport's resolution), never negative.
[[nodiscard]] inline std::chrono::milliseconds wait_for(TimePoint now, TimePoint wake,
                                                        std::chrono::milliseconds limit) {
    const auto until = std::chrono::ceil<std::chrono::milliseconds>(wake - now);
    return std::clamp(until, std::chrono::milliseconds{0},
                      std::max(limit, std::chrono::milliseconds{0}));
}

[[nodiscard]] inline Error invalid_config() noexcept {
    return Error{.code = ErrorCode::InvalidFieldValue};
}

} // namespace cigi::detail
