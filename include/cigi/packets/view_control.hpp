#pragma once

// View Control packet (ICD 4.1.16), opcode 16, Host -> IG, 32 bytes. Attaches a view or view group
// to an entity and sets its offset and rotation relative to the entity reference point. (The ICD
// text calls this packet Section 4.1.13 in Section 3.2.2; that is a typo.)
//
//   byte 0 opcode (16)   byte 1 size (32)   bytes 2-3 view ID (u16)   byte 4 group ID (u8)
//   byte 5 bits 7-6 reserved, 5 yaw enable, 4 pitch enable, 3 roll enable, 2 Z offset enable,
//          1 Y offset enable, 0 X offset enable
//   bytes 6-7 entity ID (u16)   bytes 8, 12, 16 X, Y, Z offset (f32, m)
//   bytes 20, 24, 28 roll, pitch, yaw (f32, degrees)

#include "cigi/packet.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>

namespace cigi {

struct ViewControl {
    static constexpr std::uint8_t opcode = 16;
    static constexpr Direction direction = Direction::HostToIg;
    static constexpr std::size_t wire_size = 32;

    std::uint16_t view_id = 0; ///< Ignored by the IG if group_id is non-zero.
    std::uint8_t group_id = 0; ///< 0: apply to the single view; 1..255: apply to the view group.
    bool x_offset_enabled = false;
    bool y_offset_enabled = false;
    bool z_offset_enabled = false;
    bool roll_enabled = false;
    bool pitch_enabled = false;
    bool yaw_enabled = false;
    std::uint16_t entity_id = 0;
    float x_offset = 0; ///< Metres from the entity reference point.
    float y_offset = 0;
    float z_offset = 0;
    float roll = 0;  ///< Degrees, -180..180.
    float pitch = 0; ///< Degrees, -90..90.
    float yaw = 0;   ///< Degrees, 0..360.

    // validate() checks the angle ranges whether or not the matching enable flag is set.
    static constexpr auto fields = std::tuple{
        wire::scalar(&ViewControl::view_id, 2),
        wire::scalar(&ViewControl::group_id, 4),
        wire::flag(&ViewControl::x_offset_enabled, 5, 0),
        wire::flag(&ViewControl::y_offset_enabled, 5, 1),
        wire::flag(&ViewControl::z_offset_enabled, 5, 2),
        wire::flag(&ViewControl::roll_enabled, 5, 3),
        wire::flag(&ViewControl::pitch_enabled, 5, 4),
        wire::flag(&ViewControl::yaw_enabled, 5, 5),
        wire::scalar(&ViewControl::entity_id, 6),
        wire::scalar(&ViewControl::x_offset, 8),
        wire::scalar(&ViewControl::y_offset, 12),
        wire::scalar(&ViewControl::z_offset, 16),
        wire::scalar(&ViewControl::roll, 20).range(-180.0F, 180.0F),
        wire::scalar(&ViewControl::pitch, 24).range(-90.0F, 90.0F),
        wire::scalar(&ViewControl::yaw, 28).range(0.0F, 360.0F),
    };

    friend constexpr bool operator==(const ViewControl&, const ViewControl&) noexcept = default;
};
static_assert(wire::layout_ok<ViewControl>());

} // namespace cigi
