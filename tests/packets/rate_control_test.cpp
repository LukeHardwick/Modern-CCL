#include "../support/packet_checks.hpp"
#include "cigi/packets/rate_control.hpp"

#include <array>
#include <gtest/gtest.h>
#include <random>

namespace {

using namespace cigi;
using namespace cigi::test;
using wire::ByteOrder;

constexpr RateControl sample{
    .entity_id = 0x0A0B,
    .articulated_part_id = 9,
    .apply_to_articulated_part = true,
    .coordinate_system = RateCoordinateSystem::Local,
    .x_linear_rate = 1.0F,
    .y_linear_rate = 2.0F,
    .z_linear_rate = -2.0F,
    .roll_rate = 3.0F,
    .pitch_rate = 4.0F,
    .yaw_rate = 5.0F,
};

// Hand-derived from ICD 4.1.8 (Fig. 52). Byte 5: coordinate system Local 0x02 | apply 0x01 = 0x03.
// IEEE-754: 1.0f 3F800000, 2.0f 40000000, -2.0f C0000000, 3.0f 40400000, 4.0f 40800000,
// 5.0f 40A00000.
constexpr auto golden_le = bytes_of(0x08, 0x20, 0x0B, 0x0A, 0x09, 0x03, 0x00, 0x00, //
                                    0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40, //
                                    0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x40, 0x40, //
                                    0x00, 0x00, 0x80, 0x40, 0x00, 0x00, 0xA0, 0x40);
constexpr auto golden_be = bytes_of(0x08, 0x20, 0x0A, 0x0B, 0x09, 0x03, 0x00, 0x00, //
                                    0x3F, 0x80, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, //
                                    0xC0, 0x00, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, //
                                    0x40, 0x80, 0x00, 0x00, 0x40, 0xA0, 0x00, 0x00);

constexpr bool constexpr_round_trip() {
    std::array<std::byte, 32> buf{};
    encode(sample, buf);
    const auto r = decode(std::type_identity<RateControl>{}, buf, ByteOrder::Native);
    return r.has_value() && *r == sample && buf == native_of(golden_le, golden_be);
}
static_assert(constexpr_round_trip());
static_assert(Packet<RateControl>);
static_assert(RateControl::wire_size == 32 && RateControl::opcode == 8);
static_assert(RateControl::direction == Direction::HostToIg);

TEST(RateControl, GoldenBytesBothOrders) {
    expect_golden(sample, golden_le, golden_be);
}

TEST(RateControl, FlagBitsAlone) {
    const auto flags = [](const RateControl& p) {
        std::array<std::byte, 32> b{};
        encode(p, b);
        return std::to_integer<int>(b.at(5));
    };
    EXPECT_EQ(flags({.apply_to_articulated_part = true}), 0x01);
    EXPECT_EQ(flags({.coordinate_system = RateCoordinateSystem::Local}), 0x02);
}

TEST(RateControl, ReservedBitsIgnoredOnDecode) {
    Bytes b(32);
    encode(sample, b);
    b.at(5) |= std::byte{0xFC};
    b.at(6) = std::byte{0xFF};
    b.at(7) = std::byte{0xFF};
    const auto r = decode(std::type_identity<RateControl>{}, b, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, sample);
}

TEST(RateControl, OlderPeerWithZeroCoordinateBitIsWorldParent) {
    // Reserved before 3.2 and sent as 0 (ICD 2.11 rule 2).
    Bytes b(32);
    encode(RateControl{.apply_to_articulated_part = true}, b);
    const auto r = decode(std::type_identity<RateControl>{}, b, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->coordinate_system, RateCoordinateSystem::WorldParent);
}

TEST(RateControl, StandardDecodeErrors) {
    expect_standard_decode_errors(sample);
}

TEST(RateControl, ValidateAcceptsAnyRateAndDefinedEnum) {
    EXPECT_TRUE(validate(RateControl{}).has_value());
    EXPECT_TRUE(validate(sample).has_value());
    EXPECT_TRUE(validate(RateControl{.x_linear_rate = 1.0e9F, .yaw_rate = -1.0e9F}).has_value());
    const auto r = validate(RateControl{.coordinate_system = static_cast<RateCoordinateSystem>(2)});
    EXPECT_EQ(r.error(), (Error{.code = ErrorCode::InvalidFieldValue, .offset = 5, .opcode = 8}));
}

TEST(RateControl, EnumTraits) {
    EXPECT_TRUE(EnumTraits<RateCoordinateSystem>::is_defined(RateCoordinateSystem::Local));
    EXPECT_FALSE(
        EnumTraits<RateCoordinateSystem>::is_defined(static_cast<RateCoordinateSystem>(2)));
}

TEST(RateControl, RandomRoundTrip) {
    // NOLINTNEXTLINE(bugprone-random-generator-seed): deterministic on purpose
    std::mt19937 rng(0x8A7EC008U);
    std::uniform_real_distribution<float> unit(-1.0F, 1.0F);
    for (int i = 0; i < 2000; ++i) {
        RateControl p;
        p.entity_id = static_cast<std::uint16_t>(rng());
        p.articulated_part_id = static_cast<std::uint8_t>(rng());
        p.apply_to_articulated_part = (rng() & 1U) != 0;
        p.coordinate_system = static_cast<RateCoordinateSystem>(rng() & 1U);
        p.x_linear_rate = unit(rng) * 1000.0F;
        p.y_linear_rate = unit(rng) * 1000.0F;
        p.z_linear_rate = unit(rng) * 1000.0F;
        p.roll_rate = unit(rng) * 720.0F;
        p.pitch_rate = unit(rng) * 720.0F;
        p.yaw_rate = unit(rng) * 720.0F;
        expect_round_trip_both_orders(p);
        EXPECT_TRUE(validate(p).has_value());
    }
}

} // namespace
