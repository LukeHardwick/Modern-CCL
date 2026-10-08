#include "../support/bytes.hpp"
#include "cigi/dispatch.hpp"
#include "cigi/message/builder.hpp"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <variant>
#include <vector>

namespace {

using namespace cigi;
using namespace cigi::test;

using UserSet = Concat<HostToIgPackets, PacketSet<UserPacket>>;

// ---- compile-time properties of sets -------------------------------------------------------
static_assert(HostToIgPackets::size == 9);
static_assert(IgToHostPackets::size == 3);
static_assert(HostToIgPackets::contains<EntityControl>);
static_assert(!HostToIgPackets::contains<StartOfFrame>);
static_assert(UserSet::size == 10);
static_assert(UserSet::contains<UserPacket>);
static_assert(std::is_same_v<Concat<>, PacketSet<>>);
static_assert(std::is_same_v<Concat<IgToHostPackets>, IgToHostPackets>);
static_assert(std::is_same_v<Concat<PacketSet<UserPacket>, PacketSet<HostOnlyPacket>>,
                             PacketSet<UserPacket, HostOnlyPacket>>);
static_assert(std::variant_size_v<AnyPacket<IgToHostPackets>> == 4);

static_assert(detail::opcodes_unique<IgControl, EntityControl>());
static_assert(!detail::opcodes_unique<IgControl, EntityControl, IgControl>());
static_assert(detail::directions_consistent<IgControl, UserPacket>());
static_assert(detail::directions_consistent<StartOfFrame, UserPacket>());
static_assert(!detail::directions_consistent<IgControl, StartOfFrame>());

// ---- constexpr decode_any -------------------------------------------------------------------
constexpr auto user_bytes = [] {
    std::array<std::byte, 8> b{};
    encode(UserPacket{0x1234}, b);
    return b;
}();
constexpr Result<AnyPacket<UserSet>> constexpr_decoded =
    decode_any<UserSet>(RawPacket{.opcode = 201, .bytes = user_bytes});
static_assert(constexpr_decoded.has_value());
static_assert(std::get<UserPacket>(*constexpr_decoded).value == 0x1234);
constexpr Result<AnyPacket<HostToIgPackets>> constexpr_unknown =
    decode_any<HostToIgPackets>(RawPacket{.opcode = 201, .bytes = user_bytes, .offset = 40});
static_assert(std::holds_alternative<UnknownPacket>(*constexpr_unknown));
static_assert(std::get<UnknownPacket>(*constexpr_unknown).offset == 40);

// constexpr dispatch over a message built at compile time.
constexpr int constexpr_dispatch() {
    StaticMessageBuilder<64> b(Direction::HostToIg);
    if (!b.add(IgControl{}) || !b.add(UserPacket{3})) {
        return -1;
    }
    const auto v = MessageView::parse(b.bytes());
    if (!v) {
        return -2;
    }
    int n = 0;
    const auto stats = dispatch<Extend<HostToIgPackets, UserPacket>>(
        *v,
        overloaded{[&](const IgControl&) { n += 1; }, [&](const UserPacket& p) { n += p.value; }});
    return stats.handled == 2 ? n : -3;
}
static_assert(constexpr_dispatch() == 4);

// ---- helpers --------------------------------------------------------------------------------
template <Packet P> Bytes encoded(const P& p) {
    Bytes b(encoded_size(p));
    encode(p, b);
    return b;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Bytes opaque(std::uint8_t opcode, std::size_t size) {
    Bytes b(size, std::byte{0});
    b.at(0) = static_cast<std::byte>(opcode);
    b.at(1) = static_cast<std::byte>(size);
    return b;
}

RawPacket raw_of(const Bytes& b, std::size_t offset = 0) {
    return {
        .opcode = static_cast<std::uint8_t>(b.at(0)),
        .bytes = b,
        .offset = offset,
        .byte_order = wire::ByteOrder::Native,
    };
}

template <class... Ps, class F> void for_each_packet(PacketSet<Ps...> /*set*/, const F& f) {
    (f.template operator()<Ps>(), ...);
}

/// Opcodes in the order the visitor sees them; unknown opcodes are marked +1000.
struct Recorder {
    std::vector<int> seen;
    template <Packet P> void operator()(const P& /*p*/) { seen.push_back(P::opcode); }
    void operator()(const UnknownPacket& u) { seen.push_back(1000 + u.opcode); }
};

Bytes host_message() {
    Bytes m = encoded(IgControl{});
    append(m, encoded(EntityControl{}));
    append(m, encoded(ComponentControl{}));
    append(m, encoded(ArticulatedPartControl{}));
    append(m, encoded(RateControl{}));
    append(m, encoded(CelestialSphereControl{}));
    append(m, encoded(ViewControl{}));
    append(m, encoded(ViewDefinition{}));
    append(m, encoded(HatHotRequest{}));
    append(m, opaque(150, 16));
    append(m, encoded(UserPacket{9}));
    return m;
}

Bytes ig_message() {
    Bytes m = encoded(StartOfFrame{});
    append(m, encoded(HatHotResponse{}));
    append(m, encoded(HatHotExtendedResponse{}));
    append(m, opaque(250, 8));
    return m;
}

MessageView parsed(const Bytes& m) {
    const auto v = MessageView::parse(m);
    EXPECT_TRUE(v.has_value());
    return *v;
}

// ---- decode_any -----------------------------------------------------------------------------
TEST(DecodeAny, TableDecodesEveryPacketOfBothSets) {
    const auto check = [&]<class Set>(Set set, Direction dir) {
        for_each_packet(set, [&]<Packet P> {
            const Bytes b = encoded(P{});
            const auto r = decode_any<Set>(raw_of(b), dir);
            ASSERT_TRUE(r.has_value()) << int{P::opcode};
            EXPECT_TRUE(std::holds_alternative<P>(*r)) << int{P::opcode};
        });
    };
    check(HostToIgPackets{}, Direction::HostToIg);
    check(IgToHostPackets{}, Direction::IgToHost);
    check(UserSet{}, Direction::HostToIg);
}

TEST(DecodeAny, OpcodesOutsideTheSetAreUnknown) {
    for (int op = 0; op < 256; ++op) {
        const auto opcode = static_cast<std::uint8_t>(op);
        const Bytes b = opaque(opcode, 8);
        const auto r = decode_any<IgToHostPackets>(raw_of(b, 24));
        if (opcode == 101 || opcode == 102 || opcode == 103) {
            continue; // in the set; an 8-byte stub is too short for them
        }
        ASSERT_TRUE(r.has_value());
        ASSERT_TRUE(std::holds_alternative<UnknownPacket>(*r)) << op;
        const auto& u = std::get<UnknownPacket>(*r);
        EXPECT_EQ(u.opcode, opcode);
        EXPECT_EQ(u.bytes.size(), 8U);
        EXPECT_EQ(u.offset, 24U);
    }
}

TEST(DecodeAny, DirectionRule) {
    const Bytes b = encoded(HatHotResponse{});
    const auto wrong = decode_any<IgToHostPackets>(raw_of(b, 16), Direction::HostToIg);
    ASSERT_FALSE(wrong.has_value());
    EXPECT_EQ(wrong.error(),
              (Error{.code = ErrorCode::WrongDirection, .offset = 16, .opcode = 102}));
    EXPECT_TRUE(decode_any<IgToHostPackets>(raw_of(b), Direction::IgToHost).has_value());
    EXPECT_TRUE(decode_any<IgToHostPackets>(raw_of(b)).has_value()); // no check
    // A Both packet fits either way.
    const Bytes u = encoded(UserPacket{1});
    EXPECT_TRUE(decode_any<UserSet>(raw_of(u), Direction::HostToIg).has_value());
    EXPECT_TRUE(decode_any<PacketSet<UserPacket>>(raw_of(u), Direction::IgToHost).has_value());
}

TEST(DecodeAny, BodyErrorsCarryTheMessageOffset) {
    const Bytes b = opaque(2, 8); // Entity Control needs 48 bytes
    const auto r = decode_any<HostToIgPackets>(raw_of(b, 24));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), (Error{.code = ErrorCode::InvalidPacketSize, .offset = 24, .opcode = 2}));
}

