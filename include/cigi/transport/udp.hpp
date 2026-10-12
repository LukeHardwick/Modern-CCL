#pragma once

// Synchronous, poll-friendly UDP transport (ICD 3.3 section 2: UDP over Ethernet).
// No Asio/Boost types appear here; the implementation is hidden behind a pimpl.

#include "cigi/error.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace cigi::transport {

/// Conventional ports. The ICD does not mandate any; these match common practice.
inline constexpr std::uint16_t default_ig_port = 8004;   ///< IG listens here.
inline constexpr std::uint16_t default_host_port = 8005; ///< Host listens here.

/// Largest payload of an IPv4 UDP datagram (65535 - 20 IP - 8 UDP).
inline constexpr std::size_t max_udp_payload = 65507;

/// Socket configuration. Addresses are numeric IPv4/IPv6 literals (no DNS lookups, so opening
/// never blocks).
struct UdpConfig {
    std::string local_address;     ///< Interface to bind; empty = any (IPv4 "0.0.0.0", or "::" when
                                   ///< `remote_address` is IPv6).
    std::uint16_t local_port = 0;  ///< 0 = pick an ephemeral port (see `local_port()`).
    std::string remote_address;    ///< Where `send` goes; empty = set later with `set_remote`.
    std::uint16_t remote_port = 0; ///< Must be non-zero when `remote_address` is given.
    std::size_t receive_buffer_size = 0; ///< SO_RCVBUF in bytes (<= INT_MAX); 0 = OS default.
};

/// A peer's IP address (numeric text) and port.
struct UdpPeer {
    std::string address;
    std::uint16_t port = 0;
};

/// A bound UDP socket with an optional default destination. Move-only; not thread-safe.
///
/// Failures are reported with `ErrorCode::TransportError` (errno / WSA code in `Error::os_error`),
/// `ErrorCode::Timeout`, `ErrorCode::BufferTooSmall` or `ErrorCode::InvalidFieldValue` (bad
/// address, or a send with no destination); nothing throws.
///
/// Limits of the no-exceptions build: a few rare OS failures inside Asio cannot be reported and
/// abort the process instead (the library installs an aborting `boost::throw_exception`):
/// creating the internal io_context when the process is out of file descriptors, a failing
/// `run()` inside `receive()`, and similar. Ordinary socket errors are returned as a Result.
class UdpEndpoint {
public:
    /// Create, configure and bind the socket.
    [[nodiscard]] static Result<UdpEndpoint> open(const UdpConfig& config);

    UdpEndpoint(UdpEndpoint&&) noexcept;
    UdpEndpoint& operator=(UdpEndpoint&&) noexcept;
    UdpEndpoint(const UdpEndpoint&) = delete;
    UdpEndpoint& operator=(const UdpEndpoint&) = delete;
    ~UdpEndpoint();

    /// Send one datagram to the configured remote. Datagrams larger than the OS allows fail with
    /// `TransportError` (the limit is 65507 bytes, and lower on some systems, e.g. 9216 on macOS).
    Result<void> send(std::span<const std::byte> datagram);

    /// Send one datagram to an explicit peer (numeric address), ignoring the configured remote.
    Result<void> send_to(const UdpPeer& peer, std::span<const std::byte> datagram);

    /// Send one datagram to the source of the most recently delivered datagram. No allocation.
    /// `InvalidFieldValue` if nothing has been received yet.
    Result<void> reply(std::span<const std::byte> datagram);

    /// Wait up to `timeout` for one datagram and copy it into `buffer`; returns its size.
    /// A zero (or negative) timeout polls without waiting. `Timeout` if nothing arrived.
    /// If the datagram does not fit, it is discarded and `BufferTooSmall` is returned.
    /// ICMP "port unreachable" notifications from an absent peer are ignored.
    Result<std::size_t> receive(std::span<std::byte> buffer, std::chrono::milliseconds timeout);

    /// Change the default destination used by `send`.
    Result<void> set_remote(std::string_view address, std::uint16_t port);

    /// Source of the most recently received datagram, if any (lets an IG reply to its Host).
    [[nodiscard]] std::optional<UdpPeer> last_sender() const;

    /// The configured destination of `send`, if any.
    [[nodiscard]] std::optional<UdpPeer> remote() const;

    /// True if the most recently received datagram came from the configured remote (address and
    /// port); false if it came from anyone else, or there is no remote or no datagram yet.
    [[nodiscard]] bool last_sender_is_remote() const;

    /// The bound local port (useful after binding port 0); 0 if the endpoint is not open.
    [[nodiscard]] std::uint16_t local_port() const;

    /// False only for a moved-from endpoint.
    [[nodiscard]] bool is_open() const noexcept { return impl_ != nullptr; }

private:
    struct Impl;
    explicit UdpEndpoint(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace cigi::transport
