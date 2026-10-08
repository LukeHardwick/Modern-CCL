#include "../support/bytes.hpp"
#include "cigi/message/builder.hpp"
#include "cigi/packet.hpp"
#include "cigi/packets/ig_control.hpp"
#include "cigi/wire/fields.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <tuple>

// Test-only enums live in a named namespace so EnumTraits can be specialised for them.
namespace cigi::test {

enum class FtColor : std::uint16_t { Red = 1, Green = 2, Blue = 3 };
enum class FtMode : std::uint8_t { A = 0, B = 1, C = 2, D = 3, E = 4, F = 5 };

} // namespace cigi::test

namespace cigi {

template <> struct EnumTraits<test::FtColor> {
    static constexpr bool is_defined(test::FtColor c) noexcept {
        return enum_between(c, test::FtColor::Red, test::FtColor::Blue);
    }
};
template <> struct EnumTraits<test::FtMode> {
    static constexpr bool is_defined(test::FtMode m) noexcept {
        return enum_between(m, test::FtMode::A, test::FtMode::F);
    }
};

} // namespace cigi

namespace cigi::test {
namespace {

using wire::ByteOrder;
using Millis = std::chrono::duration<std::int32_t, std::milli>;

// --- A small packet checked against hand-derived bytes ----------------------------------------

struct FtGolden {
    static constexpr std::uint8_t opcode = 210;
    static constexpr Direction direction = Direction::Both;
    static constexpr std::size_t wire_size = 24;

    std::uint16_t u = 0;
    std::int16_t s = 0;
    FtMode mode = FtMode::A;
    bool on = false;
    std::uint8_t nibble = 0;
    std::uint32_t w = 0;
    double d = 0;

    static constexpr auto fields = std::tuple{
        wire::scalar(&FtGolden::u, 2),          wire::scalar(&FtGolden::s, 4),
        wire::bits(&FtGolden::mode, 6, 1, 3),   wire::flag(&FtGolden::on, 6, 0),
        wire::bits(&FtGolden::nibble, 6, 4, 4), wire::scalar(&FtGolden::w, 8),
        wire::scalar(&FtGolden::d, 16),
    };
    friend constexpr bool operator==(const FtGolden&, const FtGolden&) noexcept = default;
};
static_assert(wire::layout_ok<FtGolden>());
static_assert(TableDrivenPacket<FtGolden> && Packet<FtGolden>);

constexpr FtGolden golden_value{
    .u = 0x0102, .s = -2, .mode = FtMode::F, .on = true, .nibble = 0xA, .w = 0x0A0B0C0D, .d = 1.0};

// Byte 6: nibble 0xA<<4 | mode 5<<1 (0x0A) | flag 1 = 0xAB. 1.0 = 0x3FF0000000000000.
constexpr auto golden_le = bytes_of(210, 24, 0x02, 0x01, 0xFE, 0xFF, 0xAB, 0, //
                                    0x0D, 0x0C, 0x0B, 0x0A, 0, 0, 0, 0,       //
                                    0, 0, 0, 0, 0, 0, 0xF0, 0x3F);
constexpr auto golden_be = bytes_of(210, 24, 0x01, 0x02, 0xFF, 0xFE, 0xAB, 0, //
                                    0x0A, 0x0B, 0x0C, 0x0D, 0, 0, 0, 0,       //
                                    0x3F, 0xF0, 0, 0, 0, 0, 0, 0);

// --- A packet using every scalar kind and several bit fields ----------------------------------

struct FtAll {
    static constexpr std::uint8_t opcode = 211;
    static constexpr Direction direction = Direction::Both;
    static constexpr std::size_t wire_size = 64;

