#include "cigi/wire/utf8.hpp"

#include <array>
#include <gtest/gtest.h>
#include <string>
#include <string_view>

namespace {

using cigi::wire::is_valid_utf8;
using cigi::wire::utf8_truncate;
using namespace std::string_view_literals;

static_assert(utf8_truncate("hello", 3) == 3);
static_assert(utf8_truncate("hello", 5) == 5);
static_assert(utf8_truncate("hello", 99) == 5);
static_assert(utf8_truncate("hello", 0) == 0);
static_assert(utf8_truncate("\xC3\xA9", 1) == 0);  // e-acute cut in half
static_assert(utf8_truncate("a\xC3\xA9", 2) == 1); // keeps 'a' only
static_assert(utf8_truncate("a\xC3\xA9", 3) == 3);
static_assert(utf8_truncate("a\x80\x80\x80", 2) == 2);
static_assert(utf8_truncate("a\x80\x80\x80\x80\x80", 5) == 5);
static_assert(utf8_truncate("\xF0\x9F\x98\x80z", 1) == 0);
static_assert(utf8_truncate("\xF0\x9F\x98\x80z", 2) == 0);
static_assert(utf8_truncate("\xF0\x9F\x98\x80z", 3) == 0);
static_assert(utf8_truncate("\xF0\x9F\x98\x80z", 4) == 4);
static_assert(is_valid_utf8(""));
static_assert(is_valid_utf8("abc\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"));
static_assert(!is_valid_utf8("\xC0\xAF"));
static_assert(!is_valid_utf8("\xED\xA0\x80"));
static_assert(!is_valid_utf8("\xF4\x90\x80\x80"));

TEST(Utf8Truncate, Ascii) {
    for (std::size_t m = 0; m < 8; ++m) {
        EXPECT_EQ(utf8_truncate("abcdef", m), std::min<std::size_t>(m, 6));
    }
}

// Every cut position of a string of multi-byte code points lands on a code point boundary.
TEST(Utf8Truncate, CutAtEveryPosition) {
    const std::array texts = {
        "\xC3\xA9\xC3\xA9"sv,                 // 2-byte x2
        "\xE2\x82\xAC\xE2\x82\xAC"sv,         // 3-byte x2
        "\xF0\x9F\x98\x80\xF0\x9F\x98\x80"sv, // 4-byte x2
        "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z"sv,
    };
    for (const auto text : texts) {
        for (std::size_t m = 0; m <= text.size() + 1; ++m) {
            const auto n = utf8_truncate(text, m);
            EXPECT_LE(n, m);
            EXPECT_LE(n, text.size());
            EXPECT_TRUE(is_valid_utf8(text.substr(0, n))) << m;
            // Longest: the next code point would not fit.
            if (n < text.size()) {
                std::size_t next = n + 1;
                while (next < text.size() &&
                       (static_cast<unsigned char>(text.at(next)) & 0xC0U) == 0x80U) {
                    ++next;
                }
                EXPECT_GT(next, m);
            }
        }
    }
    EXPECT_EQ(utf8_truncate("\xF0\x9F\x98\x80"sv, 3), 0U);
    EXPECT_EQ(utf8_truncate("\xF0\x9F\x98\x80"sv, 4), 4U);
}

TEST(Utf8Truncate, MaxZero) {
    EXPECT_EQ(utf8_truncate("abc", 0), 0U);
    EXPECT_EQ(utf8_truncate("", 0), 0U);
    EXPECT_EQ(utf8_truncate("", 5), 0U);
}

TEST(Utf8Truncate, Malformed) {
    // Stray continuation bytes belong to no lead: nothing valid is split, the cut is kept.
    EXPECT_EQ(utf8_truncate("\x80\x80\x80\x80"sv, 2), 2U);
    EXPECT_EQ(utf8_truncate("a\x80\x80\x80"sv, 2), 2U);
    EXPECT_EQ(utf8_truncate("a\x80\x80\x80\x80\x80"sv, 5), 5U);
    EXPECT_EQ(utf8_truncate("\xF0\x80\x80\x80\x80"sv, 4), 4U); // lead > 3 bytes back
    // Malformed: 3-byte lead followed by ASCII; the lead's declared length crosses the cut.
    EXPECT_EQ(utf8_truncate("\xE2zz"sv, 1), 0U);
    EXPECT_EQ(utf8_truncate("\xE2zz"sv, 2), 2U); // nearest non-continuation is 'z': no back-off
    // Invalid lead bytes (F8..FF) have length 1.
    EXPECT_EQ(utf8_truncate("\xFF\x80\x80"sv, 2), 2U);
    // Truncated lead byte at the end (input shorter than max): returned as-is.
    EXPECT_EQ(utf8_truncate("ab\xE2"sv, 10), 3U);
    // Truncated sequence cut by max_bytes.
    EXPECT_EQ(utf8_truncate("ab\xE2\x82"sv, 3), 2U);
    // Lead byte at the cut position is kept whole (it is not a continuation).
    EXPECT_EQ(utf8_truncate("ab\xE2\x82\xAC"sv, 2), 2U);
    // Embedded NUL.
    EXPECT_EQ(utf8_truncate("a\0b"sv, 2), 2U);
}

TEST(Utf8Valid, Positive) {
    EXPECT_TRUE(is_valid_utf8(""));
    EXPECT_TRUE(is_valid_utf8("plain ascii"));
    EXPECT_TRUE(is_valid_utf8("\x7F"sv));
    EXPECT_TRUE(is_valid_utf8("\xC2\x80"sv));         // U+0080
    EXPECT_TRUE(is_valid_utf8("\xDF\xBF"sv));         // U+07FF
    EXPECT_TRUE(is_valid_utf8("\xE0\xA0\x80"sv));     // U+0800
    EXPECT_TRUE(is_valid_utf8("\xEC\x80\x80"sv));     // U+C000
    EXPECT_TRUE(is_valid_utf8("\xED\x9F\xBF"sv));     // U+D7FF
    EXPECT_TRUE(is_valid_utf8("\xEE\x80\x80"sv));     // U+E000
    EXPECT_TRUE(is_valid_utf8("\xEF\xBF\xBF"sv));     // U+FFFF
    EXPECT_TRUE(is_valid_utf8("\xF0\x90\x80\x80"sv)); // U+10000
    EXPECT_TRUE(is_valid_utf8("\xF1\x80\x80\x80"sv));
    EXPECT_TRUE(is_valid_utf8("\xF4\x8F\xBF\xBF"sv)); // U+10FFFF
    EXPECT_TRUE(is_valid_utf8("a\0b"sv));
}

TEST(Utf8Valid, Negative) {
    EXPECT_FALSE(is_valid_utf8("\x80"sv)); // stray continuation
    EXPECT_FALSE(is_valid_utf8("\xBF"sv));
    EXPECT_FALSE(is_valid_utf8("\xC0\xAF"sv));         // overlong
    EXPECT_FALSE(is_valid_utf8("\xC1\xBF"sv));         // overlong
    EXPECT_FALSE(is_valid_utf8("\xE0\x80\xAF"sv));     // overlong 3-byte
    EXPECT_FALSE(is_valid_utf8("\xE0\x9F\xBF"sv));     // overlong 3-byte
    EXPECT_FALSE(is_valid_utf8("\xF0\x80\x80\xAF"sv)); // overlong 4-byte
    EXPECT_FALSE(is_valid_utf8("\xF0\x8F\xBF\xBF"sv)); // overlong 4-byte
    EXPECT_FALSE(is_valid_utf8("\xED\xA0\x80"sv));     // surrogate D800
    EXPECT_FALSE(is_valid_utf8("\xED\xBF\xBF"sv));     // surrogate DFFF
    EXPECT_FALSE(is_valid_utf8("\xF4\x90\x80\x80"sv)); // > U+10FFFF
    EXPECT_FALSE(is_valid_utf8("\xF5\x80\x80\x80"sv));
    EXPECT_FALSE(is_valid_utf8("\xFF"sv));
    EXPECT_FALSE(is_valid_utf8("\xC3"sv)); // truncated
    EXPECT_FALSE(is_valid_utf8("\xE2\x82"sv));
    EXPECT_FALSE(is_valid_utf8("\xF0\x9F\x98"sv));
    EXPECT_FALSE(is_valid_utf8("\xC3\x28"sv)); // bad continuation
    EXPECT_FALSE(is_valid_utf8("\xE2\x82\x28"sv));
    EXPECT_FALSE(is_valid_utf8("\xE2\x28\xA1"sv));
    EXPECT_FALSE(is_valid_utf8("\xF0\x9F\x98\x28"sv));
}

} // namespace
