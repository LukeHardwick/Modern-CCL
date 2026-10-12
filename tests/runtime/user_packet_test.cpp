// Typed user-defined packets in the facade: Host::on_user / Ig::on_user (ICD 2.10, 4.3).

#include "support.hpp"

#include <gtest/gtest.h>
#include <optional>
#include <tuple>
#include <utility>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,bugprone-unused-return-value):
// tests index vectors after asserting their size and drive poll() for its effects.

namespace {

using namespace cigi;
using namespace cigi::test;

/// Opcode 202, 16 bytes, Both: a u32 at bytes 4-7 and a u16 at 8-9.
struct Wide {
    static constexpr std::uint8_t opcode = 202;
    static constexpr Direction direction = Direction::Both;
    static constexpr std::size_t wire_size = 16;
    std::uint32_t id = 0;
    std::uint16_t count = 0;
    static constexpr auto fields =
        std::tuple{wire::scalar(&Wide::id, 4), wire::scalar(&Wide::count, 8)};
};
static_assert(wire::layout_ok<Wide>());

/// Opcode 203, 8 bytes, only the Host sends it.
struct ToIgOnly {
    static constexpr std::uint8_t opcode = 203;
    static constexpr Direction direction = Direction::HostToIg;
    static constexpr std::size_t wire_size = 8;
    std::uint16_t value = 0;
    static constexpr auto fields = std::tuple{wire::scalar(&ToIgOnly::value, 2)};
};
static_assert(wire::layout_ok<ToIgOnly>());

/// Opcode 204, 8 bytes, only the IG sends it.
struct ToHostOnly {
    static constexpr std::uint8_t opcode = 204;
    static constexpr Direction direction = Direction::IgToHost;
    static constexpr std::size_t wire_size = 8;
    std::uint16_t value = 0;
    static constexpr auto fields = std::tuple{wire::scalar(&ToHostOnly::value, 2)};
};
static_assert(wire::layout_ok<ToHostOnly>());

// The direction a side receives is checked at compile time.
static_assert(HostReceivableUserPacket<UserPacket> && IgReceivableUserPacket<UserPacket>);
static_assert(HostReceivableUserPacket<ToHostOnly> && !IgReceivableUserPacket<ToHostOnly>);
static_assert(IgReceivableUserPacket<ToIgOnly> && !HostReceivableUserPacket<ToIgOnly>);
static_assert(!HostReceivableUserPacket<EntityControl> && !IgReceivableUserPacket<EntityControl>);

template <class Side, class P>
concept CanOnUser = requires(Side& s) { s.template on_user<P>([](const P&) {}); };
static_assert(CanOnUser<Host, ToHostOnly> && !CanOnUser<Host, ToIgOnly>);
static_assert(CanOnUser<Ig, ToIgOnly> && !CanOnUser<Ig, ToHostOnly>);
static_assert(CanOnUser<Host, UserPacket> && CanOnUser<Ig, UserPacket>);
static_assert(!CanOnUser<Host, EntityControl> && !CanOnUser<Ig, HatHotResponse>);

struct HostFixture {
    RawPeer peer;
    FakeClock clock;
    Host host;

    explicit HostFixture(HostConfig config = {}) : host(make(std::move(config), clock, peer)) {}

    static Host make(HostConfig config, const FakeClock& clock, RawPeer& peer) {
        config.session.clock = clock.fn();
        return Host::over(std::move(config), peer.take_other()).value();
    }
    void tick(std::uint32_t ticks = frame_ticks) {
        clock.advance(ticks);
        EXPECT_TRUE(host.poll(0ms).has_value());
    }
};

struct IgPeerFixture {
    RawPeer peer;
    FakeClock clock;
    Ig ig;

    explicit IgPeerFixture(IgConfig config = {}) : ig(make(std::move(config), clock, peer)) {}