    std::uint8_t u8 = 0;
    std::int8_t i8 = 0;
    std::uint16_t u16 = 0;
    std::int16_t i16 = 0;
    std::uint32_t u32 = 0;
    std::int32_t i32 = 0;
    std::uint64_t u64 = 0;
    std::int64_t i64 = 0;
    float f32 = 0;
    FtColor color = FtColor::Red;
    bool byte_flag = false;
    double f64 = 0;
    Millis millis{};
    Timestamp stamp{};
    bool lone_flag = false;
    FtMode mode = FtMode::A;
    std::uint8_t nibble = 0;
    bool top_flag = false;
    std::uint16_t small = 0;

    static constexpr auto fields = std::tuple{
        wire::scalar(&FtAll::u8, 2),
        wire::scalar(&FtAll::i8, 3),
        wire::scalar(&FtAll::u16, 4),
        wire::scalar(&FtAll::i16, 6),
        wire::scalar(&FtAll::u32, 8),
        wire::scalar(&FtAll::i32, 12),
        wire::scalar(&FtAll::u64, 16),
        wire::scalar(&FtAll::i64, 24),
        wire::scalar(&FtAll::f32, 32),
        wire::scalar(&FtAll::color, 36),
        wire::scalar<std::uint8_t>(&FtAll::byte_flag, 38),
        wire::scalar(&FtAll::f64, 40),
        wire::scalar(&FtAll::millis, 48),
        wire::scalar(&FtAll::stamp, 52),
        wire::flag(&FtAll::lone_flag, 56, 0),
        wire::bits(&FtAll::mode, 56, 1, 3),
        wire::bits(&FtAll::nibble, 56, 4, 4),
        wire::flag(&FtAll::top_flag, 57, 7),
        wire::bits(&FtAll::small, 57, 0, 3),
    };
    friend constexpr bool operator==(const FtAll&, const FtAll&) noexcept = default;
};
static_assert(wire::layout_ok<FtAll>());

constexpr FtAll all_value{.u8 = 0xFE,
                          .i8 = -3,
                          .u16 = 0xBEEF,
                          .i16 = -12345,
                          .u32 = 0xDEADBEEF,
                          .i32 = -123456789,
                          .u64 = 0x0123456789ABCDEFULL,
                          .i64 = -1234567890123LL,
                          .f32 = -2.5F,
                          .color = FtColor::Blue,
                          .byte_flag = true,
                          .f64 = 3.141592653589793,
                          .millis = Millis{-77},
                          .stamp = Timestamp{4000000000U},
                          .lone_flag = true,
                          .mode = FtMode::E,
                          .nibble = 0x9,
                          .top_flag = true,
                          .small = 5};

constexpr bool all_round_trip() {
    std::array<std::byte, FtAll::wire_size> buf{};
    cigi::encode(all_value, buf);
    const auto r = cigi::decode(std::type_identity<FtAll>{}, buf, ByteOrder::Native);
    return r.has_value() && *r == all_value;
}
static_assert(all_round_trip());

constexpr bool golden_round_trip() {
    std::array<std::byte, 24> buf{};
    cigi::encode(golden_value, buf);
    const auto r = cigi::decode(std::type_identity<FtGolden>{}, buf, ByteOrder::Native);
    return r.has_value() && *r == golden_value && buf == native_of(golden_le, golden_be);
}
static_assert(golden_round_trip());

// --- Traits ---------------------------------------------------------------------------------

static_assert(std::same_as<wire::WireRepr<double>::type, double>);
static_assert(std::same_as<wire::WireRepr<FtColor>::type, std::uint16_t>);
static_assert(std::same_as<wire::WireRepr<Timestamp>::type, std::uint32_t>);
static_assert(std::same_as<wire::WireRepr<Millis>::type, std::int32_t>);
template <class M>
concept HasWireRepr = requires { typename wire::WireRepr<M>::type; };
static_assert(HasWireRepr<double> && !HasWireRepr<bool> && !HasWireRepr<char> &&
              !HasWireRepr<long double>);
static_assert(!HasWireRepr<std::chrono::duration<char>>);
static_assert(wire::scalar(&FtAll::u64, 16).size() == 8);
static_assert(wire::scalar<std::uint8_t>(&FtAll::byte_flag, 38).size() == 1);
static_assert(wire::bits(&FtAll::mode, 56, 1, 3).byte_mask() == 0x0E);

// --- Encode / decode ------------------------------------------------------------------------

TEST(Fields, GoldenBytes) {
    std::array<std::byte, 24> buf{};
    buf.fill(std::byte{0xFF}); // encode must not depend on prior contents
    cigi::encode(golden_value, buf);
    EXPECT_EQ(buf, native_of(golden_le, golden_be));
    EXPECT_EQ(cigi::encoded_size(golden_value), 24U);
}

TEST(Fields, DecodeNativeAndForeignGolden) {
    const auto native = cigi::decode(std::type_identity<FtGolden>{},
                                     native_of(golden_le, golden_be), ByteOrder::Native);
    ASSERT_TRUE(native.has_value());
    EXPECT_EQ(*native, golden_value);
    const auto foreign = cigi::decode(std::type_identity<FtGolden>{},
                                      foreign_of(golden_le, golden_be), ByteOrder::Swapped);
    ASSERT_TRUE(foreign.has_value());
    EXPECT_EQ(*foreign, golden_value);
}

TEST(Fields, AllKindsRoundTripNative) {
    Bytes buf(FtAll::wire_size);
    cigi::encode(all_value, buf);
    EXPECT_EQ(buf.at(0), std::byte{211});
    EXPECT_EQ(buf.at(1), std::byte{64});
    EXPECT_EQ(buf.at(38), std::byte{1}); // bool stored in a full byte
    // Byte 56: nibble 9 | mode 4<<1 | flag 1. Byte 57: top flag 0x80 | small 5.
    EXPECT_EQ(buf.at(56), std::byte{0x99});
    EXPECT_EQ(buf.at(57), std::byte{0x85});
    const auto r = cigi::decode(std::type_identity<FtAll>{}, buf, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, all_value);
}

TEST(Fields, AllKindsDecodeForeignOrder) {
    // Fabricate what a foreign-endian peer sends: every multi-byte value stored swapped.
    Bytes b(FtAll::wire_size, std::byte{0});
    const std::span<std::byte> s{b};
    const auto sw = ByteOrder::Swapped;
    wire::store<std::uint8_t>(s, 0, 211);
    wire::store<std::uint8_t>(s, 1, 64);
    wire::store<std::uint8_t>(s, 2, all_value.u8);
    wire::store<std::int8_t>(s, 3, all_value.i8);
    wire::store<std::uint16_t>(s, 4, all_value.u16, sw);
    wire::store<std::int16_t>(s, 6, all_value.i16, sw);
    wire::store<std::uint32_t>(s, 8, all_value.u32, sw);
    wire::store<std::int32_t>(s, 12, all_value.i32, sw);
    wire::store<std::uint64_t>(s, 16, all_value.u64, sw);
    wire::store<std::int64_t>(s, 24, all_value.i64, sw);
    wire::store<float>(s, 32, all_value.f32, sw);
    wire::store<std::uint16_t>(s, 36, static_cast<std::uint16_t>(all_value.color), sw);
    wire::store<std::uint8_t>(s, 38, 1);
    wire::store<double>(s, 40, all_value.f64, sw);
    wire::store<std::int32_t>(s, 48, all_value.millis.count(), sw);
    wire::store<std::uint32_t>(s, 52, all_value.stamp.count(), sw);
    wire::store<std::uint8_t>(s, 56, 0x99);
    wire::store<std::uint8_t>(s, 57, 0x85);
    const auto r = cigi::decode(std::type_identity<FtAll>{}, b, sw);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, all_value);
}

TEST(Fields, ExtremeValuesSurvive) {
    FtAll p = all_value;
    p.u64 = std::numeric_limits<std::uint64_t>::max();
    p.i64 = std::numeric_limits<std::int64_t>::min();
    p.f64 = std::numeric_limits<double>::infinity();
    p.f32 = -0.0F;
    p.small = 7;
    p.nibble = 15;
    Bytes buf(FtAll::wire_size);
    cigi::encode(p, buf);
    const auto r = cigi::decode(std::type_identity<FtAll>{}, buf, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, p);
    EXPECT_TRUE(std::signbit(r->f32));
}

TEST(Fields, NanPayloadSurvivesBitExactly) {
    FtAll p;
    p.f64 = std::bit_cast<double>(0x7FF8000000001234ULL);
    Bytes buf(FtAll::wire_size);
    cigi::encode(p, buf);
    const auto r = cigi::decode(std::type_identity<FtAll>{}, buf, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::bit_cast<std::uint64_t>(r->f64), 0x7FF8000000001234ULL);
}

TEST(Fields, BitFieldsAreMaskedToTheirWidth) {
    FtAll p;
    p.small = 0xFFFF; // only 3 bits fit
    p.nibble = 0xFF;  // only 4 bits fit
    p.mode = static_cast<FtMode>(0xFF);
    Bytes buf(FtAll::wire_size);
    cigi::encode(p, buf);
    EXPECT_EQ(buf.at(56), std::byte{0xFE}); // nibble F, mode 7, flag 0
    EXPECT_EQ(buf.at(57), std::byte{0x07}); // neighbouring flag untouched
}

TEST(Fields, BoolStoredInAByteDecodesAnyNonZeroAsTrue) {
    Bytes buf(FtAll::wire_size);
    cigi::encode(FtAll{}, buf);
    buf.at(38) = std::byte{0x7F};
    const auto r = cigi::decode(std::type_identity<FtAll>{}, buf, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->byte_flag);
}

TEST(Fields, DecodeToleratesUndefinedEnumValues) {
    Bytes buf(FtAll::wire_size);
    cigi::encode(FtAll{}, buf);
    wire::store<std::uint16_t>(std::span<std::byte>{buf}, 36, 999);
    buf.at(56) = std::byte{0x0E}; // mode 7
    const auto r = cigi::decode(std::type_identity<FtAll>{}, buf, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(static_cast<std::uint16_t>(r->color), 999U);
    EXPECT_EQ(r->mode, static_cast<FtMode>(7));
    EXPECT_FALSE(cigi::validate(*r).has_value()); // ...but validate() flags it
}

TEST(Fields, ReservedBytesAndBitsAreIgnoredOnDecode) {
    Bytes buf = to_vec(native_of(golden_le, golden_be));
    buf.at(7) = std::byte{0xFF};
    for (std::size_t i = 12; i < 16; ++i) {
        buf.at(i) = std::byte{0xFF};
    }
    const auto r = cigi::decode(std::type_identity<FtGolden>{}, buf, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, golden_value);
}

TEST(Fields, GenericDecodeHeaderErrorsReportTheFoundOpcode) {
    Bytes buf = to_vec(native_of(golden_le, golden_be));
    const auto dec = [](const Bytes& b) {
        return cigi::decode(std::type_identity<FtGolden>{}, b, ByteOrder::Native);
    };
    Bytes wrong = buf;
    wrong.at(0) = std::byte{77};
    EXPECT_EQ(dec(wrong).error(), (Error{.code = ErrorCode::OpcodeMismatch, .opcode = 77}));
    Bytes odd = buf;
    odd.at(1) = std::byte{16};
    EXPECT_EQ(dec(odd).error(), (Error{.code = ErrorCode::InvalidPacketSize, .opcode = 210}));
    buf.pop_back();
    EXPECT_EQ(dec(buf).error().code, ErrorCode::InvalidPacketSize);
    EXPECT_EQ(dec({}).error(), (Error{.code = ErrorCode::Truncated}));
}

// --- Partial (short, legacy-style) packets --------------------------------------------------

TEST(Fields, DecodeFieldsReadsOnlyFieldsFullyInsideThePacket) {
    FtGolden p{.u = 7, .s = 8, .mode = FtMode::B, .on = false, .nibble = 3, .w = 9, .d = 4.5};
    const Bytes full = to_vec(native_of(golden_le, golden_be));

    // 10 bytes: u, s, the bit byte and w's first 2 bytes. w (8..11) straddles the end, d is gone.
    wire::decode_fields(p, std::span<const std::byte>{full}.first(10), ByteOrder::Native);
    EXPECT_EQ(p.u, 0x0102);
    EXPECT_EQ(p.s, -2);
    EXPECT_EQ(p.mode, FtMode::F);
    EXPECT_TRUE(p.on);
    EXPECT_EQ(p.nibble, 0xA);
    EXPECT_EQ(p.w, 9U);  // default kept
    EXPECT_EQ(p.d, 4.5); // default kept

    FtGolden q{.u = 7, .s = 8, .mode = FtMode::B, .on = false, .nibble = 3, .w = 9, .d = 4.5};
    wire::decode_fields(q, std::span<const std::byte>{full}.first(16), ByteOrder::Native);
    EXPECT_EQ(q.w, 0x0A0B0C0DU);
    EXPECT_EQ(q.d, 4.5);

    FtGolden r{.u = 7, .s = 8, .mode = FtMode::B, .on = false, .nibble = 3, .w = 9, .d = 4.5};
    wire::decode_fields(r, std::span<const std::byte>{full}.first(6), ByteOrder::Native);
    EXPECT_EQ(r.u, 0x0102);
    EXPECT_EQ(r.mode, FtMode::B); // the bit byte is at offset 6: not inside 6 bytes
    EXPECT_EQ(r.nibble, 3);

    FtGolden none;
    wire::decode_fields(none, std::span<const std::byte>{}, ByteOrder::Native);
    EXPECT_EQ(none, FtGolden{});
}

// --- Validation -----------------------------------------------------------------------------

struct FtChecked {
    static constexpr std::uint8_t opcode = 212;
    static constexpr Direction direction = Direction::Both;
    static constexpr std::size_t wire_size = 32;

    std::int16_t level = 0;
    FtColor color = FtColor::Red;
    double ratio = 0.5;
    Timestamp stamp{7};
    FtMode kind = FtMode::A;
    std::uint8_t counter = 1;
    std::uint32_t free_value = 0;
    bool plain = false;

    static constexpr auto fields = std::tuple{
        wire::scalar(&FtChecked::level, 2).range(-10, 10),
        wire::scalar(&FtChecked::color, 4),
        wire::scalar(&FtChecked::ratio, 8).range(0.0, 1.0),
        wire::scalar(&FtChecked::stamp, 16).range(Timestamp{5}, Timestamp{10}),
        wire::bits(&FtChecked::kind, 20, 0, 3),
        wire::bits(&FtChecked::counter, 21, 0, 4).range(1, 9),
        wire::scalar(&FtChecked::free_value, 24),
        wire::flag(&FtChecked::plain, 22, 0),
    };
};
static_assert(wire::layout_ok<FtChecked>());

constexpr bool default_is_valid() {
    return cigi::validate(FtChecked{}).has_value();
}
static_assert(default_is_valid());

void expect_invalid(const FtChecked& p, std::size_t offset) {
    const auto r = cigi::validate(p);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), (Error{.code = ErrorCode::InvalidFieldValue,
                                .offset = offset,
                                .opcode = FtChecked::opcode}));
}

