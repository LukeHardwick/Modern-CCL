// User-defined packets (ICD 2.10, 4.3): the UserDefinedPacket concept, ExtendUser /
// WithoutUserPackets, the builder's size check, dispatch of user packets and the ignore switch.
#include "../support/packet_checks.hpp"
#include "cigi/dispatch.hpp"
#include "cigi/message/builder.hpp"
#include "cigi/user_packet.hpp"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <random>
#include <tuple>
#include <variant>

// The packets live directly in namespace cigi, like the library's own: the shared test helpers call
// encode/decode unqualified and find the generic table-driven templates through it.
namespace cigi {

enum class TagMode : std::uint8_t { Off = 0, On = 1, Auto = 2 };

template <> struct EnumTraits<TagMode> {
    [[nodiscard]] static constexpr bool is_defined(TagMode v) noexcept {
        return enum_between(v, TagMode::Off, TagMode::Auto);
    }
};

using namespace cigi::test;
using wire::ByteOrder;

/// A fixed-layout user packet: a field table is all it takes, byte swapping included.
/// Identifying data (tag id) sits in the first two 32-bit words (ICD 4.3).
struct SensorTag {
    static constexpr std::uint8_t opcode = 210;
    static constexpr Direction direction = Direction::Both;
    static constexpr std::size_t wire_size = 16;

    std::uint16_t tag_id = 0;
    TagMode mode = TagMode::Off;
    bool armed = false;
    float value = 0.0F;
    std::uint32_t counter = 0;

