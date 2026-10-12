#pragma once

// The UDP link of the demo apps: a DatagramTransport over a UdpEndpoint that can
//  - learn its peer from the first valid message (an IG that does not know its Host),
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
    /// Where datagrams go. Empty: learn it from the first datagram that is a Host -> IG message
    /// (stray datagrams do not redirect it); nothing is sent until then.
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
    void forget_peer() noexcept { latched_ = false; }
    [[nodiscard]] std::uint64_t datagrams_sent() const noexcept { return sent_; }
    [[nodiscard]] std::uint64_t datagrams_received() const noexcept { return received_; }
    /// Datagrams not sent because the peer was not known yet.
    [[nodiscard]] std::uint64_t datagrams_unrouted() const noexcept { return unrouted_; }

private:
    UdpLink(transport::UdpEndpoint endpoint, UdpLinkConfig config);

    void record(bool sent, const std::optional<tools::Endpoint>& remote,
                std::span<const std::byte> bytes);

    transport::UdpEndpoint endpoint_;
    UdpLinkConfig config_;
    bool learn_;
    bool latched_ = false;
    std::optional<tools::Endpoint> remote_; ///< The peer as a pcap endpoint (IPv4 only).
    tools::Ipv4Address local_ip_;
    std::uint64_t sent_ = 0;
    std::uint64_t received_ = 0;
    std::uint64_t unrouted_ = 0;
};

/// Open a pcap file for a demo; on failure `error` says why and the result is null.
[[nodiscard]] std::shared_ptr<tools::PcapWriter> open_pcap(const std::string& path,
                                                           std::string& error);

} // namespace cigi::demo
