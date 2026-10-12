#pragma once

// The seam between the Host/IG facades and the network: one datagram in, one datagram out.
//
// cigi::Host::connect / cigi::Ig::connect use UDP (transport/udp.hpp). Tests and advanced users
// (multicast, a recorded capture, a simulated lossy network) inject their own implementation with
// Host::over / Ig::over. Implementations are not required to be thread-safe.

#include "cigi/error.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>

namespace cigi {

class DatagramTransport {
public:
    DatagramTransport() = default;
    DatagramTransport(const DatagramTransport&) = delete;
    DatagramTransport& operator=(const DatagramTransport&) = delete;
    DatagramTransport(DatagramTransport&&) = delete;
    DatagramTransport& operator=(DatagramTransport&&) = delete;
    virtual ~DatagramTransport() = default;

    /// Send one datagram to the peer. A failure is fatal for the facade driving the transport
    /// (it ends run() with the error); a datagram that is merely lost is not a failure.
    virtual Result<void> send(std::span<const std::byte> datagram) = 0;

    /// Wait up to `timeout` for one datagram and copy it into `buffer`; returns its size. A zero
    /// timeout polls. `ErrorCode::Timeout` if nothing arrived; `ErrorCode::BufferTooSmall` if a
    /// datagram arrived that does not fit (it is discarded and the facade carries on); any other
    /// error is fatal.
    virtual Result<std::size_t> receive(std::span<std::byte> buffer,
                                        std::chrono::milliseconds timeout) = 0;
};

/// Two connected in-memory transports: what one sends, the other receives (a thread-safe queue;
/// receive() waits for the timeout like a socket). For tests and in-process Host/IG pairs.
[[nodiscard]] std::pair<std::unique_ptr<DatagramTransport>, std::unique_ptr<DatagramTransport>>
make_memory_transport_pair();

} // namespace cigi
