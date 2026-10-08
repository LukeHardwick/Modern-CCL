#include "../support/bytes.hpp"
#include "cigi/packets/ig_control.hpp"

#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <span>

namespace {

using namespace cigi;
using namespace cigi::test;
using wire::ByteOrder;

constexpr IgControl sample{
    .database_number = 5,
    .ig_mode = IgMode::Operate,
    .timestamp_valid = true,
    .extrapolation_enabled = true,
    .host_frame_number = 0x01020304,
    .timestamp = Timestamp{0x05060708},
    .last_ig_frame_number = 0x090A0B0C,
};

// Hand-derived from the 4.1.1 layout: byte 4 = minor 3 (0x30) | extrapolation (0x08) |
// timestamp valid (0x04) | Operate (0x01) = 0x3D; magic 0x8000.
constexpr auto golden_le = bytes_of(0x01, 0x18, 0x03, 0x05, 0x3D, 0x00, 0x00, 0x80, //
                                    0x04, 0x03, 0x02, 0x01, 0x08, 0x07, 0x06, 0x05, //
                                    0x0C, 0x0B, 0x0A, 0x09, 0x00, 0x00, 0x00, 0x00);
constexpr auto golden_be = bytes_of(0x01, 0x18, 0x03, 0x05, 0x3D, 0x00, 0x80, 0x00, //
                                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, //
                                    0x09, 0x0A, 0x0B, 0x0C, 0x00, 0x00, 0x00, 0x00);

constexpr Result<IgControl> decode_ig(std::span<const std::byte> b,
                                      ByteOrder o = ByteOrder::Native) {
    return decode(std::type_identity<IgControl>{}, b, o);
}

constexpr bool constexpr_round_trip() {
    std::array<std::byte, 24> buf{};
    encode(sample, buf);
    const auto r = decode_ig(buf);
    return r.has_value() && *r == sample && buf == native_of(golden_le, golden_be);
}
static_assert(constexpr_round_trip());
static_assert(Packet<IgControl>);
static_assert(IgControl::wire_size == 24 && IgControl::opcode == 1);
static_assert(IgControl::direction == Direction::HostToIg);

TEST(IgControl, EncodeMatchesGolden) {
    std::array<std::byte, 24> buf{};
    encode(sample, buf);
    EXPECT_EQ(buf, native_of(golden_le, golden_be));
    EXPECT_EQ(encoded_size(sample), 24U);
}

TEST(IgControl, DecodeNativeAndForeignGolden) {
    const auto native = decode_ig(native_of(golden_le, golden_be), ByteOrder::Native);
    ASSERT_TRUE(native.has_value());
    EXPECT_EQ(*native, sample);
    const auto foreign = decode_ig(foreign_of(golden_le, golden_be), ByteOrder::Swapped);
    ASSERT_TRUE(foreign.has_value());
    EXPECT_EQ(*foreign, sample);
}

TEST(IgControl, ForeignBytesDecodedAsNativeFailMagic) {
    const auto r = decode_ig(foreign_of(golden_le, golden_be), ByteOrder::Native);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidByteSwapMagic);
    EXPECT_EQ(r.error().opcode, 1);
}

TEST(IgControl, ReservedBytesAreZeroEvenIfBufferWasDirty) {
    std::array<std::byte, 24> buf{};
    buf.fill(std::byte{0xFF});
    encode(sample, buf);
    EXPECT_EQ(buf, native_of(golden_le, golden_be));
    EXPECT_EQ(buf.at(5), std::byte{0});
    for (std::size_t i = 20; i < 24; ++i) {
        EXPECT_EQ(buf.at(i), std::byte{0});
    }
}

TEST(IgControl, RoundTripEveryModeAndBoundaries) {
    for (const auto mode :
         {IgMode::ResetStandby, IgMode::Operate, IgMode::Debug, IgMode::OfflineMaintenance}) {
        for (const bool flag : {false, true}) {
            for (const std::int8_t db : {std::int8_t{-128}, std::int8_t{0}, std::int8_t{127}}) {
                const IgControl p{
                    .database_number = db,
                    .ig_mode = mode,
                    .timestamp_valid = flag,
                    .extrapolation_enabled = !flag,
                    .host_frame_number = std::numeric_limits<std::uint32_t>::max(),
                    .timestamp = Timestamp{std::numeric_limits<std::uint32_t>::max()},
                    .last_ig_frame_number = std::numeric_limits<std::uint32_t>::max(),
                };
                std::array<std::byte, 24> buf{};
                encode(p, buf);
                const auto r = decode_ig(buf);
                ASSERT_TRUE(r.has_value());
                EXPECT_EQ(*r, p);
            }
        }
    }
}

TEST(IgControl, EncodeMasksOutOfRangeMode) {
    IgControl p;
    p.ig_mode = static_cast<IgMode>(0xFF);
    std::array<std::byte, 24> buf{};
    encode(p, buf);
    // Only the two mode bits are set; minor version nibble and flags are untouched.
    EXPECT_EQ(buf.at(4), std::byte{0x33});
}

