#pragma once

// DatagramTransport over a UdpEndpoint (the T08 UDP transport). Internal to cigi_runtime.

#include "cigi/datagram_transport.hpp"
#include "cigi/transport/udp.hpp"

#include <utility>

namespace cigi::detail {

class UdpDatagramTransport final : public DatagramTransport {
public:
    /// With `reply_to_sender`, send() answers the source of the last received datagram instead
    /// of a configured remote (an IG that learns its Host from the first IG Control).
    UdpDatagramTransport(transport::UdpEndpoint endpoint, bool reply_to_sender)
        : endpoint_(std::move(endpoint)), reply_to_sender_(reply_to_sender) {}

    Result<void> send(std::span<const std::byte> datagram) override {
        return reply_to_sender_ ? endpoint_.reply(datagram) : endpoint_.send(datagram);
    }
    Result<std::size_t> receive(std::span<std::byte> buffer,
                                std::chrono::milliseconds timeout) override {
        return endpoint_.receive(buffer, timeout);
    }

private:
    transport::UdpEndpoint endpoint_;
    bool reply_to_sender_;
};

} // namespace cigi::detail
