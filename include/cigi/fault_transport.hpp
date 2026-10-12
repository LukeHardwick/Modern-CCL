#pragma once

// A DatagramTransport decorator that makes the network misbehave on demand: it drops, duplicates
// and delays the datagrams SENT through it. Put it on either side (Host::over / Ig::over) to
// test a peer's behaviour against loss, duplication and latency; the mock IGs of T09/T09b use it
// for their fault flags. Probabilities and latency can be changed at any time.
//
// Determinism: the random draws come from a seeded generator and the delay is measured with the
// injected clock, so a test with a fake clock and a fixed seed always behaves the same. Delayed
// datagrams are released from send() and receive() once the clock has passed their release time,
// in the order they were sent. Not thread-safe.

#include "cigi/datagram_transport.hpp"
#include "cigi/session/session.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace cigi {

class FaultInjectingTransport final : public DatagramTransport {
public:
    struct Stats {
        std::uint64_t sent = 0;       ///< send() calls.
        std::uint64_t dropped = 0;    ///< Datagrams discarded.
        std::uint64_t duplicated = 0; ///< Extra copies created.
        std::uint64_t delayed = 0;    ///< Datagrams that waited in the delay queue.
        std::uint64_t released = 0;   ///< Delayed datagrams passed on.
    };

    /// `clock` empty means steady_timestamp. `seed` seeds the random draws.
    explicit FaultInjectingTransport(std::unique_ptr<DatagramTransport> inner, ClockFn clock = {},
                                     std::uint64_t seed = 1);

    /// Chance (0..1, clamped) that a datagram is discarded.
    void set_drop_probability(double p) noexcept { drop_ = clamp01(p); }
    /// Chance (0..1, clamped) that a datagram is sent twice.
    void set_duplicate_probability(double p) noexcept { duplicate_ = clamp01(p); }
    /// Every datagram waits this long (negative: none) before it is passed on.
    void set_added_latency(std::chrono::microseconds latency) noexcept {
        latency_ = latency.count() > 0 ? latency : std::chrono::microseconds{0};
    }
    [[nodiscard]] double drop_probability() const noexcept { return drop_; }
    [[nodiscard]] double duplicate_probability() const noexcept { return duplicate_; }
    [[nodiscard]] std::chrono::microseconds added_latency() const noexcept { return latency_; }
    /// Datagrams waiting for their release time.
    [[nodiscard]] std::size_t pending() const noexcept { return queue_.size(); }
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

    Result<void> send(std::span<const std::byte> datagram) override;
    Result<std::size_t> receive(std::span<std::byte> buffer,
                                std::chrono::milliseconds timeout) override;

private:
    struct Delayed {
        std::vector<std::byte> data;
        std::uint32_t release_at; ///< In 10 microsecond clock ticks, wrapping.
    };

    [[nodiscard]] static double clamp01(double p) noexcept {
        return p > 0.0 ? (p < 1.0 ? p : 1.0) : 0.0; // also maps NaN to 0
    }
    [[nodiscard]] bool roll(double probability);
    void release_due();

    std::unique_ptr<DatagramTransport> inner_;
    ClockFn clock_;
    std::mt19937_64 rng_;
    double drop_ = 0.0;
    double duplicate_ = 0.0;
    std::chrono::microseconds latency_{0};
    std::vector<Delayed> queue_;
    Stats stats_;
};

} // namespace cigi
