#include "../support/packet_checks.hpp"
#include "cigi/packets/component_control.hpp"

#include <array>
#include <bit>
#include <gtest/gtest.h>
#include <limits>
#include <random>

namespace {

using namespace cigi;
using namespace cigi::test;
using wire::ByteOrder;

constexpr ComponentControl sample{
    .component_id = 0x0102,
    .instance_id = 0x0304,
    .component_class = ComponentClass::SymbolSurface,
    .component_state = 0x55,
    .data_1 = 0x11223344,
    .data_2 = 0x55667788,
    .data_3 = 0x99AABBCC,
    .data_4 = 0xDDEEFF00,
    .data_5 = 0x01020304,
    .data_6 = 0xA0B0C0D0,
};

// Hand-derived from ICD 4.1.4 (Fig. 47). Byte 6: class 14 (Symbol Surface) = 0x0E; byte 7: state.
// Each data word is byte-swapped as one u32.
constexpr auto golden_le = bytes_of(0x04, 0x20, 0x02, 0x01, 0x04, 0x03, 0x0E, 0x55, //
                                    0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55, //
                                    0xCC, 0xBB, 0xAA, 0x99, 0x00, 0xFF, 0xEE, 0xDD, //
                                    0x04, 0x03, 0x02, 0x01, 0xD0, 0xC0, 0xB0, 0xA0);
constexpr auto golden_be = bytes_of(0x04, 0x20, 0x01, 0x02, 0x03, 0x04, 0x0E, 0x55, //
                                    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, //
                                    0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, //
                                    0x01, 0x02, 0x03, 0x04, 0xA0, 0xB0, 0xC0, 0xD0);

constexpr bool constexpr_round_trip() {
    std::array<std::byte, 32> buf{};
    encode(sample, buf);
    const auto r = decode(std::type_identity<ComponentControl>{}, buf, ByteOrder::Native);
    return r.has_value() && *r == sample && buf == native_of(golden_le, golden_be);
}
static_assert(constexpr_round_trip());
static_assert(Packet<ComponentControl>);
static_assert(ComponentControl::wire_size == 32 && ComponentControl::opcode == 4);
static_assert(ComponentControl::direction == Direction::HostToIg);

TEST(ComponentControl, GoldenBytesBothOrders) {
    expect_golden(sample, golden_le, golden_be);
}

TEST(ComponentControl, ClassUsesLowSixBitsAndReservedBitsAreIgnored) {
    Bytes b(32);
    encode(ComponentControl{.component_class = ComponentClass::Symbol}, b);
    EXPECT_EQ(b.at(6), std::byte{0x0F});
    b.at(6) = std::byte{0xCF}; // reserved bits 7-6 set
    const auto r = decode(std::type_identity<ComponentControl>{}, b, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->component_class, ComponentClass::Symbol);
}

TEST(ComponentControl, StandardDecodeErrors) {
    expect_standard_decode_errors(sample);
}

TEST(ComponentControl, ValidateRejectsReservedClasses) {
    EXPECT_TRUE(validate(ComponentControl{}).has_value());
    EXPECT_TRUE(validate(sample).has_value());
    EXPECT_TRUE(validate(ComponentControl{.component_class = ComponentClass::Symbol}).has_value());
    for (const unsigned c : {16U, 40U, 63U}) {
        const auto r =
            validate(ComponentControl{.component_class = static_cast<ComponentClass>(c)});
        ASSERT_FALSE(r.has_value());
        EXPECT_EQ(r.error(),
                  (Error{.code = ErrorCode::InvalidFieldValue, .offset = 6, .opcode = 4}));
    }
    // Reserved class values survive decode (tolerant decode).
    Bytes b(32);
    encode(ComponentControl{.component_class = static_cast<ComponentClass>(63)}, b);
    const auto r = decode(std::type_identity<ComponentControl>{}, b, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->component_class, static_cast<ComponentClass>(63));
}

TEST(ComponentControl, EnumTraits) {
    EXPECT_TRUE(EnumTraits<ComponentClass>::is_defined(ComponentClass::Entity));
    EXPECT_TRUE(EnumTraits<ComponentClass>::is_defined(ComponentClass::Symbol));
    EXPECT_FALSE(EnumTraits<ComponentClass>::is_defined(static_cast<ComponentClass>(16)));
}

TEST(ComponentControl, DataAccessorsMapToTheSixWords) {
    ComponentControl p;
    ComponentData d;
    for (std::size_t i = 0; i < ComponentData::word_count; ++i) {
        d.set_u32(i, static_cast<std::uint32_t>(i + 1));
    }
    p.set_data(d);
    EXPECT_EQ(p.data_1, 1U);
    EXPECT_EQ(p.data_6, 6U);
    EXPECT_EQ(p.data(), d);
}

// The ICD's worked example (Figs. 44-46): i = 1, j = 120, k = 3600, m = 100.0. On the wire
// (little-endian host) words 1-4 are 0x00010078, 0x00000E10, 0x00000000, 0x40590000.
TEST(ComponentControl, IcdWorkedExampleBytes) {
    ComponentControl p;
    ComponentData d;
    d.set_u16x2(0, 1, 120);
    d.set_u32(1, 3600);
    d.set_f64(2, 100.0);
    p.set_data(d);
    Bytes b(32);
    encode(p, b);
    const auto w = [&](std::size_t word) {
        return wire::load<std::uint32_t>(b, 8 + (4 * word), ByteOrder::Native);
    };
    EXPECT_EQ(w(0), 0x00010078U);
    EXPECT_EQ(w(1), 0x00000E10U);
    EXPECT_EQ(w(2), 0x00000000U);
    EXPECT_EQ(w(3), 0x40590000U);
    EXPECT_EQ(w(4), 0U);
    EXPECT_EQ(w(5), 0U);

    // A peer of the other byte order receives identical values.
    const auto r = decode(std::type_identity<ComponentControl>{}, foreign_copy<ComponentControl>(b),
                          ByteOrder::Swapped);
    ASSERT_TRUE(r.has_value());
    const auto got = r->data();
    EXPECT_EQ(got.u16(0, 0), 1U);
    EXPECT_EQ(got.u16(0, 1), 120U);
    EXPECT_EQ(got.u32(1), 3600U);
    EXPECT_EQ(got.f64(2), 100.0);
}

TEST(ComponentData, ScalarsRoundTrip) {
    ComponentData d;
    d.set_u32(0, 0xDEADBEEF);
    d.set_i32(1, -123456);
    d.set_f32(2, -1.5F);
    d.set_u64(3, 0x0123456789ABCDEFULL);
    EXPECT_EQ(d.u32(0), 0xDEADBEEFU);
    EXPECT_EQ(d.i32(1), -123456);
    EXPECT_EQ(d.f32(2), -1.5F);
    EXPECT_EQ(d.u64(3), 0x0123456789ABCDEFULL);
    EXPECT_EQ(d.words().at(3), 0x89ABCDEFU); // low word first (see header)
    EXPECT_EQ(d.words().at(4), 0x01234567U);
    d.set_i64(0, std::numeric_limits<std::int64_t>::min());
    EXPECT_EQ(d.i64(0), std::numeric_limits<std::int64_t>::min());
    d.set_f64(4, 6.02214076e23);
    EXPECT_EQ(d.f64(4), 6.02214076e23);
}

TEST(ComponentData, PackedSmallValues) {
    ComponentData d;
    d.set_u16x2(5, 0xABCD, 0x1234);
    EXPECT_EQ(d.u32(5), 0xABCD1234U);
    EXPECT_EQ(d.u16(5, 0), 0xABCD);
    EXPECT_EQ(d.u16(5, 1), 0x1234);
    d.set_u8x4(0, 0x01, 0x02, 0x03, 0x04);
    EXPECT_EQ(d.u32(0), 0x01020304U);
    for (std::size_t s = 0; s < 4; ++s) {
        EXPECT_EQ(d.u8(0, s), s + 1);
    }
}

TEST(ComponentData, ConstexprAndWordsAccess) {
    constexpr auto d = [] {
        ComponentData c;
        c.set_f32(0, 0.5F);
        c.set_words({1, 2, 3, 4, 5, 6});
        c.set_f64(1, 2.0);
        return c;
    }();
    static_assert(d.u32(0) == 1 && d.f64(1) == 2.0);
    EXPECT_EQ(d.words().at(5), 6U);
}

TEST(ComponentData, ValuesSurviveBothByteOrdersOnTheWire) {
    // Random mixed packing through the packet in both orders: the classic corruption bug.
    // NOLINTNEXTLINE(bugprone-random-generator-seed): deterministic on purpose
    std::mt19937_64 rng(0xC0DEDA7AU);
    for (int i = 0; i < 2000; ++i) {
        const auto u16a = static_cast<std::uint16_t>(rng());
        const auto u16b = static_cast<std::uint16_t>(rng());
        const auto u8s = static_cast<std::uint32_t>(rng());
        const auto i32 = static_cast<std::int32_t>(rng());
        const auto f32 = static_cast<float>(static_cast<std::int32_t>(rng())) / 1024.0F;
        const auto u64 = static_cast<std::uint64_t>(rng());
        ComponentData d;
        d.set_u16x2(0, u16a, u16b);
        d.set_u8x4(1, static_cast<std::uint8_t>(u8s >> 24U), static_cast<std::uint8_t>(u8s >> 16U),
                   static_cast<std::uint8_t>(u8s >> 8U), static_cast<std::uint8_t>(u8s));
        d.set_i32(2, i32);
        d.set_f32(3, f32);
        d.set_u64(4, u64);
        ComponentControl p{
            .component_id = static_cast<std::uint16_t>(rng()),
            .instance_id = static_cast<std::uint16_t>(rng()),
            .component_class = static_cast<ComponentClass>(rng() & 15U),
            .component_state = static_cast<std::uint8_t>(rng()),
        };
        p.set_data(d);
        expect_round_trip_both_orders(p);

        Bytes b(32);
        encode(p, b);
        const auto r = decode(std::type_identity<ComponentControl>{},
                              foreign_copy<ComponentControl>(b), ByteOrder::Swapped);
        ASSERT_TRUE(r.has_value());
        const auto g = r->data();
        EXPECT_EQ(g.u16(0, 0), u16a);
        EXPECT_EQ(g.u16(0, 1), u16b);
        EXPECT_EQ(g.u8(1, 0), u8s >> 24U);
        EXPECT_EQ(g.u8(1, 3), u8s & 0xFFU);
        EXPECT_EQ(g.i32(2), i32);
        EXPECT_EQ(g.f32(3), f32);
        EXPECT_EQ(g.u64(4), u64);
    }
}

} // namespace
