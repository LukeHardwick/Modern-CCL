#include "cigi/transport/udp.hpp"

#include "receive_outcome.hpp"

#include <algorithm>
#include <array>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/system/error_code.hpp>
#include <climits>
#include <utility>

// PLATFORM: SIO_UDP_CONNRESET handling (see open()).
#ifdef _WIN32
#include <winsock2.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#endif

namespace cigi::transport {

namespace asio = boost::asio;
using asio::ip::udp;

namespace {

Error os_failure(const boost::system::error_code& ec) {
    return Error{.code = ErrorCode::TransportError, .os_error = detail::os_error_of(ec)};
}

Error bad_address() {
    return Error{.code = ErrorCode::InvalidFieldValue};
}

Error not_open() {
    return Error{.code = ErrorCode::TransportError};
}

} // namespace

struct UdpEndpoint::Impl {
    asio::io_context io{1};
    udp::socket socket{io};
    udp::endpoint remote;
    udp::endpoint sender;
    bool has_remote = false;
    bool has_sender = false;

    Result<void> send_to(const udp::endpoint& to, std::span<const std::byte> datagram) {
        boost::system::error_code ec;
        static_cast<void>(
            socket.send_to(asio::buffer(datagram.data(), datagram.size()), to, 0, ec));
        if (ec) {
            return std::unexpected(os_failure(ec));
        }
        return {}; // a UDP send is all-or-nothing
    }

