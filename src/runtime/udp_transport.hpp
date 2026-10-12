#pragma once

// DatagramTransport over a UdpEndpoint (the T08 UDP transport). Internal to cigi_runtime.

#include "cigi/datagram_transport.hpp"
#include "cigi/transport/udp.hpp"

#include <utility>

namespace cigi::detail {

class UdpDatagramTransport final : public DatagramTransport {
public:
    /// With `learn_peer`, the destination is not configured: send() goes nowhere until
    /// latch_peer() adopts the sender of the last datagram (an IG that learns its Host from the
    /// first valid message; stray datagrams that do not parse never redirect it).
    UdpDatagramTransport(transport::UdpEndpoint endpoint, bool learn_peer)
        : endpoint_(std::move(endpoint)), learn_peer_(learn_peer) {}

    Result<void> send(std::span<const std::byte> datagram) override {
        if (learn_peer_ && !latched_) {
            return {}; // no peer yet
        }
        return endpoint_.send(datagram);
    }
    /// Adopt the sender of the datagram last received as the destination of send(). True if
    /// there is nothing to learn or the peer was adopted; false (nothing changed) if no sender is
    /// known or the address was refused.
    bool latch_peer() {
        if (!learn_peer_) {
            return true;
        }
        const auto sender = endpoint_.last_sender();
        if (!sender || !endpoint_.set_remote(sender->address, sender->port)) {
            return false;
        }
        latched_ = true;
        return true;
    }
    void forget_peer() { latched_ = false; }
    Result<std::size_t> receive(std::span<std::byte> buffer,
                                std::chrono::milliseconds timeout) override {
        return endpoint_.receive(buffer, timeout);
    }

private:
    transport::UdpEndpoint endpoint_;
    bool learn_peer_;
    bool latched_ = false;
};

} // namespace cigi::detail
