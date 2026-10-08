#include "../support/bytes.hpp"
#include "cigi/packets/celestial_sphere_control.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <random>
#include <span>

namespace {

using namespace cigi;
using namespace cigi::test;
using wire::ByteOrder;
namespace chr = std::chrono;

constexpr CelestialSphereControl sample{
    .hour = 13,
    .minute = 45,
    .ephemeris_model_enabled = true,
    .sun_enabled = false,
    .moon_enabled = true,
    .star_field_enabled = true,
    .date_time_valid = true,
    .date = 10082026,              // 8 Oct 2026 = 0x0099D6EA
    .star_field_intensity = 75.5F, // 0x42970000
};

// Hand-derived from 4.1.9: byte 4 = date/time valid (0x10) | star (0x08) | moon (0x04) |
// ephemeris (0x01) = 0x1D; packet ID 9, size 16.
constexpr auto golden_le = bytes_of(0x09, 0x10, 0x0D, 0x2D, 0x1D, 0x00, 0x00, 0x00, //
                                    0xEA, 0xD6, 0x99, 0x00, 0x00, 0x00, 0x97, 0x42);
constexpr auto golden_be = bytes_of(0x09, 0x10, 0x0D, 0x2D, 0x1D, 0x00, 0x00, 0x00, //
                                    0x00, 0x99, 0xD6, 0xEA, 0x42, 0x97, 0x00, 0x00);

constexpr Result<CelestialSphereControl> dec(std::span<const std::byte> b,
                                             ByteOrder o = ByteOrder::Native) {
    return decode(std::type_identity<CelestialSphereControl>{}, b, o);
}

constexpr bool constexpr_round_trip() {
    std::array<std::byte, 16> buf{};
    encode(sample, buf);
    const auto r = dec(buf);
    return r.has_value() && *r == sample && buf == native_of(golden_le, golden_be);
}
static_assert(constexpr_round_trip());
static_assert(Packet<CelestialSphereControl>);
static_assert(CelestialSphereControl::wire_size == 16 && CelestialSphereControl::opcode == 9);
static_assert(CelestialSphereControl::direction == Direction::HostToIg);
static_assert(
    unpack_celestial_date(pack_celestial_date(chr::year{2026} / chr::October / 8).value()) ==
    chr::year{2026} / chr::October / 8);

TEST(CelestialSphereControl, EncodeMatchesGolden) {
    std::array<std::byte, 16> buf{};
    encode(sample, buf);
    EXPECT_EQ(buf, native_of(golden_le, golden_be));
    EXPECT_EQ(encoded_size(sample), 16U);
}

TEST(CelestialSphereControl, DecodeNativeAndForeignGolden) {
    const auto native = dec(native_of(golden_le, golden_be), ByteOrder::Native);
    ASSERT_TRUE(native.has_value());
    EXPECT_EQ(*native, sample);
    const auto foreign = dec(foreign_of(golden_le, golden_be), ByteOrder::Swapped);
    ASSERT_TRUE(foreign.has_value());
    EXPECT_EQ(*foreign, sample);
}

TEST(CelestialSphereControl, ReservedBitsAndBytesAreZeroEvenIfBufferWasDirty) {
    std::array<std::byte, 16> buf{};
    buf.fill(std::byte{0xFF});
    encode(sample, buf);
    EXPECT_EQ(buf, native_of(golden_le, golden_be));
}

TEST(CelestialSphereControl, DefaultsFollowTheIcd) {
    const CelestialSphereControl d;
    EXPECT_TRUE(d.ephemeris_model_enabled); // ICD default: Enable
    EXPECT_FALSE(d.date_time_valid);
    std::array<std::byte, 16> buf{};
    encode(d, buf);
    EXPECT_EQ(buf.at(4), std::byte{0x01});
}

TEST(CelestialSphereControl, EachFlagUsesItsOwnBit) {
    const auto byte4 = [](const CelestialSphereControl& p) {
        std::array<std::byte, 16> buf{};
        encode(p, buf);
        return buf.at(4);
    };
    CelestialSphereControl p;
    p.ephemeris_model_enabled = false;
    EXPECT_EQ(byte4(p), std::byte{0x00});
    p.ephemeris_model_enabled = true;
    EXPECT_EQ(byte4(p), std::byte{0x01});
    p.ephemeris_model_enabled = false;
    p.sun_enabled = true;
    EXPECT_EQ(byte4(p), std::byte{0x02});
    p.sun_enabled = false;
    p.moon_enabled = true;
    EXPECT_EQ(byte4(p), std::byte{0x04});
    p.moon_enabled = false;
    p.star_field_enabled = true;
    EXPECT_EQ(byte4(p), std::byte{0x08});
    p.star_field_enabled = false;
    p.date_time_valid = true;
    EXPECT_EQ(byte4(p), std::byte{0x10});
}

TEST(CelestialSphereControl, DecodeIgnoresReservedAndTrailingBytes) {
    Bytes b = to_vec(native_of(golden_le, golden_be));
    b.at(4) = static_cast<std::byte>(std::to_integer<unsigned>(b.at(4)) | 0xE0U);
    b.at(5) = b.at(6) = b.at(7) = std::byte{0xFF};
    b.at(1) = std::byte{24};
    b.resize(24, std::byte{0xAB});
    const auto r = dec(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, sample);
}

TEST(CelestialSphereControl, DecodeErrors) {
    const Bytes good = to_vec(native_of(golden_le, golden_be));
    EXPECT_EQ(dec({}).error().code, ErrorCode::Truncated);
    EXPECT_EQ(dec(std::span<const std::byte>{good}.first(15)).error().code,
              ErrorCode::InvalidPacketSize);
    Bytes wrong = good;
    wrong.at(0) = std::byte{10};
    const auto r = dec(wrong);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::OpcodeMismatch);
    EXPECT_EQ(r.error().opcode, 10);
    Bytes short_size = good;
    short_size.at(1) = std::byte{8};
    EXPECT_EQ(dec(short_size).error().code, ErrorCode::InvalidPacketSize);
}

