#pragma once

// Start of Frame packet (ICD 4.2.1), opcode 101, IG -> Host. The first packet of every IG message.
//
//   byte 0 opcode (101)          byte 1 size (24)           byte 2 major version (3)
//   byte 3 database number (i8)  byte 4 IG status code
//   byte 5 bits 7-4 minor version, 3 earth reference model, 2 timestamp valid, 1-0 IG mode
//   bytes 6-7 byte swap magic    bytes 8-11 IG frame number bytes 12-15 timestamp
//   bytes 16-19 last host frame number                      bytes 20-23 reserved
//
// Peers older than 3.2 send 16 bytes (no last-host-frame field; bytes 8-11 are a "frame
// counter"). Tolerated on decode, never produced.
// The field table covers the body; version, magic and legacy rules wrap it in custom codecs.

#include "cigi/packet.hpp"
#include "cigi/packets/common.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>

namespace cigi {

/// Earth Reference Model in force (ICD 4.2.1).
enum class EarthReferenceModel : std::uint8_t {
    Wgs84 = 0,
    HostDefined = 1,
};

template <> struct EnumTraits<EarthReferenceModel> {
    [[nodiscard]] static constexpr bool is_defined(EarthReferenceModel m) noexcept {
        return enum_between(m, EarthReferenceModel::Wgs84, EarthReferenceModel::HostDefined);
    }
};

struct StartOfFrame {
    static constexpr std::uint8_t opcode = start_of_frame_opcode;
    static constexpr Direction direction = Direction::IgToHost;
    static constexpr std::size_t wire_size = 24;

    /// -128 unavailable, -127..-1 loading, 0 IG controls, 1..127 loaded (every int8 is defined).
    std::int8_t database_number = 0;
    std::uint8_t ig_status = 0; ///< IG Status Code (application-defined, ICD 4.2.1).
    IgMode ig_mode = IgMode::ResetStandby;
    bool timestamp_valid = false;
    EarthReferenceModel earth_reference_model = EarthReferenceModel::Wgs84;
    std::uint32_t ig_frame_number = 0;
    Timestamp timestamp{};
    std::uint32_t last_host_frame_number = 0;
    ProtocolVersion version = protocol_version; ///< Filled by decode; encode always writes 3.3.

    static constexpr auto fields = std::tuple{
        wire::scalar(&StartOfFrame::database_number, 3),
        wire::scalar(&StartOfFrame::ig_status, 4),
        wire::bits(&StartOfFrame::ig_mode, 5, 0, 2),
        wire::flag(&StartOfFrame::timestamp_valid, 5, 2),
        wire::bits(&StartOfFrame::earth_reference_model, 5, 3, 1),
        wire::scalar(&StartOfFrame::ig_frame_number, 8),
        wire::scalar(&StartOfFrame::timestamp, 12),
        wire::scalar(&StartOfFrame::last_host_frame_number, 16),
    };

    friend constexpr bool operator==(const StartOfFrame&, const StartOfFrame&) noexcept = default;
};
static_assert(wire::layout_ok<StartOfFrame>());

[[nodiscard]] constexpr std::size_t encoded_size(const StartOfFrame& /*p*/) noexcept {
    return StartOfFrame::wire_size;
}

/// Precondition: out.size() == encoded_size(p).
constexpr void encode(const StartOfFrame& p, std::span<std::byte> out) noexcept {
    detail::write_frame_header(out, StartOfFrame::opcode, 5);
    wire::encode_fields(p, out);
}

[[nodiscard]] constexpr Result<StartOfFrame> decode(std::type_identity<StartOfFrame> /*tag*/,
                                                    std::span<const std::byte> bytes,
                                                    wire::ByteOrder order) noexcept {
    const auto version = detail::validate_frame_packet(bytes, StartOfFrame::opcode, 5, order);
    if (!version) {
        return std::unexpected(version.error());
    }
    StartOfFrame p;
    wire::decode_fields(p, bytes, order);
    p.version = *version;
    if (version->minor < detail::first_modern_minor) {
        p.last_host_frame_number = 0; // the field only exists from 3.2
    }
    return p;
}

/// IG Mode and Earth Reference Model must be defined values (ICD 4.2.1); every Database Number
/// and IG Status Code is defined.
[[nodiscard]] constexpr Result<void> validate(const StartOfFrame& p) noexcept {
    return wire::validate_fields(p);
}

} // namespace cigi
