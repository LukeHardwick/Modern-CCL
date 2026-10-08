#include "../support/bytes.hpp"
#include "cigi/packets/start_of_frame.hpp"

#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <span>

namespace {

using namespace cigi;
using namespace cigi::test;
using wire::ByteOrder;

constexpr StartOfFrame sample{
    .database_number = -3,
    .ig_status = 0x7E,
    .ig_mode = IgMode::OfflineMaintenance,
    .timestamp_valid = true,
    .earth_reference_model = EarthReferenceModel::HostDefined,
    .ig_frame_number = 0x11121314,
    .timestamp = Timestamp{0x21222324},
    .last_host_frame_number = 0x31323334,
};

// Hand-derived from the 4.2.1 layout: byte 5 = minor 3 (0x30) | host-defined ERM (0x08) |
// timestamp valid (0x04) | Offline Maintenance (0x03) = 0x3F; opcode 101 = 0x65.
constexpr auto golden_le = bytes_of(0x65, 0x18, 0x03, 0xFD, 0x7E, 0x3F, 0x00, 0x80, //
                                    0x14, 0x13, 0x12, 0x11, 0x24, 0x23, 0x22, 0x21, //
                                    0x34, 0x33, 0x32, 0x31, 0x00, 0x00, 0x00, 0x00);
constexpr auto golden_be = bytes_of(0x65, 0x18, 0x03, 0xFD, 0x7E, 0x3F, 0x80, 0x00, //
                                    0x11, 0x12, 0x13, 0x14, 0x21, 0x22, 0x23, 0x24, //
                                    0x31, 0x32, 0x33, 0x34, 0x00, 0x00, 0x00, 0x00);

constexpr Result<StartOfFrame> decode_sof(std::span<const std::byte> b,
                                          ByteOrder o = ByteOrder::Native) {
    return decode(std::type_identity<StartOfFrame>{}, b, o);
}

constexpr bool constexpr_round_trip() {
    std::array<std::byte, 24> buf{};
    encode(sample, buf);
    const auto r = decode_sof(buf);
    return r.has_value() && *r == sample && buf == native_of(golden_le, golden_be);
}
static_assert(constexpr_round_trip());
static_assert(Packet<StartOfFrame>);
static_assert(StartOfFrame::wire_size == 24 && StartOfFrame::opcode == 101);
static_assert(StartOfFrame::direction == Direction::IgToHost);

TEST(StartOfFrame, EncodeMatchesGolden) {
    std::array<std::byte, 24> buf{};
    encode(sample, buf);
    EXPECT_EQ(buf, native_of(golden_le, golden_be));
    EXPECT_EQ(encoded_size(sample), 24U);
}

TEST(StartOfFrame, DecodeNativeAndForeignGolden) {
    const auto native = decode_sof(native_of(golden_le, golden_be), ByteOrder::Native);
    ASSERT_TRUE(native.has_value());
    EXPECT_EQ(*native, sample);
    const auto foreign = decode_sof(foreign_of(golden_le, golden_be), ByteOrder::Swapped);
    ASSERT_TRUE(foreign.has_value());
    EXPECT_EQ(*foreign, sample);
}

TEST(StartOfFrame, ReservedBytesAreZeroEvenIfBufferWasDirty) {
    std::array<std::byte, 24> buf{};
    buf.fill(std::byte{0xFF});
    encode(sample, buf);
    EXPECT_EQ(buf, native_of(golden_le, golden_be));
}

TEST(StartOfFrame, RoundTripEveryModeAndBoundaries) {
    for (const auto mode :
         {IgMode::ResetStandby, IgMode::Operate, IgMode::Debug, IgMode::OfflineMaintenance}) {
        for (const auto erm : {EarthReferenceModel::Wgs84, EarthReferenceModel::HostDefined}) {
            for (const std::int8_t db : {std::int8_t{-128}, std::int8_t{-1}, std::int8_t{127}}) {
                const StartOfFrame p{
                    .database_number = db,
                    .ig_status = 255,
                    .ig_mode = mode,
                    .timestamp_valid = db < 0,
                    .earth_reference_model = erm,
                    .ig_frame_number = std::numeric_limits<std::uint32_t>::max(),
                    .timestamp = Timestamp{std::numeric_limits<std::uint32_t>::max()},
                    .last_host_frame_number = std::numeric_limits<std::uint32_t>::max(),
                };
                std::array<std::byte, 24> buf{};
                encode(p, buf);
                const auto r = decode_sof(buf);
                ASSERT_TRUE(r.has_value());
                EXPECT_EQ(*r, p);
            }
        }
    }
}

TEST(StartOfFrame, EncodeMasksOutOfRangeMode) {
    StartOfFrame p;
    p.ig_mode = static_cast<IgMode>(0xFF);
    std::array<std::byte, 24> buf{};
    encode(p, buf);
    EXPECT_EQ(buf.at(5), std::byte{0x33});
}

TEST(StartOfFrame, AcceptsLargerPacketAndIgnoresReservedBits) {
    Bytes b = to_vec(native_of(golden_le, golden_be));
    b.at(1) = std::byte{32};
    b.resize(32, std::byte{0xAB});
    for (std::size_t i = 20; i < 24; ++i) {
        b.at(i) = std::byte{0xFF};
    }
    const auto r = decode_sof(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, sample);
}

TEST(StartOfFrame, LegacyThreeZeroPacket) {
    Bytes b = frame_packet(101, 16, 0);
    b.at(3) = std::byte{1};
    b.at(4) = std::byte{7};
    b.at(5) = std::byte{0x05}; // timestamp valid + Operate
    const std::span<std::byte> s{b};
    wire::store<std::uint32_t>(s, 8, 1234); // "Frame Counter"
    wire::store<std::uint32_t>(s, 12, 4321);
    const auto r = decode_sof(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->version, (version_of(3, 0)));
    EXPECT_EQ(r->ig_status, 7);
    EXPECT_EQ(r->ig_mode, IgMode::Operate);
    EXPECT_TRUE(r->timestamp_valid);
    EXPECT_EQ(r->earth_reference_model, EarthReferenceModel::Wgs84);
    EXPECT_EQ(r->ig_frame_number, 1234U);
    EXPECT_EQ(r->timestamp.count(), 4321U);
    EXPECT_EQ(r->last_host_frame_number, 0U);
}

TEST(StartOfFrame, SixteenBytesWithModernMinorIsInvalidSize) {
    const auto r = decode_sof(frame_packet(101, 16, 3));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidPacketSize);
    EXPECT_EQ(r.error().opcode, 101);
}

TEST(StartOfFrame, UnsupportedMajorVersion) {
    for (const int major : {2, 4}) {
        Bytes b = frame_packet(101, 24, 3);
        b.at(2) = static_cast<std::byte>(major);
        EXPECT_EQ(decode_sof(b).error().code, ErrorCode::UnsupportedVersion);
    }
}

TEST(StartOfFrame, BadMagicAndWrongOpcode) {
    Bytes b = frame_packet(101, 24, 3);
    wire::store<std::uint16_t>(std::span<std::byte>{b}, 6, 0);
    EXPECT_EQ(decode_sof(b).error().code, ErrorCode::InvalidByteSwapMagic);
    EXPECT_EQ(decode_sof(frame_packet(1, 24, 3)).error().code, ErrorCode::InvalidFieldValue);
}

TEST(StartOfFrame, TruncatedAndSizeMismatch) {
    const Bytes good = frame_packet(101, 24, 3);
    EXPECT_EQ(decode_sof({}).error().code, ErrorCode::Truncated);
    EXPECT_EQ(decode_sof(std::span<const std::byte>{good}.first(23)).error().code,
              ErrorCode::InvalidPacketSize);
}

} // namespace
