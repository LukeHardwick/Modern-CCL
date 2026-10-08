#include "../support/bytes.hpp"
#include "cigi/message/builder.hpp"
#include "cigi/packets/ig_control.hpp"
#include "cigi/packets/start_of_frame.hpp"

#include <array>
#include <gtest/gtest.h>
#include <type_traits>

namespace {

using namespace cigi;
using namespace cigi::test;

constexpr IgControl control{.database_number = 1, .host_frame_number = 9};
constexpr StartOfFrame sof{.ig_frame_number = 4};

constexpr bool constexpr_build() {
    std::array<std::byte, 32> storage{};
    MessageBuilder b(Direction::HostToIg, storage);
    return b.add(control).has_value() && b.add(UserPacket{7}).has_value() && b.size() == 32 &&
           b.capacity() == 32 && b.bytes().size() == 32;
}
static_assert(constexpr_build());
static_assert(!std::is_copy_constructible_v<StaticMessageBuilder<8>>);
static_assert(!std::is_move_constructible_v<StaticMessageBuilder<8>>);

TEST(MessageBuilder, HostMessageStartsWithIgControl) {
    std::array<std::byte, 64> storage{};
    MessageBuilder b(Direction::HostToIg, storage);
    EXPECT_EQ(b.direction(), Direction::HostToIg);
    EXPECT_EQ(b.size(), 0U);
    EXPECT_EQ(b.capacity(), 64U);
    ASSERT_TRUE(b.add(control).has_value());
    EXPECT_EQ(b.size(), 24U);
    ASSERT_TRUE(b.add(UserPacket{0x1234}).has_value());
    ASSERT_TRUE(b.add(HostOnlyPacket{}).has_value());
    EXPECT_EQ(b.size(), 40U);

    std::array<std::byte, 24> expected{};
    encode(control, expected);
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), b.bytes().begin()));
    EXPECT_EQ(b.bytes().subspan(24).front(), std::byte{201});
    EXPECT_EQ(b.bytes().subspan(32).front(), std::byte{150});
}

TEST(MessageBuilder, IgMessageStartsWithStartOfFrame) {
    std::array<std::byte, 24> storage{};
    MessageBuilder b(Direction::IgToHost, storage);
    ASSERT_TRUE(b.add(sof).has_value());
    EXPECT_EQ(b.size(), 24U);
}

TEST(MessageBuilder, FirstPacketMustBeFramePacket) {
    std::array<std::byte, 64> storage{};
    MessageBuilder b(Direction::HostToIg, storage);
    const auto r = b.add(UserPacket{});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::UnexpectedFirstPacket);
    EXPECT_EQ(r.error().opcode, 201);
    EXPECT_EQ(b.size(), 0U);
}

TEST(MessageBuilder, DuplicateFramePacketRejected) {
    std::array<std::byte, 64> storage{};
    MessageBuilder b(Direction::HostToIg, storage);
    ASSERT_TRUE(b.add(control).has_value());
    const auto r = b.add(control);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::DuplicateFramePacket);
    EXPECT_EQ(r.error().offset, 24U);
    EXPECT_EQ(b.size(), 24U);
}

TEST(MessageBuilder, WrongDirectionRejected) {
    std::array<std::byte, 64> storage{};
    MessageBuilder host(Direction::HostToIg, storage);
    EXPECT_EQ(host.add(sof).error().code, ErrorCode::WrongDirection);
    MessageBuilder ig(Direction::IgToHost, storage);
    EXPECT_EQ(ig.add(control).error().code, ErrorCode::WrongDirection);
    ASSERT_TRUE(ig.add(sof).has_value());
    EXPECT_EQ(ig.add(HostOnlyPacket{}).error().code, ErrorCode::WrongDirection);
    EXPECT_TRUE(ig.add(UserPacket{}).has_value()); // Direction::Both is always allowed
    EXPECT_EQ(ig.size(), 32U);
}

TEST(MessageBuilder, BufferTooSmallLeavesBuilderUnchanged) {
    std::array<std::byte, 30> storage{};
    MessageBuilder b(Direction::HostToIg, storage);
    ASSERT_TRUE(b.add(control).has_value());
    const Bytes before = to_vec(b.bytes());
    const auto r = b.add(UserPacket{1}); // needs 8, only 6 left
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::BufferTooSmall);
    EXPECT_EQ(r.error().offset, 24U);
    EXPECT_EQ(r.error().opcode, 201);
    EXPECT_EQ(b.size(), 24U);
    EXPECT_EQ(to_vec(b.bytes()), before);

    std::array<std::byte, 23> tiny{};
    MessageBuilder t(Direction::HostToIg, tiny);
    EXPECT_EQ(t.add(control).error().code, ErrorCode::BufferTooSmall);
    EXPECT_EQ(t.size(), 0U);
}

TEST(MessageBuilder, CapacityExactlyFitsAndClearReuses) {
    std::array<std::byte, 32> storage{};
    MessageBuilder b(Direction::HostToIg, storage);
    ASSERT_TRUE(b.add(control).has_value());
    ASSERT_TRUE(b.add(UserPacket{}).has_value());
    EXPECT_EQ(b.size(), b.capacity());
    EXPECT_EQ(b.add(UserPacket{}).error().code, ErrorCode::BufferTooSmall);
    b.clear();
    EXPECT_EQ(b.size(), 0U);
    EXPECT_TRUE(b.bytes().empty());
    ASSERT_TRUE(b.add(control).has_value()); // the frame packet is allowed again
    EXPECT_EQ(b.size(), 24U);
}

TEST(StaticMessageBuilder, OwnsStorage) {
    StaticMessageBuilder<40> b(Direction::HostToIg);
    EXPECT_EQ(b.capacity(), 40U);
    EXPECT_EQ(b.direction(), Direction::HostToIg);
    ASSERT_TRUE(b.add(control).has_value());
    EXPECT_EQ(b.add(control).error().code, ErrorCode::DuplicateFramePacket);
    EXPECT_EQ(b.size(), 24U);
    EXPECT_EQ(b.bytes().size(), 24U);
    b.clear();
    EXPECT_EQ(b.size(), 0U);
    EXPECT_EQ(b.add(sof).error().code, ErrorCode::WrongDirection);
}

} // namespace
