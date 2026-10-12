#include "demo_common/udp_link.hpp"

#include <chrono>
#include <filesystem>
#include <utility>

namespace cigi::demo {

namespace {

std::optional<tools::Endpoint> to_endpoint(const transport::UdpPeer& peer) {
    const auto address = tools::Ipv4Address::parse(peer.address);
    if (!address) {
        return std::nullopt;
    }
    return tools::Endpoint{*address, peer.port};
}

} // namespace

UdpLink::UdpLink(std::unique_ptr<UdpDatagramTransport> udp, UdpLinkConfig config)
    : udp_(std::move(udp)), config_(std::move(config)) {
    const auto bound = tools::Ipv4Address::parse(config_.local_address);
    if (bound && bound->octets != std::array<std::uint8_t, 4>{}) {
        local_ip_ = *bound;
    }
}

Result<std::unique_ptr<UdpLink>> UdpLink::open(UdpLinkConfig config) {
    auto udp = UdpDatagramTransport::open({
        .local_address = config.local_address,
        .local_port = config.local_port,
        .remote_address = config.remote_address,
        .remote_port = config.remote_address.empty() ? std::uint16_t{0} : config.remote_port,
    });
    if (!udp) {
        return std::unexpected(udp.error());
    }
    return std::unique_ptr<UdpLink>(new UdpLink(std::move(*udp), std::move(config)));
}

void UdpLink::record(bool sent, const std::optional<transport::UdpPeer>& remote_peer,
                     std::span<const std::byte> bytes) {
    if (config_.tap) {
        config_.tap(sent, bytes);
    }
    if (!config_.pcap || !remote_peer) {
        return;
    }
    const auto remote = to_endpoint(*remote_peer);
    if (!remote) {
        return; // IPv6: the pcap holds IPv4 only
    }
    // Bound to any interface: the OS does not say which one a datagram used. For a loopback peer
    // that is 127.0.0.1; otherwise the capture shows 0.0.0.0 as our address.
    tools::Ipv4Address ip = local_ip_;
    if (ip.octets == std::array<std::uint8_t, 4>{} && remote->address.octets.front() == 127) {
        ip = tools::Ipv4Address{{127, 0, 0, 1}};
    }
    const tools::Endpoint local{ip, udp_->local_port()};
    const auto time = tools::PcapWriter::to_time(std::chrono::system_clock::now());
    // A failed pcap write must not disturb the link; the app reports pcap->ok() at exit.
    static_cast<void>(sent ? config_.pcap->write_udp(time, local, *remote, bytes)
                           : config_.pcap->write_udp(time, *remote, local, bytes));
}

Result<void> UdpLink::send(std::span<const std::byte> datagram) {
    if (!udp_->has_peer()) {
        return {}; // nowhere to go yet (the facade counts it as unrouted)
    }
    if (auto sent = udp_->send(datagram); !sent) {
        return sent;
    }
    ++sent_;
    record(true, udp_->peer(), datagram);
    return {};
}

Result<std::size_t> UdpLink::receive(std::span<std::byte> buffer,
                                     std::chrono::milliseconds timeout) {
    auto received = udp_->receive(buffer, timeout);
    if (!received) {
        return received;
    }
    ++received_;
    record(false, udp_->last_sender(), std::span<const std::byte>{buffer}.first(*received));
    return received;
}

std::shared_ptr<tools::PcapWriter> open_pcap(const std::string& path, std::string& error) {
    auto writer = std::make_shared<tools::PcapWriter>(std::filesystem::path{path});
    if (!writer->ok()) {
        error = "cannot open " + path + ": " + writer->open_error().message();
        return nullptr;
    }
    return writer;
}

} // namespace cigi::demo