TEST(FieldsValidate, DefaultsAndBoundariesAreValid) {
    EXPECT_TRUE(cigi::validate(FtChecked{}).has_value());
    FtChecked p;
    p.level = -10;
    p.ratio = 0.0;
    p.stamp = Timestamp{5};
    p.counter = 9;
    EXPECT_TRUE(cigi::validate(p).has_value());
    p.level = 10;
    p.ratio = 1.0;
    p.stamp = Timestamp{10};
    p.counter = 1;
    p.kind = FtMode::F;
    p.color = FtColor::Blue;
    EXPECT_TRUE(wire::validate_fields(p).has_value());
}

TEST(FieldsValidate, NumericRangesReportFieldOffset) {
    FtChecked p;
    p.level = 11;
    expect_invalid(p, 2);
    p.level = -11;
    expect_invalid(p, 2);
    p = FtChecked{};
    p.ratio = 1.5;
    expect_invalid(p, 8);
    p.ratio = -0.01;
    expect_invalid(p, 8);
    p.ratio = std::nan("");
    expect_invalid(p, 8);
    p = FtChecked{};
    p.stamp = Timestamp{4};
    expect_invalid(p, 16);
    p.stamp = Timestamp{11};
    expect_invalid(p, 16);
    p = FtChecked{};
    p.counter = 0;
    expect_invalid(p, 21);
    p.counter = 10;
    expect_invalid(p, 21);
}

