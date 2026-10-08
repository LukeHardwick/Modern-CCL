#include "../support/bytes.hpp"
#include "cigi/message/builder.hpp"
#include "cigi/message/view.hpp"
#include "cigi/packets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <initializer_list>
#include <limits>
#include <random>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace cigi;
using namespace cigi::test;
using wire::ByteOrder;

template <class P> constexpr Result<P> decode_p(std::span<const std::byte> b, ByteOrder o) {
    return decode(std::type_identity<P>{}, b, o);
}

template <class P> constexpr Result<P> decode_native(std::span<const std::byte> b) {
    return decode_p<P>(b, ByteOrder::Native);
}

// --- Frame LSN ---------------------------------------------------------------------------------

static_assert(host_frame_lsn(0) == 0);
static_assert(host_frame_lsn(0x1234567F) == 0xF);
static_assert(host_frame_lsn(0xFFFFFFF0U) == 0);
static_assert(host_frame_lsn(17) == 1);

TEST(FrameLsn, KeepsOnlyTheLowNybble) {
    for (std::uint32_t f :
         {0U, 1U, 15U, 16U, 0xABCDEF12U, std::numeric_limits<std::uint32_t>::max()}) {
        EXPECT_EQ(host_frame_lsn(f), f & 0xFU);
        EXPECT_LE(host_frame_lsn(f), 15);
    }
}

// --- HAT/HOT Request ---------------------------------------------------------------------------

constexpr HatHotRequest request{
    .hat_hot_id = 0x1234,
    .request_type = HatHotRequestType::Extended,
    .coordinate_system = HatHotCoordinateSystem::Entity,
    .update_period = 5,
    .entity_id = 0xABCD,
    .latitude_or_x = 1.0,
    .longitude_or_y = -2.0,
    .altitude_or_z = 0.5,
};

