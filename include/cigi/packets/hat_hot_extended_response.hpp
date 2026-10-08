#pragma once

// HAT/HOT Extended Response packet (ICD 4.2.3), opcode 103, IG -> Host, 40 bytes.
//
//   bytes 2-3 HAT/HOT ID   byte 4 bits 7-4 Host Frame Number LSN, 3-1 reserved, 0 valid
//   bytes 5-7 reserved     bytes 8-15 HAT   16-23 HOT   24-27 material code
//   bytes 28-31 normal azimuth   32-35 normal elevation   36-39 reserved
//
// CIGI 3.0/3.1 reserved the LSN nybble (added in 3.2, ICD Appendix B). A conforming 3.0/3.1 peer
// writes 0 there; decode itself is version-agnostic and passes any value through, so sessions
// (T07) mask the field by the peer's minor version. Table-driven; the normal's angle ranges only
// bind when Valid is set, so validate() is custom.

#include "cigi/packet.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>

namespace cigi {

struct HatHotExtendedResponse {
    static constexpr std::uint8_t opcode = 103;
    static constexpr Direction direction = Direction::IgToHost;
    static constexpr std::size_t wire_size = 40;

    std::uint16_t hat_hot_id = 0; ///< HAT/HOT ID of the request being answered.
    bool valid = false; ///< False: the test point was outside the database; data fields unset.
    std::uint8_t host_frame_lsn = 0; ///< 0..15, see host_frame_lsn(); 3.2+, ignore if one-shot.
    double hat = 0;                  ///< Height above terrain, metres (negative: below).
    double hot = 0;                  ///< Height of terrain, metres above MSL.
    std::uint32_t material_code = 0;
    float normal_azimuth = 0;   ///< Degrees from true north, -180..180.
    float normal_elevation = 0; ///< Degrees above the geodetic reference plane, -90..90.

    static constexpr auto fields = std::tuple{
        wire::scalar(&HatHotExtendedResponse::hat_hot_id, 2),
        wire::flag(&HatHotExtendedResponse::valid, 4, 0),
        wire::bits(&HatHotExtendedResponse::host_frame_lsn, 4, 4, 4).range(0, 15),
        wire::scalar(&HatHotExtendedResponse::hat, 8),
        wire::scalar(&HatHotExtendedResponse::hot, 16),
        wire::scalar(&HatHotExtendedResponse::material_code, 24),
        wire::scalar(&HatHotExtendedResponse::normal_azimuth, 28),
        wire::scalar(&HatHotExtendedResponse::normal_elevation, 32),
    };

    friend constexpr bool operator==(const HatHotExtendedResponse&,
                                     const HatHotExtendedResponse&) noexcept = default;
};
static_assert(wire::layout_ok<HatHotExtendedResponse>());

/// The LSN must be 0..15. When Valid is set the normal's azimuth must be -180..180 and elevation
/// -90..90 (NaN fails); an invalid response carries no meaningful data, so nothing else is
/// checked. Errors point at the field's byte offset.
[[nodiscard]] constexpr Result<void> validate(const HatHotExtendedResponse& p) noexcept {
    if (auto ok = wire::validate_fields(p); !ok) {
        return ok;
    }
    if (p.valid) {
        const bool az_ok = p.normal_azimuth >= -180.0F && p.normal_azimuth <= 180.0F;
        const bool el_ok = p.normal_elevation >= -90.0F && p.normal_elevation <= 90.0F;
        if (!az_ok || !el_ok) {
            return std::unexpected(Error{
                .code = ErrorCode::InvalidFieldValue,
                .offset = az_ok ? std::size_t{32} : std::size_t{28},
                .opcode = HatHotExtendedResponse::opcode,
            });
        }
    }
    return {};
}

} // namespace cigi