// ---- dispatch (visitor) ---------------------------------------------------------------------
TEST(Dispatch, HostMessageVisitsEveryPacketInOrder) {
    const Bytes m = host_message();
    Recorder rec;
    const auto stats = dispatch<HostToIgPackets>(parsed(m), rec);
    EXPECT_EQ(rec.seen, (std::vector<int>{1, 2, 4, 6, 8, 9, 16, 21, 24, 1150, 1201}));
    EXPECT_EQ(stats, (DispatchStats{.handled = 9, .ignored = 0, .unknown = 2, .errors = 0}));
}

TEST(Dispatch, UserPacketsAreDecodedWhenTheSetIsExtended) {
    const Bytes m = host_message();
    Recorder rec;
    const auto stats = dispatch<UserSet>(parsed(m), rec);
    EXPECT_EQ(rec.seen, (std::vector<int>{1, 2, 4, 6, 8, 9, 16, 21, 24, 1150, 201}));
    EXPECT_EQ(stats, (DispatchStats{.handled = 10, .ignored = 0, .unknown = 1, .errors = 0}));
}

TEST(Dispatch, IgMessageVisitsEveryPacketInOrder) {
    const Bytes m = ig_message();
    Recorder rec;
    const auto stats = dispatch<IgToHostPackets>(parsed(m), rec);
    EXPECT_EQ(rec.seen, (std::vector<int>{101, 102, 103, 1250}));
    EXPECT_EQ(stats, (DispatchStats{.handled = 3, .ignored = 0, .unknown = 1, .errors = 0}));
}

