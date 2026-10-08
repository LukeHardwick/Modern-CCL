#pragma once

// Rate Control packet (ICD 4.1.8), opcode 8, Host -> IG, 32 bytes. Table-driven.
//
//   2-3 entity id   4 articulated part id
//   5 bits 7-2 reserved, 1 coordinate system (3.2+), 0 apply to articulated part
//   6-7 reserved    8 X  12 Y  16 Z linear rate (f32 m/s)   20 roll  24 pitch  28 yaw (f32 deg/s)
//
// The coordinate-system bit is reserved (zero, World/Parent) in 3.0/3.1; senders zero reserved
// bits (ICD 2.11 rule 2), so older packets decode as World/Parent. The ICD gives no range for
// rates.

#include "cigi/packet.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>

namespace cigi {

/// Reference frame of the rates (ICD 4.1.8, Table 14). Ignored when applied to a part.
enum class RateCoordinateSystem : std::uint8_t {
    WorldParent = 0,
    Local = 1,
};

template <> struct EnumTraits<RateCoordinateSystem> {
    [[nodiscard]] static constexpr bool is_defined(RateCoordinateSystem v) noexcept {
        return enum_between(v, RateCoordinateSystem::WorldParent, RateCoordinateSystem::Local);
    }
};

struct RateControl {
    static constexpr std::uint8_t opcode = 8;
    static constexpr Direction direction = Direction::HostToIg;
    static constexpr std::size_t wire_size = 32;

    std::uint16_t entity_id = 0;
    std::uint8_t articulated_part_id = 0; ///< Ignored unless apply_to_articulated_part.
    bool apply_to_articulated_part = false;
    RateCoordinateSystem coordinate_system = RateCoordinateSystem::WorldParent;
    float x_linear_rate = 0.0F; ///< m/s.
    float y_linear_rate = 0.0F;
    float z_linear_rate = 0.0F;
    float roll_rate = 0.0F; ///< deg/s.
    float pitch_rate = 0.0F;
    float yaw_rate = 0.0F;

    static constexpr auto fields = std::tuple{
        wire::scalar(&RateControl::entity_id, 2),
        wire::scalar(&RateControl::articulated_part_id, 4),
        wire::flag(&RateControl::apply_to_articulated_part, 5, 0),
        wire::bits(&RateControl::coordinate_system, 5, 1, 1),
        wire::scalar(&RateControl::x_linear_rate, 8),
        wire::scalar(&RateControl::y_linear_rate, 12),
        wire::scalar(&RateControl::z_linear_rate, 16),
        wire::scalar(&RateControl::roll_rate, 20),
        wire::scalar(&RateControl::pitch_rate, 24),
        wire::scalar(&RateControl::yaw_rate, 28),
    };

    friend constexpr bool operator==(const RateControl&, const RateControl&) noexcept = default;
};
static_assert(wire::layout_ok<RateControl>());

} // namespace cigi
