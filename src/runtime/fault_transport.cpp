#include "cigi/fault_transport.hpp"

#include <algorithm>
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

void FaultInjectingTransport::release_due() {
    if (queue_.empty()) {
        return;
    }
    const std::uint32_t now = clock_().count();
    std::vector<Delayed> keep;
    for (auto& item : queue_) {
        // Signed difference of the wrapping tick counters: due once now has reached release_at.
        if (static_cast<std::int32_t>(now - item.release_at) >= 0) {
            ++stats_.released;
            static_cast<void>(
                inner_->send(item.data)); // a failing inner send is the inner's to report
        } else {
            keep.push_back(std::move(item));
        }
    }
    queue_ = std::move(keep);
}

Result<void> FaultInjectingTransport::send(std::span<const std::byte> datagram) {
    release_due();
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
        } else if (auto sent = inner_->send(datagram); !sent && result) {
            result = sent;
        }
    }
    return result;
}

Result<std::size_t> FaultInjectingTransport::receive(std::span<std::byte> buffer,
                                                     std::chrono::milliseconds timeout) {
    release_due();
    return inner_->receive(buffer, timeout);
}

} // namespace cigi
