#include "demo_common/udp_link.hpp"

#include <chrono>
#include <cigi/message/view.hpp>
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

UdpLink::UdpLink(transport::UdpEndpoint endpoint, UdpLinkConfig config)
    : endpoint_(std::move(endpoint)), config_(std::move(config)),
      learn_(config_.remote_address.empty()) {
    if (!learn_) {
        remote_ = to_endpoint({config_.remote_address, config_.remote_port});
    }
    const auto bound = tools::Ipv4Address::parse(config_.local_address);
    if (bound && bound->octets != std::array<std::uint8_t, 4>{}) {
        local_ip_ = *bound;
    }
}

Result<std::unique_ptr<UdpLink>> UdpLink::open(UdpLinkConfig config) {
    auto endpoint = transport::UdpEndpoint::open({
        .local_address = config.local_address,
        .local_port = config.local_port,
        .remote_address = config.remote_address,
        .remote_port = config.remote_address.empty() ? std::uint16_t{0} : config.remote_port,
    });
    if (!endpoint) {
        return std::unexpected(endpoint.error());
    }
    return std::unique_ptr<UdpLink>(new UdpLink(std::move(*endpoint), std::move(config)));
}

void UdpLink::record(bool sent, const std::optional<tools::Endpoint>& remote,
                     std::span<const std::byte> bytes) {
    if (config_.tap) {
        config_.tap(sent, bytes);
    }
    if (!config_.pcap || !remote) {
        return;
    }
    // Bound to any interface: the OS does not say which one a datagram used. For a loopback peer
    // that is 127.0.0.1; otherwise the capture shows 0.0.0.0 as our address.
    tools::Ipv4Address ip = local_ip_;
    if (ip.octets == std::array<std::uint8_t, 4>{} && remote->address.octets.front() == 127) {
        ip = tools::Ipv4Address{{127, 0, 0, 1}};
    }
    const tools::Endpoint local{ip, endpoint_.local_port()};
    const auto time = tools::PcapWriter::to_time(std::chrono::system_clock::now());
    // A failed pcap write must not disturb the link; the app reports pcap->ok() at exit.
    static_cast<void>(sent ? config_.pcap->write_udp(time, local, *remote, bytes)
                           : config_.pcap->write_udp(time, *remote, local, bytes));
}

bool UdpLink::same_peer(const transport::UdpPeer& sender) const {
    const transport::UdpPeer expected =
        learn_ ? latched_peer_ : transport::UdpPeer{config_.remote_address, config_.remote_port};
    if (sender.port != expected.port) {
        return false;
    }
    const auto a = tools::Ipv4Address::parse(sender.address);
    const auto b = tools::Ipv4Address::parse(expected.address);
    return a && b ? *a == *b : sender.address == expected.address;
}

bool UdpLink::latch_candidate() {
    if (!learn_ || latched_) {
        return has_peer();
    }
    if (!candidate_ || !endpoint_.set_remote(candidate_->address, candidate_->port)) {
        return false;
    }
    latched_ = true;
    remote_ = to_endpoint(*candidate_);
    latched_peer_ = *candidate_;
    candidate_.reset();
    return true;
}

Result<void> UdpLink::send(std::span<const std::byte> datagram) {
    if (!has_peer()) {
        ++unrouted_;
        return {};
    }
    if (auto sent = endpoint_.send(datagram); !sent) {
        return sent;
    }
    ++sent_;
    record(true, remote_, datagram);
    return {};
}

Result<std::size_t> UdpLink::receive(std::span<std::byte> buffer,
                                     std::chrono::milliseconds timeout) {
    for (;;) {
        auto received = endpoint_.receive(buffer, timeout);
        if (!received) {
            return received;
        }
        ++received_;
        const auto datagram = std::span<const std::byte>{buffer}.first(*received);
        const auto sender = endpoint_.last_sender();
        const std::optional<tools::Endpoint> from =
            sender ? to_endpoint(*sender) : std::optional<tools::Endpoint>{};
        record(false, from ? from : remote_, datagram);
        if (!sender) {
            return received;
        }
        if (learn_ && !latched_) {
            // Unknown peer: the first datagram that parses as a Host -> IG message nominates one.
            const auto view = MessageView::parse(datagram);
            if (view && view->direction() == Direction::HostToIg) {
                candidate_ = *sender;
            }
            return received;
        }
        if (same_peer(*sender)) {
            return received;
        }
        ++foreign_;
        --received_;
        timeout = std::chrono::milliseconds{0}; // look for the next datagram without waiting again
    }
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