TEST(FieldsValidate, UndefinedEnumsFail) {
    FtChecked p;
    p.color = static_cast<FtColor>(0);
    expect_invalid(p, 4);
    p.color = static_cast<FtColor>(4);
    expect_invalid(p, 4);
    p = FtChecked{};
    p.kind = static_cast<FtMode>(6);
    expect_invalid(p, 20);
}

TEST(FieldsValidate, FirstFailingFieldInTableOrderWins) {
    FtChecked p;
    p.counter = 0;
    p.ratio = 2.0;
    p.level = 99;
    expect_invalid(p, 2);
    p.level = 0;
    expect_invalid(p, 8);
}

TEST(FieldsValidate, ValidatesWithoutAnyWireData) {
    // Out-of-range values encode (masked / stored as-is) and decode back, validate() flags them.
    FtChecked p;
    p.counter = 12;
    Bytes buf(FtChecked::wire_size);
    cigi::encode(p, buf);
    const auto r = cigi::decode(std::type_identity<FtChecked>{}, buf, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->counter, 12);
    EXPECT_FALSE(cigi::validate(*r).has_value());
}

// --- Overload selection: a custom (non-template) codec wins over the generic one -------------

// Only encode is customised (it stamps 0xEE in a reserved byte); everything else is generic.
struct FtStamped {
    static constexpr std::uint8_t opcode = 214;
    static constexpr Direction direction = Direction::Both;
    static constexpr std::size_t wire_size = 8;
    std::uint16_t value = 0;
    static constexpr auto fields = std::tuple{wire::scalar(&FtStamped::value, 2)};
};
constexpr void encode(const FtStamped& p, std::span<std::byte> out) noexcept {
    std::ranges::fill(out, std::byte{0});
    wire::store<std::uint8_t>(out, 0, FtStamped::opcode);
    wire::store<std::uint8_t>(out, 1, 8);
    wire::store<std::uint8_t>(out, 7, 0xEE); // reserved byte used as a marker
    wire::encode_fields(p, out);
}

