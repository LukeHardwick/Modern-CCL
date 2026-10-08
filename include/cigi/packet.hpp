#pragma once

// Packet-level vocabulary: the Packet concept, packet header, and shared decode validation.
//
// Packet codec conventions. Every packet type in cigi::packets follows these, so a packet is a
// plain value type plus three free functions found by ADL (ICD 2.3, 2.5, 2.6, 2.11):
//
//   a. encode() writes every reserved byte/bit as 0 and masks each value to its field width. It
//      always writes the host's native byte order and the library's own protocol version.
//   b. decode() validates, in this order: the span holds at least the 2-byte header; the opcode
//      byte matches (else OpcodeMismatch); the size byte equals the span size and is at least
//      the packet's minimum (else InvalidPacketSize). Bytes beyond the known layout are ignored
//      (ICD 2.11 rule 4), so a larger packet from a newer peer still decodes.
//   c. decode() is tolerant of enum values the ICD does not define: enums are
//      `enum class X : std::uint8_t` and the raw value is stored without error. Whether a value
//      is acceptable is the application's decision.
//   d. decode() ignores reserved bits and bytes.
//   e. Errors carry the opcode byte found in the data (equal to the packet's own once it matched).
//   A packet-level error has offset 0 (relative to the
//      packet); the message layer rewrites it to the offset within the datagram.

#include "cigi/detail/assert.hpp"
#include "cigi/error.hpp"
#include "cigi/wire/byte_order.hpp"
#include "cigi/wire/codec.hpp"

#include <algorithm>
#include <chrono>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ratio>
#include <span>
#include <type_traits>

namespace cigi {

/// Which way a packet (or message) travels (ICD 2.3).
enum class Direction : std::uint8_t {
    HostToIg, ///< Host to Image Generator.
    IgToHost, ///< Image Generator to Host.
    Both,     ///< Either way (user-defined packets, ICD 4.3).
};

/// CIGI version as carried in the Major/Minor Version fields (ICD 4.1.1, 4.2.1).
struct ProtocolVersion {
    std::uint8_t major;
    std::uint8_t minor;

    friend constexpr auto operator<=>(const ProtocolVersion&,
                                      const ProtocolVersion&) noexcept = default;
};

/// The version this library sends (3.3).
inline constexpr ProtocolVersion protocol_version{.major = 3, .minor = 3};

/// Timestamp in the ICD's 10 microsecond units (ICD 4.1.1); wraps after about 11.9 hours.
using Timestamp = std::chrono::duration<std::uint32_t, std::ratio<1, 100'000>>;

/// Opcodes of the two packets that start every message (ICD 2.6).
inline constexpr std::uint8_t ig_control_opcode = 1;
inline constexpr std::uint8_t start_of_frame_opcode = 101;

/// The packet that must start a message travelling in direction `d` (HostToIg or IgToHost).
[[nodiscard]] constexpr std::uint8_t frame_packet_opcode(Direction d) noexcept {
    return d == Direction::IgToHost ? start_of_frame_opcode : ig_control_opcode;
}

/// A packet type: static `opcode` and `direction`, plus ADL-found
///   encoded_size(const P&) -> std::size_t
///   encode(const P&, std::span<std::byte>) -> void   (span size must equal encoded_size)
///   decode(std::type_identity<P>, std::span<const std::byte>, wire::ByteOrder) -> Result<P>
/// Fixed-size packets also expose `static constexpr std::size_t wire_size`.
template <class P>
concept Packet = requires(const P& p, std::span<std::byte> out, std::span<const std::byte> in,
                          wire::ByteOrder order) {
    requires std::same_as<std::remove_cvref_t<decltype(P::opcode)>, std::uint8_t>;
    requires std::same_as<std::remove_cvref_t<decltype(P::direction)>, Direction>;
    { encoded_size(p) } -> std::same_as<std::size_t>;
    { encode(p, out) } -> std::same_as<void>;
    { decode(std::type_identity<P>{}, in, order) } -> std::same_as<Result<P>>;
};

/// The 2-byte header every packet starts with (ICD 2.6).
struct PacketHeader {
    std::uint8_t opcode;
    std::uint8_t size; ///< Whole packet size in bytes, header included.
};

/// Read the header at `offset`. Truncated (with `offset`) if fewer than 2 bytes remain.
[[nodiscard]] constexpr Result<PacketHeader> read_header(std::span<const std::byte> bytes,
                                                         std::size_t offset) noexcept {
    if (!wire::fits(bytes, offset, 2)) {
        return std::unexpected(Error{.code = ErrorCode::Truncated, .offset = offset});
    }
    return PacketHeader{
        .opcode = wire::load<std::uint8_t>(bytes, offset, wire::ByteOrder::Native),
        .size = wire::load<std::uint8_t>(bytes, offset + 1, wire::ByteOrder::Native),
    };
}

namespace detail {

/// Convention (b): header, opcode and size checks shared by every decode().
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
[[nodiscard]] constexpr Result<void> validate_packet(std::span<const std::byte> bytes,
                                                     std::uint8_t opcode,
                                                     std::size_t min_size) noexcept {
    // Every error reports the opcode byte actually found, when there is one to read.
    const std::uint8_t found = bytes.empty()
                                   ? std::uint8_t{0}
                                   : wire::load<std::uint8_t>(bytes, 0, wire::ByteOrder::Native);
    const auto header = read_header(bytes, 0);
    if (!header) {
        return std::unexpected(Error{.code = ErrorCode::Truncated, .opcode = found});
    }
    if (header->opcode != opcode) {
        return std::unexpected(Error{.code = ErrorCode::OpcodeMismatch, .opcode = found});
    }
    if (header->size != bytes.size() || bytes.size() < min_size) {
        return std::unexpected(Error{.code = ErrorCode::InvalidPacketSize, .opcode = found});
    }
    return {};
}
// NOLINTEND(bugprone-easily-swappable-parameters)

} // namespace detail

} // namespace cigi