TEST(CelestialSphereControl, DecodeIsTolerantOfOutOfRangeValues) {
    CelestialSphereControl p;
    p.hour = 99;
    p.minute = 200;
    p.star_field_intensity = 1000.0F;
    std::array<std::byte, 16> buf{};
    encode(p, buf);
    const auto r = dec(buf);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, p);
}

TEST(CelestialSphereControl, DateHelperKnownValues) {
    EXPECT_EQ(pack_celestial_date(chr::year{2026} / chr::October / 8), 10082026U);
    EXPECT_EQ(pack_celestial_date(chr::year{1999} / chr::January / 1), 1011999U); // seven digits
    EXPECT_EQ(pack_celestial_date(chr::year{9999} / chr::December / 31), 12319999U);
    EXPECT_EQ(unpack_celestial_date(2292024), chr::year{2024} / chr::February / 29);
}

TEST(CelestialSphereControl, SetDateAndCalendarDate) {
    CelestialSphereControl p;
    EXPECT_FALSE(p.calendar_date().has_value()); // date 0
    EXPECT_TRUE(p.set_date(chr::year{2026} / chr::October / 8));
    EXPECT_EQ(p.date, 10082026U);
    EXPECT_EQ(p.calendar_date(), chr::year{2026} / chr::October / 8);
    EXPECT_FALSE(p.set_date(chr::year{2026} / chr::February / 30));
    EXPECT_FALSE(p.set_date(chr::year{10000} / chr::January / 1));
    EXPECT_EQ(p.date, 10082026U); // unchanged on failure
}

TEST(CelestialSphereControl, DateHelperRoundTripsEveryDayOf2000To2030) {
    for (auto d = chr::sys_days{chr::year{2000} / chr::January / 1};
         d < chr::sys_days{chr::year{2031} / chr::January / 1}; d += chr::days{1}) {
        const chr::year_month_day ymd{d};
        const auto packed = pack_celestial_date(ymd);
        ASSERT_TRUE(packed.has_value());
        EXPECT_EQ(unpack_celestial_date(*packed), ymd);
    }
}

