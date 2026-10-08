#pragma once

// Entity Control packet (ICD 4.1.2), opcode 2, Host -> IG, 48 bytes. Creates, places and animates
// an entity. Table-driven except validate(), which applies the geodetic range checks only to
// top-level entities (see below).
//
//   2-3 entity id                 4 bit 7 reserved, 6-5 ground/ocean clamp, 4 inherit alpha,
//                                   3 collision detection enable, 2 attach state, 1-0 entity state
//   5 bits 7-5 reserved, 4 linear extrapolation/interpolation enable (3.3), 3-2 animation state,
//                                   1 animation loop mode, 0 animation direction
//   6 alpha  7 reserved           8-9 entity type  10-11 parent id
//   12 roll  16 pitch  20 yaw (f32)    24 latitude/X  32 longitude/Y  40 altitude/Z (f64)
//
// The extrapolation bit is reserved (zero) in 3.2 and older; senders zero reserved bits (ICD 2.11
// rule 2), so an older peer's packet decodes with extrapolation_enabled == false.

#include "cigi/packet.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>

namespace cigi {

/// Entity State (ICD 4.1.2, Table 7). 3 is undefined.
enum class EntityState : std::uint8_t {
    InactiveStandby = 0,
    Active = 1,
    Destroyed = 2,
};

template <> struct EnumTraits<EntityState> {
    [[nodiscard]] static constexpr bool is_defined(EntityState v) noexcept {
        return enum_between(v, EntityState::InactiveStandby, EntityState::Destroyed);
    }
};

/// Attach State (ICD 4.1.2, Table 7).
enum class AttachState : std::uint8_t {
    Detach = 0,
    Attach = 1,
};

template <> struct EnumTraits<AttachState> {
    [[nodiscard]] static constexpr bool is_defined(AttachState v) noexcept {
        return enum_between(v, AttachState::Detach, AttachState::Attach);
    }
};

/// Ground/Ocean Clamp (ICD 4.1.2, Table 7). 3 is undefined.
enum class GroundClamp : std::uint8_t {
    NoClamp = 0,
    NonConformal = 1,
    Conformal = 2,
};

template <> struct EnumTraits<GroundClamp> {
    [[nodiscard]] static constexpr bool is_defined(GroundClamp v) noexcept {
        return enum_between(v, GroundClamp::NoClamp, GroundClamp::Conformal);
    }
};

/// Animation Direction (ICD 4.1.2, Table 7).
enum class AnimationDirection : std::uint8_t {
    Forward = 0,
    Backward = 1,
};

template <> struct EnumTraits<AnimationDirection> {
    [[nodiscard]] static constexpr bool is_defined(AnimationDirection v) noexcept {
        return enum_between(v, AnimationDirection::Forward, AnimationDirection::Backward);
    }
};

/// Animation Loop Mode (ICD 4.1.2, Table 7).
enum class AnimationLoopMode : std::uint8_t {
    OneShot = 0,
    Continuous = 1,
};

template <> struct EnumTraits<AnimationLoopMode> {
    [[nodiscard]] static constexpr bool is_defined(AnimationLoopMode v) noexcept {
        return enum_between(v, AnimationLoopMode::OneShot, AnimationLoopMode::Continuous);
    }
};

/// Animation State (ICD 4.1.2, Table 7). All four 2-bit values are defined.
enum class AnimationState : std::uint8_t {
    Stop = 0,
    Pause = 1,
    Play = 2,
    Continue = 3,
};

template <> struct EnumTraits<AnimationState> {
    [[nodiscard]] static constexpr bool is_defined(AnimationState v) noexcept {
        return enum_between(v, AnimationState::Stop, AnimationState::Continue);
    }
};

struct EntityControl {
    static constexpr std::uint8_t opcode = 2;
    static constexpr Direction direction = Direction::HostToIg;
    static constexpr std::size_t wire_size = 48;

