#pragma once

// Shared assertions for table-driven packet tests: golden bytes in both byte orders, reserved
// bytes zero after encoding into a dirty buffer, and a foreign-order copy for random round trips.

#include "bytes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <gtest/gtest.h>
#include <span>
#include <tuple>

namespace cigi::test {

/// `p` must encode to exactly `golden_le` on a little-endian host and `golden_be` on a big-endian
/// one (hand-derived from the ICD), also into a 0xFF-filled buffer; the foreign-order golden must
/// decode back to `p` with ByteOrder::Swapped.
template <class P, std::size_t N>
void expect_golden(const P& p, const std::array<std::byte, N>& golden_le,
                   const std::array<std::byte, N>& golden_be) {
    static_assert(N == P::wire_size);
    EXPECT_EQ(encoded_size(p), N);

    std::array<std::byte, N> buf{};
    encode(p, buf);
    EXPECT_EQ(buf, native_of(golden_le, golden_be));

    buf.fill(std::byte{0xFF});
    encode(p, buf);
    EXPECT_EQ(buf, native_of(golden_le, golden_be)) << "reserved bytes/bits must encode as 0";

    const auto native =
        decode(std::type_identity<P>{}, native_of(golden_le, golden_be), wire::ByteOrder::Native);
    ASSERT_TRUE(native.has_value());
    EXPECT_EQ(*native, p);

    const auto foreign =
        decode(std::type_identity<P>{}, foreign_of(golden_le, golden_be), wire::ByteOrder::Swapped);
    ASSERT_TRUE(foreign.has_value());
    EXPECT_EQ(*foreign, p);
}

/// What a foreign-endian peer would send for the native encoding `b` of a P: every table field's
/// bytes reversed (single-byte and bit fields are unaffected).
template <class P> Bytes foreign_copy(const Bytes& b) {
    Bytes out = b;
    std::apply(
        [&](const auto&... f) {
            (std::reverse(out.begin() + static_cast<std::ptrdiff_t>(f.offset),
                          out.begin() + static_cast<std::ptrdiff_t>(f.offset + f.size())),
             ...);
        },
        P::fields);
    return out;
}

/// Encode `p`, decode it natively and as a foreign-order packet; both must equal `p`.
template <class P> void expect_round_trip_both_orders(const P& p) {
    Bytes native(encoded_size(p));
    encode(p, native);
    const auto a = decode(std::type_identity<P>{}, native, wire::ByteOrder::Native);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(*a, p);
    const auto b =
        decode(std::type_identity<P>{}, foreign_copy<P>(native), wire::ByteOrder::Swapped);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(*b, p);
}

/// Decode errors every packet shares: truncated, wrong size, wrong opcode; larger packets are
/// accepted (ICD 2.11 rule 4) and the trailing bytes ignored.
template <class P> void expect_standard_decode_errors(const P& p) {
    Bytes good(encoded_size(p));
    encode(p, good);
    const auto dec = [](const Bytes& b) {
        return decode(std::type_identity<P>{}, b, wire::ByteOrder::Native);
    };
    EXPECT_EQ(dec({}).error().code, ErrorCode::Truncated);
    Bytes shorter(good.begin(), good.end() - 1);
    EXPECT_EQ(dec(shorter).error().code, ErrorCode::InvalidPacketSize);
    Bytes wrong = good;
    wrong.at(0) = std::byte{250};
    const auto r = dec(wrong);
    EXPECT_EQ(r.error().code, ErrorCode::OpcodeMismatch);
    EXPECT_EQ(r.error().opcode, 250);
    Bytes bad_size = good;
    bad_size.at(1) = std::byte{8};
    EXPECT_EQ(dec(bad_size).error().code, ErrorCode::InvalidPacketSize);

    Bytes longer = good;
    longer.resize(good.size() + 8, std::byte{0xAB});
    longer.at(1) = static_cast<std::byte>(longer.size());
    const auto big = dec(longer);
    ASSERT_TRUE(big.has_value());
    EXPECT_EQ(*big, p);
}

} // namespace cigi::test
