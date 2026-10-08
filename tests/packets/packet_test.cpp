#include "../support/bytes.hpp"
#include "cigi/packet.hpp"
#include "cigi/version.hpp"

#include <chrono>
#include <gtest/gtest.h>

namespace {

using namespace cigi;
using namespace cigi::test;

static_assert(Packet<UserPacket>);
static_assert(Packet<HostOnlyPacket>);
static_assert(!Packet<int>);
struct NoCodec {
    static constexpr std::uint8_t opcode = 9;
    [[maybe_unused]] static constexpr Direction direction = Direction::Both;
};
static_assert(!Packet<NoCodec>);
struct WrongOpcodeType {
    static constexpr int opcode = 9;
    [[maybe_unused]] static constexpr Direction direction = Direction::Both;
};
static_assert(!Packet<WrongOpcodeType>);

static_assert(protocol_version == version_of(3, 3));
static_assert(version_of(3, 2) < protocol_version);
static_assert(protocol_version_major == 3 && protocol_version_minor == 3);
static_assert(frame_packet_opcode(Direction::HostToIg) == 1);
static_assert(frame_packet_opcode(Direction::IgToHost) == 101);

TEST(Packet, TimestampTickIsTenMicroseconds) {
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::microseconds>(Timestamp{1}).count(), 10);
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(Timestamp{100}).count(), 1);
}

TEST(Packet, ReadHeader) {
    const auto b = bytes_of(0x65, 0x18, 0xFF);
    const auto h = read_header(b, 0);
    ASSERT_TRUE(h.has_value());
    EXPECT_EQ(h->opcode, 0x65);
    EXPECT_EQ(h->size, 0x18);
    const auto h2 = read_header(b, 1);
    ASSERT_TRUE(h2.has_value());
    EXPECT_EQ(h2->opcode, 0x18);
}

TEST(Packet, ReadHeaderTruncatedCarriesOffset) {
    const auto b = bytes_of(1, 2, 3);
    for (const std::size_t off : {std::size_t{2}, std::size_t{3}, std::size_t{100}}) {
        const auto h = read_header(b, off);
        ASSERT_FALSE(h.has_value());
        EXPECT_EQ(h.error().code, ErrorCode::Truncated);
        EXPECT_EQ(h.error().offset, off);
    }
}

TEST(Packet, ValidatePacketOrder) {
    const auto short_one = bytes_of(201);
    const auto truncated = detail::validate_packet(short_one, 201, 8);
    EXPECT_EQ(truncated.error().code, ErrorCode::Truncated);
    EXPECT_EQ(truncated.error().opcode, 201); // a single byte is still readable
    EXPECT_EQ(detail::validate_packet({}, 201, 8).error().opcode, 0);
    auto b = bytes_of(200, 8, 0, 0, 0, 0, 0, 0);
    const auto mismatch = detail::validate_packet(b, 201, 8);
    EXPECT_EQ(mismatch.error().code, ErrorCode::OpcodeMismatch);
    EXPECT_EQ(mismatch.error().opcode, 200);
    b.at(0) = std::byte{201};
    EXPECT_TRUE(detail::validate_packet(b, 201, 8).has_value());
    EXPECT_EQ(detail::validate_packet(b, 201, 16).error().code, ErrorCode::InvalidPacketSize);
    b.at(1) = std::byte{16};
    EXPECT_EQ(detail::validate_packet(b, 201, 8).error().code, ErrorCode::InvalidPacketSize);
}

} // namespace
