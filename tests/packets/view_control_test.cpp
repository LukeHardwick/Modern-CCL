#include "../support/bytes.hpp"
#include "cigi/packets/view_control.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <random>
#include <span>

namespace {

using namespace cigi;
using namespace cigi::test;
using wire::ByteOrder;

constexpr ViewControl sample{
    .view_id = 0x0102,
    .group_id = 3,
    .x_offset_enabled = true,
    .y_offset_enabled = false,
    .z_offset_enabled = true,
    .roll_enabled = false,
    .pitch_enabled = true,
    .yaw_enabled = true,
    .entity_id = 0x0A0B,
    .x_offset = 1.0F,  // 0x3F800000
    .y_offset = -1.0F, // 0xBF800000
    .z_offset = 2.0F,  // 0x40000000
    .roll = 0.5F,      // 0x3F000000
    .pitch = -45.0F,   // 0xC2340000
    .yaw = 90.0F,      // 0x42B40000
};

// Hand-derived from 4.1.16: byte 5 = yaw (0x20) | pitch (0x10) | Z (0x04) | X (0x01) = 0x35;
// packet ID 16 = 0x10, size 32 = 0x20.
constexpr auto golden_le = bytes_of(0x10, 0x20, 0x02, 0x01, 0x03, 0x35, 0x0B, 0x0A, //
                                    0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0xBF, //
                                    0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x3F, //
                                    0x00, 0x00, 0x34, 0xC2, 0x00, 0x00, 0xB4, 0x42);
constexpr auto golden_be = bytes_of(0x10, 0x20, 0x01, 0x02, 0x03, 0x35, 0x0A, 0x0B, //
                                    0x3F, 0x80, 0x00, 0x00, 0xBF, 0x80, 0x00, 0x00, //
                                    0x40, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, //
                                    0xC2, 0x34, 0x00, 0x00, 0x42, 0xB4, 0x00, 0x00);

constexpr Result<ViewControl> dec(std::span<const std::byte> b, ByteOrder o = ByteOrder::Native) {
    return decode(std::type_identity<ViewControl>{}, b, o);
}

constexpr bool constexpr_round_trip() {
    std::array<std::byte, 32> buf{};
    encode(sample, buf);
    const auto r = dec(buf);
    return r.has_value() && *r == sample && buf == native_of(golden_le, golden_be);
}
static_assert(constexpr_round_trip());
static_assert(Packet<ViewControl>);
static_assert(ViewControl::wire_size == 32 && ViewControl::opcode == 16);
static_assert(ViewControl::direction == Direction::HostToIg);

TEST(ViewControl, EncodeMatchesGolden) {
    std::array<std::byte, 32> buf{};
    encode(sample, buf);
    EXPECT_EQ(buf, native_of(golden_le, golden_be));
    EXPECT_EQ(encoded_size(sample), 32U);
}

TEST(ViewControl, DecodeNativeAndForeignGolden) {
    const auto native = dec(native_of(golden_le, golden_be), ByteOrder::Native);
    ASSERT_TRUE(native.has_value());
    EXPECT_EQ(*native, sample);
    const auto foreign = dec(foreign_of(golden_le, golden_be), ByteOrder::Swapped);
    ASSERT_TRUE(foreign.has_value());
    EXPECT_EQ(*foreign, sample);
}

TEST(ViewControl, ReservedBitsAreZeroEvenIfBufferWasDirty) {
    std::array<std::byte, 32> buf{};
    buf.fill(std::byte{0xFF});
    encode(sample, buf);
    EXPECT_EQ(buf, native_of(golden_le, golden_be));
}

TEST(ViewControl, EachEnableFlagUsesItsOwnBit) {
    const auto byte5 = [](const ViewControl& p) {
        std::array<std::byte, 32> buf{};
        encode(p, buf);
        return buf.at(5);
    };
    EXPECT_EQ(byte5(ViewControl{.x_offset_enabled = true}), std::byte{0x01});
    EXPECT_EQ(byte5(ViewControl{.y_offset_enabled = true}), std::byte{0x02});
    EXPECT_EQ(byte5(ViewControl{.z_offset_enabled = true}), std::byte{0x04});
    EXPECT_EQ(byte5(ViewControl{.roll_enabled = true}), std::byte{0x08});
    EXPECT_EQ(byte5(ViewControl{.pitch_enabled = true}), std::byte{0x10});
    EXPECT_EQ(byte5(ViewControl{.yaw_enabled = true}), std::byte{0x20});
}

TEST(ViewControl, DecodeIgnoresReservedBitsAndTrailingBytes) {
    Bytes b = to_vec(native_of(golden_le, golden_be));
    b.at(5) = static_cast<std::byte>(std::to_integer<unsigned>(b.at(5)) | 0xC0U);
    b.at(1) = std::byte{40};
    b.resize(40, std::byte{0xAB});
    const auto r = dec(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, sample);
}

TEST(ViewControl, DecodeErrors) {
    const Bytes good = to_vec(native_of(golden_le, golden_be));
    EXPECT_EQ(dec({}).error().code, ErrorCode::Truncated);
    EXPECT_EQ(dec(std::span<const std::byte>{good}.first(31)).error().code,
              ErrorCode::InvalidPacketSize);
    Bytes wrong = good;
    wrong.at(0) = std::byte{21};
    const auto r = dec(wrong);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::OpcodeMismatch);
    EXPECT_EQ(r.error().opcode, 21);
    Bytes small = good;
    small.at(1) = std::byte{16};
    small.resize(16);
    EXPECT_EQ(dec(small).error().code, ErrorCode::InvalidPacketSize);
}

