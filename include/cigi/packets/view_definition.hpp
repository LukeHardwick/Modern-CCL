#pragma once

// View Definition packet (ICD 4.1.21), opcode 21, Host -> IG, 32 bytes. Overrides the IG's default
// configuration of a view: projection type, viewing volume, view group, mirroring.
//
//   byte 0 opcode (21)   byte 1 size (32)   bytes 2-3 view ID (u16)   byte 4 group ID (u8)
//   byte 5 bits 7-6 mirror mode, 5 bottom enable, 4 top enable, 3 right enable, 2 left enable,
//          1 far enable, 0 near enable
//   byte 6 bits 7-5 view type, 4 reorder, 3 projection type, 2-0 pixel replication mode
//   byte 7 reserved   bytes 8, 12 near, far (f32, m)   bytes 16, 20, 24, 28 left, right, top,
//   bottom half-angles (f32, degrees)

#include "cigi/packet.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>

namespace cigi {

/// Mirroring performed on the view (ICD 4.1.21).
enum class MirrorMode : std::uint8_t {
    None = 0,
    Horizontal = 1,
    Vertical = 2,
    HorizontalAndVertical = 3,
};

template <> struct EnumTraits<MirrorMode> {
    [[nodiscard]] static constexpr bool is_defined(MirrorMode m) noexcept {
        return enum_between(m, MirrorMode::None, MirrorMode::HorizontalAndVertical);
    }
};

/// Pixel replication (electronic zoom). 4..7 are "defined by IG" (ICD 4.1.21), so every 3-bit
/// value counts as defined.
enum class PixelReplicationMode : std::uint8_t {
    None = 0,
    OneByTwo = 1,
    TwoByOne = 2,
    TwoByTwo = 3,
};

template <> struct EnumTraits<PixelReplicationMode> {
    [[nodiscard]] static constexpr bool is_defined(PixelReplicationMode m) noexcept {
        return enum_between(m, PixelReplicationMode::None, static_cast<PixelReplicationMode>(7));
    }
};

/// View projection (ICD 4.1.21, 3.2.1).
enum class ProjectionType : std::uint8_t {
    Perspective = 0,
    OrthographicParallel = 1,
};

template <> struct EnumTraits<ProjectionType> {
    [[nodiscard]] static constexpr bool is_defined(ProjectionType m) noexcept {
        return enum_between(m, ProjectionType::Perspective, ProjectionType::OrthographicParallel);
    }
};

struct ViewDefinition {
    static constexpr std::uint8_t opcode = 21;
    static constexpr Direction direction = Direction::HostToIg;
    static constexpr std::size_t wire_size = 32;

    std::uint16_t view_id = 0;
    std::uint8_t group_id = 0; ///< 0: not in a group; 1..255: view group.
    bool near_enabled = false; ///< Each enable flag says whether the matching value is applied.
    bool far_enabled = false;
    bool left_enabled = false;
    bool right_enabled = false;
    bool top_enabled = false;
    bool bottom_enabled = false;
    MirrorMode mirror_mode = MirrorMode::None;
    PixelReplicationMode pixel_replication_mode = PixelReplicationMode::None;
    ProjectionType projection_type = ProjectionType::Perspective;
    bool reorder = false;       ///< true: bring the view to the top of overlapping views.
    std::uint8_t view_type = 0; ///< IG-defined, 0..7.
    float near_plane = 0;       ///< Metres; > 0 and < far_plane.
    float far_plane = 0;        ///< Metres; > near_plane.
    float left = 0;             ///< Half-angle, degrees; > -90 and < right.
    float right = 0;            ///< Degrees; > left and < 90.
    float top = 0;              ///< Degrees; > bottom and < 90.
    float bottom = 0;           ///< Degrees; > -90 and < top.

    static constexpr auto fields = std::tuple{
        wire::scalar(&ViewDefinition::view_id, 2),
        wire::scalar(&ViewDefinition::group_id, 4),
        wire::flag(&ViewDefinition::near_enabled, 5, 0),
        wire::flag(&ViewDefinition::far_enabled, 5, 1),
        wire::flag(&ViewDefinition::left_enabled, 5, 2),
        wire::flag(&ViewDefinition::right_enabled, 5, 3),
        wire::flag(&ViewDefinition::top_enabled, 5, 4),
        wire::flag(&ViewDefinition::bottom_enabled, 5, 5),
        wire::bits(&ViewDefinition::mirror_mode, 5, 6, 2),
        wire::bits(&ViewDefinition::pixel_replication_mode, 6, 0, 3),
        wire::bits(&ViewDefinition::projection_type, 6, 3, 1),
        wire::flag(&ViewDefinition::reorder, 6, 4),
        wire::bits(&ViewDefinition::view_type, 6, 5, 3).range(0, 7),
        wire::scalar(&ViewDefinition::near_plane, 8),
        wire::scalar(&ViewDefinition::far_plane, 12),
        wire::scalar(&ViewDefinition::left, 16),
        wire::scalar(&ViewDefinition::right, 20),
        wire::scalar(&ViewDefinition::top, 24),
        wire::scalar(&ViewDefinition::bottom, 28),
    };

    friend constexpr bool operator==(const ViewDefinition&,
                                     const ViewDefinition&) noexcept = default;
};
static_assert(wire::layout_ok<ViewDefinition>());

/// Enum checks, then the frustum rules of ICD 4.1.21, applied only to values whose enable flag is
/// set (a disabled value is ignored by the IG). The error offset is the offending field:
///   near > 0 (8); far > 0 (12) and, if near is also enabled, near < far (12);
///   each enabled angle strictly inside (-90, 90) (16, 20, 24, 28);
///   left < right (20) and bottom < top (28) when both of a pair are enabled.
/// NaN fails every rule.
[[nodiscard]] constexpr Result<void> validate(const ViewDefinition& p) noexcept {
    if (auto ok = wire::validate_fields(p); !ok) {
        return ok;
    }
    const auto bad = [](std::size_t offset) {
        return std::unexpected(Error{
            .code = ErrorCode::InvalidFieldValue,
            .offset = offset,
            .opcode = ViewDefinition::opcode,
        });
    };
    const auto angle_ok = [](float a) { return a > -90.0F && a < 90.0F; };
    if (p.near_enabled && !(p.near_plane > 0.0F)) {
        return bad(8);
    }
    const bool far_ok = p.far_plane > 0.0F && (!p.near_enabled || p.near_plane < p.far_plane);
    if (p.far_enabled && !far_ok) {
        return bad(12);
    }
    if (p.left_enabled && !angle_ok(p.left)) {
        return bad(16);
    }
    const bool right_ok = angle_ok(p.right) && (!p.left_enabled || p.left < p.right);
    if (p.right_enabled && !right_ok) {
        return bad(20);
    }
    if (p.top_enabled && !angle_ok(p.top)) {
        return bad(24);
    }
    const bool bottom_ok = angle_ok(p.bottom) && (!p.top_enabled || p.bottom < p.top);
    if (p.bottom_enabled && !bottom_ok) {
        return bad(28);
    }
    return {};
}

} // namespace cigi
