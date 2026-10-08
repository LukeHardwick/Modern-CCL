#pragma once

// HAT/HOT Request packet (ICD 4.1.24), opcode 24, Host -> IG, 32 bytes.
//
//   bytes 2-3 HAT/HOT ID   byte 4 bits 7-3 reserved, 2 coordinate system, 1-0 request type
//   byte 5 update period   bytes 6-7 entity ID
//   bytes 8-15 latitude / X offset   16-23 longitude / Y offset   24-31 altitude / Z offset
//
// CIGI 3.0/3.1 reserved byte 5 (Update Period was added in 3.2, ICD Appendix B); it decodes as 0
// from such peers, which means "one-shot" - exactly what they meant. Table-driven; the geodetic
// ranges only apply in the geodetic coordinate system, so validate() is custom.

#include "cigi/packet.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>

namespace cigi {

/// What the IG should return (ICD 4.1.24 Request Type).
enum class HatHotRequestType : std::uint8_t {
    Hat = 0,      ///< Height Above Terrain, answered by a HAT/HOT Response.
    Hot = 1,      ///< Height Of Terrain, answered by a HAT/HOT Response.
    Extended = 2, ///< Both, plus material and normal, answered by an Extended Response.
};

template <> struct EnumTraits<HatHotRequestType> {
    [[nodiscard]] static constexpr bool is_defined(HatHotRequestType t) noexcept {
        return enum_between(t, HatHotRequestType::Hat, HatHotRequestType::Extended);
    }
};

/// Frame in which the test point is expressed (ICD 4.1.24 Coordinate System).
enum class HatHotCoordinateSystem : std::uint8_t {
    Geodetic = 0, ///< Latitude, longitude, altitude.
    Entity = 1,   ///< X, Y, Z offsets from the reference point of `entity_id`.
};

template <> struct EnumTraits<HatHotCoordinateSystem> {
    [[nodiscard]] static constexpr bool is_defined(HatHotCoordinateSystem c) noexcept {
        return enum_between(c, HatHotCoordinateSystem::Geodetic, HatHotCoordinateSystem::Entity);
    }
};

struct HatHotRequest {
    static constexpr std::uint8_t opcode = 24;
    static constexpr Direction direction = Direction::HostToIg;
    static constexpr std::size_t wire_size = 32;

    std::uint16_t id = 0; ///< HAT/HOT ID; echoed by the response.
    HatHotRequestType request_type = HatHotRequestType::Hat;
    HatHotCoordinateSystem coordinate_system = HatHotCoordinateSystem::Geodetic;
    std::uint8_t update_period = 0; ///< 0: one-shot; n: respond every nth frame (3.2+).
    std::uint16_t entity_id = 0;    ///< Ignored when geodetic.
    double latitude_x = 0;  ///< Latitude in degrees (-90..90) if geodetic, else X offset in metres.
    double longitude_y = 0; ///< Longitude in degrees (-180..180) if geodetic, else Y offset.
    double altitude_z = 0;  ///< Altitude above MSL if geodetic (ignored for HOT), else Z offset.

    static constexpr auto fields = std::tuple{
        wire::scalar(&HatHotRequest::id, 2),
        wire::bits(&HatHotRequest::request_type, 4, 0, 2),
        wire::bits(&HatHotRequest::coordinate_system, 4, 2, 1),
        wire::scalar(&HatHotRequest::update_period, 5),
        wire::scalar(&HatHotRequest::entity_id, 6),
        wire::scalar(&HatHotRequest::latitude_x, 8),
        wire::scalar(&HatHotRequest::longitude_y, 16),
        wire::scalar(&HatHotRequest::altitude_z, 24),
    };

    friend constexpr bool operator==(const HatHotRequest&, const HatHotRequest&) noexcept = default;
};
static_assert(wire::layout_ok<HatHotRequest>());

/// Enums must be defined; in the geodetic system latitude is -90..90 and longitude -180..180
/// (NaN fails). Errors point at the field's byte offset.
[[nodiscard]] constexpr Result<void> validate(const HatHotRequest& p) noexcept {
    if (auto ok = wire::validate_fields(p); !ok) {
        return ok;
    }
    if (p.coordinate_system == HatHotCoordinateSystem::Geodetic) {
        const bool lat_ok = p.latitude_x >= -90.0 && p.latitude_x <= 90.0;
        const bool lon_ok = p.longitude_y >= -180.0 && p.longitude_y <= 180.0;
        if (!lat_ok || !lon_ok) {
            return std::unexpected(Error{
                .code = ErrorCode::InvalidFieldValue,
                .offset = lat_ok ? std::size_t{16} : std::size_t{8},
                .opcode = HatHotRequest::opcode,
            });
        }
    }
    return {};
}

} // namespace cigi
