#include "../support/bytes.hpp"
#include "cigi/packets/view_definition.hpp"

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

constexpr ViewDefinition sample{
    .view_id = 0x0304,
    .group_id = 5,
    .near_enabled = true,
    .far_enabled = true,
    .left_enabled = true,
    .right_enabled = true,
    .top_enabled = true,
    .bottom_enabled = true,
    .mirror_mode = MirrorMode::Vertical,
    .pixel_replication_mode = PixelReplicationMode::TwoByTwo,
    .projection_type = ProjectionType::OrthographicParallel,
    .reorder = true,
    .view_type = 5,
    .near_plane = 1.0F,   // 0x3F800000
    .far_plane = 1000.0F, // 0x447A0000
    .left = -45.0F,       // 0xC2340000
    .right = 45.0F,       // 0x42340000
    .top = 10.0F,         // 0x41200000
    .bottom = -10.0F,     // 0xC1200000
};

// Hand-derived from 4.1.21: byte 5 = mirror mode 2 (0x80) | all six enables (0x3F) = 0xBF;
// byte 6 = view type 5 (0xA0) | reorder (0x10) | orthographic (0x08) | replication 3 (0x03) =
// 0xBB; packet ID 21 = 0x15, size 32 = 0x20.
constexpr auto golden_le = bytes_of(0x15, 0x20, 0x04, 0x03, 0x05, 0xBF, 0xBB, 0x00, //
                                    0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x7A, 0x44, //
                                    0x00, 0x00, 0x34, 0xC2, 0x00, 0x00, 0x34, 0x42, //
                                    0x00, 0x00, 0x20, 0x41, 0x00, 0x00, 0x20, 0xC1);
constexpr auto golden_be = bytes_of(0x15, 0x20, 0x03, 0x04, 0x05, 0xBF, 0xBB, 0x00, //
                                    0x3F, 0x80, 0x00, 0x00, 0x44, 0x7A, 0x00, 0x00, //
                                    0xC2, 0x34, 0x00, 0x00, 0x42, 0x34, 0x00, 0x00, //
                                    0x41, 0x20, 0x00, 0x00, 0xC1, 0x20, 0x00, 0x00);

constexpr Result<ViewDefinition> dec(std::span<const std::byte> b,
                                     ByteOrder o = ByteOrder::Native) {
    return decode(std::type_identity<ViewDefinition>{}, b, o);
}

constexpr bool constexpr_round_trip() {
    std::array<std::byte, 32> buf{};
    encode(sample, buf);
    const auto r = dec(buf);
    return r.has_value() && *r == sample && buf == native_of(golden_le, golden_be);
}
static_assert(constexpr_round_trip());
static_assert(Packet<ViewDefinition>);
static_assert(ViewDefinition::wire_size == 32 && ViewDefinition::opcode == 21);
static_assert(ViewDefinition::direction == Direction::HostToIg);

constexpr Error err(std::size_t offset) {
    return Error{.code = ErrorCode::InvalidFieldValue, .offset = offset, .opcode = 21};
}

TEST(ViewDefinition, EncodeMatchesGolden) {
    std::array<std::byte, 32> buf{};
    encode(sample, buf);
    EXPECT_EQ(buf, native_of(golden_le, golden_be));
    EXPECT_EQ(encoded_size(sample), 32U);
}

TEST(ViewDefinition, DecodeNativeAndForeignGolden) {
    const auto native = dec(native_of(golden_le, golden_be), ByteOrder::Native);
    ASSERT_TRUE(native.has_value());
    EXPECT_EQ(*native, sample);
    const auto foreign = dec(foreign_of(golden_le, golden_be), ByteOrder::Swapped);
    ASSERT_TRUE(foreign.has_value());
    EXPECT_EQ(*foreign, sample);
}

TEST(ViewDefinition, ReservedByteIsZeroEvenIfBufferWasDirty) {
    std::array<std::byte, 32> buf{};
    buf.fill(std::byte{0xFF});
    encode(sample, buf);
    EXPECT_EQ(buf, native_of(golden_le, golden_be));
}

