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
    // The pcap needs a source address for datagrams we send: the bound interface, else loopback
    // when talking to a loopback peer, else 0.0.0.0 (unknown without asking the OS).
    const auto bound = tools::Ipv4Address::parse(config_.local_address);
    if (bound && bound->octets != std::array<std::uint8_t, 4>{}) {
        local_ip_ = *bound;
    } else if (remote_ && remote_->address.octets.front() == 127) {
        local_ip_ = tools::Ipv4Address{{127, 0, 0, 1}};
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
    const tools::Endpoint local{local_ip_, endpoint_.local_port()};
    const auto time = tools::PcapWriter::to_time(std::chrono::system_clock::now());
    // A failed pcap write must not disturb the link; the app reports pcap->ok() at exit.
    static_cast<void>(sent ? config_.pcap->write_udp(time, local, *remote, bytes)
                           : config_.pcap->write_udp(time, *remote, local, bytes));
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
    auto received = endpoint_.receive(buffer, timeout);
    if (!received) {
        return received;
    }
    ++received_;
    const auto datagram = std::span<const std::byte>{buffer}.first(*received);
    std::optional<tools::Endpoint> from = remote_;
    if (learn_ || config_.pcap) {
        if (const auto sender = endpoint_.last_sender()) {
            from = to_endpoint(*sender);
            if (learn_ && !latched_) {
                // Only a Host -> IG message makes its sender the peer.
                const auto view = MessageView::parse(datagram);
                if (view && view->direction() == Direction::HostToIg &&
                    endpoint_.set_remote(sender->address, sender->port)) {
                    latched_ = true;
                    remote_ = from;
                }
            }
        }
    }
    record(false, from, datagram);
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
