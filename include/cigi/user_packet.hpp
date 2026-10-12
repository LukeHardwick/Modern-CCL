#pragma once

// User-defined packets (ICD 2.10, 4.3): opcodes 201-255, size 4 + 4n bytes and a multiple of 8
// (so at least 8), identifying data in the first two 32-bit words (a convention, not checked).
//
// A user packet is written exactly like a standard one (a field table for a fixed layout, which
// also gives byte swapping for free, or its own codec for a variable layout) and is registered by
// extending a packet set: `ExtendUser<HostToIgPackets, MyPacket>` (see cigi/packet_set.hpp).

#include "cigi/packet.hpp"

#include <cstddef>
#include <cstdint>

namespace cigi {

/// First and last opcode reserved for user-defined packets (ICD 2.10, 4.3).
inline constexpr std::uint8_t user_opcode_min = 201;
inline constexpr std::uint8_t user_opcode_max = 255;

/// True if `opcode` lies in the user-defined range.
[[nodiscard]] constexpr bool is_user_opcode(std::uint8_t opcode) noexcept {
    static_assert(user_opcode_max == 0xFF, "the upper bound is the largest 8-bit opcode");
    return opcode >= user_opcode_min;
}

/// True if `size` is a legal user-defined packet size (ICD 4.3): 4 + 4n, a multiple of 8 (ICD
/// 2.6), at least 8, and it fits the 8-bit size field.
[[nodiscard]] constexpr bool is_valid_user_size(std::size_t size) noexcept {
    return size >= 8 && size % 8 == 0 && size <= wire::max_packet_size;
}

namespace detail {

template <class P> [[nodiscard]] consteval bool fixed_user_size_ok() noexcept {
    if constexpr (requires { P::wire_size; }) {
        return is_valid_user_size(P::wire_size);
    } else {
        return true;
    }
}

} // namespace detail

/// A Packet whose opcode is in 201-255 and, for fixed-size packets (those with a `wire_size`),
/// whose size is legal. Variable-size user packets are checked when encoded: MessageBuilder::add
/// rejects an encoded_size that is not a legal user size (InvalidPacketSize). Direction may be
/// HostToIg, IgToHost or Both (ICD 4.3: either way).
template <class P>
concept UserDefinedPacket =
    Packet<P> && is_user_opcode(P::opcode) && detail::fixed_user_size_ok<P>();

} // namespace cigi

namespace cigi {

/// A user-defined packet a Host can receive (direction IgToHost or Both): what Host::on_user takes.
template <class P>
concept HostReceivableUserPacket = UserDefinedPacket<P> && P::direction != Direction::HostToIg;

/// A user-defined packet an IG can receive (direction HostToIg or Both): what Ig::on_user takes.
template <class P>
concept IgReceivableUserPacket = UserDefinedPacket<P> && P::direction != Direction::IgToHost;

} // namespace cigi