    static constexpr auto fields = std::tuple{
        wire::scalar(&SensorTag::tag_id, 2),
        wire::scalar(&SensorTag::mode, 4),
        wire::flag(&SensorTag::armed, 5, 0),
        wire::scalar(&SensorTag::value, 8).range(-100.0F, 100.0F),
        wire::scalar(&SensorTag::counter, 12),
    };
    friend constexpr bool operator==(const SensorTag&, const SensorTag&) noexcept = default;
};
static_assert(wire::layout_ok<SensorTag>());

/// Another user packet, to test unknown user opcodes (8 bytes).
struct OtherUser {
    static constexpr std::uint8_t opcode = 230;
    static constexpr Direction direction = Direction::Both;
    static constexpr std::size_t wire_size = 8;
    std::uint16_t value = 0;
    static constexpr auto fields = std::tuple{wire::scalar(&OtherUser::value, 2)};
    friend constexpr bool operator==(const OtherUser&, const OtherUser&) noexcept = default;
};

/// A variable-size user packet with its own codec: 4 + 4 * words bytes (ICD 4.3). Only some word
/// counts give a legal size (odd words: 8, 16, ...).
struct WordsPacket {
    static constexpr std::uint8_t opcode = 220;
    static constexpr Direction direction = Direction::HostToIg;
    std::uint8_t words = 1;
    friend constexpr bool operator==(const WordsPacket&, const WordsPacket&) noexcept = default;
};
constexpr std::size_t encoded_size(const WordsPacket& p) noexcept {
    return 4U + (4U * p.words);
}
constexpr void encode(const WordsPacket& p, std::span<std::byte> out) noexcept {
    std::ranges::fill(out, std::byte{0});
    wire::store<std::uint8_t>(out, 0, WordsPacket::opcode);
    wire::store<std::uint8_t>(out, 1, static_cast<std::uint8_t>(out.size()));
    wire::store<std::uint8_t>(out, 2, p.words);
}
constexpr Result<WordsPacket> decode(std::type_identity<WordsPacket> /*tag*/,
                                     std::span<const std::byte> bytes,
                                     ByteOrder /*order*/) noexcept {
    if (auto ok = detail::validate_packet(bytes, WordsPacket::opcode, 8); !ok) {
        return std::unexpected(ok.error());
    }
    return WordsPacket{.words = wire::load<std::uint8_t>(bytes, 2, ByteOrder::Native)};
}
constexpr Result<void> validate(const WordsPacket& /*p*/) noexcept {
    return {};
}

// Packets that are not legal user packets: opcode below the range; size not 4 + 4n multiple of 8.
struct LowOpcode {
    static constexpr std::uint8_t opcode = 200;
    static constexpr Direction direction = Direction::Both;
    static constexpr std::size_t wire_size = 8;
    std::uint16_t v = 0;
    static constexpr auto fields = std::tuple{wire::scalar(&LowOpcode::v, 2)};
};
struct BadSize {
    static constexpr std::uint8_t opcode = 201;
    static constexpr Direction direction = Direction::Both;
    static constexpr std::size_t wire_size = 12;
    std::uint16_t v = 0;
    static constexpr auto fields = std::tuple{wire::scalar(&BadSize::v, 2)};
};
struct LastOpcode {
    static constexpr std::uint8_t opcode = 255;
    static constexpr Direction direction = Direction::IgToHost;
    static constexpr std::size_t wire_size = 248;
    std::uint16_t v = 0;
    static constexpr auto fields = std::tuple{wire::scalar(&LastOpcode::v, 2)};
};

// ---- compile-time rejection -----------------------------------------------------------------
static_assert(is_user_opcode(201) && is_user_opcode(255) && is_user_opcode(230));
static_assert(!is_user_opcode(200) && !is_user_opcode(0) && !is_user_opcode(117));
static_assert(is_valid_user_size(8) && is_valid_user_size(16) && is_valid_user_size(248));
static_assert(!is_valid_user_size(0) && !is_valid_user_size(4) && !is_valid_user_size(12) &&
              !is_valid_user_size(20) && !is_valid_user_size(256) && !is_valid_user_size(252));

static_assert(Packet<LowOpcode> && Packet<BadSize> && Packet<LastOpcode>);
static_assert(UserDefinedPacket<SensorTag> && UserDefinedPacket<OtherUser>);
static_assert(UserDefinedPacket<WordsPacket>); // variable size: checked when encoded
static_assert(UserDefinedPacket<LastOpcode>);
static_assert(UserDefinedPacket<test::UserPacket>); // the shared test helper (201, 8 bytes)
static_assert(!UserDefinedPacket<LowOpcode>, "opcode 200 is a standard-range opcode");
static_assert(!UserDefinedPacket<BadSize>, "12 bytes is not a multiple of 8");
static_assert(!UserDefinedPacket<IgControl> && !UserDefinedPacket<StartOfFrame>);
static_assert(!UserDefinedPacket<int>);

static_assert(!detail::opcodes_unique<SensorTag, SensorTag>(), "duplicate opcode is rejected");
static_assert(detail::opcodes_unique<SensorTag, OtherUser>());

using Mine = ExtendUser<HostToIgPackets, SensorTag, OtherUser, WordsPacket>;
static_assert(Mine::size == HostToIgPackets::size + 3);
static_assert(Mine::contains<SensorTag> && Mine::contains<IgControl>);
static_assert(std::is_same_v<ExtendUser<IgToHostPackets, LastOpcode>,
                             Concat<IgToHostPackets, PacketSet<LastOpcode>>>);
static_assert(std::is_same_v<WithoutUserPackets<Mine>, HostToIgPackets>);
static_assert(std::is_same_v<WithoutUserPackets<PacketSet<>>, PacketSet<>>);
static_assert(std::is_same_v<WithoutUserPackets<HostToIgPackets>, HostToIgPackets>);
// A user packet that travels both ways fits an IG -> Host set as well.
static_assert(ExtendUser<IgToHostPackets, SensorTag>::contains<SensorTag>);

// ---- golden bytes ---------------------------------------------------------------------------
constexpr SensorTag sample{
    .tag_id = 0x0102,
    .mode = TagMode::Auto,
    .armed = true,
    .value = 1.5F,
    .counter = 0x0A0B0C0DU,
};
// Hand-derived from ICD 4.3 (Fig. 135): byte 0 opcode 210 = 0xD2, byte 1 size 16 = 0x10. Tag id
// 0x0102 at 2-3, mode Auto = 2 at byte 4, armed = bit 0 of byte 5, 6-7 reserved. 1.5f is
// 0x3FC00000 at 8-11, counter at 12-15.
constexpr auto golden_le = bytes_of(0xD2, 0x10, 0x02, 0x01, 0x02, 0x01, 0x00, 0x00, //
                                    0x00, 0x00, 0xC0, 0x3F, 0x0D, 0x0C, 0x0B, 0x0A);
constexpr auto golden_be = bytes_of(0xD2, 0x10, 0x01, 0x02, 0x02, 0x01, 0x00, 0x00, //
                                    0x3F, 0xC0, 0x00, 0x00, 0x0A, 0x0B, 0x0C, 0x0D);

constexpr bool constexpr_round_trip() {
    std::array<std::byte, 16> buf{};
    encode(sample, buf);
    const auto r = decode(std::type_identity<SensorTag>{}, buf, ByteOrder::Native);
    return r.has_value() && *r == sample && buf == native_of(golden_le, golden_be);
}
static_assert(constexpr_round_trip());

TEST(UserPacket, GoldenBytesBothOrders) {
    expect_golden(sample, golden_le, golden_be);
}

TEST(UserPacket, ReservedBytesIgnoredOnDecode) {
    Bytes b(16);
    encode(sample, b);
    b.at(5) |= std::byte{0xFE};
    b.at(6) = std::byte{0xFF};
    b.at(7) = std::byte{0xFF};
    const auto r = decode(std::type_identity<SensorTag>{}, b, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, sample);
}

TEST(UserPacket, StandardDecodeErrors) {
    expect_standard_decode_errors(sample);
}

TEST(UserPacket, Validate) {
    EXPECT_TRUE(validate(SensorTag{}).has_value());
    EXPECT_TRUE(validate(sample).has_value());
    const auto bad_mode = validate(SensorTag{.mode = static_cast<TagMode>(3)});
    EXPECT_EQ(bad_mode.error(),
              (Error{.code = ErrorCode::InvalidFieldValue, .offset = 4, .opcode = 210}));
    const auto bad_value = validate(SensorTag{.value = 100.5F});
    EXPECT_EQ(bad_value.error().offset, 8U);
}

TEST(UserPacket, RandomRoundTrip) {
    // NOLINTNEXTLINE(bugprone-random-generator-seed): deterministic on purpose
    std::mt19937 rng(0x17D1CE17U);
    std::uniform_real_distribution<float> unit(-100.0F, 100.0F);
    for (int i = 0; i < 2000; ++i) {
        SensorTag p;
        p.tag_id = static_cast<std::uint16_t>(rng());
        p.mode = static_cast<TagMode>(rng() % 3U);
        p.armed = (rng() & 1U) != 0;
        p.value = unit(rng);
        p.counter = static_cast<std::uint32_t>(rng());
        expect_round_trip_both_orders(p);
        EXPECT_TRUE(validate(p).has_value());
    }
}

TEST(UserPacket, VariableSizePacketCodec) {
    const WordsPacket p{.words = 3};
    EXPECT_EQ(encoded_size(p), 16U);
    Bytes b(16, std::byte{0xFF});
    encode(p, b);
    EXPECT_EQ(b.at(0), std::byte{220});
    EXPECT_EQ(b.at(1), std::byte{16});
    EXPECT_EQ(b.at(2), std::byte{3});
    EXPECT_EQ(b.at(15), std::byte{0});
    const auto r = decode(std::type_identity<WordsPacket>{}, b, ByteOrder::Native);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, p);
    EXPECT_TRUE(validate(p).has_value());
    EXPECT_EQ(decode(std::type_identity<WordsPacket>{}, {}, ByteOrder::Native).error().code,
              ErrorCode::Truncated);
}

