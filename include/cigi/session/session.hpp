#pragma once

// Types shared by HostSession and IgSession: configuration, statistics and the per-message report.
//
// A session holds the protocol state of ONE peer relationship (frame numbers, IG mode, database
// handshake, peer version and byte order) and turns byte spans into packets and back. It never
// touches a socket: the transport (T08) moves the bytes. Sessions are NOT thread-safe: use one
// session per thread, or serialise access yourself.

#include "cigi/packet.hpp"
#include "cigi/wire/byte_order.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace spdlog {
class logger; // NOLINT(readability-identifier-naming): spdlog's own name
}

namespace cigi {

/// Source of the 10 microsecond Timestamp (ICD 4.1.1, 4.2.1). Inject one in tests.
using ClockFn = std::function<Timestamp()>;

/// std::chrono::steady_clock in 10 microsecond ticks, wrapping modulo 2^32 (about 11.9 hours).
[[nodiscard]] Timestamp steady_timestamp() noexcept;

struct SessionConfig {
    /// Synchronous (ICD 2.2.1): the Host sends one message per received Start of Frame. When
    /// false (asynchronous, ICD 2.2.2) the Host may send whenever it likes.
    bool synchronous = true;
    /// Enforce the start-up sequence of ICD 2.8: the Host sends nothing but IG Control until the
    /// IG reports Operate (or Debug), and the IG sends nothing but Start of Frame while it is in
    /// Reset/Standby or Offline Maintenance. A violating add() fails with NotOperational.
    bool enforce_startup_sequence = true;
    /// Run validate() on every packet passed to add() and fail on a bad field value. Off by
    /// default: the ICD's strict ranges (e.g. yaw 0..360) reject conventions common in hosts.
    bool validate_outgoing = false;
    /// ICD 4.3: skip user-defined packets (opcodes 201-255) on receive. They then reach neither a
    /// handler nor on_unknown. Off by default, so they arrive as unknown packets (the sessions'
    /// sets hold no user packets; Host::on_user / Ig::on_user decode them for you).
    bool ignore_user_packets = false;
    /// Size of the buffer a message is built in (at least one frame packet, 24 bytes).
    std::size_t max_message_bytes = 16384;
    /// How many numbers behind the newest one count as a reordered (stale) message rather than a
    /// restart of the peer. A message 1..stale_window behind is reported as `stale`, not acted on.
    std::uint32_t stale_window = 8;
    /// Number the first message this session sends carries (then +1 per message, wrapping).
    std::uint32_t initial_frame_number = 0;
    /// Timestamp source; empty means steady_timestamp.
    ClockFn clock;
    /// Where the session logs; null means nothing is logged.
    std::shared_ptr<spdlog::logger> logger;
};

/// What the session remembers about the peer from its last valid message.
struct PeerInfo {
    ProtocolVersion version;
    wire::ByteOrder byte_order;

    friend constexpr bool operator==(const PeerInfo&, const PeerInfo&) noexcept = default;
};

struct SessionStats {
    std::uint64_t messages_sent = 0;
    std::uint64_t messages_received = 0;
    std::uint64_t lost_frames = 0;       ///< Frame numbers skipped by the peer (messages lost).
    std::uint64_t frame_regressions = 0; ///< Frame number jumped far back or far ahead: the peer
                                         ///< restarted.
    std::uint64_t duplicate_frames = 0;  ///< Messages repeating the previous frame number.
    std::uint64_t stale_frames = 0;      ///< Messages a few frame numbers behind (reordered).
    std::uint64_t decode_errors = 0;     ///< Datagrams or packets that failed to decode.
    std::uint64_t ignored_packets = 0;   ///< Packets dropped by the mode rules of ICD 2.8.
    std::uint64_t unknown_packets = 0;   ///< Packets with an opcode outside the session's set.
    std::uint64_t rejected_packets = 0;  ///< add() calls that failed.
    std::uint64_t mode_requests_refused = 0; ///< IgSession: mode requests that were refused.

    friend constexpr bool operator==(const SessionStats&, const SessionStats&) noexcept = default;
};

/// What one receive() learned beyond the dispatched packets.
struct ReceiveReport {
    std::uint32_t lost_frames = 0; ///< Messages missed since the previous one.
    bool frame_regressed = false;  ///< Frame number went far back or jumped far ahead: the peer
                                   ///< restarted.
    bool duplicate = false;        ///< Same frame number as the previous message. Its frame packet
                                   ///< changed nothing; its other packets were still delivered.
    bool stale = false;            ///< Frame number a few behind the newest (reordered). As above.
    bool mode_changed = false;     ///< Host: the IG's reported mode changed. IG: the Host
                                   ///< requested a different mode than before.
    bool database_changed = false; ///< Host: the IG's reported database number changed. IG: the
                                   ///< Host's requested database number changed.

    friend constexpr bool operator==(const ReceiveReport&, const ReceiveReport&) noexcept = default;
};

} // namespace cigi
