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
#include <utility>

namespace cigi::transport {

namespace asio = boost::asio;
using asio::ip::udp;

namespace {

Error os_failure(const boost::system::error_code& ec) {
    return Error{.code = ErrorCode::TransportError, .os_error = ec.value()};
}

Error bad_address() {
    return Error{.code = ErrorCode::InvalidFieldValue};
}

} // namespace

struct UdpEndpoint::Impl {
    asio::io_context io{1};
    udp::socket socket{io};
    udp::endpoint remote;
    udp::endpoint sender;
    bool has_remote = false;
    bool has_sender = false;
};

UdpEndpoint::UdpEndpoint(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
UdpEndpoint::UdpEndpoint(UdpEndpoint&&) noexcept = default;
UdpEndpoint& UdpEndpoint::operator=(UdpEndpoint&&) noexcept = default;
UdpEndpoint::~UdpEndpoint() = default;

Result<UdpEndpoint> UdpEndpoint::open(const UdpConfig& config) {
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
    if (config.receive_buffer_size != 0) {
        ec = impl->socket.set_option(
            asio::socket_base::receive_buffer_size(static_cast<int>(config.receive_buffer_size)),
            ec);
        if (ec) {
            return std::unexpected(os_failure(ec));
        }
    }
    ec = impl->socket.bind(local, ec);
    if (ec) {
        return std::unexpected(os_failure(ec));
    }
    return UdpEndpoint{std::move(impl)};
}

Result<void> UdpEndpoint::send(std::span<const std::byte> datagram) {
    if (!impl_ || !impl_->has_remote) {
        return std::unexpected(Error{.code = ErrorCode::TransportError});
    }
    boost::system::error_code ec;
    static_cast<void>(impl_->socket.send_to(asio::buffer(datagram.data(), datagram.size()),
                                            impl_->remote, 0, ec));
    if (ec) {
        return std::unexpected(os_failure(ec));
    }
    return {}; // a UDP send is all-or-nothing
}

Result<std::size_t> UdpEndpoint::receive(std::span<std::byte> buffer,
                                         std::chrono::milliseconds timeout) {
    if (!impl_) {
        return std::unexpected(Error{.code = ErrorCode::TransportError});
    }
    using Clock = std::chrono::steady_clock;
    Impl& s = *impl_;
    const auto deadline = Clock::now() + std::max(timeout, std::chrono::milliseconds{0});

    // Scatter receive into the caller's buffer plus one spare byte: if the spare byte is written
    // (or the OS reports message_size) the datagram did not fit. No copy, no large scratch buffer.
    std::byte overflow{};
    const std::array<asio::mutable_buffer, 2> sequence{
        asio::mutable_buffer{buffer.data(), buffer.size()},
        asio::mutable_buffer{&overflow, 1},
    };

    for (;;) {
        boost::system::error_code ec = asio::error::would_block;
        std::size_t received = 0;
        s.socket.async_receive_from(sequence, s.sender,
                                    [&](const boost::system::error_code& result, std::size_t n) {
                                        ec = result;
                                        received = n;
                                    });
        s.io.restart();
        s.io.run_for(std::max(deadline - Clock::now(), Clock::duration::zero()));
        if (ec == asio::error::would_block) {
            // Still pending: cancel and let the aborted handler run.
            boost::system::error_code cancel_ec; // the aborted handler reports the outcome
            cancel_ec = s.socket.cancel(cancel_ec);
            static_cast<void>(cancel_ec);
            s.io.restart();
            s.io.run();
        }

        if (ec == asio::error::operation_aborted || ec == asio::error::would_block) {
            return std::unexpected(Error{.code = ErrorCode::Timeout});
        }
        const detail::ReceiveOutcome outcome =
            detail::classify_receive(ec, received, buffer.size());
        if (outcome.action == detail::ReceiveAction::Retry) {
            continue; // the loop re-polls with whatever time is left, so no deadline check here
        }
        if (outcome.action == detail::ReceiveAction::Fail) {
            return std::unexpected(outcome.error);
        }
        s.has_sender = true;
        return received;
    }
}

Result<void> UdpEndpoint::set_remote(std::string_view address, std::uint16_t port) {
    if (!impl_) {
        return std::unexpected(Error{.code = ErrorCode::TransportError});
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
    return UdpPeer{.address = impl_->sender.address().to_string(), .port = impl_->sender.port()};
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