TEST(Fields, NonTemplateOverloadBeatsGenericTemplate) {
    // MessageBuilder calls encode() unqualified from namespace cigi: ordinary lookup finds the
    // generic template, ADL finds FtStamped's own overload, and the non-template wins.
    StaticMessageBuilder<64> msg(Direction::HostToIg);
    ASSERT_TRUE(msg.add(IgControl{}).has_value());
    ASSERT_TRUE(msg.add(FtStamped{.value = 0x0102}).has_value());
    const Bytes buf = to_vec(msg.bytes().subspan(24));
    ASSERT_EQ(buf.size(), 8U);
    EXPECT_EQ(buf.at(7), std::byte{0xEE}); // the custom encode ran
    // decode / encoded_size / validate were not customised: the generic ones apply.
    const auto r = cigi::decode(std::type_identity<FtStamped>{}, buf, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->value, 0x0102);
    EXPECT_EQ(cigi::encoded_size(*r), 8U);
    EXPECT_TRUE(cigi::validate(*r).has_value());
    static_assert(Packet<FtStamped>);
}

// --- Compile-time layout verification (negative cases) --------------------------------------

struct Lay {
    std::uint8_t a = 0;
    std::uint16_t b = 0;
    std::uint32_t c = 0;
    double d = 0;
    std::uint8_t e = 0;
    bool f = false;
};
using wire::bits;
using wire::flag;
using wire::layout_check;
using wire::LayoutError;
using wire::scalar;