TEST(Dispatch, PartialOverloadsSkipTheRestSilently) {
    const Bytes m = host_message();
    int controls = 0;
    std::uint16_t user = 0;
    const auto stats =
        dispatch<UserSet>(parsed(m), overloaded{
                                         [&](const IgControl&) { ++controls; },
                                         [&](const UserPacket& p) { user = p.value; },
                                     });
    EXPECT_EQ(controls, 1);
    EXPECT_EQ(user, 9);
    EXPECT_EQ(stats, (DispatchStats{.handled = 2, .ignored = 8, .unknown = 1, .errors = 0}));
}

TEST(Dispatch, VisitorMayReturnValuesAndTakeUnknown) {
    const Bytes m = host_message();
    std::vector<std::uint8_t> unknown;
    const auto on_unknown = [&](const UnknownPacket& u) {
        unknown.push_back(u.opcode);
        return 5;
    };
    const auto on_entity = [](const EntityControl&) { return true; };
    const auto stats = dispatch<HostToIgPackets>(parsed(m), overloaded{on_unknown, on_entity});
    EXPECT_EQ(unknown, (std::vector<std::uint8_t>{150, 201}));
    EXPECT_EQ(stats.unknown, 2U);
    EXPECT_EQ(stats.handled, 1U);
    EXPECT_EQ(stats.ignored, 8U);
}

TEST(Dispatch, WrongDirectionIsReportedAndSkipped) {
    Bytes m = encoded(IgControl{});
    append(m, encoded(HatHotResponse{})); // an IG packet inside a Host message
    append(m, encoded(UserPacket{3}));
    std::vector<Error> errors;
    Recorder rec;
    const auto stats = dispatch<Concat<IgToHostPackets, PacketSet<UserPacket>>>(
        parsed(m), rec, [&](const Error& e) { errors.push_back(e); });
    EXPECT_EQ(rec.seen, (std::vector<int>{1001, 201}));
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors.front(),
              (Error{.code = ErrorCode::WrongDirection, .offset = 24, .opcode = 102}));
    EXPECT_EQ(stats, (DispatchStats{.handled = 1, .ignored = 0, .unknown = 1, .errors = 1}));
}

TEST(Dispatch, BodyDecodeErrorContinuesWithTheNextPacket) {
    Bytes m = encoded(IgControl{});
    append(m, opaque(2, 8)); // too short for Entity Control
    append(m, encoded(ViewControl{}));
    std::vector<Error> errors;
    Recorder rec;
    const auto stats =
        dispatch<HostToIgPackets>(parsed(m), rec, [&](const Error& e) { errors.push_back(e); });
    EXPECT_EQ(rec.seen, (std::vector<int>{1, 16}));
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors.front(),
              (Error{.code = ErrorCode::InvalidPacketSize, .offset = 24, .opcode = 2}));
    EXPECT_EQ(stats, (DispatchStats{.handled = 2, .ignored = 0, .unknown = 0, .errors = 1}));
}

TEST(Dispatch, HeaderErrorStopsTheWalk) {
    Bytes m = encoded(IgControl{});
    append(m, opaque(16, 8));
    Bytes bad = opaque(16, 8);
    bad.at(1) = std::byte{12}; // not a multiple of 8
    append(m, bad);
    append(m, encoded(ViewControl{})); // never reached
    std::vector<Error> errors;
    Recorder rec;
    const auto stats =
        dispatch<HostToIgPackets>(parsed(m), rec, [&](const Error& e) { errors.push_back(e); });
    // The 8-byte View Control is also too short, so: error (continue), header error (stop).
    ASSERT_EQ(errors.size(), 2U);
    EXPECT_EQ(errors.front().code, ErrorCode::InvalidPacketSize);
    EXPECT_EQ(errors.at(1),
              (Error{.code = ErrorCode::InvalidPacketSize, .offset = 32, .opcode = 16}));
    EXPECT_EQ(rec.seen, (std::vector<int>{1}));
    EXPECT_EQ(stats.errors, 2U);
    EXPECT_EQ(stats.handled, 1U);
}