// ---- builder size check (ICD 4.3) -----------------------------------------------------------
TEST(UserPacket, BuilderAcceptsLegalSizes) {
    StaticMessageBuilder<512> b(Direction::HostToIg);
    ASSERT_TRUE(b.add(IgControl{}).has_value());
    ASSERT_TRUE(b.add(sample).has_value());
    ASSERT_TRUE(b.add(WordsPacket{.words = 1}).has_value());  // 8 bytes
    ASSERT_TRUE(b.add(WordsPacket{.words = 3}).has_value());  // 16 bytes
    ASSERT_TRUE(b.add(WordsPacket{.words = 61}).has_value()); // 248 bytes
    EXPECT_EQ(b.size(), 24U + 16U + 8U + 16U + 248U);
}

TEST(UserPacket, BuilderRejectsIllegalSizes) {
    StaticMessageBuilder<512> b(Direction::HostToIg);
    ASSERT_TRUE(b.add(IgControl{}).has_value());
    for (const int w : {0, 2, 4, 62, 63, 255}) {
        const auto r = b.add(WordsPacket{.words = static_cast<std::uint8_t>(w)});
        ASSERT_FALSE(r.has_value()) << w;
        EXPECT_EQ(r.error(),
                  (Error{.code = ErrorCode::InvalidPacketSize, .offset = 24, .opcode = 220}))
            << w;
        EXPECT_EQ(b.size(), 24U) << "a failed add leaves the builder unchanged";
    }
}

// ---- dispatch -------------------------------------------------------------------------------
namespace {

Bytes make_message() {
    Bytes out(512);
    MessageBuilder b(Direction::HostToIg, out);
    EXPECT_TRUE(b.add(IgControl{}).has_value());
    EXPECT_TRUE(b.add(sample).has_value());
    EXPECT_TRUE(b.add(OtherUser{.value = 9}).has_value());
    EXPECT_TRUE(b.add(EntityControl{}).has_value());
    out.resize(b.size());
    return out;
}

} // namespace

