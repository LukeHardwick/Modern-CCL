#include "../support/bytes.hpp"
#include "cigi/packets/ig_control.hpp"
#include "cigi/packets/start_of_frame.hpp"

#include <gtest/gtest.h>
#include <random>

namespace {

using namespace cigi;
using namespace cigi::test;
using wire::ByteOrder;

constexpr int iterations = 2000;

template <class P> void check_both_orders(const P& p) {
    Bytes native(encoded_size(p));
    encode(p, native);
    const auto a = decode(std::type_identity<P>{}, native, ByteOrder::Native);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(*a, p);

    Bytes foreign = native;
    swap_frame_fields(foreign);
    const auto b = decode(std::type_identity<P>{}, foreign, ByteOrder::Swapped);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(*b, p);
}

TEST(RandomRoundTrip, IgControlAndStartOfFrame) {
    // NOLINTNEXTLINE(bugprone-random-generator-seed): deterministic on purpose
    std::mt19937 rng(0xC1613300U);
    for (int i = 0; i < iterations; ++i) {
        const auto u32 = [&] { return static_cast<std::uint32_t>(rng()); };
        const auto u8 = [&] { return static_cast<std::uint8_t>(rng()); };
        IgControl c;
        c.database_number = static_cast<std::int8_t>(u8());
        c.ig_mode = static_cast<IgMode>(u8() & 3U);
        c.timestamp_valid = (u8() & 1U) != 0;
        c.extrapolation_enabled = (u8() & 1U) != 0;
        c.host_frame_number = u32();
        c.timestamp = Timestamp{u32()};
        c.last_ig_frame_number = u32();
        check_both_orders(c);

        StartOfFrame s;
        s.database_number = static_cast<std::int8_t>(u8());
        s.ig_status = u8();
        s.ig_mode = static_cast<IgMode>(u8() & 3U);
        s.timestamp_valid = (u8() & 1U) != 0;
        s.earth_reference_model = static_cast<EarthReferenceModel>(u8() & 1U);
        s.ig_frame_number = u32();
        s.timestamp = Timestamp{u32()};
        s.last_host_frame_number = u32();
        check_both_orders(s);
    }
}

} // namespace