TEST(Dispatch, ErrorsWithoutACallbackAreStillCounted) {
    Bytes m = encoded(IgControl{});
    append(m, opaque(2, 8));
    const auto stats = dispatch<HostToIgPackets>(parsed(m), Recorder{});
    EXPECT_EQ(stats.errors, 1U);
}

TEST(Dispatch, GenericVisitorHandlesEveryKnownPacketAndUnknowns) {
    const Bytes m = host_message();
    const auto stats = dispatch<HostToIgPackets>(parsed(m), [](const auto&) {});
    // A generic lambda accepts UnknownPacket too, so nothing is ignored.
    EXPECT_EQ(stats.ignored, 0U);
    EXPECT_EQ(stats.handled, 9U);
    EXPECT_EQ(stats.unknown, 2U);
}

TEST(Dispatch, ForeignByteOrderMessage) {
    Bytes m = encoded(IgControl{.host_frame_number = 0x01020304});
    swap_frame_fields(m);
    std::uint32_t frame = 0;
    const auto stats = dispatch<HostToIgPackets>(
        parsed(m), overloaded{[&](const IgControl& c) { frame = c.host_frame_number; }});
    EXPECT_EQ(frame, 0x01020304U);
    EXPECT_EQ(stats.handled, 1U);
}

TEST(Dispatch, DuplicateFramePacketIsReportedAndSkipped) {
    Bytes m = encoded(IgControl{});
    append(m, encoded(EntityControl{}));
    append(m, encoded(IgControl{})); // offset 72
    append(m, encoded(ViewControl{}));
    std::vector<Error> errors;
    Recorder rec;
    const auto stats =
        dispatch<HostToIgPackets>(parsed(m), rec, [&](const Error& e) { errors.push_back(e); });
    EXPECT_EQ(rec.seen, (std::vector<int>{1, 2, 16}));
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors.front(),
              (Error{.code = ErrorCode::DuplicateFramePacket, .offset = 72, .opcode = 1}));
    EXPECT_EQ(stats, (DispatchStats{.handled = 3, .ignored = 0, .unknown = 0, .errors = 1}));
    EXPECT_EQ(parsed(m).validate_all().error(), errors.front());

    Bytes ig = encoded(StartOfFrame{});
    append(ig, encoded(StartOfFrame{}));
    EXPECT_EQ(dispatch<IgToHostPackets>(parsed(ig), Recorder{}).errors, 1U);
}

TEST(Dispatch, OppositeDirectionPacketInAMatchingSetIsUnknown) {
    Bytes m = encoded(IgControl{});
    append(m, encoded(HatHotResponse{}));
    Recorder rec;
    const auto stats = dispatch<HostToIgPackets>(parsed(m), rec);
    EXPECT_EQ(rec.seen, (std::vector<int>{1, 1102}));
    EXPECT_EQ(stats.unknown, 1U);
    EXPECT_EQ(stats.errors, 0U);
}

TEST(Dispatch, BothDirectionPacketInAnIgMessage) {
    Bytes m = ig_message();
    append(m, encoded(UserPacket{5}));
    Recorder rec;
    const auto stats = dispatch<Extend<IgToHostPackets, UserPacket>>(parsed(m), rec);
    EXPECT_EQ(rec.seen, (std::vector<int>{101, 102, 103, 1250, 201}));
    EXPECT_EQ(stats.errors, 0U);
}

TEST(Dispatch, LegacyThreeOneMessage) {
    // 16-byte 3.1 IG Control, then a View Control.
    Bytes m = frame_packet(1, 16, 1);
    append(m, encoded(ViewControl{}));
    Recorder rec;
    const auto stats = dispatch<HostToIgPackets>(parsed(m), rec);
    EXPECT_EQ(rec.seen, (std::vector<int>{1, 16}));
    EXPECT_EQ(stats.errors, 0U);

    Bytes ig = frame_packet(101, 16, 1);
    append(ig, encoded(HatHotResponse{}));
    const auto s2 = dispatch<IgToHostPackets>(parsed(ig), Recorder{});
    EXPECT_EQ(s2, (DispatchStats{.handled = 2, .ignored = 0, .unknown = 0, .errors = 0}));
}

