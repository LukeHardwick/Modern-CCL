#pragma once

#include "cigi/packet.hpp"
#include "cigi/wire/bits.hpp"
#include "cigi/wire/fields.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cigi {

/// IG Mode (ICD 4.1.1 / 4.2.1). Raw wire value; values the ICD does not define survive decode.
/// OfflineMaintenance is only defined for Start of Frame.
enum class IgMode : std::uint8_t {
    ResetStandby = 0,
    Operate = 1,
    Debug = 2,
    OfflineMaintenance = 3,
};

template <> struct EnumTraits<IgMode> {
    [[nodiscard]] static constexpr bool is_defined(IgMode m) noexcept {
        return enum_between(m, IgMode::ResetStandby, IgMode::OfflineMaintenance);
    }
};

namespace detail {

/// Layout shared by the IG Control and Start of Frame headers (ICD 4.1.1, 4.2.1). The flags byte
/// is byte 4 in IG Control and byte 5 in Start of Frame; IG Mode, Timestamp Valid and Minor
/// Version sit at the same bit positions in both.
using MinorVersionField = wire::BitField<4, 4>;

inline constexpr std::size_t frame_magic_offset = 6;  ///< Byte Swap Magic Number, bytes 6-7.
inline constexpr std::size_t frame_legacy_size = 16;  ///< CIGI 3.0/3.1 (minor < 2) packet size.
inline constexpr std::size_t frame_size = 24;         ///< CIGI 3.2/3.3 packet size.
inline constexpr std::uint8_t first_modern_minor = 2; ///< Minor versions >= this use frame_size.

/// Zero `out` and write the parts of the frame packet header that are not table fields: opcode,
/// size, major version, the Minor Version nibble (in the flags byte at `flags_offset`) and the
/// byte swap magic number. The caller then calls wire::encode_fields.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
constexpr void write_frame_header(std::span<std::byte> out, std::uint8_t opcode,
                                  std::size_t flags_offset) noexcept {
    CIGI_ASSERT(out.size() == frame_size);
    std::ranges::fill(out, std::byte{0});
    wire::store<std::uint8_t>(out, 0, opcode);
    wire::store<std::uint8_t>(out, 1, static_cast<std::uint8_t>(frame_size));
    wire::store<std::uint8_t>(out, 2, protocol_version.major);
    wire::store<std::uint8_t>(out, flags_offset, MinorVersionField::set(0, protocol_version.minor));
    wire::store<std::uint16_t>(out, frame_magic_offset, wire::byte_swap_magic);
}

/// Validation common to IG Control and Start of Frame decode (conventions b and the version /
/// byte-swap rules of ICD 2.5 and 4.1.1 / 4.2.1). `flags_offset` is the byte holding the Minor
/// Version nibble. Returns the peer's version; the caller reads the remaining fields.
[[nodiscard]] constexpr Result<ProtocolVersion>
validate_frame_packet(std::span<const std::byte> bytes, std::uint8_t opcode,
                      std::size_t flags_offset, wire::ByteOrder order) noexcept {
    if (auto ok = validate_packet(bytes, opcode, frame_legacy_size); !ok) {
        return std::unexpected(ok.error());
    }
    const auto major = wire::load<std::uint8_t>(bytes, 2, order);
    if (major != protocol_version.major) {
        return std::unexpected(Error{.code = ErrorCode::UnsupportedVersion, .opcode = opcode});
    }
    const auto minor = MinorVersionField::get(wire::load<std::uint8_t>(bytes, flags_offset, order));
    if (minor >= first_modern_minor && bytes.size() < frame_size) {
        return std::unexpected(Error{.code = ErrorCode::InvalidPacketSize, .opcode = opcode});
    }
    if (wire::load<std::uint16_t>(bytes, frame_magic_offset, order) != wire::byte_swap_magic) {
        return std::unexpected(Error{.code = ErrorCode::InvalidByteSwapMagic, .opcode = opcode});
    }
    return ProtocolVersion{.major = major, .minor = minor};
}
// NOLINTEND(bugprone-easily-swappable-parameters)

} // namespace detail

} // namespace cigi