constexpr auto lay = [](auto... fields) { return std::tuple{fields...}; };

static_assert(layout_check(lay(scalar(&Lay::a, 2), scalar(&Lay::b, 4), scalar(&Lay::c, 8),
                               scalar(&Lay::d, 16)),
                           24) == LayoutError::None);
static_assert(layout_check(lay(), 8) == LayoutError::None);
static_assert(layout_check(lay(scalar(&Lay::a, 7)), 8) == LayoutError::None);
static_assert(layout_check(lay(scalar(&Lay::d, 248)), 256) == LayoutError::BadWireSize);
static_assert(layout_check(lay(scalar(&Lay::d, 240)), 248) == LayoutError::None);

// Wire size must be 8..255 and a multiple of 8.
static_assert(layout_check(lay(), 0) == LayoutError::BadWireSize);
static_assert(layout_check(lay(), 12) == LayoutError::BadWireSize);
static_assert(layout_check(lay(), 256) == LayoutError::BadWireSize);

// Fields must lie inside the packet.
static_assert(layout_check(lay(scalar(&Lay::c, 8)), 8) == LayoutError::OutOfBounds);
static_assert(layout_check(lay(scalar(&Lay::c, 4)), 8) == LayoutError::None);
static_assert(layout_check(lay(scalar(&Lay::a, 8)), 8) == LayoutError::OutOfBounds);
static_assert(layout_check(lay(scalar(&Lay::a, std::numeric_limits<std::size_t>::max())), 8) ==
              LayoutError::OutOfBounds);

