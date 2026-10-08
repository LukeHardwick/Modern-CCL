#pragma once

// Test helpers: byte-array literals, fabricating foreign-endian frames, and test-only packets.

#include "cigi/packet.hpp"
#include "cigi/wire/codec.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cigi::test {

inline constexpr bool host_is_little = std::endian::native == std::endian::little;

template <class... T> constexpr std::array<std::byte, sizeof...(T)> bytes_of(T... values) {
    return {static_cast<std::byte>(values)...};
}

/// Choose the golden array matching the host byte order, and the one a foreign peer would send.
template <class A> constexpr const A& native_of(const A& little, const A& big) {
    return host_is_little ? little : big;
}
template <class A> constexpr const A& foreign_of(const A& little, const A& big) {
    return host_is_little ? big : little;
}

constexpr ProtocolVersion version_of(std::uint8_t major, std::uint8_t minor) {
    return {.major = major, .minor = minor};
}

using Bytes = std::vector<std::byte>;

inline Bytes to_vec(std::span<const std::byte> s) {
    return {s.begin(), s.end()};
}
inline void append(Bytes& dst, std::span<const std::byte> src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

/// Reverse the byte order of the multi-byte fields of a 24-byte IG Control / Start of Frame
/// packet at `base` (bytes 6-7 and the three u32 at 8, 12, 16), turning a native frame packet
/// into what a foreign-endian peer would have sent.
inline void swap_frame_fields(Bytes& b, std::size_t base = 0) {
    const auto rev = [&](std::size_t off, std::size_t n) {
        std::reverse(b.begin() + static_cast<std::ptrdiff_t>(base + off),
                     b.begin() + static_cast<std::ptrdiff_t>(base + off + n));
    };
    rev(6, 2);
    rev(8, 4);
    rev(12, 4);
    rev(16, 4);
}

/// A zeroed IG Control (opcode 1) or Start of Frame (opcode 101) packet of `size` bytes with
/// major version 3, the given minor version and a valid magic number in `order`.
inline Bytes frame_packet(std::uint8_t opcode, std::size_t size, std::uint8_t minor,
                          wire::ByteOrder order = wire::ByteOrder::Native) {
    Bytes b(size, std::byte{0});
    const std::span<std::byte> s{b};
    wire::store<std::uint8_t>(s, 0, opcode);
    wire::store<std::uint8_t>(s, 1, static_cast<std::uint8_t>(size));
    wire::store<std::uint8_t>(s, 2, 3);
    wire::store<std::uint8_t>(s, opcode == ig_control_opcode ? 4 : 5,
                              static_cast<std::uint8_t>(minor << 4));
    wire::store<std::uint16_t>(s, 6, wire::byte_swap_magic, order);
    return b;
}

/// A user-defined-style packet (Direction::Both), opcode 201, 8 bytes: u16 value at bytes 2-3.
struct UserPacket {
    static constexpr std::uint8_t opcode = 201;
    static constexpr Direction direction = Direction::Both;
    static constexpr std::size_t wire_size = 8;
    std::uint16_t value = 0;
    friend constexpr bool operator==(const UserPacket&, const UserPacket&) noexcept = default;
};
constexpr std::size_t encoded_size(const UserPacket& /*p*/) noexcept {
    return UserPacket::wire_size;
}
constexpr void encode(const UserPacket& p, std::span<std::byte> out) noexcept {
    std::ranges::fill(out, std::byte{0});
    wire::store<std::uint8_t>(out, 0, UserPacket::opcode);
    wire::store<std::uint8_t>(out, 1, UserPacket::wire_size);
    wire::store<std::uint16_t>(out, 2, p.value);
}
constexpr Result<UserPacket> decode(std::type_identity<UserPacket> /*tag*/,
                                    std::span<const std::byte> bytes,
                                    wire::ByteOrder order) noexcept {
    if (auto ok = detail::validate_packet(bytes, UserPacket::opcode, UserPacket::wire_size); !ok) {
        return std::unexpected(ok.error());
    }
    return UserPacket{wire::load<std::uint16_t>(bytes, 2, order)};
}

/// A Host-to-IG-only packet, opcode 150, 8 bytes.
struct HostOnlyPacket {
    static constexpr std::uint8_t opcode = 150;
    static constexpr Direction direction = Direction::HostToIg;
};
constexpr std::size_t encoded_size(const HostOnlyPacket& /*p*/) noexcept {
    return 8;
}
constexpr void encode(const HostOnlyPacket& /*p*/, std::span<std::byte> out) noexcept {
    std::ranges::fill(out, std::byte{0});
    wire::store<std::uint8_t>(out, 0, HostOnlyPacket::opcode);
    wire::store<std::uint8_t>(out, 1, 8);
}
constexpr Result<HostOnlyPacket> decode(std::type_identity<HostOnlyPacket> /*tag*/,
                                        std::span<const std::byte> bytes,
                                        wire::ByteOrder /*order*/) noexcept {
    if (auto ok = detail::validate_packet(bytes, HostOnlyPacket::opcode, 8); !ok) {
        return std::unexpected(ok.error());
    }
    return HostOnlyPacket{};
}

} // namespace cigi::test
