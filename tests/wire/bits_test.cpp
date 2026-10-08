#include "cigi/wire/bits.hpp"

#include <gtest/gtest.h>

namespace {

using cigi::wire::BitField;
using cigi::wire::get_bits;
using cigi::wire::get_flag;
using cigi::wire::set_bits;
using cigi::wire::set_flag;

static_assert(get_bits(0b1011'0100, 2, 3) == 0b101);
static_assert(get_bits(0xFF, 0, 8) == 0xFF);
static_assert(set_bits(0x00, 4, 4, 0xA) == 0xA0);
static_assert(set_bits(0xFF, 4, 4, 0x0) == 0x0F);
static_assert(set_bits(0xFF, 0, 8, 0x12) == 0x12);
static_assert(set_bits(0x00, 1, 2, 0xFF) == 0b110); // value masked to width
static_assert(get_flag(0x80, 7) && !get_flag(0x80, 6));
static_assert(set_flag(0x00, 7, true) == 0x80);
static_assert(set_flag(0xFF, 0, false) == 0xFE);

using Mode = BitField<2, 2>;
static_assert(Mode::get(0b0000'1100) == 3);
static_assert(Mode::set(0xFF, 1) == 0b1111'0111);
static_assert(BitField<0, 8>::get(0xA5) == 0xA5);
static_assert(BitField<7, 1>::set(0x00, 1) == 0x80);

TEST(Bits, ExhaustiveGetSet) {
    for (const unsigned byte : {0x00U, 0xFFU, 0xA5U, 0x5AU, 0x81U}) {
        for (unsigned lsb = 0; lsb < 8; ++lsb) {
            for (unsigned width = 1; lsb + width <= 8; ++width) {
                const auto b = static_cast<std::uint8_t>(byte);
                const unsigned mask = ((1U << width) - 1U) << lsb;
                EXPECT_EQ(get_bits(b, lsb, width), (byte & mask) >> lsb);
                for (unsigned v = 0; v < 256; ++v) {
                    const auto r = set_bits(b, lsb, width, static_cast<std::uint8_t>(v));
                    EXPECT_EQ(r & ~mask, byte & ~mask) << "other bits preserved";
                    EXPECT_EQ(get_bits(r, lsb, width), v & ((1U << width) - 1U));
                }
            }
        }
    }
}

TEST(Bits, Flags) {
    std::uint8_t b = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
        b = set_flag(b, bit, true);
        EXPECT_TRUE(get_flag(b, bit));
    }
    EXPECT_EQ(b, 0xFF);
    b = set_flag(b, 3, false);
    EXPECT_EQ(b, 0xF7);
    EXPECT_FALSE(get_flag(b, 3));
}

TEST(Bits, BitFieldRuntime) {
    using F = BitField<4, 3>;
    EXPECT_EQ(F::get(0b0101'0000), 5);
    EXPECT_EQ(F::set(0x8F, 0b111), 0xFF);
}

} // namespace
