#include "cigi/error.hpp"

#include <gtest/gtest.h>

namespace {

using cigi::Error;
using cigi::ErrorCode;

TEST(Error, ToStringCoversAllCodes) {
    EXPECT_EQ(cigi::to_string(ErrorCode::BufferTooSmall), "BufferTooSmall");
    EXPECT_EQ(cigi::to_string(ErrorCode::Truncated), "Truncated");
    EXPECT_EQ(cigi::to_string(ErrorCode::InvalidPacketSize), "InvalidPacketSize");
    EXPECT_EQ(cigi::to_string(ErrorCode::InvalidByteSwapMagic), "InvalidByteSwapMagic");
    EXPECT_EQ(cigi::to_string(ErrorCode::InvalidFieldValue), "InvalidFieldValue");
    EXPECT_EQ(cigi::to_string(ErrorCode::UnexpectedFirstPacket), "UnexpectedFirstPacket");
    EXPECT_EQ(cigi::to_string(ErrorCode::UnsupportedVersion), "UnsupportedVersion");
    EXPECT_EQ(cigi::to_string(ErrorCode::UnknownOpcode), "UnknownOpcode");
}

TEST(Error, ToStringUnknownValue) {
    EXPECT_EQ(cigi::to_string(static_cast<ErrorCode>(200)), "Unknown");
}

TEST(Error, EqualityAndDefaults) {
    constexpr Error a{.code = ErrorCode::Truncated};
    static_assert(a.offset == 0 && a.opcode == 0);
    static_assert(a == Error{.code = ErrorCode::Truncated});
    static_assert(a != Error{.code = ErrorCode::Truncated, .offset = 1});
    static_assert(a != Error{.code = ErrorCode::Truncated, .opcode = 1});
    static_assert(a != Error{.code = ErrorCode::BufferTooSmall});
    EXPECT_EQ(a, (Error{.code = ErrorCode::Truncated}));
}

TEST(Error, ResultCarriesError) {
    const cigi::Result<int> bad =
        std::unexpected(Error{.code = ErrorCode::UnknownOpcode, .offset = 8, .opcode = 77});
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().opcode, 77);
    EXPECT_EQ(bad.error().offset, 8U);
}

} // namespace
