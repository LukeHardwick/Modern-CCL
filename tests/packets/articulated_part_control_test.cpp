#include "../support/packet_checks.hpp"
#include "cigi/packets/articulated_part_control.hpp"

#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <random>

namespace {

using namespace cigi;
using namespace cigi::test;
using wire::ByteOrder;

constexpr ArticulatedPartControl sample{
    .entity_id = 0x0102,
    .articulated_part_id = 7,
    .part_enabled = true,
    .x_offset_enabled = true,
    .y_offset_enabled = false,
    .z_offset_enabled = true,
    .roll_enabled = false,
    .pitch_enabled = true,
    .yaw_enabled = true,
    .x_offset = 1.0F,
    .y_offset = -1.0F,
    .z_offset = 2.0F,
    .roll = 3.0F,
    .pitch = 4.0F,
    .yaw = 5.0F,
};

// Hand-derived from ICD 4.1.6 (Fig. 50). Byte 5: part 0x01, X 0x02, Z 0x08, pitch 0x20,
// yaw 0x40 = 0x6B. IEEE-754: 1.0f 3F800000, -1.0f BF800000, 2.0f 40000000, 3.0f 40400000,
// 4.0f 40800000, 5.0f 40A00000.
constexpr auto golden_le = bytes_of(0x06, 0x20, 0x02, 0x01, 0x07, 0x6B, 0x00, 0x00, //
                                    0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0xBF, //
                                    0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x40, //
                                    0x00, 0x00, 0x80, 0x40, 0x00, 0x00, 0xA0, 0x40);
constexpr auto golden_be = bytes_of(0x06, 0x20, 0x01, 0x02, 0x07, 0x6B, 0x00, 0x00, //
                                    0x3F, 0x80, 0x00, 0x00, 0xBF, 0x80, 0x00, 0x00, //
                                    0x40, 0x00, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, //
                                    0x40, 0x80, 0x00, 0x00, 0x40, 0xA0, 0x00, 0x00);

constexpr bool constexpr_round_trip() {
    std::array<std::byte, 32> buf{};
    encode(sample, buf);
    const auto r = decode(std::type_identity<ArticulatedPartControl>{}, buf, ByteOrder::Native);
    return r.has_value() && *r == sample && buf == native_of(golden_le, golden_be);
}
static_assert(constexpr_round_trip());
static_assert(Packet<ArticulatedPartControl>);
static_assert(ArticulatedPartControl::wire_size == 32 && ArticulatedPartControl::opcode == 6);
static_assert(ArticulatedPartControl::direction == Direction::HostToIg);

TEST(ArticulatedPartControl, GoldenBytesBothOrders) {
    expect_golden(sample, golden_le, golden_be);
}

TEST(ArticulatedPartControl, EachEnableBitAlone) {
    const auto flags = [](const ArticulatedPartControl& p) {
        std::array<std::byte, 32> b{};
        encode(p, b);
        return std::to_integer<int>(b.at(5));
    };
    EXPECT_EQ(flags({.part_enabled = true}), 0x01);
    EXPECT_EQ(flags({.x_offset_enabled = true}), 0x02);
    EXPECT_EQ(flags({.y_offset_enabled = true}), 0x04);
    EXPECT_EQ(flags({.z_offset_enabled = true}), 0x08);
    EXPECT_EQ(flags({.roll_enabled = true}), 0x10);
    EXPECT_EQ(flags({.pitch_enabled = true}), 0x20);
    EXPECT_EQ(flags({.yaw_enabled = true}), 0x40);
}

TEST(ArticulatedPartControl, ReservedBitsIgnoredOnDecode) {
    Bytes b(32);
    encode(sample, b);
    b.at(5) |= std::byte{0x80};
    b.at(6) = std::byte{0xFF};
    b.at(7) = std::byte{0xFF};
    const auto r = decode(std::type_identity<ArticulatedPartControl>{}, b, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, sample);
}

TEST(ArticulatedPartControl, StandardDecodeErrors) {
    expect_standard_decode_errors(sample);
}

TEST(ArticulatedPartControl, Validate) {
    EXPECT_TRUE(validate(ArticulatedPartControl{}).has_value());
    EXPECT_TRUE(validate(sample).has_value());
    EXPECT_TRUE(validate(ArticulatedPartControl{.roll = -180.0F, .pitch = 90.0F, .yaw = 360.0F})
                    .has_value());
    const auto offset_of = [](const ArticulatedPartControl& p) {
        const auto r = validate(p);
        EXPECT_FALSE(r.has_value());
        EXPECT_EQ(r.error().code, ErrorCode::InvalidFieldValue);
        EXPECT_EQ(r.error().opcode, 6);
        return r.error().offset;
    };
    EXPECT_EQ(offset_of({.roll = 181.0F}), 20U);
    EXPECT_EQ(offset_of({.pitch = -91.0F}), 24U);
    EXPECT_EQ(offset_of({.yaw = -0.5F}), 28U);
    EXPECT_EQ(offset_of({.yaw = std::numeric_limits<float>::quiet_NaN()}), 28U);
}

TEST(ArticulatedPartControl, RandomRoundTrip) {
    // NOLINTNEXTLINE(bugprone-random-generator-seed): deterministic on purpose
    std::mt19937 rng(0xA871C006U);
    std::uniform_real_distribution<float> unit(-1.0F, 1.0F);
    for (int i = 0; i < 2000; ++i) {
        const auto flag = [&] { return (rng() & 1U) != 0; };
        ArticulatedPartControl p;
        p.entity_id = static_cast<std::uint16_t>(rng());
        p.articulated_part_id = static_cast<std::uint8_t>(rng());
        p.part_enabled = flag();
        p.x_offset_enabled = flag();
        p.y_offset_enabled = flag();
        p.z_offset_enabled = flag();
        p.roll_enabled = flag();
        p.pitch_enabled = flag();
        p.yaw_enabled = flag();
        p.x_offset = unit(rng) * 100.0F;
        p.y_offset = unit(rng) * 100.0F;
        p.z_offset = unit(rng) * 100.0F;
        p.roll = unit(rng) * 180.0F;
        p.pitch = unit(rng) * 90.0F;
        p.yaw = (unit(rng) + 1.0F) * 180.0F;
        expect_round_trip_both_orders(p);
        EXPECT_TRUE(validate(p).has_value());
    }
}

} // namespace
