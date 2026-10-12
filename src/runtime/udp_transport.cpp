#include "cigi/udp_datagram_transport.hpp"

#include <algorithm>
#include <utility>

namespace cigi {

Result<std::unique_ptr<UdpDatagramTransport>>
UdpDatagramTransport::open(const transport::UdpConfig& config, bool filter_senders) {
    auto endpoint = transport::UdpEndpoint::open(config);
    if (!endpoint) {
        return std::unexpected(endpoint.error());
    }
    return std::make_unique<UdpDatagramTransport>(std::move(*endpoint), filter_senders);
}

UdpDatagramTransport::UdpDatagramTransport(transport::UdpEndpoint endpoint, bool filter_senders)
    : endpoint_(std::move(endpoint)), learn_(!endpoint_.remote()), filter_(filter_senders) {}

Result<void> UdpDatagramTransport::send(std::span<const std::byte> datagram) {
    if (!has_peer()) {
        return {}; // no peer yet
    }
    return endpoint_.send(datagram);
}

Result<std::size_t> UdpDatagramTransport::receive(std::span<std::byte> buffer,
                                                  std::chrono::milliseconds timeout) {
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::max(timeout, std::chrono::milliseconds{0});
    for (;;) {
        const auto received = endpoint_.receive(buffer, timeout);
        // Unknown peer (learning): any sender is a candidate. Otherwise only the peer is heard.
        if (!received || !filter_ || !has_peer() || endpoint_.last_sender_is_remote()) {
            return received;
        }
        ++foreign_; // dropped before anyone decodes it
        timeout = std::max(std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()),
                           std::chrono::milliseconds{0});
    }
}

bool UdpDatagramTransport::latch_peer() {
    if (has_peer()) {
        return true; // configured, or already learnt
    }
    const auto sender = endpoint_.last_sender();
    if (!sender || !endpoint_.set_remote(sender->address, sender->port)) {
        return false;
    }
    latched_ = true;
    return true;
}

void UdpDatagramTransport::forget_peer() {
    latched_ = false; // a configured peer is not learnt, so it stays known
}

std::optional<transport::UdpPeer> UdpDatagramTransport::peer() const {
    return has_peer() ? endpoint_.remote() : std::nullopt;
}

} // namespace cigi