TEST(IgControl, DecodeDefaultsAndVersionOfSample) {
    const auto r = decode_ig(native_of(golden_le, golden_be));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->version, (version_of(3, 3)));
}

TEST(IgControl, AcceptsLargerPacketAndIgnoresExtraBytes) {
    Bytes b = to_vec(native_of(golden_le, golden_be));
    b.at(1) = std::byte{32};
    b.resize(32, std::byte{0xAB});
    const auto r = decode_ig(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, sample);
}

TEST(IgControl, IgnoresReservedBits) {
    Bytes b = to_vec(native_of(golden_le, golden_be));
    b.at(5) = std::byte{0xFF};
    for (std::size_t i = 20; i < 24; ++i) {
        b.at(i) = std::byte{0xFF};
    }
    const auto r = decode_ig(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, sample);
}

TEST(IgControl, LegacyThreeZeroPacket) {
    Bytes b = frame_packet(1, 16, 0);
    b.at(3) = std::byte{9};
    b.at(4) = std::byte{0x0D}; // timestamp valid + Operate; bit 3 is reserved before 3.3
    const std::span<std::byte> s{b};
    wire::store<std::uint32_t>(s, 8, 77);  // "Frame Counter"
    wire::store<std::uint32_t>(s, 12, 99); // Timestamp
    const auto r = decode_ig(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->version, (version_of(3, 0)));
    EXPECT_EQ(r->database_number, 9);
    EXPECT_EQ(r->ig_mode, IgMode::Operate);
    EXPECT_TRUE(r->timestamp_valid);
    EXPECT_FALSE(r->extrapolation_enabled);
    EXPECT_EQ(r->host_frame_number, 77U);
    EXPECT_EQ(r->timestamp.count(), 99U);
    EXPECT_EQ(r->last_ig_frame_number, 0U);
}

TEST(IgControl, ThreeTwoPacketIgnoresReservedExtrapolationBit) {
    Bytes b = frame_packet(1, 24, 2);
    b.at(4) = std::byte{0x2C}; // minor 2, bit 3 set (reserved in 3.2), timestamp valid
    wire::store<std::uint32_t>(std::span<std::byte>{b}, 16, 5);
    const auto r = decode_ig(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r->extrapolation_enabled);
    EXPECT_TRUE(r->timestamp_valid);
    EXPECT_EQ(r->last_ig_frame_number, 5U);
    EXPECT_EQ(r->version, (version_of(3, 2)));
}

TEST(IgControl, ForeignOrderLegacyPacket) {
    Bytes b = frame_packet(1, 16, 0, ByteOrder::Swapped);
    wire::store<std::uint32_t>(std::span<std::byte>{b}, 8, 0xAABBCCDD, ByteOrder::Swapped);
    const auto r = decode_ig(b, ByteOrder::Swapped);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->host_frame_number, 0xAABBCCDDU);
}

TEST(IgControl, SixteenBytesWithModernMinorIsInvalidSize) {
    for (const int minor : {2, 3, 15}) {
        const auto r = decode_ig(frame_packet(1, 16, static_cast<std::uint8_t>(minor)));
        ASSERT_FALSE(r.has_value());
        EXPECT_EQ(r.error().code, ErrorCode::InvalidPacketSize);
        EXPECT_EQ(r.error().opcode, 1);
    }
}

TEST(IgControl, UnsupportedMajorVersion) {
    for (const int major : {2, 4}) {
        Bytes b = frame_packet(1, 24, 3);
        b.at(2) = static_cast<std::byte>(major);
        const auto r = decode_ig(b);
        ASSERT_FALSE(r.has_value());
        EXPECT_EQ(r.error().code, ErrorCode::UnsupportedVersion);
    }
}

TEST(IgControl, BadMagic) {
    Bytes b = frame_packet(1, 24, 3);
    wire::store<std::uint16_t>(std::span<std::byte>{b}, 6, 0x1234);
    const auto r = decode_ig(b);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidByteSwapMagic);
}

TEST(IgControl, WrongOpcode) {
    Bytes b = frame_packet(1, 24, 3);
    b.at(0) = std::byte{2};
    const auto r = decode_ig(b);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidFieldValue);
    EXPECT_EQ(r.error().opcode, 1);
}

TEST(IgControl, TruncatedAndSizeMismatch) {
    const Bytes good = frame_packet(1, 24, 3);
    EXPECT_EQ(decode_ig({}).error().code, ErrorCode::Truncated);
    EXPECT_EQ(decode_ig(std::span<const std::byte>{good}.first(1)).error().code,
              ErrorCode::Truncated);
    EXPECT_EQ(decode_ig(std::span<const std::byte>{good}.first(20)).error().code,
              ErrorCode::InvalidPacketSize);
    Bytes small = frame_packet(1, 8, 3); // size byte 8, below the 16-byte minimum
    EXPECT_EQ(decode_ig(small).error().code, ErrorCode::InvalidPacketSize);
}

TEST(IgControl, EqualityComparesEveryField) {
    IgControl a = sample;
    EXPECT_EQ(a, sample);
    a.version = version_of(3, 0);
    EXPECT_NE(a, sample);
}

} // namespace
