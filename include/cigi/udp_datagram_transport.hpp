#pragma once

// The built-in UDP DatagramTransport (what Host::connect / Ig::connect use), public so that a
// decorator can wrap it: a pcap recorder, a tap, fault injection. It implements the peer hooks of
// DatagramTransport:
//  - a configured peer (remote_address given) is fixed, and datagrams from anyone else are dropped
//    and counted (foreign_datagrams()) before the facade decodes them;
//  - without a remote the transport LEARNS its peer: send() goes nowhere (and succeeds) until
//    latch_peer() adopts the sender of the last datagram; from then on others are dropped as
//    above, until forget_peer().
// `filter_senders = false` accepts datagrams from any sender (asymmetric ports, multi-homed IGs).

#include "cigi/datagram_transport.hpp"
#include "cigi/transport/udp.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace cigi {

class UdpDatagramTransport final : public DatagramTransport {
public:
    /// Open the socket (transport::UdpEndpoint::open). Empty `config.remote_address`: learn the
    /// peer.
    [[nodiscard]] static Result<std::unique_ptr<UdpDatagramTransport>>
    open(const transport::UdpConfig& config, bool filter_senders = true);

    /// Wrap an open endpoint; it learns its peer if the endpoint has no remote.
    explicit UdpDatagramTransport(transport::UdpEndpoint endpoint, bool filter_senders = true);

    Result<void> send(std::span<const std::byte> datagram) override;
    Result<std::size_t> receive(std::span<std::byte> buffer,
                                std::chrono::milliseconds timeout) override;

    [[nodiscard]] bool has_peer() const override { return !learn_ || latched_; }
    bool latch_peer() override;
    void forget_peer() override;
    [[nodiscard]] std::uint64_t foreign_datagrams() const override { return foreign_; }
    [[nodiscard]] std::uint16_t local_port() const override { return endpoint_.local_port(); }

    /// Where send() goes now (nullopt while a learning transport has no peer).
    [[nodiscard]] std::optional<transport::UdpPeer> peer() const;
    /// Source of the datagram last returned by receive().
    [[nodiscard]] std::optional<transport::UdpPeer> last_sender() const {
        return endpoint_.last_sender();
    }

private:
    transport::UdpEndpoint endpoint_;
    bool learn_;
    bool filter_;
    bool latched_ = false;
    std::uint64_t foreign_ = 0;
};

} // namespace cigi
