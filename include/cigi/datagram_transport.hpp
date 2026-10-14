#pragma once

// The seam between the Host/IG facades and the network: one datagram in, one datagram out.
//
// cigi::Host::connect / cigi::Ig::connect use UDP (transport/udp.hpp). Tests and advanced users
// (multicast, a recorded capture, a simulated lossy network) inject their own implementation with
// Host::over / Ig::over. Implementations are not required to be thread-safe.
//
// Peer learning (ICD 2.8): an IG that is not told where its Host is learns it from the first valid
// Host message. The facade drives this through the optional hooks below (has_peer, latch_peer,
// forget_peer); a transport that always knows its peer keeps the defaults. A transport that
// filters senders counts what it drops in foreign_datagrams(). A decorator (recorder, fault
// injection) must forward every hook to the transport it wraps.

#include "cigi/error.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
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

    // ---- optional hooks (the defaults suit a transport whose peer is fixed) -------------------

    /// True while send() has somewhere to go. An IG whose transport reports false sends nothing
    /// (no Start of Frame) until latch_peer() succeeds. Default: true.
    [[nodiscard]] virtual bool has_peer() const { return true; }
    /// Adopt the sender of the datagram last returned by receive() as the peer, and return true
    /// if the peer is known afterwards. The facade calls it only for a datagram that parses as a
    /// message from the peer's side (an IG: a Host message), and only while has_peer() is false; on
    /// false it simply tries again with the next such datagram. Default: true.
    virtual bool latch_peer() { return true; }
    /// Forget a learnt peer, so the next valid datagram can nominate a new one (the IG calls it
    /// when the Host timed out or the IG restarted). A configured peer is never forgotten.
    /// Default: nothing.
    virtual void forget_peer() {}
    /// Datagrams receive() dropped because they came from someone other than the peer (they are
    /// never returned, so the facade neither decodes nor counts them as errors). Default: 0.
    [[nodiscard]] virtual std::uint64_t foreign_datagrams() const { return 0; }
    /// The bound local port, if the transport has one (useful after binding port 0); 0 if not.
    [[nodiscard]] virtual std::uint16_t local_port() const { return 0; }
};

/// Two connected in-memory transports: what one sends, the other receives (a thread-safe queue;
/// receive() waits for the timeout like a socket). For tests and in-process Host/IG pairs.
[[nodiscard]] std::pair<std::unique_ptr<DatagramTransport>, std::unique_ptr<DatagramTransport>>
make_memory_transport_pair();

} // namespace cigi
