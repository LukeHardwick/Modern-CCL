#pragma once

// The UDP link of the demo apps: a DatagramTransport over a UdpEndpoint that can
//  - learn its peer (an IG that does not know its Host): the sender of the first datagram that
//    parses as a Host -> IG message is a *candidate*; the owner calls latch_candidate() once its
//    session accepted a message, as the facade does. Once the peer is known (learnt or
//    configured), datagrams from any other sender are dropped and counted,
//  - write every sent and received datagram to a pcap file (Wireshark), and
//  - hand every datagram to a tap (the debug log).
// The facades (cigi::Host / cigi::Ig) accept it through Host::over / Ig::over, which is how the
// demos record traffic and inject faults; Host::connect / Ig::connect cannot do either.

#include <array>
#include <chrono>
#include <cigi/datagram_transport.hpp>
#include <cigi/tools/pcap_writer.hpp>
#include <cigi/transport/udp.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace cigi::demo {

struct UdpLinkConfig {
    std::string local_address;    ///< Interface to bind; empty = any.
    std::uint16_t local_port = 0; ///< 0 = ephemeral (see UdpLink::local_port()).
    /// Where datagrams go (and the only sender accepted; numeric address). Empty: learn it, see
    /// above; nothing is sent until the peer is latched.
    std::string remote_address;
    std::uint16_t remote_port = 0;
    /// Every datagram sent or received is appended (IPv4 peers only). May be null.
    std::shared_ptr<tools::PcapWriter> pcap;
    /// Called for every datagram: (true = sent, bytes). May be empty.
    std::function<void(bool, std::span<const std::byte>)> tap;
};

class UdpLink final : public DatagramTransport {
public:
    [[nodiscard]] static Result<std::unique_ptr<UdpLink>> open(UdpLinkConfig config);

    Result<void> send(std::span<const std::byte> datagram) override;
    Result<std::size_t> receive(std::span<std::byte> buffer,
                                std::chrono::milliseconds timeout) override;

    [[nodiscard]] std::uint16_t local_port() const { return endpoint_.local_port(); }
    /// True while datagrams have somewhere to go (always, unless the peer is learnt and unknown).
    [[nodiscard]] bool has_peer() const noexcept { return !learn_ || latched_; }
    /// Forget a learnt peer (no effect when the peer is configured).
    void forget_peer() noexcept {
        if (learn_) {
            latched_ = false;
            candidate_.reset();
        }
    }
    /// Make the sender of the first Host -> IG message since the peer was forgotten the peer.
    /// True if the peer is now known.
    bool latch_candidate();
    [[nodiscard]] std::uint64_t datagrams_sent() const noexcept { return sent_; }
    [[nodiscard]] std::uint64_t datagrams_received() const noexcept { return received_; }
    /// Datagrams not sent because the peer was not known yet.
    [[nodiscard]] std::uint64_t datagrams_unrouted() const noexcept { return unrouted_; }
    /// Datagrams dropped because they came from someone other than the peer.
    [[nodiscard]] std::uint64_t datagrams_foreign() const noexcept { return foreign_; }

private:
    UdpLink(transport::UdpEndpoint endpoint, UdpLinkConfig config);

    void record(bool sent, const std::optional<tools::Endpoint>& remote,
                std::span<const std::byte> bytes);
    [[nodiscard]] bool same_peer(const transport::UdpPeer& sender) const;

    transport::UdpEndpoint endpoint_;
    UdpLinkConfig config_;
    bool learn_;
    bool latched_ = false;
    std::optional<tools::Endpoint> remote_;       ///< The peer as a pcap endpoint (IPv4 only).
    transport::UdpPeer latched_peer_;             ///< Learning: the peer once latched.
    std::optional<transport::UdpPeer> candidate_; ///< Learning: sender of the first valid message.
    tools::Ipv4Address local_ip_; ///< Bound interface; unset (0.0.0.0) when bound to any.
    std::uint64_t sent_ = 0;
    std::uint64_t received_ = 0;
    std::uint64_t unrouted_ = 0;
    std::uint64_t foreign_ = 0;
};

/// Open a pcap file for a demo; on failure `error` says why and the result is null.
[[nodiscard]] std::shared_ptr<tools::PcapWriter> open_pcap(const std::string& path,
                                                           std::string& error);

} // namespace cigi::demo