    /// Wait up to `remaining` for the socket to become readable. Returns `Timeout` if it did not,
    /// or the failure; success only means "try receiving again".
    Result<void> wait_readable(std::chrono::steady_clock::duration remaining) {
        boost::system::error_code ec = asio::error::would_block;
        bool done = false;
        socket.async_wait(udp::socket::wait_read, [&](const boost::system::error_code& result) {
            ec = result;
            done = true;
        });
        io.restart();
        io.run_for(remaining);
        if (!done) {
            // Still pending: cancel and let the aborted handler run.
            boost::system::error_code cancel_ec;
            cancel_ec = socket.cancel(cancel_ec);
            if (cancel_ec) {
                // Cannot withdraw the wait; close the socket rather than risk a blocking run().
                boost::system::error_code close_ec;
                close_ec = socket.close(close_ec);
                static_cast<void>(close_ec);
                io.restart();
                io.poll();
                return std::unexpected(os_failure(cancel_ec));
            }
            io.restart();
            io.run();
        }
        if (ec == asio::error::operation_aborted) {
            return std::unexpected(Error{.code = ErrorCode::Timeout});
        }
        if (ec) {
            return std::unexpected(os_failure(ec));
        }
        return {};
    }
};

UdpEndpoint::UdpEndpoint(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
UdpEndpoint::UdpEndpoint(UdpEndpoint&&) noexcept = default;
UdpEndpoint& UdpEndpoint::operator=(UdpEndpoint&&) noexcept = default;
UdpEndpoint::~UdpEndpoint() = default;

Result<UdpEndpoint> UdpEndpoint::open(const UdpConfig& config) {
    if (config.receive_buffer_size > static_cast<std::size_t>(INT_MAX)) {
        return std::unexpected(bad_address());
    }
    boost::system::error_code ec;
    auto impl = std::make_unique<Impl>();

    if (!config.remote_address.empty()) {
        impl->remote =
            udp::endpoint{asio::ip::make_address(config.remote_address, ec), config.remote_port};
        if (ec || config.remote_port == 0) {
            return std::unexpected(bad_address());
        }
        impl->has_remote = true;
    }

    std::string local_text = config.local_address;
    if (local_text.empty()) {
        local_text = impl->has_remote && impl->remote.address().is_v6() ? "::" : "0.0.0.0";
    }
    const udp::endpoint local{asio::ip::make_address(local_text, ec), config.local_port};
    if (ec) {
        return std::unexpected(bad_address());
    }
    if (impl->has_remote && impl->remote.address().is_v6() != local.address().is_v6()) {
        return std::unexpected(bad_address()); // address families must match
    }

    ec = impl->socket.open(local.protocol(), ec);
    if (ec) {
        return std::unexpected(os_failure(ec));
    }
    ec = impl->socket.non_blocking(true, ec); // receive() polls first, then waits
    if (ec) {
        return std::unexpected(os_failure(ec));
    }
    if (config.receive_buffer_size != 0) {
        ec = impl->socket.set_option(
            asio::socket_base::receive_buffer_size(static_cast<int>(config.receive_buffer_size)),
            ec);
        if (ec) {
            return std::unexpected(os_failure(ec));
        }
    }
#ifdef _WIN32
    {
        // PLATFORM: by default Windows fails the next receive with WSAECONNRESET after an ICMP
        // "port unreachable" for an earlier send; switch that off. Best effort (the retry
        // classifier in receive_outcome.hpp remains as a fallback). Unverified: Windows only.
        DWORD bytes_returned = 0;
        BOOL behaviour = FALSE;
        WSAIoctl(impl->socket.native_handle(), SIO_UDP_CONNRESET, &behaviour, sizeof(behaviour),
                 nullptr, 0, &bytes_returned, nullptr, nullptr);
    }
#endif
    ec = impl->socket.bind(local, ec);
    if (ec) {
        return std::unexpected(os_failure(ec));
    }
    return UdpEndpoint{std::move(impl)};
}

Result<void> UdpEndpoint::send(std::span<const std::byte> datagram) {
    if (!impl_) {
        return std::unexpected(not_open());
    }
    if (!impl_->has_remote) {
        return std::unexpected(bad_address()); // no destination configured
    }
    return impl_->send_to(impl_->remote, datagram);
}

Result<void> UdpEndpoint::send_to(const UdpPeer& peer, std::span<const std::byte> datagram) {
    if (!impl_) {
        return std::unexpected(not_open());
    }
    boost::system::error_code ec;
    const auto ip = asio::ip::make_address(peer.address, ec);
    if (ec || peer.port == 0) {
        return std::unexpected(bad_address());
    }
    return impl_->send_to(udp::endpoint{ip, peer.port}, datagram);
}

Result<void> UdpEndpoint::reply(std::span<const std::byte> datagram) {
    if (!impl_) {
        return std::unexpected(not_open());
    }
    if (!impl_->has_sender) {
        return std::unexpected(bad_address()); // nothing received yet
    }
    return impl_->send_to(impl_->sender, datagram);
}

Result<std::size_t> UdpEndpoint::receive(std::span<std::byte> buffer,
                                         std::chrono::milliseconds timeout) {
    if (!impl_) {
        return std::unexpected(not_open());
    }
    using Clock = std::chrono::steady_clock;
    Impl& s = *impl_;
    // Capped at a year, so that a huge timeout (milliseconds::max()) cannot overflow the clock.
    constexpr std::chrono::milliseconds longest = std::chrono::hours{24 * 365};
    const auto deadline = Clock::now() + std::clamp(timeout, std::chrono::milliseconds{0}, longest);

    // Scatter receive into the caller's buffer plus one spare byte: if the spare byte is written
    // (or the OS reports message_size) the datagram did not fit. No copy, no large scratch buffer.
    std::byte overflow{};
    const std::array<asio::mutable_buffer, 2> sequence{
        asio::mutable_buffer{buffer.data(), buffer.size()},
        asio::mutable_buffer{&overflow, 1},
    };

    for (;;) {
        // Try the socket directly first, so a zero timeout (polling) never touches the reactor.
        udp::endpoint from;
        boost::system::error_code ec;
        const std::size_t received = s.socket.receive_from(sequence, from, 0, ec);
        if (ec == asio::error::would_block || ec == asio::error::try_again) {
            const auto remaining = deadline - Clock::now();
            if (remaining <= Clock::duration::zero()) {
                return std::unexpected(Error{.code = ErrorCode::Timeout});
            }
            if (auto waited = s.wait_readable(remaining); !waited) {
                return std::unexpected(waited.error());
            }
            continue;
        }
        const detail::ReceiveOutcome outcome =
            detail::classify_receive(ec, received, buffer.size());
        if (outcome.action == detail::ReceiveAction::Retry) {
            continue; // the loop re-polls with whatever time is left, so no deadline check here
        }
        if (outcome.action == detail::ReceiveAction::Fail) {
            return std::unexpected(outcome.error);
        }
        s.sender = from; // only a delivered datagram updates the sender
        s.has_sender = true;
        return received;
    }
}

Result<void> UdpEndpoint::set_remote(std::string_view address, std::uint16_t port) {
    if (!impl_) {
        return std::unexpected(not_open());
    }
    boost::system::error_code ec;
    const std::string text{address};
    const auto ip = asio::ip::make_address(text, ec);
    if (ec || port == 0 || ip.is_v6() != impl_->socket.local_endpoint(ec).address().is_v6()) {
        return std::unexpected(bad_address());
    }
    impl_->remote = udp::endpoint{ip, port};
    impl_->has_remote = true;
    return {};
}

std::optional<UdpPeer> UdpEndpoint::last_sender() const {
    if (!impl_ || !impl_->has_sender) {
        return std::nullopt;
    }
    // to_string() has no error_code overload; it can only fail (and abort) on a corrupt address.
    return UdpPeer{.address = impl_->sender.address().to_string(), .port = impl_->sender.port()};
}

std::optional<UdpPeer> UdpEndpoint::remote() const {
    if (!impl_ || !impl_->has_remote) {
        return std::nullopt;
    }
    return UdpPeer{.address = impl_->remote.address().to_string(), .port = impl_->remote.port()};
}

bool UdpEndpoint::last_sender_is_remote() const {
    return impl_ && impl_->has_remote && impl_->has_sender && impl_->sender == impl_->remote;
}

std::uint16_t UdpEndpoint::local_port() const {
    if (!impl_) {
        return 0;
    }
    boost::system::error_code ec;
    const auto ep = impl_->socket.local_endpoint(ec);
    return ec ? std::uint16_t{0} : ep.port();
}

} // namespace cigi::transport