TEST(ViewDefinition, EachFlagAndFieldUsesItsOwnBits) {
    const auto bytes56 = [](const ViewDefinition& p) {
        std::array<std::byte, 32> buf{};
        encode(p, buf);
        return std::pair{std::to_integer<unsigned>(buf.at(5)),
                         std::to_integer<unsigned>(buf.at(6))};
    };
    EXPECT_EQ(bytes56(ViewDefinition{.near_enabled = true}), (std::pair{0x01U, 0x00U}));
    EXPECT_EQ(bytes56(ViewDefinition{.far_enabled = true}), (std::pair{0x02U, 0x00U}));
    EXPECT_EQ(bytes56(ViewDefinition{.left_enabled = true}), (std::pair{0x04U, 0x00U}));
    EXPECT_EQ(bytes56(ViewDefinition{.right_enabled = true}), (std::pair{0x08U, 0x00U}));
    EXPECT_EQ(bytes56(ViewDefinition{.top_enabled = true}), (std::pair{0x10U, 0x00U}));
    EXPECT_EQ(bytes56(ViewDefinition{.bottom_enabled = true}), (std::pair{0x20U, 0x00U}));
    EXPECT_EQ(bytes56(ViewDefinition{.mirror_mode = MirrorMode::Horizontal}),
              (std::pair{0x40U, 0x00U}));
    EXPECT_EQ(bytes56(ViewDefinition{.mirror_mode = MirrorMode::HorizontalAndVertical}),
              (std::pair{0xC0U, 0x00U}));
    EXPECT_EQ(bytes56(ViewDefinition{.pixel_replication_mode = PixelReplicationMode::OneByTwo}),
              (std::pair{0x00U, 0x01U}));
    EXPECT_EQ(
        bytes56(ViewDefinition{.pixel_replication_mode = static_cast<PixelReplicationMode>(7)}),
        (std::pair{0x00U, 0x07U}));
    EXPECT_EQ(bytes56(ViewDefinition{.projection_type = ProjectionType::OrthographicParallel}),
              (std::pair{0x00U, 0x08U}));
    EXPECT_EQ(bytes56(ViewDefinition{.reorder = true}), (std::pair{0x00U, 0x10U}));
    EXPECT_EQ(bytes56(ViewDefinition{.view_type = 7}), (std::pair{0x00U, 0xE0U}));
}

TEST(ViewDefinition, EncodeMasksOversizedValues) {
    ViewDefinition p;
    p.mirror_mode = static_cast<MirrorMode>(0xFF);
    p.view_type = 0xFF;
    std::array<std::byte, 32> buf{};
    encode(p, buf);
    EXPECT_EQ(buf.at(5), std::byte{0xC0});
    EXPECT_EQ(buf.at(6), std::byte{0xE0});
}

TEST(ViewDefinition, DecodeIgnoresReservedByteAndTrailingBytes) {
    Bytes b = to_vec(native_of(golden_le, golden_be));
    b.at(7) = std::byte{0xFF};
    b.at(1) = std::byte{40};
    b.resize(40, std::byte{0xAB});
    const auto r = dec(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, sample);
}

TEST(ViewDefinition, DecodeErrors) {
    const Bytes good = to_vec(native_of(golden_le, golden_be));
    EXPECT_EQ(dec({}).error().code, ErrorCode::Truncated);
    EXPECT_EQ(dec(std::span<const std::byte>{good}.first(31)).error().code,
              ErrorCode::InvalidPacketSize);
    Bytes wrong = good;
    wrong.at(0) = std::byte{16};
    const auto r = dec(wrong);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::OpcodeMismatch);
    EXPECT_EQ(r.error().opcode, 16);
    Bytes small = good;
    small.at(1) = std::byte{16};
    small.resize(16);
    EXPECT_EQ(dec(small).error().code, ErrorCode::InvalidPacketSize);
}

