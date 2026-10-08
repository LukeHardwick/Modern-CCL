#pragma once

// IG Control packet (ICD 4.1.1), opcode 1, Host -> IG. The first packet of every Host message.
//
//   byte 0 opcode (1)            byte 1 size (24)           byte 2 major version (3)
//   byte 3 database number (i8)  byte 4 bits 7-4 minor version, 3 extrapolation/interpolation
//                                enable, 2 timestamp valid, 1-0 IG mode
//   byte 5 reserved              bytes 6-7 byte swap magic  bytes 8-11 host frame number
//   bytes 12-15 timestamp        bytes 16-19 last IG frame number     bytes 20-23 reserved
//
// Peers older than 3.2 send 16 bytes (no last-IG-frame field; bytes 8-11 are a "frame counter");
// 3.2 has no extrapolation flag. Both are tolerated on decode, never produced.

#include "cigi/packet.hpp"
#include "cigi/packets/common.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace cigi {

struct IgControl {
    static constexpr std::uint8_t opcode = ig_control_opcode;
    static constexpr Direction direction = Direction::HostToIg;
    static constexpr std::size_t wire_size = 24;

    std::int8_t database_number = 0; ///< 0: no load; 1..127: load that database.
    IgMode ig_mode = IgMode::ResetStandby;
    bool timestamp_valid = false;
    bool extrapolation_enabled = false;
    std::uint32_t host_frame_number = 0;
    Timestamp timestamp{};
    std::uint32_t last_ig_frame_number = 0;
    ProtocolVersion version = protocol_version; ///< Filled by decode; encode always writes 3.3.

    friend constexpr bool operator==(const IgControl&, const IgControl&) noexcept = default;
};

[[nodiscard]] constexpr std::size_t encoded_size(const IgControl& /*p*/) noexcept {
    return IgControl::wire_size;
}

/// Precondition: out.size() == encoded_size(p).
constexpr void encode(const IgControl& p, std::span<std::byte> out) noexcept {
    CIGI_ASSERT(out.size() == IgControl::wire_size);
    std::ranges::fill(out, std::byte{0});
    using wire::store;
    store<std::uint8_t>(out, 0, IgControl::opcode);
    store<std::uint8_t>(out, 1, IgControl::wire_size);
    store<std::uint8_t>(out, 2, protocol_version.major);
    store<std::int8_t>(out, 3, p.database_number);
    std::uint8_t flags = detail::MinorVersionField::set(0, protocol_version.minor);
    flags = wire::set_flag(flags, 3, p.extrapolation_enabled);
    flags = wire::set_flag(flags, detail::timestamp_valid_bit, p.timestamp_valid);
    flags = detail::IgModeField::set(flags, static_cast<std::uint8_t>(p.ig_mode));
    store<std::uint8_t>(out, 4, flags);
    store<std::uint16_t>(out, detail::frame_magic_offset, wire::byte_swap_magic);
    store<std::uint32_t>(out, 8, p.host_frame_number);
    store<std::uint32_t>(out, 12, p.timestamp.count());
    store<std::uint32_t>(out, 16, p.last_ig_frame_number);
}

[[nodiscard]] constexpr Result<IgControl> decode(std::type_identity<IgControl> /*tag*/,
                                                 std::span<const std::byte> bytes,
                                                 wire::ByteOrder order) noexcept {
    const auto version = detail::validate_frame_packet(bytes, IgControl::opcode, 4, order);
    if (!version) {
        return std::unexpected(version.error());
    }
    using wire::load;
    const auto flags = load<std::uint8_t>(bytes, 4, order);
    IgControl p;
    p.version = *version;
    p.database_number = load<std::int8_t>(bytes, 3, order);
    p.ig_mode = static_cast<IgMode>(detail::IgModeField::get(flags));
    p.timestamp_valid = wire::get_flag(flags, detail::timestamp_valid_bit);
    // The extrapolation flag is reserved before 3.3 (ICD 3.2 4.1.1); ignore it there.
    p.extrapolation_enabled = version->minor >= 3 && wire::get_flag(flags, 3);
    p.host_frame_number = load<std::uint32_t>(bytes, 8, order);
    p.timestamp = Timestamp{load<std::uint32_t>(bytes, 12, order)};
    if (version->minor >= detail::first_modern_minor) {
        p.last_ig_frame_number = load<std::uint32_t>(bytes, 16, order);
    }
    return p;
}

} // namespace cigi