TEST(ViewControl, DecodeIsTolerantOfOutOfRangeAngles) {
    ViewControl p;
    p.roll = 500.0F;
    p.pitch = -300.0F;
    p.yaw = -1.0F;
    std::array<std::byte, 32> buf{};
    encode(p, buf);
    const auto r = dec(buf);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, p);
}

TEST(ViewControl, ValidateAcceptsSampleDefaultsAndBoundaries) {
    EXPECT_TRUE(validate(sample).has_value());
    EXPECT_TRUE(validate(ViewControl{}).has_value());
    EXPECT_TRUE(validate(ViewControl{.roll = -180.0F, .pitch = -90.0F, .yaw = 0.0F}).has_value());
    EXPECT_TRUE(validate(ViewControl{.roll = 180.0F, .pitch = 90.0F, .yaw = 360.0F}).has_value());
}

TEST(ViewControl, ValidateRejectsOutOfRangeAngles) {
    const auto err = [](std::size_t offset) {
        return Error{.code = ErrorCode::InvalidFieldValue, .offset = offset, .opcode = 16};
    };
    EXPECT_EQ(validate(ViewControl{.roll = 180.5F}).error(), err(20));
    EXPECT_EQ(validate(ViewControl{.roll = -181.0F}).error(), err(20));
    EXPECT_EQ(validate(ViewControl{.pitch = 90.5F}).error(), err(24));
    EXPECT_EQ(validate(ViewControl{.pitch = -91.0F}).error(), err(24));
    EXPECT_EQ(validate(ViewControl{.yaw = 360.5F}).error(), err(28));
    EXPECT_EQ(validate(ViewControl{.yaw = -0.5F}).error(), err(28));
    EXPECT_EQ(validate(ViewControl{.yaw = std::nanf("")}).error(), err(28));
}

TEST(ViewControl, RandomRoundTripBothOrders) {
    // NOLINTNEXTLINE(bugprone-random-generator-seed): deterministic on purpose
    std::mt19937 rng(0xC1613316U);
    for (int i = 0; i < 2000; ++i) {
        const auto bit = [&] { return (rng() & 1U) != 0; };
        const auto f = [&] {
            return static_cast<float>(static_cast<int>(rng() % 200001U) - 100000) / 64.0F;
        };
        ViewControl p;
        p.view_id = static_cast<std::uint16_t>(rng());
        p.group_id = static_cast<std::uint8_t>(rng());
        p.x_offset_enabled = bit();
        p.y_offset_enabled = bit();
        p.z_offset_enabled = bit();
        p.roll_enabled = bit();
        p.pitch_enabled = bit();
        p.yaw_enabled = bit();
        p.entity_id = static_cast<std::uint16_t>(rng());
        p.x_offset = f();
        p.y_offset = f();
        p.z_offset = f();
        p.roll = f();
        p.pitch = f();
        p.yaw = f();

        Bytes native(encoded_size(p));
        encode(p, native);
        const auto a = dec(native);
        ASSERT_TRUE(a.has_value());
        EXPECT_EQ(*a, p);

        Bytes foreign = native;
        std::reverse(foreign.begin() + 2, foreign.begin() + 4);
        std::reverse(foreign.begin() + 6, foreign.begin() + 8);
        for (std::ptrdiff_t off = 8; off < 32; off += 4) {
            std::reverse(foreign.begin() + off, foreign.begin() + off + 4);
        }
        const auto b = dec(foreign, ByteOrder::Swapped);
        ASSERT_TRUE(b.has_value());
        EXPECT_EQ(*b, p);
    }
}

} // namespace
