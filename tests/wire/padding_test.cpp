#include "cigi/wire/padding.hpp"

#include <gtest/gtest.h>

namespace {

using cigi::wire::is_valid_packet_size;
using cigi::wire::max_packet_size;
using cigi::wire::pad_to_8;

static_assert(pad_to_8(0) == 0);
static_assert(pad_to_8(1) == 8);
static_assert(pad_to_8(7) == 8);
static_assert(pad_to_8(8) == 8);
static_assert(pad_to_8(9) == 16);
static_assert(pad_to_8(245) == 248);
static_assert(max_packet_size == 248);
static_assert(max_packet_size % 8 == 0 && max_packet_size <= 255 && max_packet_size + 8 > 255);
static_assert(!is_valid_packet_size(0));
static_assert(!is_valid_packet_size(4));
static_assert(is_valid_packet_size(8));
static_assert(!is_valid_packet_size(12));
static_assert(is_valid_packet_size(248));
static_assert(!is_valid_packet_size(256));

TEST(Padding, PadTo8Exhaustive) {
    for (std::size_t n = 0; n < 1000; ++n) {
        const auto p = pad_to_8(n);
        EXPECT_EQ(p % 8, 0U);
        EXPECT_GE(p, n);
        EXPECT_LT(p - n, 8U);
    }
}

TEST(Padding, ValidPacketSize) {
    for (std::size_t n = 0; n < 600; ++n) {
        EXPECT_EQ(is_valid_packet_size(n), n >= 8 && n <= 248 && n % 8 == 0) << n;
    }
}

} // namespace
