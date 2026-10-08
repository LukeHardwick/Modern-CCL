#include "cigi/wire/codec.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>

namespace {

using cigi::wire::ByteOrder;
using cigi::wire::fits;
using cigi::wire::load;
using cigi::wire::store;
using cigi::wire::WireScalar;

constexpr bool little = std::endian::native == std::endian::little;
constexpr ByteOrder order_of_big_endian_peer = little ? ByteOrder::Swapped : ByteOrder::Native;
constexpr ByteOrder order_of_little_endian_peer = little ? ByteOrder::Native : ByteOrder::Swapped;

// --- concept ---------------------------------------------------------------------------------
static_assert(WireScalar<std::uint8_t> && WireScalar<std::int8_t> && WireScalar<std::uint16_t> &&
              WireScalar<std::int16_t> && WireScalar<std::uint32_t> && WireScalar<std::int32_t> &&
              WireScalar<std::uint64_t> && WireScalar<std::int64_t> && WireScalar<float> &&
              WireScalar<double>);
static_assert(!WireScalar<bool> && !WireScalar<char> && !WireScalar<long double> &&
              !WireScalar<std::byte>);

// --- constexpr round trips ---------------------------------------------------------------------
template <WireScalar T> constexpr bool round_trips(T v, ByteOrder order) {
    std::array<std::byte, 16> buf{};
    store<T>(buf, 3, v, order);
    return std::bit_cast<std::array<std::byte, sizeof(T)>>(load<T>(buf, 3, order)) ==
           std::bit_cast<std::array<std::byte, sizeof(T)>>(v);
}
static_assert(round_trips<std::uint16_t>(0xBEEF, ByteOrder::Swapped));
static_assert(round_trips<std::int32_t>(-12345, ByteOrder::Native));
static_assert(round_trips<std::uint64_t>(0x0102030405060708ULL, ByteOrder::Swapped));
static_assert(round_trips<float>(-1.5F, ByteOrder::Swapped));
static_assert(round_trips<double>(2.5, ByteOrder::Swapped));

constexpr std::array<std::byte, 4> be32_bytes{
    std::byte{1},
    std::byte{2},
    std::byte{3},
    std::byte{4},
};
static_assert(load<std::uint32_t>(be32_bytes, 0, order_of_big_endian_peer) == 0x01020304U);
static_assert(load<std::uint32_t>(be32_bytes, 0, order_of_little_endian_peer) == 0x04030201U);

constexpr bool store_golden() {
    std::array<std::byte, 4> b{};
    store<std::uint32_t>(b, 0, 0x01020304U, order_of_big_endian_peer);
    return b == be32_bytes;
}
static_assert(store_golden());

static_assert(fits(std::span<const std::byte>(be32_bytes), 0, 4));
static_assert(fits(std::span<const std::byte>(be32_bytes), 4, 0));
static_assert(!fits(std::span<const std::byte>(be32_bytes), 1, 4));
static_assert(!fits(std::span<const std::byte>(be32_bytes), 5, 0));
static_assert(!fits(std::span<const std::byte>(be32_bytes), 0, static_cast<std::size_t>(-1)));
static_assert(!fits(std::span<const std::byte>(be32_bytes), static_cast<std::size_t>(-1), 2));

// --- helpers ------------------------------------------------------------------------------------
template <class T> auto raw_bits(T v) {
    return std::bit_cast<std::array<std::byte, sizeof(T)>>(v);
}

template <WireScalar T> void check_round_trip(T v) {
    for (const auto order : {ByteOrder::Native, ByteOrder::Swapped}) {
        for (const std::size_t offset : {0U, 1U, 3U}) {
            std::array<std::byte, 16> buf{};
            store<T>(buf, offset, v, order);
            EXPECT_EQ(raw_bits(load<T>(buf, offset, order)), raw_bits(v));
        }
    }
}

template <class T> class IntegerCodec : public ::testing::Test {};
using IntegerTypes = ::testing::Types<std::uint8_t, std::int8_t, std::uint16_t, std::int16_t,
                                      std::uint32_t, std::int32_t, std::uint64_t, std::int64_t>;
TYPED_TEST_SUITE(IntegerCodec, IntegerTypes);

TYPED_TEST(IntegerCodec, RoundTripsLimitsAndPatterns) {
    using T = TypeParam;
    check_round_trip<T>(0);
    check_round_trip<T>(1);
    check_round_trip<T>(std::numeric_limits<T>::min());
    check_round_trip<T>(std::numeric_limits<T>::max());
    check_round_trip<T>(static_cast<T>(-1));
    check_round_trip<T>(static_cast<T>(0x5A));
    check_round_trip<T>(static_cast<T>(0x0123456789ABCDEFULL));
}

TEST(Codec, FloatRoundTrips) {
    using L = std::numeric_limits<float>;
    for (const float v : {
             0.0F,
             -0.0F,
             1.0F,
             -1.0F,
             2.5F,
             L::infinity(),
             -L::infinity(),
             L::denorm_min(),
             -L::denorm_min(),
             L::min(),
             L::max(),
             L::lowest(),
         }) {
        check_round_trip<float>(v);
    }
    check_round_trip<float>(std::bit_cast<float>(0x7FC00001U)); // quiet NaN with payload
    check_round_trip<float>(std::bit_cast<float>(0x7F800001U)); // signalling NaN
    check_round_trip<float>(std::bit_cast<float>(0xFFC12345U)); // negative NaN
}

TEST(Codec, DoubleRoundTrips) {
    using L = std::numeric_limits<double>;
    for (const double v : {
             0.0,
             -0.0,
             1.0,
             -1.0,
             2.52653589793,
             L::infinity(),
             -L::infinity(),
             L::denorm_min(),
             -L::denorm_min(),
             L::min(),
             L::max(),
             L::lowest(),
         }) {
        check_round_trip<double>(v);
    }
    check_round_trip<double>(std::bit_cast<double>(0x7FF8000000000001ULL));
    check_round_trip<double>(std::bit_cast<double>(0x7FF0000000000001ULL));
    check_round_trip<double>(std::bit_cast<double>(0xFFF8123456789ABCULL));
}

TEST(Codec, NegativeZeroKeepsSign) {
    std::array<std::byte, 8> buf{};
    store<double>(buf, 0, -0.0, ByteOrder::Swapped);
    EXPECT_TRUE(std::signbit(load<double>(buf, 0, ByteOrder::Swapped)));
}

// --- golden bytes -------------------------------------------------------------------------------
TEST(Codec, GoldenU32Native) {
    std::array<std::byte, 4> buf{};
    store<std::uint32_t>(buf, 0, 0x01020304U);
    if constexpr (little) {
        EXPECT_EQ(buf, (std::array{std::byte{4}, std::byte{3}, std::byte{2}, std::byte{1}}));
        EXPECT_EQ(load<std::uint32_t>(buf, 0, ByteOrder::Swapped), 0x04030201U);
    } else {
        EXPECT_EQ(buf, (std::array{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}));
        EXPECT_EQ(load<std::uint32_t>(buf, 0, ByteOrder::Swapped), 0x04030201U);
    }
    EXPECT_EQ(load<std::uint32_t>(buf, 0, ByteOrder::Native), 0x01020304U);
}

TEST(Codec, GoldenSwappedStoreIsReverseOfNative) {
    std::array<std::byte, 4> native{};
    std::array<std::byte, 4> swapped{};
    store<std::uint32_t>(native, 0, 0x01020304U, ByteOrder::Native);
    store<std::uint32_t>(swapped, 0, 0x01020304U, ByteOrder::Swapped);
    EXPECT_EQ(swapped, (std::array{native.at(3), native.at(2), native.at(1), native.at(0)}));
}

TEST(Codec, BigEndianPeerDouble) {
    // 1.0 as written by a big-endian sender.
    const std::array<std::byte, 8> bytes{
        std::byte{0x3F}, std::byte{0xF0}, std::byte{0}, std::byte{0},
        std::byte{0},    std::byte{0},    std::byte{0}, std::byte{0},
    };
    EXPECT_EQ(load<double>(bytes, 0, order_of_big_endian_peer), 1.0);
    // The same value from a little-endian sender.
    const std::array<std::byte, 8> le{
        std::byte{0}, std::byte{0}, std::byte{0},    std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{0xF0}, std::byte{0x3F},
    };
    EXPECT_EQ(load<double>(le, 0, order_of_little_endian_peer), 1.0);
}

TEST(Codec, BigEndianPeerFloatAndU16) {
    const std::array<std::byte, 4> f{std::byte{0x3F}, std::byte{0x80}, std::byte{0}, std::byte{0}};
    EXPECT_EQ(load<float>(f, 0, order_of_big_endian_peer), 1.0F);
    const std::array<std::byte, 2> w{std::byte{0x80}, std::byte{0x00}};
    EXPECT_EQ(load<std::uint16_t>(w, 0, order_of_big_endian_peer), 0x8000U);
}

TEST(Codec, StoreDoesNotTouchNeighbours) {
    std::array<std::byte, 8> buf{};
    buf.fill(std::byte{0xAA});
    store<std::uint16_t>(buf, 3, 0x1234);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        if (i != 3 && i != 4) {
            EXPECT_EQ(buf.at(i), std::byte{0xAA}) << i;
        }
    }
}

TEST(Codec, SingleByteIgnoresOrder) {
    std::array<std::byte, 2> buf{};
    store<std::uint8_t>(buf, 1, 0x7E, ByteOrder::Swapped);
    EXPECT_EQ(buf.at(1), std::byte{0x7E});
    EXPECT_EQ(load<std::int8_t>(buf, 1, ByteOrder::Swapped), 0x7E);
}

TEST(Codec, FitsRuntime) {
    const std::array<std::byte, 8> buf{};
    EXPECT_TRUE(fits(buf, 0, 8));
    EXPECT_TRUE(fits(buf, 8, 0));
    EXPECT_FALSE(fits(buf, 4, 5));
    EXPECT_FALSE(fits(buf, 9, 0));
}

} // namespace
