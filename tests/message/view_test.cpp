#include "../support/bytes.hpp"
#include "cigi/message/builder.hpp"
#include "cigi/message/view.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace {

using namespace cigi;
using namespace cigi::test;
using wire::ByteOrder;

constexpr IgControl control{
    .database_number = 5,
    .ig_mode = IgMode::Operate,
    .timestamp_valid = true,
    .extrapolation_enabled = true,
    .host_frame_number = 0x01020304,
    .timestamp = Timestamp{0x05060708},
    .last_ig_frame_number = 0x090A0B0C,
};
constexpr StartOfFrame sof{.database_number = 2, .ig_status = 3, .ig_frame_number = 77};

template <Packet P> Bytes encoded(const P& p) {
    Bytes b(encoded_size(p));
    encode(p, b);
    return b;
}

/// An opaque packet with the given opcode and size (no validation by the library).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Bytes opaque(std::uint8_t opcode, std::size_t size) {
    Bytes b(size, std::byte{0});
    b.at(0) = static_cast<std::byte>(opcode);
    if (size > 1) {
        b.at(1) = static_cast<std::byte>(size);
    }
    return b;
}

std::vector<Result<RawPacket>> collect(const MessageView& v) {
    std::vector<Result<RawPacket>> out;
    for (const auto& item : v) {
        out.push_back(item);
    }
    return out;
}

Bytes host_message() {
    Bytes m = encoded(control);
    append(m, opaque(150, 16));
    append(m, encoded(UserPacket{0xBEEF}));
    return m;
}

TEST(MessageView, ParsesHostMessageAndYieldsEveryPacket) {
    const Bytes m = host_message();
    const auto v = MessageView::parse(m);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->direction(), Direction::HostToIg);
    EXPECT_EQ(v->version(), (version_of(3, 3)));
    EXPECT_EQ(v->byte_order(), ByteOrder::Native);
    EXPECT_EQ(v->bytes().size(), m.size());

    const auto items = collect(*v);
    ASSERT_EQ(items.size(), 3U);
    for (const auto& i : items) {
        ASSERT_TRUE(i.has_value());
    }
    EXPECT_EQ(items.at(0)->opcode, 1);
    EXPECT_EQ(items.at(0)->offset, 0U);
    EXPECT_EQ(items.at(1)->opcode, 150);
    EXPECT_EQ(items.at(1)->offset, 24U);
    EXPECT_EQ(items.at(1)->bytes.size(), 16U);
    EXPECT_EQ(items.at(2)->opcode, 201);
    EXPECT_EQ(items.at(2)->offset, 40U);

    EXPECT_EQ(*items.at(0)->decode_as<IgControl>(), control);
    EXPECT_EQ(*items.at(2)->decode_as<UserPacket>(), UserPacket{0xBEEF});
    const auto wrong = items.at(1)->decode_as<IgControl>();
    ASSERT_FALSE(wrong.has_value());
    EXPECT_EQ(wrong.error().code, ErrorCode::InvalidFieldValue);
    EXPECT_EQ(wrong.error().offset, 24U);
    EXPECT_EQ(wrong.error().opcode, 150);
}

TEST(MessageView, DecodeErrorsGetMessageOffset) {
    Bytes m = encoded(control);
    Bytes bad = encoded(control); // a second opcode-1 packet that is corrupt
    bad.at(2) = std::byte{4};
    append(m, bad);
    const auto v = MessageView::parse(m);
    ASSERT_TRUE(v.has_value());
    const auto items = collect(*v);
    ASSERT_EQ(items.size(), 2U);
    const auto r = items.at(1)->decode_as<IgControl>();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::UnsupportedVersion);
    EXPECT_EQ(r.error().offset, 24U);
}

TEST(MessageView, BuilderOutputRoundTrips) {
    StaticMessageBuilder<64> b(Direction::HostToIg);
    ASSERT_TRUE(b.add(control).has_value());
    ASSERT_TRUE(b.add(UserPacket{3}).has_value());
    const auto v = MessageView::parse(b.bytes());
    ASSERT_TRUE(v.has_value());
    const auto items = collect(*v);
    ASSERT_EQ(items.size(), 2U);
    EXPECT_EQ(*items.at(0)->decode_as<IgControl>(), control);
}

TEST(MessageView, IgToHostStartsWithStartOfFrame) {
    Bytes m = encoded(sof);
    append(m, opaque(200, 8));
    const auto v = MessageView::parse(m);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->direction(), Direction::IgToHost);
    const auto items = collect(*v);
    ASSERT_EQ(items.size(), 2U);
    EXPECT_EQ(*items.at(0)->decode_as<StartOfFrame>(), sof);
}

TEST(MessageView, ForeignByteOrderParsesAndDecodes) {
    Bytes m = host_message();
    swap_frame_fields(m);
    const auto v = MessageView::parse(m);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->byte_order(), ByteOrder::Swapped);
    const auto items = collect(*v);
    ASSERT_EQ(items.size(), 3U);
    EXPECT_EQ(*items.at(0)->decode_as<IgControl>(), control);
}