    std::uint16_t entity_id = 0; ///< 0 is the Ownship.
    EntityState entity_state = EntityState::InactiveStandby;
    AttachState attach_state = AttachState::Detach;
    bool collision_detection_enabled = false;
    bool inherit_alpha = false; ///< Ignored when detached.
    GroundClamp ground_clamp = GroundClamp::NoClamp;
    AnimationDirection animation_direction = AnimationDirection::Forward;
    AnimationLoopMode animation_loop_mode = AnimationLoopMode::OneShot;
    AnimationState animation_state = AnimationState::Stop;
    bool extrapolation_enabled = false; ///< 3.3 only; reserved (0) in older minors.
    std::uint8_t alpha = 0;             ///< 0 transparent .. 255 opaque.
    std::uint16_t entity_type = 0;      ///< 0: null type, no geometry.
    std::uint16_t parent_id = 0;        ///< Ignored when detached.
    float roll = 0.0F;                  ///< Degrees, -180..180.
    float pitch = 0.0F;                 ///< Degrees, -90..90.
    float yaw = 0.0F;                   ///< Degrees, 0..360 (ICD 4.1.2; not -180..180).
    /// Latitude (degrees) when detached, X offset (m, parent frame) when attached.
    double latitude_or_x = 0.0;
    /// Longitude (degrees) when detached, Y offset (m) when attached.
    double longitude_or_y = 0.0;
    /// Altitude (m) when detached, Z offset (m) when attached.
    double altitude_or_z = 0.0;

    static constexpr auto fields = std::tuple{
        wire::scalar(&EntityControl::entity_id, 2),
        wire::bits(&EntityControl::entity_state, 4, 0, 2),
        wire::bits(&EntityControl::attach_state, 4, 2, 1),
        wire::flag(&EntityControl::collision_detection_enabled, 4, 3),
        wire::flag(&EntityControl::inherit_alpha, 4, 4),
        wire::bits(&EntityControl::ground_clamp, 4, 5, 2),
        wire::bits(&EntityControl::animation_direction, 5, 0, 1),
        wire::bits(&EntityControl::animation_loop_mode, 5, 1, 1),
        wire::bits(&EntityControl::animation_state, 5, 2, 2),
        wire::flag(&EntityControl::extrapolation_enabled, 5, 4),
        wire::scalar(&EntityControl::alpha, 6),
        wire::scalar(&EntityControl::entity_type, 8),
        wire::scalar(&EntityControl::parent_id, 10),
        wire::scalar(&EntityControl::roll, 12).range(-180.0F, 180.0F),
        wire::scalar(&EntityControl::pitch, 16).range(-90.0F, 90.0F),
        wire::scalar(&EntityControl::yaw, 20).range(0.0F, 360.0F),
        wire::scalar(&EntityControl::latitude_or_x, 24),
        wire::scalar(&EntityControl::longitude_or_y, 32),
        wire::scalar(&EntityControl::altitude_or_z, 40),
    };

    friend constexpr bool operator==(const EntityControl&, const EntityControl&) noexcept = default;
};
static_assert(wire::layout_ok<EntityControl>());

/// Field-table checks (enums defined, angle ranges), then, only for a top-level entity
/// (Attach State = Detach), latitude -90..90 and longitude -180..180 (ICD 4.1.2). For an attached
/// entity these fields are X/Y offsets in meters and are not range-checked. Errors carry the
/// field's byte offset.
[[nodiscard]] constexpr Result<void> validate(const EntityControl& p) noexcept {
    if (auto ok = wire::validate_fields(p); !ok) {
        return ok;
    }
    if (p.attach_state == AttachState::Detach) {
        // NaN fails both checks.
        const auto within = [](double v, double limit) { return -limit <= v && v <= limit; };
        const auto bad_at = [](std::size_t offset) {
            return std::unexpected(Error{
                .code = ErrorCode::InvalidFieldValue,
                .offset = offset,
                .opcode = EntityControl::opcode,
            });
        };
        if (!within(p.latitude_or_x, 90.0)) {
            return bad_at(24);
        }
        if (!within(p.longitude_or_y, 180.0)) {
            return bad_at(32);
        }
    }
    return {};
}

} // namespace cigi