    static Ig make(IgConfig config, const FakeClock& clock, RawPeer& peer) {
        config.session.clock = clock.fn();
        return Ig::over(std::move(config), peer.take_other()).value();
    }
    void tick(std::uint32_t ticks = frame_ticks) {
        clock.advance(ticks);
        EXPECT_TRUE(ig.poll(0ms).has_value());
    }
};

TEST(UserPacketFacade, TypedPacketsFlowInBothDirections) {
    Rig rig;
    std::optional<Wide> at_ig;
    std::optional<ToIgOnly> only_ig;
    std::optional<UserPacket> at_host;
    std::optional<ToHostOnly> only_host;
    rig.ig().on_user<Wide>([&](const Wide& w) { at_ig = w; });
    rig.ig().on_user<ToIgOnly>([&](const ToIgOnly& p) { only_ig = p; });
    rig.host().on_user<UserPacket>([&](const UserPacket& p) { at_host = p; });
    rig.host().on_user<ToHostOnly>([&](const ToHostOnly& p) { only_host = p; });
    rig.host().on_frame([](HostFrame& f) {
        EXPECT_TRUE(f.add(Wide{.id = 0xA1B2C3D4U, .count = 99}));
        EXPECT_TRUE(f.add(ToIgOnly{.value = 5}));
    });
    rig.ig().on_frame([](IgFrame& f) {
        EXPECT_TRUE(f.add(UserPacket{.value = 777}));
        EXPECT_TRUE(f.add(ToHostOnly{.value = 6}));
    });
    ASSERT_TRUE(rig.run_until(LinkState::Operate));
    rig.steps(3);
    ASSERT_TRUE(at_ig && only_ig && at_host && only_host);
    EXPECT_EQ(at_ig->id, 0xA1B2C3D4U);
    EXPECT_EQ(at_ig->count, 99);
    EXPECT_EQ(only_ig->value, 5);
    EXPECT_EQ(at_host->value, 777);
    EXPECT_EQ(only_host->value, 6);
}

TEST(UserPacketFacade, ASwappedPeerIsDecodedInItsOwnByteOrder) {
    // Ig: the Host's message is byte-swapped; the user packet value 0x1234 is written big-endian
    // by the peer (the packet is hand-built as a foreign-endian sender would write it).
    IgPeerFixture ig;
    std::optional<UserPacket> at_ig;
    ig.ig.on_user<UserPacket>([&](const UserPacket& p) { at_ig = p; });
    Bytes control = control_message(1, IgMode::Operate);
    swap_frame_fields(control);
    // Wire order is 12 34 for a big-endian peer, 34 12 for a little-endian one.
    const auto user = foreign_of(bytes_of(201, 8, 0x34, 0x12, 0, 0, 0, 0),
                                 bytes_of(201, 8, 0x12, 0x34, 0, 0, 0, 0));
    append(control, user);
    ig.peer.send(control);
    ig.tick();
    ASSERT_TRUE(at_ig.has_value());
    EXPECT_EQ(at_ig->value, 0x1234);

    // Host: the same for an IG -> Host message.
    HostFixture host;
    std::optional<Wide> at_host;
    host.host.on_user<Wide>([&](const Wide& w) { at_host = w; });
    Bytes sof = sof_message(1, IgMode::Operate);
    swap_frame_fields(sof);
    const auto wide =
        foreign_of(bytes_of(202, 16, 0, 0, 0x04, 0x03, 0x02, 0x01, 0x06, 0x05, 0, 0, 0, 0, 0, 0),
                   bytes_of(202, 16, 0, 0, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0, 0, 0, 0, 0, 0));
    append(sof, wide);
    host.peer.send(sof);
    host.tick();
    ASSERT_TRUE(at_host.has_value());
    EXPECT_EQ(at_host->id, 0x01020304U);
    EXPECT_EQ(at_host->count, 0x0506);
}

TEST(UserPacketFacade, UnregisteredUserOpcodesFallBackToOnUnknown) {
    HostFixture f;
    int typed = 0;
    int unknown = 0;
    std::uint8_t last = 0;
    f.host.on_user<UserPacket>([&](const UserPacket&) { ++typed; });
    f.host.on_unknown([&](const UnknownPacket& u) {
        ++unknown;
        last = u.opcode;
    });
    Bytes m = sof_message(1, IgMode::Operate);
    append(m, bytes_of(201, 8, 0, 1, 0, 0, 0, 0));
    append(m, bytes_of(230, 8, 0, 0, 0, 0, 0, 0)); // no handler registered
    append(m, bytes_of(120, 8, 0, 0, 0, 0, 0, 0)); // not a user opcode
    f.peer.send(m);
    f.tick();
    EXPECT_EQ(typed, 1);
    EXPECT_EQ(unknown, 2);
    EXPECT_EQ(last, 120);
}

TEST(UserPacketFacade, WithoutAnyUnknownHandlerAnUnregisteredPacketIsDropped) {
    HostFixture f;
    f.host.on_user<UserPacket>([](const UserPacket&) {});
    Bytes m = sof_message(1, IgMode::Operate);
    append(m, bytes_of(230, 8, 0, 0, 0, 0, 0, 0));
    f.peer.send(m);
    f.tick();
    EXPECT_EQ(f.host.stats().session.unknown_packets, 1U);
}

TEST(UserPacketFacade, RegisteringAgainReplacesTheHandler) {
    HostFixture f;
    int first = 0;
    int second = 0;
    f.host.on_user<UserPacket>([&](const UserPacket&) { ++first; });
    f.host.on_user<UserPacket>([&](const UserPacket&) { ++second; });
    Bytes m = sof_message(1, IgMode::Operate);
    append(m, bytes_of(201, 8, 0, 1, 0, 0, 0, 0));
    f.peer.send(m);
    f.tick();
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
}

TEST(UserPacketFacade, TheIgnoreSwitchDeliversNothing) {
    HostConfig config;
    config.session.ignore_user_packets = true;
    HostFixture f{config};
    int typed = 0;
    int unknown = 0;
    f.host.on_user<UserPacket>([&](const UserPacket&) { ++typed; });
    f.host.on_unknown([&](const UnknownPacket&) { ++unknown; });
    Bytes m = sof_message(1, IgMode::Operate);
    append(m, bytes_of(201, 8, 0, 1, 0, 0, 0, 0));
    append(m, bytes_of(230, 8, 0, 0, 0, 0, 0, 0));
    f.peer.send(m);
    f.tick();
    EXPECT_EQ(typed, 0);
    EXPECT_EQ(unknown, 0);
}

TEST(UserPacketFacade, AMalformedUserPacketGoesToOnError) {
    // Opcode 202 is registered as the 16-byte Wide, but the peer sent 8 bytes.
    HostFixture f;
    int calls = 0;
    std::optional<Error> error;
    f.host.on_user<Wide>([&](const Wide&) { ++calls; });
    f.host.on_error([&](const Error& e) { error = e; });
    Bytes m = sof_message(1, IgMode::Operate);
    append(m, bytes_of(202, 8, 0, 0, 0, 0, 0, 0));
    f.peer.send(m);
    f.tick();
    EXPECT_EQ(calls, 0);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->offset, 24U); // within the message, after the Start of Frame
}