// Natural alignment (ICD 2.6): offsets are multiples of the field size.
static_assert(layout_check(lay(scalar(&Lay::b, 3)), 16) == LayoutError::Misaligned);
static_assert(layout_check(lay(scalar(&Lay::c, 6)), 16) == LayoutError::Misaligned);
static_assert(layout_check(lay(scalar(&Lay::d, 12)), 24) == LayoutError::Misaligned);
static_assert(layout_check(lay(scalar(&Lay::d, 8)), 16) == LayoutError::None);
static_assert(layout_check(lay(scalar(&Lay::a, 3), scalar(&Lay::e, 5)), 8) == LayoutError::None);

// Nothing may touch the opcode and size bytes.
static_assert(layout_check(lay(scalar(&Lay::a, 0)), 8) == LayoutError::HeaderOverlap);
static_assert(layout_check(lay(scalar(&Lay::a, 1)), 8) == LayoutError::HeaderOverlap);
static_assert(layout_check(lay(scalar(&Lay::b, 0)), 8) == LayoutError::HeaderOverlap);
static_assert(layout_check(lay(flag(&Lay::f, 1, 3)), 8) == LayoutError::HeaderOverlap);

// Scalars may not share bytes.
static_assert(layout_check(lay(scalar(&Lay::a, 4), scalar(&Lay::a, 4)), 8) == LayoutError::Overlap);
static_assert(layout_check(lay(scalar(&Lay::b, 4), scalar(&Lay::a, 5)), 8) == LayoutError::Overlap);
static_assert(layout_check(lay(scalar(&Lay::c, 4), scalar(&Lay::b, 6)), 8) == LayoutError::Overlap);
static_assert(layout_check(lay(scalar(&Lay::d, 8), scalar(&Lay::c, 12)), 16) ==
              LayoutError::Overlap);
static_assert(layout_check(lay(scalar(&Lay::b, 4), scalar(&Lay::b, 6)), 8) == LayoutError::None);