TEST(ViewDefinition, EnumTraits) {
    EXPECT_TRUE(EnumTraits<MirrorMode>::is_defined(MirrorMode::HorizontalAndVertical));
    EXPECT_FALSE(EnumTraits<MirrorMode>::is_defined(static_cast<MirrorMode>(4)));
    EXPECT_TRUE(EnumTraits<PixelReplicationMode>::is_defined(static_cast<PixelReplicationMode>(7)));
    EXPECT_FALSE(
        EnumTraits<PixelReplicationMode>::is_defined(static_cast<PixelReplicationMode>(8)));
    EXPECT_TRUE(EnumTraits<ProjectionType>::is_defined(ProjectionType::OrthographicParallel));
    EXPECT_FALSE(EnumTraits<ProjectionType>::is_defined(static_cast<ProjectionType>(2)));
}

TEST(ViewDefinition, ValidateAcceptsSampleAndDefaults) {
    EXPECT_TRUE(validate(sample).has_value());
    EXPECT_TRUE(validate(ViewDefinition{}).has_value()); // nothing enabled: nothing to check
    // Disabled values are ignored, however odd.
    EXPECT_TRUE(validate(ViewDefinition{.near_plane = -5.0F,
                                        .far_plane = -9.0F,
                                        .left = 500.0F,
                                        .right = -500.0F,
                                        .top = std::nanf(""),
                                        .bottom = 99.0F})
                    .has_value());
}

TEST(ViewDefinition, ValidateRejectsUndefinedEnums) {
    EXPECT_EQ(validate(ViewDefinition{.mirror_mode = static_cast<MirrorMode>(0xFF)}).error(),
              err(5));
    // Decode can only produce 0..1 for the 1-bit projection type, but a hand-built value can
    // differ.
    EXPECT_EQ(validate(ViewDefinition{.projection_type = static_cast<ProjectionType>(2)}).error(),
              err(6));
    EXPECT_EQ(
        validate(ViewDefinition{.pixel_replication_mode = static_cast<PixelReplicationMode>(8)})
            .error(),
        err(6));
    for (std::uint8_t v = 4; v <= 7; ++v) {
        EXPECT_TRUE(validate(
            ViewDefinition{.pixel_replication_mode = static_cast<PixelReplicationMode>(v)}));
    }
}

TEST(ViewDefinition, ValidateNearFar) {
    const auto v = [](float n, float f) {
        return validate(ViewDefinition{
            .near_enabled = true,
            .far_enabled = true,
            .near_plane = n,
            .far_plane = f,
        });
    };
    EXPECT_TRUE(v(0.1F, 0.2F));
    EXPECT_EQ(v(0.0F, 10.0F).error(), err(8));
    EXPECT_EQ(v(-1.0F, 10.0F).error(), err(8));
    EXPECT_EQ(v(std::nanf(""), 10.0F).error(), err(8));
    EXPECT_EQ(v(5.0F, 5.0F).error(), err(12));
    EXPECT_EQ(v(6.0F, 5.0F).error(), err(12));
    EXPECT_EQ(v(1.0F, std::nanf("")).error(), err(12));
    // Only one enabled: far must still be positive; near is not compared with a disabled far.
    EXPECT_TRUE(
        validate(ViewDefinition{.far_enabled = true, .near_plane = 99.0F, .far_plane = 1.0F}));
    EXPECT_EQ(validate(ViewDefinition{.far_enabled = true, .far_plane = 0.0F}).error(), err(12));
    EXPECT_TRUE(
        validate(ViewDefinition{.near_enabled = true, .near_plane = 1.0F, .far_plane = 0.0F}));
}

