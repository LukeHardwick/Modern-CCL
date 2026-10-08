#pragma once

// Articulated Part Control packet (ICD 4.1.6), opcode 6, Host -> IG, 32 bytes. Table-driven.
//
//   2-3 entity id   4 articulated part id
//   5 bit 7 reserved, 6 yaw enable, 5 pitch enable, 4 roll enable, 3 Z offset enable,
//     2 Y offset enable, 1 X offset enable, 0 articulated part enable
//   6-7 reserved    8 X  12 Y  16 Z offset (f32 m)   20 roll  24 pitch  28 yaw (f32 degrees)

#include "cigi/packet.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>

namespace cigi {

struct ArticulatedPartControl {
    static constexpr std::uint8_t opcode = 6;
    static constexpr Direction direction = Direction::HostToIg;
    static constexpr std::size_t wire_size = 32;

    std::uint16_t entity_id = 0;
    std::uint8_t articulated_part_id = 0;
    bool part_enabled = false; ///< Part is in the scene (IG-configurable default).
    bool x_offset_enabled = false;
    bool y_offset_enabled = false;
    bool z_offset_enabled = false;
    bool roll_enabled = false;
    bool pitch_enabled = false;
    bool yaw_enabled = false;
    float x_offset = 0.0F; ///< Meters.
    float y_offset = 0.0F;
    float z_offset = 0.0F;
    float roll = 0.0F;  ///< Degrees, -180..180.
    float pitch = 0.0F; ///< Degrees, -90..90.
    float yaw = 0.0F;   ///< Degrees, 0..360.

    static constexpr auto fields = std::tuple{
        wire::scalar(&ArticulatedPartControl::entity_id, 2),
        wire::scalar(&ArticulatedPartControl::articulated_part_id, 4),
        wire::flag(&ArticulatedPartControl::part_enabled, 5, 0),
        wire::flag(&ArticulatedPartControl::x_offset_enabled, 5, 1),
        wire::flag(&ArticulatedPartControl::y_offset_enabled, 5, 2),
        wire::flag(&ArticulatedPartControl::z_offset_enabled, 5, 3),
        wire::flag(&ArticulatedPartControl::roll_enabled, 5, 4),
        wire::flag(&ArticulatedPartControl::pitch_enabled, 5, 5),
        wire::flag(&ArticulatedPartControl::yaw_enabled, 5, 6),
        wire::scalar(&ArticulatedPartControl::x_offset, 8),
        wire::scalar(&ArticulatedPartControl::y_offset, 12),
        wire::scalar(&ArticulatedPartControl::z_offset, 16),
        wire::scalar(&ArticulatedPartControl::roll, 20).range(-180.0F, 180.0F),
        wire::scalar(&ArticulatedPartControl::pitch, 24).range(-90.0F, 90.0F),
        wire::scalar(&ArticulatedPartControl::yaw, 28).range(0.0F, 360.0F),
    };

    friend constexpr bool operator==(const ArticulatedPartControl&,
                                     const ArticulatedPartControl&) noexcept = default;
};
static_assert(wire::layout_ok<ArticulatedPartControl>());

} // namespace cigi
