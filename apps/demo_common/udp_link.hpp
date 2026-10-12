#pragma once

// The UDP link of the demo apps: the library's UdpDatagramTransport (which learns the peer and
// drops foreign senders, see cigi/udp_datagram_transport.hpp) wrapped to
//  - write every sent and received datagram to a pcap file (Wireshark), and
//  - hand every datagram to a tap (the debug log).
// The peer hooks are forwarded to the wrapped transport. The facades (cigi::Host / cigi::Ig)
// accept it through Host::over / Ig::over, which is how the demos record traffic and inject
// faults; Host::connect / Ig::connect cannot do either.

#include <chrono>
#include <cigi/datagram_transport.hpp>
#include <cigi/tools/pcap_writer.hpp>
#include <cigi/transport/udp.hpp>
#include <cigi/udp_datagram_transport.hpp>
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
    /// Where datagrams go (and the only sender accepted; numeric address). Empty: the peer is
    /// learnt through the hooks of DatagramTransport; nothing is sent until it is latched.
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

    [[nodiscard]] bool has_peer() const override { return udp_->has_peer(); }
    bool latch_peer() override { return udp_->latch_peer(); }
    void forget_peer() override { udp_->forget_peer(); }
    [[nodiscard]] std::uint64_t foreign_datagrams() const override {
        return udp_->foreign_datagrams();
    }
    [[nodiscard]] std::uint16_t local_port() const override { return udp_->local_port(); }

    /// Datagrams that reached the socket / were received from the peer.
    [[nodiscard]] std::uint64_t datagrams_sent() const noexcept { return sent_; }
    [[nodiscard]] std::uint64_t datagrams_received() const noexcept { return received_; }

private:
    UdpLink(std::unique_ptr<UdpDatagramTransport> udp, UdpLinkConfig config);

    void record(bool sent, const std::optional<transport::UdpPeer>& remote,
                std::span<const std::byte> bytes);

    std::unique_ptr<UdpDatagramTransport> udp_;
    UdpLinkConfig config_;
    tools::Ipv4Address local_ip_; ///< Bound interface; unset (0.0.0.0) when bound to any.
    std::uint64_t sent_ = 0;
    std::uint64_t received_ = 0;
};

/// Open a pcap file for a demo; on failure `error` says why and the result is null.
[[nodiscard]] std::shared_ptr<tools::PcapWriter> open_pcap(const std::string& path,
                                                           std::string& error);

} // namespace cigi::demo