TEST(ViewDefinition, ValidateAngles) {
    EXPECT_TRUE(validate(ViewDefinition{.left_enabled = true, .left = -89.9F}));
    EXPECT_EQ(validate(ViewDefinition{.left_enabled = true, .left = -90.0F}).error(), err(16));
    EXPECT_EQ(validate(ViewDefinition{.left_enabled = true, .left = 90.0F}).error(), err(16));
    EXPECT_TRUE(validate(ViewDefinition{.right_enabled = true, .right = 89.9F}));
    EXPECT_EQ(validate(ViewDefinition{.right_enabled = true, .right = 90.0F}).error(), err(20));
    EXPECT_EQ(validate(ViewDefinition{.right_enabled = true, .right = -90.0F}).error(), err(20));
    EXPECT_TRUE(validate(ViewDefinition{.top_enabled = true, .top = 89.9F}));
    EXPECT_EQ(validate(ViewDefinition{.top_enabled = true, .top = 90.0F}).error(), err(24));
    EXPECT_EQ(validate(ViewDefinition{.top_enabled = true, .top = -90.0F}).error(), err(24));
    EXPECT_TRUE(validate(ViewDefinition{.bottom_enabled = true, .bottom = -89.9F}));
    EXPECT_EQ(validate(ViewDefinition{.bottom_enabled = true, .bottom = -90.0F}).error(), err(28));
    EXPECT_EQ(validate(ViewDefinition{.bottom_enabled = true, .bottom = 90.0F}).error(), err(28));
    EXPECT_EQ(validate(ViewDefinition{.bottom_enabled = true, .bottom = std::nanf("")}).error(),
              err(28));
}

TEST(ViewDefinition, ValidateAnglePairs) {
    const auto lr = [](float l, float r) {
        return validate(
            ViewDefinition{.left_enabled = true, .right_enabled = true, .left = l, .right = r});
    };
    EXPECT_TRUE(lr(-30.0F, 30.0F));
    EXPECT_EQ(lr(30.0F, 30.0F).error(), err(20));
    EXPECT_EQ(lr(30.0F, -30.0F).error(), err(20));
    const auto bt = [](float b, float t) {
        return validate(
            ViewDefinition{.top_enabled = true, .bottom_enabled = true, .top = t, .bottom = b});
    };
    EXPECT_TRUE(bt(-20.0F, 20.0F));
    EXPECT_EQ(bt(20.0F, 20.0F).error(), err(28));
    EXPECT_EQ(bt(20.0F, -20.0F).error(), err(28));
    // Pair rule is skipped when the partner is disabled.
    EXPECT_TRUE(validate(ViewDefinition{.right_enabled = true, .left = 50.0F, .right = 10.0F}));
    EXPECT_TRUE(validate(ViewDefinition{.bottom_enabled = true, .top = -50.0F, .bottom = 10.0F}));
}

TEST(ViewDefinition, RandomRoundTripBothOrders) {
    // NOLINTNEXTLINE(bugprone-random-generator-seed): deterministic on purpose
    std::mt19937 rng(0xC1613321U);
    for (int i = 0; i < 2000; ++i) {
        const auto bit = [&] { return (rng() & 1U) != 0; };
        const auto f = [&] {
            return static_cast<float>(static_cast<int>(rng() % 200001U) - 100000) / 64.0F;
        };
        ViewDefinition p;
        p.view_id = static_cast<std::uint16_t>(rng());
        p.group_id = static_cast<std::uint8_t>(rng());
        p.near_enabled = bit();
        p.far_enabled = bit();
        p.left_enabled = bit();
        p.right_enabled = bit();
        p.top_enabled = bit();
        p.bottom_enabled = bit();
        p.mirror_mode = static_cast<MirrorMode>(rng() & 3U);
        p.pixel_replication_mode = static_cast<PixelReplicationMode>(rng() & 7U);
        p.projection_type = static_cast<ProjectionType>(rng() & 1U);
        p.reorder = bit();
        p.view_type = static_cast<std::uint8_t>(rng() & 7U);
        p.near_plane = f();
        p.far_plane = f();
        p.left = f();
        p.right = f();
        p.top = f();
        p.bottom = f();

        Bytes native(encoded_size(p));
        encode(p, native);
        const auto a = dec(native);
        ASSERT_TRUE(a.has_value());
        EXPECT_EQ(*a, p);

        Bytes foreign = native;
        std::reverse(foreign.begin() + 2, foreign.begin() + 4);
        for (std::ptrdiff_t off = 8; off < 32; off += 4) {
            std::reverse(foreign.begin() + off, foreign.begin() + off + 4);
        }
        const auto b = dec(foreign, ByteOrder::Swapped);
        ASSERT_TRUE(b.has_value());
        EXPECT_EQ(*b, p);
    }
}

} // namespace