// Hand-derived from 4.1.24: opcode 24 = 0x18, size 32 = 0x20; byte 4 = entity coordinate system
// (bit 2 = 0x04) | extended request type (2) = 0x06. 1.0 = 0x3FF0.., -2.0 = 0xC000.., 0.5 =
// 0x3FE0..
constexpr auto request_le = bytes_of(0x18, 0x20, 0x34, 0x12, 0x06, 0x05, 0xCD, 0xAB, //
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F, //
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, //
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x3F);
constexpr auto request_be = bytes_of(0x18, 0x20, 0x12, 0x34, 0x06, 0x05, 0xAB, 0xCD, //
                                     0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                                     0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                                     0x3F, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

constexpr bool request_constexpr_round_trip() {
    std::array<std::byte, 32> buf{};
    encode(request, buf);
    const auto r = decode_native<HatHotRequest>(buf);
    return r.has_value() && *r == request && buf == native_of(request_le, request_be);
}
static_assert(request_constexpr_round_trip());
static_assert(Packet<HatHotRequest>);
static_assert(HatHotRequest::wire_size == 32 && HatHotRequest::opcode == 24);
static_assert(HatHotRequest::direction == Direction::HostToIg);

TEST(HatHotRequest, EncodeMatchesGolden) {
    std::array<std::byte, 32> buf{};
    encode(request, buf);
    EXPECT_EQ(buf, native_of(request_le, request_be));
    EXPECT_EQ(encoded_size(request), 32U);
}

TEST(HatHotRequest, DecodeNativeAndForeignGolden) {
    const auto native =
        decode_p<HatHotRequest>(native_of(request_le, request_be), ByteOrder::Native);
    ASSERT_TRUE(native.has_value());
    EXPECT_EQ(*native, request);
    const auto foreign =
        decode_p<HatHotRequest>(foreign_of(request_le, request_be), ByteOrder::Swapped);
    ASSERT_TRUE(foreign.has_value());
    EXPECT_EQ(*foreign, request);
}

TEST(HatHotRequest, ReservedBitsAreZeroEvenIfBufferWasDirty) {
    std::array<std::byte, 32> buf{};
    buf.fill(std::byte{0xFF});
    encode(request, buf);
    EXPECT_EQ(buf, native_of(request_le, request_be));
    const HatHotRequest geodetic_hat;
    buf.fill(std::byte{0xFF});
    encode(geodetic_hat, buf);
    EXPECT_EQ(buf.at(4), std::byte{0});
}

TEST(HatHotRequest, DecodeIgnoresReservedBitsAndTrailingBytes) {
    Bytes b = to_vec(native_of(request_le, request_be));
    b.at(4) = b.at(4) | std::byte{0xF8};
    b.at(1) = std::byte{40};
    b.resize(40, std::byte{0xAB});
    const auto r = decode_native<HatHotRequest>(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, request);
}

TEST(HatHotRequest, ReservedByteFromLegacyPeerPassesThroughDecode) {
    // CIGI 3.0/3.1 reserved byte 5. Decode is version-agnostic: a non-conforming legacy peer's
    // non-zero value is read as the update period; sessions mask it by the peer's minor version.
    Bytes b = to_vec(native_of(request_le, request_be));
    b.at(5) = std::byte{0x7B};
    const auto r = decode_native<HatHotRequest>(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->update_period, 0x7B);
}

TEST(HatHotRequest, AllTypesAndSystemsRoundTrip) {
    for (const auto type :
         {HatHotRequestType::Hat, HatHotRequestType::Hot, HatHotRequestType::Extended}) {
        for (const auto sys : {HatHotCoordinateSystem::Geodetic, HatHotCoordinateSystem::Entity}) {
            const HatHotRequest p{
                .hat_hot_id = 0xFFFF,
                .request_type = type,
                .coordinate_system = sys,
                .update_period = 255,
                .entity_id = 0xFFFF,
                .latitude_or_x = -90.0,
                .longitude_or_y = 180.0,
                .altitude_or_z = std::numeric_limits<double>::max(),
            };
            std::array<std::byte, 32> buf{};
            encode(p, buf);
            const auto r = decode_native<HatHotRequest>(buf);
            ASSERT_TRUE(r.has_value());
            EXPECT_EQ(*r, p);
        }
    }
}

TEST(HatHotRequest, EncodeMasksOutOfRangeEnums) {
    HatHotRequest p;
    p.request_type = static_cast<HatHotRequestType>(0xFF);
    p.coordinate_system = static_cast<HatHotCoordinateSystem>(0xFE);
    std::array<std::byte, 32> buf{};
    encode(p, buf);
    EXPECT_EQ(buf.at(4), std::byte{0x03});
}

TEST(HatHotRequest, DecodeErrors) {
    const Bytes good = to_vec(native_of(request_le, request_be));
    EXPECT_EQ(decode_native<HatHotRequest>({}).error().code, ErrorCode::Truncated);
    EXPECT_EQ(decode_native<HatHotRequest>(std::span<const std::byte>{good}.first(31)).error().code,
              ErrorCode::InvalidPacketSize);
    Bytes wrong = good;
    wrong.at(0) = std::byte{25};
    const auto r = decode_native<HatHotRequest>(wrong);
    EXPECT_EQ(r.error().code, ErrorCode::OpcodeMismatch);
    EXPECT_EQ(r.error().opcode, 25);
}

TEST(HatHotRequest, DecodeKeepsUndefinedRequestType) {
    Bytes b = to_vec(native_of(request_le, request_be));
    b.at(4) = std::byte{0x03}; // request type 3 is undefined
    const auto r = decode_native<HatHotRequest>(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->request_type, static_cast<HatHotRequestType>(3));
    EXPECT_EQ(validate(*r).error(),
              (Error{.code = ErrorCode::InvalidFieldValue, .offset = 4, .opcode = 24}));
}

TEST(HatHotRequest, ValidateGeodeticRanges) {
    EXPECT_TRUE(validate(request).has_value());
    EXPECT_TRUE(validate(HatHotRequest{}).has_value());
    EXPECT_TRUE(
        validate(HatHotRequest{.latitude_or_x = 90.0, .longitude_or_y = -180.0}).has_value());
    EXPECT_TRUE(
        validate(HatHotRequest{.latitude_or_x = -90.0, .longitude_or_y = 180.0}).has_value());
    const auto lat = validate(HatHotRequest{.latitude_or_x = 90.5});
    EXPECT_EQ(lat.error(),
              (Error{.code = ErrorCode::InvalidFieldValue, .offset = 8, .opcode = 24}));
    const auto lon = validate(HatHotRequest{.longitude_or_y = -180.5});
    EXPECT_EQ(lon.error(),
              (Error{.code = ErrorCode::InvalidFieldValue, .offset = 16, .opcode = 24}));
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(validate(HatHotRequest{.latitude_or_x = nan}).has_value());
    EXPECT_FALSE(validate(HatHotRequest{.longitude_or_y = nan}).has_value());
}

TEST(HatHotRequest, EntityOffsetsAreNotRangeChecked) {
    const HatHotRequest p{
        .coordinate_system = HatHotCoordinateSystem::Entity,
        .latitude_or_x = 1000.0,
        .longitude_or_y = -5000.0,
    };
    EXPECT_TRUE(validate(p).has_value());
}

TEST(HatHotRequest, ValidateRejectsUndefinedCoordinateSystem) {
    const auto r =
        validate(HatHotRequest{.coordinate_system = static_cast<HatHotCoordinateSystem>(2)});
    EXPECT_EQ(r.error(), (Error{.code = ErrorCode::InvalidFieldValue, .offset = 4, .opcode = 24}));
}

TEST(HatHotRequest, EnumTraits) {
    EXPECT_TRUE(EnumTraits<HatHotRequestType>::is_defined(HatHotRequestType::Extended));
    EXPECT_FALSE(EnumTraits<HatHotRequestType>::is_defined(static_cast<HatHotRequestType>(3)));
    EXPECT_TRUE(EnumTraits<HatHotCoordinateSystem>::is_defined(HatHotCoordinateSystem::Entity));
    EXPECT_FALSE(
        EnumTraits<HatHotCoordinateSystem>::is_defined(static_cast<HatHotCoordinateSystem>(2)));
}

// --- HAT/HOT Response --------------------------------------------------------------------------

constexpr HatHotResponse response{
    .hat_hot_id = 0x0102,
    .valid = true,
    .response_type = HatHotResponseType::Hot,
    .host_frame_lsn = 0xA,
    .height = 100.0,
};

// Hand-derived from 4.2.2: opcode 102 = 0x66, size 16 = 0x10; byte 4 = LSN 0xA (0xA0) | response
// type HOT (bit 1 = 0x02) | valid (bit 0 = 0x01) = 0xA3. 100.0 = 0x4059000000000000.
constexpr auto response_le = bytes_of(0x66, 0x10, 0x02, 0x01, 0xA3, 0x00, 0x00, 0x00, //
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x59, 0x40);
constexpr auto response_be = bytes_of(0x66, 0x10, 0x01, 0x02, 0xA3, 0x00, 0x00, 0x00, //
                                      0x40, 0x59, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

constexpr bool response_constexpr_round_trip() {
    std::array<std::byte, 16> buf{};
    encode(response, buf);
    const auto r = decode_native<HatHotResponse>(buf);
    return r.has_value() && *r == response && buf == native_of(response_le, response_be);
}
static_assert(response_constexpr_round_trip());
static_assert(Packet<HatHotResponse>);
static_assert(HatHotResponse::wire_size == 16 && HatHotResponse::opcode == 102);
static_assert(HatHotResponse::direction == Direction::IgToHost);

TEST(HatHotResponse, EncodeMatchesGolden) {
    std::array<std::byte, 16> buf{};
    encode(response, buf);
    EXPECT_EQ(buf, native_of(response_le, response_be));
    EXPECT_EQ(encoded_size(response), 16U);
}

TEST(HatHotResponse, DecodeNativeAndForeignGolden) {
    const auto native =
        decode_p<HatHotResponse>(native_of(response_le, response_be), ByteOrder::Native);
    ASSERT_TRUE(native.has_value());
    EXPECT_EQ(*native, response);
    const auto foreign =
        decode_p<HatHotResponse>(foreign_of(response_le, response_be), ByteOrder::Swapped);
    ASSERT_TRUE(foreign.has_value());
    EXPECT_EQ(*foreign, response);
}

TEST(HatHotResponse, ReservedBytesAreZeroEvenIfBufferWasDirty) {
    std::array<std::byte, 16> buf{};
    buf.fill(std::byte{0xFF});
    encode(response, buf);
    EXPECT_EQ(buf, native_of(response_le, response_be));
}

TEST(HatHotResponse, DecodeIgnoresReservedAndTrailing) {
    Bytes b = to_vec(native_of(response_le, response_be));
    b.at(4) = b.at(4) | std::byte{0x0C};
    b.at(5) = b.at(6) = b.at(7) = std::byte{0xFF};
    b.at(1) = std::byte{24};
    b.resize(24, std::byte{0x55});
    const auto r = decode_native<HatHotResponse>(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, response);
}

TEST(HatHotResponse, ReservedNybbleFromLegacyPeerPassesThroughDecode) {
    // 3.0/3.1 reserved bits 7-4 of byte 4. Decode is version-agnostic: a non-zero value is read
    // as the LSN; sessions mask it by the peer's minor version.
    Bytes b = to_vec(native_of(response_le, response_be));
    b.at(4) = std::byte{0x93};
    const auto r = decode_native<HatHotResponse>(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->host_frame_lsn, 9);
    EXPECT_TRUE(r->valid);
    EXPECT_EQ(r->response_type, HatHotResponseType::Hot);
}

TEST(HatHotResponse, EveryLsnAndFlagCombinationRoundTrips) {
    for (std::uint8_t lsn = 0; lsn < 16; ++lsn) {
        for (const bool valid : {false, true}) {
            for (const auto type : {HatHotResponseType::Hat, HatHotResponseType::Hot}) {
                const HatHotResponse p{
                    .hat_hot_id = 7,
                    .valid = valid,
                    .response_type = type,
                    .host_frame_lsn = lsn,
                    .height = -12.25,
                };
                std::array<std::byte, 16> buf{};
                encode(p, buf);
                const auto r = decode_native<HatHotResponse>(buf);
                ASSERT_TRUE(r.has_value());
                EXPECT_EQ(*r, p);
            }
        }
    }
}

TEST(HatHotResponse, EncodeMasksLsnToFourBits) {
    HatHotResponse p;
    p.host_frame_lsn = 0xFF;
    std::array<std::byte, 16> buf{};
    encode(p, buf);
    EXPECT_EQ(buf.at(4), std::byte{0xF0});
}

TEST(HatHotResponse, DecodeErrors) {
    const Bytes good = to_vec(native_of(response_le, response_be));
    EXPECT_EQ(decode_native<HatHotResponse>({}).error().code, ErrorCode::Truncated);
    EXPECT_EQ(
        decode_native<HatHotResponse>(std::span<const std::byte>{good}.first(15)).error().code,
        ErrorCode::InvalidPacketSize);
    Bytes wrong = good;
    wrong.at(0) = std::byte{103};
    EXPECT_EQ(decode_native<HatHotResponse>(wrong).error().code, ErrorCode::OpcodeMismatch);
}

TEST(HatHotResponse, Validate) {
    EXPECT_TRUE(validate(response).has_value());
    EXPECT_TRUE(validate(HatHotResponse{}).has_value());
    HatHotResponse p;
    p.response_type = static_cast<HatHotResponseType>(2);
    EXPECT_EQ(validate(p).error(),
              (Error{.code = ErrorCode::InvalidFieldValue, .offset = 4, .opcode = 102}));
    p = {};
    p.host_frame_lsn = 16;
    EXPECT_FALSE(validate(p).has_value());
}

TEST(HatHotResponse, EnumTraits) {
    EXPECT_TRUE(EnumTraits<HatHotResponseType>::is_defined(HatHotResponseType::Hot));
    EXPECT_FALSE(EnumTraits<HatHotResponseType>::is_defined(static_cast<HatHotResponseType>(2)));
}

// --- HAT/HOT Extended Response -----------------------------------------------------------------

constexpr HatHotExtendedResponse extended{
    .hat_hot_id = 0x0304,
    .valid = true,
    .host_frame_lsn = 5,
    .hat = -1.5,
    .hot = 2.0,
    .material_code = 0x11223344,
    .normal_azimuth = 90.0F,
    .normal_elevation = -45.0F,
};

// Hand-derived from 4.2.3: opcode 103 = 0x67, size 40 = 0x28; byte 4 = LSN 5 (0x50) | valid = 0x51.
// -1.5 = 0xBFF8.., 2.0 = 0x4000.., 90.0f = 0x42B40000, -45.0f = 0xC2340000.
constexpr auto extended_le = bytes_of(0x67, 0x28, 0x04, 0x03, 0x51, 0x00, 0x00, 0x00, //
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0xBF, //
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, //
                                      0x44, 0x33, 0x22, 0x11, 0x00, 0x00, 0xB4, 0x42, //
                                      0x00, 0x00, 0x34, 0xC2, 0x00, 0x00, 0x00, 0x00);
constexpr auto extended_be = bytes_of(0x67, 0x28, 0x03, 0x04, 0x51, 0x00, 0x00, 0x00, //
                                      0xBF, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                                      0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                                      0x11, 0x22, 0x33, 0x44, 0x42, 0xB4, 0x00, 0x00, //
                                      0xC2, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

constexpr bool extended_constexpr_round_trip() {
    std::array<std::byte, 40> buf{};
    encode(extended, buf);
    const auto r = decode_native<HatHotExtendedResponse>(buf);
    return r.has_value() && *r == extended && buf == native_of(extended_le, extended_be);
}
static_assert(extended_constexpr_round_trip());
static_assert(Packet<HatHotExtendedResponse>);
static_assert(HatHotExtendedResponse::wire_size == 40 && HatHotExtendedResponse::opcode == 103);
static_assert(HatHotExtendedResponse::direction == Direction::IgToHost);

TEST(HatHotExtendedResponse, EncodeMatchesGolden) {
    std::array<std::byte, 40> buf{};
    encode(extended, buf);
    EXPECT_EQ(buf, native_of(extended_le, extended_be));
    EXPECT_EQ(encoded_size(extended), 40U);
}

TEST(HatHotExtendedResponse, DecodeNativeAndForeignGolden) {
    const auto native =
        decode_p<HatHotExtendedResponse>(native_of(extended_le, extended_be), ByteOrder::Native);
    ASSERT_TRUE(native.has_value());
    EXPECT_EQ(*native, extended);
    const auto foreign =
        decode_p<HatHotExtendedResponse>(foreign_of(extended_le, extended_be), ByteOrder::Swapped);
    ASSERT_TRUE(foreign.has_value());
    EXPECT_EQ(*foreign, extended);
}

TEST(HatHotExtendedResponse, ReservedBytesAreZeroEvenIfBufferWasDirty) {
    std::array<std::byte, 40> buf{};
    buf.fill(std::byte{0xFF});
    encode(extended, buf);
    EXPECT_EQ(buf, native_of(extended_le, extended_be));
}

TEST(HatHotExtendedResponse, DecodeIgnoresReservedAndTrailing) {
    Bytes b = to_vec(native_of(extended_le, extended_be));
    b.at(4) = b.at(4) | std::byte{0x0E};
    b.at(5) = b.at(6) = b.at(7) = std::byte{0xFF};
    for (std::size_t i = 36; i < 40; ++i) {
        b.at(i) = std::byte{0xFF};
    }
    b.at(1) = std::byte{48};
    b.resize(48, std::byte{0x77});
    const auto r = decode_native<HatHotExtendedResponse>(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, extended);
}

TEST(HatHotExtendedResponse, ReservedNybbleFromLegacyPeerPassesThroughDecode) {
    // As for HatHotResponse: decode is version-agnostic, sessions mask by the peer's minor version.
    Bytes b = to_vec(native_of(extended_le, extended_be));
    b.at(4) = std::byte{0xC1};
    const auto r = decode_native<HatHotExtendedResponse>(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->host_frame_lsn, 12);
    EXPECT_TRUE(r->valid);
}

TEST(HatHotExtendedResponse, DecodeErrors) {
    const Bytes good = to_vec(native_of(extended_le, extended_be));
    EXPECT_EQ(decode_native<HatHotExtendedResponse>({}).error().code, ErrorCode::Truncated);
    EXPECT_EQ(decode_native<HatHotExtendedResponse>(std::span<const std::byte>{good}.first(39))
                  .error()
                  .code,
              ErrorCode::InvalidPacketSize);
    Bytes wrong = good;
    wrong.at(0) = std::byte{102};
    EXPECT_EQ(decode_native<HatHotExtendedResponse>(wrong).error().code, ErrorCode::OpcodeMismatch);
}

TEST(HatHotExtendedResponse, ValidateNormalRangesOnlyWhenValid) {
    EXPECT_TRUE(validate(extended).has_value());
    EXPECT_TRUE(validate(HatHotExtendedResponse{}).has_value());
    HatHotExtendedResponse p{.valid = true, .normal_azimuth = 180.0F, .normal_elevation = -90.0F};
    EXPECT_TRUE(validate(p).has_value());
    p.normal_azimuth = 180.5F;
    EXPECT_EQ(validate(p).error(),
              (Error{.code = ErrorCode::InvalidFieldValue, .offset = 28, .opcode = 103}));
    p.normal_azimuth = 0.0F;
    p.normal_elevation = 91.0F;
    EXPECT_EQ(validate(p).error(),
              (Error{.code = ErrorCode::InvalidFieldValue, .offset = 32, .opcode = 103}));
    p.normal_elevation = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(validate(p).has_value());
    EXPECT_FALSE(validate(HatHotExtendedResponse{.host_frame_lsn = 16}).has_value());
    p.valid = false; // an invalid response carries no meaningful data
    EXPECT_TRUE(validate(p).has_value());
}

// --- Random round trips ------------------------------------------------------------------------

template <class P> void check_round_trip(const P& p) {
    Bytes native(encoded_size(p));
    encode(p, native);
    const auto a = decode_p<P>(native, ByteOrder::Native);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(*a, p);
}

TEST(HatHotRandomRoundTrip, AllThreePackets) {
    // NOLINTNEXTLINE(bugprone-random-generator-seed): deterministic on purpose
    std::mt19937_64 rng(0xCA7C0DE5U);
    for (int i = 0; i < 2000; ++i) {
        const auto u64 = [&] { return rng(); };
        const auto f64 = [&] {
            return static_cast<double>(static_cast<std::int64_t>(u64())) / 1024.0;
        };
        const auto f32 = [&] {
            return static_cast<float>(static_cast<std::int32_t>(u64())) / 64.0F;
        };

        const HatHotRequest rq{
            .hat_hot_id = static_cast<std::uint16_t>(u64()),
            .request_type = static_cast<HatHotRequestType>(u64() % 3),
            .coordinate_system = static_cast<HatHotCoordinateSystem>(u64() & 1U),
            .update_period = static_cast<std::uint8_t>(u64()),
            .entity_id = static_cast<std::uint16_t>(u64()),
            .latitude_or_x = f64(),
            .longitude_or_y = f64(),
            .altitude_or_z = f64(),
        };
        const HatHotResponse rs{
            .hat_hot_id = static_cast<std::uint16_t>(u64()),
            .valid = (u64() & 1U) != 0,
            .response_type = static_cast<HatHotResponseType>(u64() & 1U),
            .host_frame_lsn = static_cast<std::uint8_t>(u64() & 0xFU),
            .height = f64(),
        };
        const HatHotExtendedResponse ex{
            .hat_hot_id = static_cast<std::uint16_t>(u64()),
            .valid = (u64() & 1U) != 0,
            .host_frame_lsn = static_cast<std::uint8_t>(u64() & 0xFU),
            .hat = f64(),
            .hot = f64(),
            .material_code = static_cast<std::uint32_t>(u64()),
            .normal_azimuth = f32(),
            .normal_elevation = f32(),
        };
        check_round_trip(rq);
        check_round_trip(rs);
        check_round_trip(ex);

        // Foreign order: byte-reverse every multi-byte field of the encoded packet.
        const auto check_foreign = [](const auto& p, std::initializer_list<std::pair<int, int>> f) {
            Bytes b(encoded_size(p));
            encode(p, b);
            for (const auto& [off, n] : f) {
                std::reverse(b.begin() + off, b.begin() + off + n);
            }
            const auto r = decode_p<std::remove_cvref_t<decltype(p)>>(b, ByteOrder::Swapped);
            ASSERT_TRUE(r.has_value());
            EXPECT_EQ(*r, p);
        };
        check_foreign(rq, {{2, 2}, {6, 2}, {8, 8}, {16, 8}, {24, 8}});
        check_foreign(rs, {{2, 2}, {8, 8}});
        check_foreign(ex, {{2, 2}, {8, 8}, {16, 8}, {24, 4}, {28, 4}, {32, 4}});
    }
}

// --- In messages -------------------------------------------------------------------------------

TEST(HatHotMessages, RequestInHostMessage) {
    StaticMessageBuilder<128> msg(Direction::HostToIg);
    ASSERT_TRUE(
        msg.add(IgControl{.ig_mode = IgMode::Operate, .host_frame_number = 33}).has_value());
    ASSERT_TRUE(msg.add(request).has_value());
    ASSERT_TRUE(msg.add(HatHotRequest{.hat_hot_id = 2, .request_type = HatHotRequestType::Hot})
                    .has_value());
    EXPECT_EQ(msg.size(), 24U + 32U + 32U);
    // An IG-to-Host packet is refused in a Host message.
    EXPECT_EQ(msg.add(response).error().code, ErrorCode::WrongDirection);

    const auto view = MessageView::parse(msg.bytes());
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(view->validate_all().has_value());
    std::vector<RawPacket> items;
    for (const auto& item : *view) {
        ASSERT_TRUE(item.has_value());
        items.push_back(*item);
    }
    ASSERT_EQ(items.size(), 3U);
    EXPECT_EQ(items.at(1).opcode, 24);
    EXPECT_EQ(items.at(1).offset, 24U);
    EXPECT_EQ(*items.at(1).decode_as<HatHotRequest>(), request);
    EXPECT_EQ(items.at(2).decode_as<HatHotRequest>()->request_type, HatHotRequestType::Hot);
    EXPECT_EQ(items.at(2).decode_as<HatHotRequest>()->hat_hot_id, 2);
}

TEST(HatHotMessages, ResponsesInIgMessage) {
    StaticMessageBuilder<128> msg(Direction::IgToHost);
    ASSERT_TRUE(msg.add(StartOfFrame{.ig_frame_number = 9}).has_value());
    ASSERT_TRUE(msg.add(response).has_value());
    ASSERT_TRUE(msg.add(extended).has_value());
    EXPECT_EQ(msg.add(request).error().code, ErrorCode::WrongDirection);
    EXPECT_EQ(msg.size(), 24U + 16U + 40U);

    const auto view = MessageView::parse(msg.bytes());
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(view->validate_all().has_value());
    std::vector<RawPacket> items;
    for (const auto& item : *view) {
        ASSERT_TRUE(item.has_value());
        items.push_back(*item);
    }
    ASSERT_EQ(items.size(), 3U);
    EXPECT_EQ(*items.at(1).decode_as<HatHotResponse>(), response);
    EXPECT_EQ(*items.at(2).decode_as<HatHotExtendedResponse>(), extended);
    EXPECT_EQ(items.at(1).decode_as<HatHotExtendedResponse>().error().code,
              ErrorCode::OpcodeMismatch);
}

} // namespace
