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
// The field table covers the body; version, magic and legacy rules wrap it in custom codecs.

#include "cigi/packet.hpp"
#include "cigi/packets/common.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
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

    static constexpr auto fields = std::tuple{
        wire::scalar(&IgControl::database_number, 3).range(0, 127),
        wire::bits(&IgControl::ig_mode, 4, 0, 2),
        wire::flag(&IgControl::timestamp_valid, 4, 2),
        wire::flag(&IgControl::extrapolation_enabled, 4, 3),
        wire::scalar(&IgControl::host_frame_number, 8),
        wire::scalar(&IgControl::timestamp, 12),
        wire::scalar(&IgControl::last_ig_frame_number, 16),
    };

    friend constexpr bool operator==(const IgControl&, const IgControl&) noexcept = default;
};
static_assert(wire::layout_ok<IgControl>());

[[nodiscard]] constexpr std::size_t encoded_size(const IgControl& /*p*/) noexcept {
    return IgControl::wire_size;
}

/// Precondition: out.size() == encoded_size(p).
constexpr void encode(const IgControl& p, std::span<std::byte> out) noexcept {
    detail::write_frame_header(out, IgControl::opcode, 4);
    wire::encode_fields(p, out);
}

[[nodiscard]] constexpr Result<IgControl> decode(std::type_identity<IgControl> /*tag*/,
                                                 std::span<const std::byte> bytes,
                                                 wire::ByteOrder order) noexcept {
    const auto version = detail::validate_frame_packet(bytes, IgControl::opcode, 4, order);
    if (!version) {
        return std::unexpected(version.error());
    }
    IgControl p;
    wire::decode_fields(p, bytes, order);
    p.version = *version;
    // The extrapolation flag is reserved before 3.3 (ICD 3.2 4.1.1); ignore it there. Likewise
    // the last-IG-frame field only exists from 3.2.
    p.extrapolation_enabled = p.extrapolation_enabled && version->minor >= 3;
    if (version->minor < detail::first_modern_minor) {
        p.last_ig_frame_number = 0;
    }
    return p;
}

/// Field ranges (Database Number 0..127; negative is not a valid command) plus IG Mode: the Host
/// cannot command Offline Maintenance (ICD 4.1.1, 2.8). Errors point at the field's byte offset.
[[nodiscard]] constexpr Result<void> validate(const IgControl& p) noexcept {
    if (auto ok = wire::validate_fields(p); !ok) {
        return ok;
    }
    if (p.ig_mode == IgMode::OfflineMaintenance) {
        return std::unexpected(
            Error{.code = ErrorCode::InvalidFieldValue, .offset = 4, .opcode = IgControl::opcode});
    }
    return {};
}

} // namespace cigi
