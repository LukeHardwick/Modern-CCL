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

#include "cigi/packet.hpp"
#include "cigi/packets/common.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace cigi {

/// Earth Reference Model in force (ICD 4.2.1).
enum class EarthReferenceModel : std::uint8_t {
    Wgs84 = 0,
    HostDefined = 1,
};

struct StartOfFrame {
    static constexpr std::uint8_t opcode = start_of_frame_opcode;
    static constexpr Direction direction = Direction::IgToHost;
    static constexpr std::size_t wire_size = 24;

    /// -128 unavailable, -127..-1 loading, 0 IG controls, 1..127 loaded.
    std::int8_t database_number = 0;
    std::uint8_t ig_status = 0; ///< IG Status Code (application-defined, ICD 4.2.1).
    IgMode ig_mode = IgMode::ResetStandby;
    bool timestamp_valid = false;
    EarthReferenceModel earth_reference_model = EarthReferenceModel::Wgs84;
    std::uint32_t ig_frame_number = 0;
    Timestamp timestamp{};
    std::uint32_t last_host_frame_number = 0;
    ProtocolVersion version = protocol_version; ///< Filled by decode; encode always writes 3.3.

    friend constexpr bool operator==(const StartOfFrame&, const StartOfFrame&) noexcept = default;
};

[[nodiscard]] constexpr std::size_t encoded_size(const StartOfFrame& /*p*/) noexcept {
    return StartOfFrame::wire_size;
}

/// Precondition: out.size() == encoded_size(p).
constexpr void encode(const StartOfFrame& p, std::span<std::byte> out) noexcept {
    CIGI_ASSERT(out.size() == StartOfFrame::wire_size);
    std::ranges::fill(out, std::byte{0});
    using wire::store;
    store<std::uint8_t>(out, 0, StartOfFrame::opcode);
    store<std::uint8_t>(out, 1, StartOfFrame::wire_size);
    store<std::uint8_t>(out, 2, protocol_version.major);
    store<std::int8_t>(out, 3, p.database_number);
    store<std::uint8_t>(out, 4, p.ig_status);
    std::uint8_t flags = detail::MinorVersionField::set(0, protocol_version.minor);
    flags = wire::set_flag(flags, 3, p.earth_reference_model == EarthReferenceModel::HostDefined);
    flags = wire::set_flag(flags, detail::timestamp_valid_bit, p.timestamp_valid);
    flags = detail::IgModeField::set(flags, static_cast<std::uint8_t>(p.ig_mode));
    store<std::uint8_t>(out, 5, flags);
    store<std::uint16_t>(out, detail::frame_magic_offset, wire::byte_swap_magic);
    store<std::uint32_t>(out, 8, p.ig_frame_number);
    store<std::uint32_t>(out, 12, p.timestamp.count());
    store<std::uint32_t>(out, 16, p.last_host_frame_number);
}

[[nodiscard]] constexpr Result<StartOfFrame> decode(std::type_identity<StartOfFrame> /*tag*/,
                                                    std::span<const std::byte> bytes,
                                                    wire::ByteOrder order) noexcept {
    const auto version = detail::validate_frame_packet(bytes, StartOfFrame::opcode, 5, order);
    if (!version) {
        return std::unexpected(version.error());
    }
    using wire::load;
    const auto flags = load<std::uint8_t>(bytes, 5, order);
    StartOfFrame p;
    p.version = *version;
    p.database_number = load<std::int8_t>(bytes, 3, order);
    p.ig_status = load<std::uint8_t>(bytes, 4, order);
    p.ig_mode = static_cast<IgMode>(detail::IgModeField::get(flags));
    p.timestamp_valid = wire::get_flag(flags, detail::timestamp_valid_bit);
    p.earth_reference_model = static_cast<EarthReferenceModel>(wire::get_bits(flags, 3, 1));
    p.ig_frame_number = load<std::uint32_t>(bytes, 8, order);
    p.timestamp = Timestamp{load<std::uint32_t>(bytes, 12, order)};
    if (version->minor >= detail::first_modern_minor) {
        p.last_host_frame_number = load<std::uint32_t>(bytes, 16, order);
    }
    return p;
}

} // namespace cigi