TEST(CelestialSphereControl, DateHelperRejectsInvalidDates) {
    EXPECT_FALSE(pack_celestial_date(chr::year{2026} / chr::February / 30).has_value());
    EXPECT_FALSE(pack_celestial_date(chr::year{10000} / chr::January / 1).has_value());
    EXPECT_FALSE(pack_celestial_date(chr::year{-1} / chr::January / 1).has_value());
    EXPECT_FALSE(unpack_celestial_date(0).has_value());
    EXPECT_FALSE(unpack_celestial_date(13012026).has_value()); // month 13
    EXPECT_FALSE(unpack_celestial_date(1002026).has_value());  // day 0
    EXPECT_FALSE(unpack_celestial_date(1322026).has_value());  // day 32
    EXPECT_FALSE(unpack_celestial_date(2302026).has_value());  // 30 Feb
    EXPECT_FALSE(unpack_celestial_date(2292025).has_value());  // 29 Feb, not a leap year
    EXPECT_FALSE(unpack_celestial_date(std::numeric_limits<std::uint32_t>::max()).has_value());
    // A month that would wrap to a valid value if truncated to 8 bits (257 -> 1).
    EXPECT_FALSE(unpack_celestial_date(257'012'026U).has_value());
}

TEST(CelestialSphereControl, ValidateAcceptsSampleAndDefaults) {
    EXPECT_TRUE(validate(sample).has_value());
    EXPECT_TRUE(validate(CelestialSphereControl{}).has_value());
}

TEST(CelestialSphereControl, ValidateRangeBoundaries) {
    EXPECT_TRUE(validate(CelestialSphereControl{.hour = 23, .minute = 59}).has_value());
    EXPECT_TRUE(validate(CelestialSphereControl{.star_field_intensity = 0.0F}).has_value());
    EXPECT_TRUE(validate(CelestialSphereControl{.star_field_intensity = 100.0F}).has_value());
    const auto bad_hour = validate(CelestialSphereControl{.hour = 24});
    EXPECT_EQ(bad_hour.error(),
              (Error{.code = ErrorCode::InvalidFieldValue, .offset = 2, .opcode = 9}));
    const auto bad_minute = validate(CelestialSphereControl{.minute = 60});
    EXPECT_EQ(bad_minute.error(),
              (Error{.code = ErrorCode::InvalidFieldValue, .offset = 3, .opcode = 9}));
    for (const float v : {-0.5F, 100.5F, std::nanf("")}) {
        const auto r = validate(CelestialSphereControl{.star_field_intensity = v});
        EXPECT_EQ(r.error(),
                  (Error{.code = ErrorCode::InvalidFieldValue, .offset = 12, .opcode = 9}));
    }
}

TEST(CelestialSphereControl, ValidateChecksDateOnlyWhenDateTimeValid) {
    for (const std::uint32_t bad : {0U, 13012026U, 2302026U}) {
        EXPECT_TRUE(validate(CelestialSphereControl{.date_time_valid = false, .date = bad}));
        const auto r = validate(CelestialSphereControl{.date_time_valid = true, .date = bad});
        ASSERT_FALSE(r.has_value());
        EXPECT_EQ(r.error(),
                  (Error{.code = ErrorCode::InvalidFieldValue, .offset = 8, .opcode = 9}));
    }
    EXPECT_TRUE(validate(CelestialSphereControl{.date_time_valid = true, .date = 2292024}));
}

TEST(CelestialSphereControl, RandomRoundTripBothOrders) {
    // NOLINTNEXTLINE(bugprone-random-generator-seed): deterministic on purpose
    std::mt19937 rng(0xC1613309U);
    for (int i = 0; i < 2000; ++i) {
        const auto bit = [&] { return (rng() & 1U) != 0; };
        CelestialSphereControl p;
        p.hour = static_cast<std::uint8_t>(rng());
        p.minute = static_cast<std::uint8_t>(rng());
        p.ephemeris_model_enabled = bit();
        p.sun_enabled = bit();
        p.moon_enabled = bit();
        p.star_field_enabled = bit();
        p.date_time_valid = bit();
        p.date = static_cast<std::uint32_t>(rng());
        p.star_field_intensity = static_cast<float>(rng() % 100000U) / 997.0F;

        Bytes native(encoded_size(p));
        encode(p, native);
        const auto a = dec(native);
        ASSERT_TRUE(a.has_value());
        EXPECT_EQ(*a, p);

        Bytes foreign = native; // reverse the two multi-byte fields to fabricate a foreign peer
        std::reverse(foreign.begin() + 8, foreign.begin() + 12);
        std::reverse(foreign.begin() + 12, foreign.begin() + 16);
        const auto b = dec(foreign, ByteOrder::Swapped);
        ASSERT_TRUE(b.has_value());
        EXPECT_EQ(*b, p);
    }
}

} // namespace