TEST(UserPacketDispatch, VisitorReceivesUserPacket) {
    const auto msg = make_message();
    const auto view = MessageView::parse(msg);
    ASSERT_TRUE(view.has_value());
    int tags = 0;
    int others = 0;
    const auto stats = dispatch<Mine>(*view, overloaded{[&](const SensorTag& t) {
                                                            EXPECT_EQ(t, sample);
                                                            ++tags;
                                                        },
                                                        [&](const OtherUser& o) {
                                                            EXPECT_EQ(o.value, 9);
                                                            ++others;
                                                        },
                                                        [](const IgControl&) {}});
    EXPECT_EQ(tags, 1);
    EXPECT_EQ(others, 1);
    EXPECT_EQ(stats, (DispatchStats{.handled = 3, .ignored = 1, .unknown = 0, .errors = 0}));
}

TEST(UserPacketDispatch, DispatcherHandlerAndUnknownUserOpcode) {
    // OtherUser (230) is not in this set: it is an unknown packet, delivered to on_unknown.
    using Partial = ExtendUser<HostToIgPackets, SensorTag>;
    const auto msg = make_message();
    const auto view = MessageView::parse(msg);
    ASSERT_TRUE(view.has_value());
    Dispatcher<Partial> d;
    int tags = 0;
    std::uint8_t unknown_opcode = 0;
    d.on<SensorTag>([&](const SensorTag& t) {
        EXPECT_EQ(t, sample);
        ++tags;
    });
    d.on_unknown([&](const UnknownPacket& u) { unknown_opcode = u.opcode; });
    const auto stats = d.dispatch(*view);
    EXPECT_EQ(tags, 1);
    EXPECT_EQ(unknown_opcode, 230);
    EXPECT_EQ(stats.handled, 1U);
    EXPECT_EQ(stats.unknown, 1U);
    EXPECT_FALSE(d.ignoring_user_packets());
}

TEST(UserPacketDispatch, IgnoreUserPacketsSwitch) {
    const auto msg = make_message();
    const auto view = MessageView::parse(msg);
    ASSERT_TRUE(view.has_value());
    Dispatcher<Mine> d;
    int tags = 0;
    int unknowns = 0;
    int entities = 0;
    d.on<SensorTag>([&](const SensorTag&) { ++tags; });
    d.on<EntityControl>([&](const EntityControl&) { ++entities; });
    d.on_unknown([&](const UnknownPacket&) { ++unknowns; });
    d.ignore_user_packets(true);
    EXPECT_TRUE(d.ignoring_user_packets());
    const auto stats = d.dispatch(*view);
    EXPECT_EQ(tags, 0);
    EXPECT_EQ(unknowns, 0) << "ignored user packets are not delivered as unknown either";
    EXPECT_EQ(entities, 1);
    EXPECT_EQ(stats, (DispatchStats{.handled = 1, .ignored = 3, .unknown = 0, .errors = 0}));
    d.ignore_user_packets(false);
    EXPECT_EQ(d.dispatch(*view).handled, 2U);
}

TEST(UserPacketDispatch, WithoutUserPacketsMakesThemUnknown) {
    const auto msg = make_message();
    const auto view = MessageView::parse(msg);
    ASSERT_TRUE(view.has_value());
    int unknowns = 0;
    const auto stats = dispatch<WithoutUserPackets<Mine>>(
        *view, overloaded{[&](const UnknownPacket&) { ++unknowns; }, [](const IgControl&) {}});
    EXPECT_EQ(unknowns, 2);
    EXPECT_EQ(stats.unknown, 2U);
}

TEST(UserPacketDispatch, MalformedUserPacketIsReportedNotFatal) {
    auto msg = make_message();
    // Declare the SensorTag (at byte 24) as 8 bytes: shorter than its 16-byte layout.
    msg.at(25) = std::byte{8};
    msg.erase(msg.begin() + 24 + 8, msg.begin() + 24 + 16);
    const auto view = MessageView::parse(msg);
    ASSERT_TRUE(view.has_value());
    int errors = 0;
    const auto stats =
        dispatch<Mine>(*view, overloaded{[](const IgControl&) {}}, [&](const Error& e) {
            EXPECT_EQ(e.code, ErrorCode::InvalidPacketSize);
            ++errors;
        });
    EXPECT_EQ(errors, 1);
    EXPECT_EQ(stats.errors, 1U);
}

} // namespace cigi
