#include "../support/packet_checks.hpp"
#include "cigi/packets/entity_control.hpp"

#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <random>

namespace {

using namespace cigi;
using namespace cigi::test;
using wire::ByteOrder;

constexpr EntityControl sample{
    .entity_id = 0x0102,
    .entity_state = EntityState::Active,
    .attach_state = AttachState::Attach,
    .collision_detection_enabled = true,
    .inherit_alpha = true,
    .ground_clamp = GroundClamp::Conformal,
    .animation_direction = AnimationDirection::Backward,
    .animation_loop_mode = AnimationLoopMode::Continuous,
    .animation_state = AnimationState::Play,
    .extrapolation_enabled = true,
    .alpha = 0xAB,
    .entity_type = 0x0304,
    .parent_id = 0x0506,
    .roll = 1.0F,
    .pitch = 2.0F,
    .yaw = 3.0F,
    .latitude_or_x = 1.0,
    .longitude_or_y = 2.0,
    .altitude_or_z = -2.0,
};

// Hand-derived from ICD 4.1.2 (Fig. 42). Byte 4: clamp 2 -> 0x40, inherit alpha 0x10, collision
// 0x08, attach 0x04, state 1 -> 0x5D. Byte 5: extrapolation 0x10, animation state 2 -> 0x08, loop
// 0x02, direction 0x01 -> 0x1B. IEEE-754: 1.0f 3F800000, 2.0f 40000000, 3.0f 40400000,
// 1.0 3FF0..., 2.0 4000..., -2.0 C000....
constexpr auto golden_le = bytes_of(0x02, 0x30, 0x02, 0x01, 0x5D, 0x1B, 0xAB, 0x00, //
                                    0x04, 0x03, 0x06, 0x05, 0x00, 0x00, 0x80, 0x3F, //
                                    0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x40, //
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F, //
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, //
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0);
constexpr auto golden_be = bytes_of(0x02, 0x30, 0x01, 0x02, 0x5D, 0x1B, 0xAB, 0x00, //
                                    0x03, 0x04, 0x05, 0x06, 0x3F, 0x80, 0x00, 0x00, //
                                    0x40, 0x00, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, //
                                    0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                                    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                                    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

constexpr bool constexpr_round_trip() {
    std::array<std::byte, 48> buf{};
    encode(sample, buf);
    const auto r = decode(std::type_identity<EntityControl>{}, buf, ByteOrder::Native);
    return r.has_value() && *r == sample && buf == native_of(golden_le, golden_be);
}
static_assert(constexpr_round_trip());
static_assert(Packet<EntityControl>);
static_assert(EntityControl::wire_size == 48 && EntityControl::opcode == 2);
static_assert(EntityControl::direction == Direction::HostToIg);

TEST(EntityControl, GoldenBytesBothOrders) {
    expect_golden(sample, golden_le, golden_be);
}

TEST(EntityControl, EveryFlagAndEnumValueAlone) {
    // Each bit lands where the ICD says: byte 4 / byte 5 images for one field set at a time.
    const auto byte_of = [](const EntityControl& p, std::size_t i) {
        std::array<std::byte, 48> b{};
        encode(p, b);
        return std::to_integer<int>(b.at(i));
    };
    EXPECT_EQ(byte_of({.entity_state = EntityState::Destroyed}, 4), 0x02);
    EXPECT_EQ(byte_of({.attach_state = AttachState::Attach}, 4), 0x04);
    EXPECT_EQ(byte_of({.collision_detection_enabled = true}, 4), 0x08);
    EXPECT_EQ(byte_of({.inherit_alpha = true}, 4), 0x10);
    EXPECT_EQ(byte_of({.ground_clamp = GroundClamp::NonConformal}, 4), 0x20);
    EXPECT_EQ(byte_of({.ground_clamp = GroundClamp::Conformal}, 4), 0x40);
    EXPECT_EQ(byte_of({.animation_direction = AnimationDirection::Backward}, 5), 0x01);
    EXPECT_EQ(byte_of({.animation_loop_mode = AnimationLoopMode::Continuous}, 5), 0x02);
    EXPECT_EQ(byte_of({.animation_state = AnimationState::Pause}, 5), 0x04);
    EXPECT_EQ(byte_of({.animation_state = AnimationState::Continue}, 5), 0x0C);
    EXPECT_EQ(byte_of({.extrapolation_enabled = true}, 5), 0x10);
}

TEST(EntityControl, ReservedBitsIgnoredOnDecode) {
    Bytes b(48);
    encode(sample, b);
    b.at(4) |= std::byte{0x80};
    b.at(5) |= std::byte{0xE0};
    b.at(7) = std::byte{0xFF};
    const auto r = decode(std::type_identity<EntityControl>{}, b, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, sample);
}

TEST(EntityControl, ThreeTwoPeerWithoutExtrapolationBitDecodesAsDisabled) {
    // A 3.2 sender writes the (then reserved) extrapolation bit as 0 (ICD 2.11 rule 2).
    Bytes b(48);
    encode(sample, b);
    b.at(5) &= ~std::byte{0x10};
    const auto r = decode(std::type_identity<EntityControl>{}, b, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r->extrapolation_enabled);
    EntityControl expected = sample;
    expected.extrapolation_enabled = false;
    EXPECT_EQ(*r, expected);
}

TEST(EntityControl, UndefinedEnumValuesSurviveDecode) {
    Bytes b(48);
    const EntityControl defaults;
    encode(defaults, b);
    b.at(4) = std::byte{0x63}; // clamp 3, state 3
    const auto r = decode(std::type_identity<EntityControl>{}, b, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->entity_state, static_cast<EntityState>(3));
    EXPECT_EQ(r->ground_clamp, static_cast<GroundClamp>(3));
    EXPECT_FALSE(validate(*r).has_value());
}

TEST(EntityControl, StandardDecodeErrors) {
    expect_standard_decode_errors(sample);
}

TEST(EntityControl, ValidateAcceptsTypicalAndBoundaryValues) {
    EXPECT_TRUE(validate(EntityControl{}).has_value());
    EXPECT_TRUE(validate(sample).has_value());
    EXPECT_TRUE(validate(EntityControl{.roll = -180.0F,
                                       .pitch = -90.0F,
                                       .yaw = 360.0F,
                                       .latitude_or_x = -90.0,
                                       .longitude_or_y = 180.0})
                    .has_value());
    EXPECT_TRUE(validate(EntityControl{.roll = 180.0F,
                                       .pitch = 90.0F,
                                       .yaw = 0.0F,
                                       .latitude_or_x = 90.0,
                                       .longitude_or_y = -180.0})
                    .has_value());
}

TEST(EntityControl, ValidateRejectsOutOfRangeWithFieldOffset) {
    const auto offset_of = [](const EntityControl& p) {
        const auto r = validate(p);
        EXPECT_FALSE(r.has_value());
        EXPECT_EQ(r.error().code, ErrorCode::InvalidFieldValue);
        EXPECT_EQ(r.error().opcode, 2);
        return r.error().offset;
    };
    EXPECT_EQ(offset_of({.roll = 180.5F}), 12U);
    EXPECT_EQ(offset_of({.pitch = -90.5F}), 16U);
    EXPECT_EQ(offset_of({.yaw = -1.0F}), 20U);
    EXPECT_EQ(offset_of({.yaw = 361.0F}), 20U);
    EXPECT_EQ(offset_of({.roll = std::numeric_limits<float>::quiet_NaN()}), 12U);
    EXPECT_EQ(offset_of({.latitude_or_x = 90.5}), 24U);
    EXPECT_EQ(offset_of({.longitude_or_y = -181.0}), 32U);
    EXPECT_EQ(offset_of({.longitude_or_y = std::numeric_limits<double>::quiet_NaN()}), 32U);
    EXPECT_EQ(offset_of({.entity_state = static_cast<EntityState>(3)}), 4U);
    EXPECT_EQ(offset_of({.ground_clamp = static_cast<GroundClamp>(3)}), 4U);
}

TEST(EntityControl, AttachedEntityOffsetsAreNotGeodeticRanges) {
    const EntityControl child{
        .attach_state = AttachState::Attach,
        .latitude_or_x = 1000.0,
        .longitude_or_y = -500.0,
        .altitude_or_z = 1.0e9,
    };
    EXPECT_TRUE(validate(child).has_value());
    EntityControl top = child;
    top.attach_state = AttachState::Detach;
    EXPECT_FALSE(validate(top).has_value());
    // The altitude of a top-level entity has no ICD range.
    EXPECT_TRUE(validate(EntityControl{.altitude_or_z = 1.0e9}).has_value());
}

TEST(EntityControl, EnumTraits) {
    EXPECT_TRUE(EnumTraits<EntityState>::is_defined(EntityState::Destroyed));
    EXPECT_FALSE(EnumTraits<EntityState>::is_defined(static_cast<EntityState>(3)));
    EXPECT_TRUE(EnumTraits<AttachState>::is_defined(AttachState::Attach));
    EXPECT_FALSE(EnumTraits<AttachState>::is_defined(static_cast<AttachState>(2)));
    EXPECT_TRUE(EnumTraits<GroundClamp>::is_defined(GroundClamp::Conformal));
    EXPECT_FALSE(EnumTraits<GroundClamp>::is_defined(static_cast<GroundClamp>(3)));
    EXPECT_TRUE(EnumTraits<AnimationDirection>::is_defined(AnimationDirection::Backward));
    EXPECT_FALSE(EnumTraits<AnimationDirection>::is_defined(static_cast<AnimationDirection>(2)));
    EXPECT_TRUE(EnumTraits<AnimationLoopMode>::is_defined(AnimationLoopMode::Continuous));
    EXPECT_FALSE(EnumTraits<AnimationLoopMode>::is_defined(static_cast<AnimationLoopMode>(2)));
    EXPECT_TRUE(EnumTraits<AnimationState>::is_defined(AnimationState::Continue));
    EXPECT_FALSE(EnumTraits<AnimationState>::is_defined(static_cast<AnimationState>(4)));
}

TEST(EntityControl, RandomRoundTrip) {
    // NOLINTNEXTLINE(bugprone-random-generator-seed): deterministic on purpose
    std::mt19937 rng(0xE1717C03U);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    for (int i = 0; i < 2000; ++i) {
        const auto bits = [&](std::uint32_t n) { return static_cast<std::uint32_t>(rng()) % n; };
        EntityControl p;
        p.entity_id = static_cast<std::uint16_t>(rng());
        p.entity_state = static_cast<EntityState>(bits(3));
        p.attach_state = static_cast<AttachState>(bits(2));
        p.collision_detection_enabled = bits(2) != 0;
        p.inherit_alpha = bits(2) != 0;
        p.ground_clamp = static_cast<GroundClamp>(bits(3));
        p.animation_direction = static_cast<AnimationDirection>(bits(2));
        p.animation_loop_mode = static_cast<AnimationLoopMode>(bits(2));
        p.animation_state = static_cast<AnimationState>(bits(4));
        p.extrapolation_enabled = bits(2) != 0;
        p.alpha = static_cast<std::uint8_t>(rng());
        p.entity_type = static_cast<std::uint16_t>(rng());
        p.parent_id = static_cast<std::uint16_t>(rng());
        p.roll = static_cast<float>(unit(rng) * 180.0);
        p.pitch = static_cast<float>(unit(rng) * 90.0);
        p.yaw = static_cast<float>((unit(rng) + 1.0) * 180.0);
        p.latitude_or_x = unit(rng) * 90.0;
        p.longitude_or_y = unit(rng) * 180.0;
        p.altitude_or_z = unit(rng) * 1.0e5;
        expect_round_trip_both_orders(p);
        EXPECT_TRUE(validate(p).has_value());
    }
}

} // namespace