TEST(UserPacketFacade, AnIgDeliversUserPacketsOnlyWhileOperating) {
    IgPeerFixture f;
    int calls = 0;
    f.ig.on_user<UserPacket>([&](const UserPacket&) { ++calls; });
    Bytes m = control_message(1, IgMode::ResetStandby);
    append(m, bytes_of(201, 8, 0, 1, 0, 0, 0, 0));
    f.peer.send(m);
    f.tick();
    EXPECT_EQ(calls, 0);
}

TEST(UserPacketFacade, AnIgFallsBackToOnUnknownAndIgnoresOnRequest) {
    IgConfig config;
    config.session.ignore_user_packets = true;
    IgPeerFixture ignoring{config};
    IgPeerFixture plain;
    int unknown_plain = 0;
    int unknown_ignoring = 0;
    plain.ig.on_unknown([&](const UnknownPacket&) { ++unknown_plain; });
    ignoring.ig.on_unknown([&](const UnknownPacket&) { ++unknown_ignoring; });
    Bytes m = control_message(1, IgMode::Operate);
    append(m, bytes_of(230, 8, 0, 0, 0, 0, 0, 0));
    plain.peer.send(m);
    ignoring.peer.send(m);
    plain.tick();
    ignoring.tick();
    EXPECT_EQ(unknown_plain, 1);
    EXPECT_EQ(unknown_ignoring, 0);
}

} // namespace

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,bugprone-unused-return-value)