// Bit fields: no shared bits in a byte, but distinct bits of one byte are fine.
static_assert(layout_check(lay(bits(&Lay::a, 2, 0, 4), bits(&Lay::a, 2, 4, 4)), 8) ==
              LayoutError::None);
static_assert(layout_check(lay(bits(&Lay::a, 2, 0, 4), bits(&Lay::a, 2, 3, 2)), 8) ==
              LayoutError::Overlap);
static_assert(layout_check(lay(flag(&Lay::f, 2, 7), flag(&Lay::f, 2, 7)), 8) ==
              LayoutError::Overlap);
static_assert(layout_check(lay(flag(&Lay::f, 2, 0), flag(&Lay::f, 3, 0)), 8) == LayoutError::None);
static_assert(layout_check(lay(scalar(&Lay::a, 2), flag(&Lay::f, 2, 0)), 8) ==
              LayoutError::Overlap);
static_assert(layout_check(lay(flag(&Lay::f, 4, 0), scalar(&Lay::b, 4)), 8) ==
              LayoutError::Overlap);

// Bit fields may not be empty or cross a byte boundary (ICD 2.7.3).
static_assert(layout_check(lay(bits(&Lay::a, 2, 6, 3)), 8) == LayoutError::BadBitRange);
static_assert(layout_check(lay(bits(&Lay::a, 2, 5, 3)), 8) == LayoutError::None);
static_assert(layout_check(lay(bits(&Lay::a, 2, 0, 0)), 8) == LayoutError::BadBitRange);
static_assert(layout_check(lay(bits(&Lay::a, 2, 0, 9)), 8) == LayoutError::BadBitRange);
static_assert(layout_check(lay(bits(&Lay::a, 2, 8, 1)), 8) == LayoutError::BadBitRange);
static_assert(layout_check(lay(flag(&Lay::f, 2, 8)), 8) == LayoutError::BadBitRange);

// The first failing field decides, in table order.
static_assert(layout_check(lay(scalar(&Lay::b, 3), scalar(&Lay::c, 12)), 8) ==
              LayoutError::Misaligned);
static_assert(layout_check(lay(scalar(&Lay::c, 12), scalar(&Lay::b, 3)), 8) ==
              LayoutError::OutOfBounds);

struct BadTable {
    [[maybe_unused]] static constexpr std::uint8_t opcode = 220;
    [[maybe_unused]] static constexpr Direction direction = Direction::Both;
    static constexpr std::size_t wire_size = 8;
    std::uint16_t x = 0;
    static constexpr auto fields = std::tuple{wire::scalar(&BadTable::x, 3)};
};
static_assert(!wire::layout_ok<BadTable>());
static_assert(wire::layout_check<BadTable>() == LayoutError::Misaligned);

TEST(FieldsLayout, RuntimeSpotChecks) {
    // The same checks as above, observable as ordinary test results.
    EXPECT_TRUE(wire::layout_ok<FtGolden>());
    EXPECT_TRUE(wire::layout_ok<FtAll>());
    EXPECT_FALSE(wire::layout_ok<BadTable>());
    EXPECT_EQ(wire::layout_check<BadTable>(), LayoutError::Misaligned);
}

// --- TableDrivenPacket concept --------------------------------------------------------------

struct NoTable {
    [[maybe_unused]] static constexpr std::uint8_t opcode = 1;
    [[maybe_unused]] static constexpr Direction direction = Direction::Both;
    [[maybe_unused]] static constexpr std::size_t wire_size = 8;
};
struct NoSize {
    [[maybe_unused]] static constexpr std::uint8_t opcode = 1;
    [[maybe_unused]] static constexpr Direction direction = Direction::Both;
    [[maybe_unused]] static constexpr auto fields = std::tuple{};
};
static_assert(!TableDrivenPacket<NoTable>);
static_assert(!TableDrivenPacket<NoSize>);
static_assert(!TableDrivenPacket<HostOnlyPacket>);
static_assert(TableDrivenPacket<UserPacket>);

} // namespace
} // namespace cigi::test