TEST(DecodeAny, OversizeKnownPacketStillDecodes) {
    Bytes b = encoded(EntityControl{});
    b.resize(56, std::byte{0});
    b.at(1) = std::byte{56};
    const auto r = decode_any<HostToIgPackets>(raw_of(b), Direction::HostToIg);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(std::holds_alternative<EntityControl>(*r));
}

static_assert(
    std::is_same_v<Extend<IgToHostPackets, UserPacket>,
                   PacketSet<StartOfFrame, HatHotResponse, HatHotExtendedResponse, UserPacket>>);
static_assert(std::is_same_v<Extend<PacketSet<>>, PacketSet<>>);

// ---- Dispatcher -----------------------------------------------------------------------------
TEST(Dispatcher, CallsRegisteredHandlersAndCounts) {
    Dispatcher<HostToIgPackets> d;
    int controls = 0;
    int views = 0;
    std::vector<std::uint8_t> unknown;
    d.on<IgControl>([&](const IgControl&) { ++controls; });
    d.on<ViewControl>([&](const ViewControl&) { ++views; });
    d.on_unknown([&](const UnknownPacket& u) { unknown.push_back(u.opcode); });
    const Bytes m = host_message();
    const auto stats = d.dispatch(parsed(m));
    EXPECT_EQ(controls, 1);
    EXPECT_EQ(views, 1);
    EXPECT_EQ(unknown, (std::vector<std::uint8_t>{150, 201}));
    EXPECT_EQ(stats, (DispatchStats{.handled = 2, .ignored = 7, .unknown = 2, .errors = 0}));
}

TEST(Dispatcher, RegisteringAgainReplacesAndClearRemoves) {
    Dispatcher<IgToHostPackets> d;
    int first = 0;
    int second = 0;
    d.on<StartOfFrame>([&](const StartOfFrame&) { ++first; });
    d.on<StartOfFrame>([&](const StartOfFrame&) { ++second; });
    const Bytes m = ig_message();
    EXPECT_EQ(d.dispatch(parsed(m)).handled, 1U);
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
    d.clear<StartOfFrame>();
    EXPECT_EQ(d.dispatch(parsed(m)), (DispatchStats{.handled = 0, .ignored = 3, .unknown = 1}));
    EXPECT_EQ(second, 1);
}

TEST(Dispatcher, HandlersSeeDecodedValues) {
    Dispatcher<IgToHostPackets> d;
    std::uint32_t frame = 0;
    d.on<StartOfFrame>([&](const StartOfFrame& s) { frame = s.ig_frame_number; });
    StaticMessageBuilder<64> b(Direction::IgToHost);
    ASSERT_TRUE(b.add(StartOfFrame{.ig_frame_number = 77}).has_value());
    ASSERT_TRUE(b.add(HatHotResponse{}).has_value());
    const auto v = MessageView::parse(b.bytes());
    ASSERT_TRUE(v.has_value());
    d.dispatch(*v);
    EXPECT_EQ(frame, 77U);
}

TEST(Dispatcher, ErrorHandlerReceivesErrors) {
    Dispatcher<HostToIgPackets> d;
    std::vector<ErrorCode> codes;
    d.on_error([&](const Error& e) { codes.push_back(e.code); });
    Bytes m = encoded(IgControl{});
    append(m, opaque(2, 8));
    Bytes bad = opaque(16, 8);
    bad.at(1) = std::byte{12};
    append(m, bad);
    const auto stats = d.dispatch(parsed(m));
    EXPECT_EQ(codes,
              (std::vector<ErrorCode>{ErrorCode::InvalidPacketSize, ErrorCode::InvalidPacketSize}));
    EXPECT_EQ(stats.errors, 2U);
}

TEST(Dispatcher, MissingErrorAndUnknownHandlersAreFine) {
    const Dispatcher<HostToIgPackets> d;
    Bytes m = encoded(IgControl{});
    append(m, opaque(2, 8));
    append(m, opaque(150, 8));
    const auto stats = d.dispatch(parsed(m));
    EXPECT_EQ(stats, (DispatchStats{.handled = 0, .ignored = 1, .unknown = 1, .errors = 1}));
}

TEST(Dispatcher, ExtendedSetHandlesUserPackets) {
    Dispatcher<UserSet> d;
    std::uint16_t value = 0;
    d.on<UserPacket>([&](const UserPacket& p) { value = p.value; });
    const Bytes m = host_message();
    d.dispatch(parsed(m));
    EXPECT_EQ(value, 9);
}

} // namespace
