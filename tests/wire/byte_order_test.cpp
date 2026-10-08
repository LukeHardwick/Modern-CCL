#include "cigi/wire/byte_order.hpp"

#include <gtest/gtest.h>

namespace {

using cigi::ErrorCode;
using cigi::wire::ByteOrder;
using cigi::wire::detect_byte_order;

static_assert(detect_byte_order(0x8000).value() == ByteOrder::Native);
static_assert(detect_byte_order(0x0080).value() == ByteOrder::Swapped);
static_assert(!detect_byte_order(0x0000).has_value());
static_assert(cigi::wire::byte_swap_magic == 0x8000);

TEST(ByteOrder, DetectNative) {
    const auto r = detect_byte_order(0x8000);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, ByteOrder::Native);
}

TEST(ByteOrder, DetectSwapped) {
    const auto r = detect_byte_order(0x0080);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, ByteOrder::Swapped);
}

TEST(ByteOrder, RejectsOthers) {
    for (const std::uint16_t bad :
         std::initializer_list<std::uint16_t>{0x0000, 0xFFFF, 0x8080, 0x0001, 0x8001}) {
        const auto r = detect_byte_order(bad);
        ASSERT_FALSE(r.has_value()) << bad;
        EXPECT_EQ(r.error().code, ErrorCode::InvalidByteSwapMagic);
    }
}

} // namespace