TEST(MessageView, ForeignIgMessage) {
    Bytes m = encoded(sof);
    swap_frame_fields(m);
    const auto v = MessageView::parse(m);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->byte_order(), ByteOrder::Swapped);
    EXPECT_EQ(*collect(*v).at(0)->decode_as<StartOfFrame>(), sof);
}

TEST(MessageView, OlderPeerVersionIsReported) {
    const Bytes m = frame_packet(1, 16, 0);
    const auto v = MessageView::parse(m);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->version(), (version_of(3, 0)));
    EXPECT_EQ(collect(*v).size(), 1U);
    const auto ig = MessageView::parse(frame_packet(101, 16, 1));
    ASSERT_TRUE(ig.has_value());
    EXPECT_EQ(ig->version(), (version_of(3, 1)));
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void expect_single_error_then_end(const Bytes& m, ErrorCode code, std::size_t offset,
                                  std::uint8_t opcode) {
    const auto v = MessageView::parse(m);
    ASSERT_TRUE(v.has_value());
    const auto items = collect(*v);
    ASSERT_EQ(items.size(), 2U);
    ASSERT_TRUE(items.at(0).has_value());
    ASSERT_FALSE(items.at(1).has_value());
    EXPECT_EQ(items.at(1).error().code, code);
    EXPECT_EQ(items.at(1).error().offset, offset);
    EXPECT_EQ(items.at(1).error().opcode, opcode);
}

TEST(MessageView, ZeroSizePacket) {
    Bytes m = encoded(control);
    append(m, opaque(150, 16));
    m.at(25) = std::byte{0};
    append(m, encoded(UserPacket{}));
    expect_single_error_then_end(m, ErrorCode::InvalidPacketSize, 24, 150);
}

TEST(MessageView, SizeNotMultipleOfEight) {
    Bytes m = encoded(control);
    append(m, opaque(150, 16));
    m.at(25) = std::byte{12};
    expect_single_error_then_end(m, ErrorCode::InvalidPacketSize, 24, 150);
}

TEST(MessageView, PacketOverrunsMessage) {
    Bytes m = encoded(control);
    append(m, opaque(150, 8));
    m.at(25) = std::byte{16};
    expect_single_error_then_end(m, ErrorCode::Truncated, 24, 150);
}

TEST(MessageView, TrailingSingleByte) {
    Bytes m = encoded(control);
    m.push_back(std::byte{0x42});
    expect_single_error_then_end(m, ErrorCode::Truncated, 24, 0);
}

TEST(MessageView, ErrorIsYieldedOnceAndIteratorsAreMultipass) {
    Bytes m = encoded(control);
    m.push_back(std::byte{0x42});
    const auto v = MessageView::parse(m);
    ASSERT_TRUE(v.has_value());
    auto it = v->begin();
    const auto copy = it;
    ++it;
    EXPECT_EQ(copy, v->begin());
    EXPECT_NE(it, v->end());
    const auto post = it++;
    EXPECT_FALSE((*post).has_value());
    EXPECT_EQ(it, v->end());
    EXPECT_TRUE((*copy).has_value()); // the older copy still reads the first packet
    EXPECT_EQ(MessageView::iterator{}, MessageView::iterator{});
}

TEST(MessageView, SingleFramePacketOnly) {
    const Bytes m = encoded(control);
    const auto v = MessageView::parse(m);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(collect(*v).size(), 1U);
}

TEST(MessageView, ShortDatagrams) {
    const Bytes empty;
    EXPECT_EQ(MessageView::parse(empty).error().code, ErrorCode::Truncated);
    const Bytes m = encoded(control);
    EXPECT_EQ(MessageView::parse(std::span<const std::byte>{m}.first(15)).error().code,
              ErrorCode::Truncated);
    // The first packet says 24 bytes but only 16 are present.
    const auto r = MessageView::parse(std::span<const std::byte>{m}.first(16));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::Truncated);
    EXPECT_EQ(r.error().opcode, 1);
}

TEST(MessageView, FirstPacketMustBeFramePacket) {
    Bytes m = host_message();
    m.at(0) = std::byte{5};
    const auto r = MessageView::parse(m);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::UnexpectedFirstPacket);
    EXPECT_EQ(r.error().opcode, 5);
    // A user packet first, whatever its size.
    EXPECT_EQ(MessageView::parse(opaque(201, 16)).error().code, ErrorCode::UnexpectedFirstPacket);
}

TEST(MessageView, VersionAndMagicChecks) {
    Bytes m = host_message();
    m.at(2) = std::byte{2};
    EXPECT_EQ(MessageView::parse(m).error().code, ErrorCode::UnsupportedVersion);
    m = host_message();
    m.at(6) = std::byte{0};
    m.at(7) = std::byte{0};
    const auto r = MessageView::parse(m);
    EXPECT_EQ(r.error().code, ErrorCode::InvalidByteSwapMagic);
    EXPECT_EQ(r.error().opcode, 1);
}

TEST(MessageView, FirstPacketIsFullyValidated) {
    EXPECT_EQ(MessageView::parse(frame_packet(1, 16, 3)).error().code,
              ErrorCode::InvalidPacketSize);
    Bytes m = host_message();
    m.at(1) = std::byte{12}; // not a multiple of 8
    EXPECT_EQ(MessageView::parse(m).error().code, ErrorCode::InvalidPacketSize);
}

} // namespace
