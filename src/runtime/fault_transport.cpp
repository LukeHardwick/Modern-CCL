#include "cigi/fault_transport.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace cigi {

FaultInjectingTransport::FaultInjectingTransport(std::unique_ptr<DatagramTransport> inner,
                                                 ClockFn clock, std::uint64_t seed)
    : inner_(std::move(inner)), clock_(clock ? std::move(clock) : ClockFn{steady_timestamp}),
      rng_(seed) {}

bool FaultInjectingTransport::roll(double probability) {
    if (probability <= 0.0) {
        return false; // no draw: a fault that is off must not disturb the sequence of the others
    }
    if (probability >= 1.0) {
        return true;
    }
    // 53 random bits mapped to [0, 1): the same on every standard library, unlike
    // std::uniform_real_distribution.
    constexpr double scale = 1.0 / 9007199254740992.0; // 2^-53
    const double draw = static_cast<double>(rng_() >> 11U) * scale;
    return draw < probability;
}

Result<void> FaultInjectingTransport::release_due() {
    Result<void> result;
    if (queue_.empty()) {
        return result;
    }
    const std::uint32_t now = clock_().count();
    std::vector<Delayed> keep;
    for (auto& item : queue_) {
        // Signed difference of the wrapping tick counters: due once now has reached release_at.
        if (static_cast<std::int32_t>(now - item.release_at) >= 0) {
            ++stats_.released;
            if (const auto sent = inner_->send(item.data); !sent && result) {
                result = sent; // the first failure is reported, the rest are still released
            }
        } else {
            keep.push_back(std::move(item));
        }
    }
    queue_ = std::move(keep);
    return result;
}

std::chrono::milliseconds FaultInjectingTransport::until_next_release() const {
    constexpr std::int64_t ticks_per_ms = 100; // 10 microsecond ticks
    const std::uint32_t now = clock_().count();
    std::int64_t soonest = std::numeric_limits<std::int64_t>::max();
    for (const auto& item : queue_) {
        soonest = std::min<std::int64_t>(soonest, static_cast<std::int32_t>(item.release_at - now));
    }
    if (soonest <= 0) {
        return std::chrono::milliseconds{0};
    }
    return std::chrono::milliseconds{(soonest + ticks_per_ms - 1) / ticks_per_ms};
}

Result<void> FaultInjectingTransport::send(std::span<const std::byte> datagram) {
    const Result<void> released = release_due();
    ++stats_.sent;
    if (roll(drop_)) {
        ++stats_.dropped;
        return {};
    }
    int copies = 1;
    if (roll(duplicate_)) {
        ++stats_.duplicated;
        copies = 2;
    }
    Result<void> result;
    for (int i = 0; i < copies; ++i) {
        if (latency_.count() > 0) {
            ++stats_.delayed;
            const auto ticks = std::chrono::duration_cast<Timestamp>(latency_).count();
            queue_.push_back(Delayed{
                .data = std::vector<std::byte>(datagram.begin(), datagram.end()),
                .release_at = clock_().count() + ticks,
            });
        } else if (const auto sent = inner_->send(datagram); !sent && result) {
            result = sent;
        }
    }
    if (result && !released) {
        return released; // a delayed datagram released now could not be sent
    }
    return result;
}

Result<std::size_t> FaultInjectingTransport::receive(std::span<std::byte> buffer,
                                                     std::chrono::milliseconds timeout) {
    if (const auto released = release_due(); !released) {
        return std::unexpected(released.error());
    }
    // Never sleep past the moment a delayed datagram is due: wake for it, release it, and let the
    // caller poll again.
    const bool capped = !queue_.empty() && until_next_release() < timeout;
    const auto received = inner_->receive(buffer, capped ? until_next_release() : timeout);
    if (const auto released = release_due(); !released) {
        return std::unexpected(released.error());
    }
    return received;
}

} // namespace cigi
