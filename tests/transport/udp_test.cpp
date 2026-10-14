#include "cigi/transport/udp.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

namespace {

using cigi::ErrorCode;
using cigi::transport::UdpConfig;
using cigi::transport::UdpEndpoint;
using namespace std::chrono_literals;

constexpr auto wait = 2000ms; // generous: loopback delivery is immediate, CI machines are noisy

/// Config builders (avoid partial designated initialisers, which the warning set rejects).
UdpConfig cfg(std::string local = {}) {
    UdpConfig c;
    c.local_address = std::move(local);
    return c;
}
UdpConfig to(UdpConfig c, std::string remote, std::uint16_t port) {
    c.remote_address = std::move(remote);
    c.remote_port = port;
    return c;
}
UdpConfig on_port(UdpConfig c, std::uint16_t port) {
    c.local_port = port;
    return c;
}

std::vector<std::byte> pattern(std::size_t n) {
    std::vector<std::byte> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v.at(i) = static_cast<std::byte>(((i * 7U) + 3U) & 0xFFU);
    }
    return v;
}

UdpEndpoint open_ok(const UdpConfig& c) {
    auto e = UdpEndpoint::open(c);
    EXPECT_TRUE(e.has_value());
    return std::move(*e);
}

/// Two endpoints on ephemeral loopback ports, each addressing the other.
struct Pair {
    UdpEndpoint a;
    UdpEndpoint b;

    Pair() : a(open_ok(cfg("127.0.0.1"))), b(open_ok(cfg("127.0.0.1"))) {
        EXPECT_TRUE(a.set_remote("127.0.0.1", b.local_port()));
        EXPECT_TRUE(b.set_remote("127.0.0.1", a.local_port()));
    }
};

TEST(Udp, DefaultPorts) {
    EXPECT_EQ(cigi::transport::default_ig_port, 8004);
    EXPECT_EQ(cigi::transport::default_host_port, 8005);
}

TEST(Udp, OpenBindsEphemeralPort) {
    auto e = UdpEndpoint::open(cfg("127.0.0.1"));
    ASSERT_TRUE(e);
    EXPECT_TRUE(e->is_open());
    EXPECT_NE(e->local_port(), 0);
    EXPECT_FALSE(e->last_sender());
}

TEST(Udp, OpenWithDefaultAnyAddressAndRecvBuffer) {
    auto e = UdpEndpoint::open([] {
        UdpConfig c;
        c.receive_buffer_size = 65536;
        return c;
    }());
    ASSERT_TRUE(e);
    EXPECT_NE(e->local_port(), 0);
}

TEST(Udp, ReceiveBufferSizeAboveIntMaxRejected) {
    UdpConfig c = cfg("127.0.0.1");
    c.receive_buffer_size = static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1;
    auto e = UdpEndpoint::open(c);
    ASSERT_FALSE(e);
    EXPECT_EQ(e.error().code, ErrorCode::InvalidFieldValue);
}

TEST(Udp, LoopbackRoundTrip) {
    Pair p;
    const auto msg = pattern(64);
    ASSERT_TRUE(p.a.send(msg));
    std::array<std::byte, 128> buf{};
    auto n = p.b.receive(buf, wait);
    ASSERT_TRUE(n);
    ASSERT_EQ(*n, msg.size());
    EXPECT_TRUE(std::equal(msg.begin(), msg.end(), buf.begin()));

    auto from = p.b.last_sender();
    ASSERT_TRUE(from);
    EXPECT_EQ(from->address, "127.0.0.1");
    EXPECT_EQ(from->port, p.a.local_port());

    // And back the other way, using the sender learned from the first datagram.
    ASSERT_TRUE(p.b.set_remote(from->address, from->port));
    ASSERT_TRUE(p.b.send(msg));
    auto back = p.a.receive(buf, wait);
    ASSERT_TRUE(back);
    EXPECT_EQ(*back, msg.size());
}

TEST(Udp, OpenWithRemoteInConfig) {
    auto b = UdpEndpoint::open(cfg("127.0.0.1"));
    ASSERT_TRUE(b);
    auto a = UdpEndpoint::open(to(cfg("127.0.0.1"), "127.0.0.1", b->local_port()));
    ASSERT_TRUE(a);
    const auto msg = pattern(8);
    ASSERT_TRUE(a->send(msg));
    std::array<std::byte, 16> buf{};
    auto n = b->receive(buf, wait);
    ASSERT_TRUE(n);
    EXPECT_EQ(*n, 8U);
}

TEST(Udp, ZeroLengthDatagram) {
    Pair p;
    ASSERT_TRUE(p.a.send({}));
    std::array<std::byte, 8> buf{};
    auto n = p.b.receive(buf, wait);
    ASSERT_TRUE(n);
    EXPECT_EQ(*n, 0U);
}

TEST(Udp, SeveralDatagramsKeepOrderAndBoundaries) {
    Pair p;
    for (const std::size_t len : {8U, 16U, 24U}) {
        ASSERT_TRUE(p.a.send(pattern(len)));
    }
    std::array<std::byte, 64> buf{};
    for (const std::size_t len : {8U, 16U, 24U}) {
        auto n = p.b.receive(buf, wait);
        ASSERT_TRUE(n);
        EXPECT_EQ(*n, len);
    }
}

TEST(Udp, TimeoutWhenNothingArrives) {
    Pair p;
    std::array<std::byte, 16> buf{};
    const auto t0 = std::chrono::steady_clock::now();
    auto n = p.a.receive(buf, 50ms);
    const auto dt = std::chrono::steady_clock::now() - t0;
    ASSERT_FALSE(n);
    EXPECT_EQ(n.error().code, ErrorCode::Timeout);
    EXPECT_GE(dt, 45ms);
    EXPECT_LT(dt, 1500ms);
}

TEST(Udp, AHugeTimeoutIsCappedNotOverflowed) {
    Pair p;
    std::array<std::byte, 16> buf{};
    std::thread sender([&] {
        std::this_thread::sleep_for(50ms);
        EXPECT_TRUE(p.b.send(pattern(8)));
    });
    const auto n = p.a.receive(buf, std::chrono::milliseconds::max());
    sender.join();
    ASSERT_TRUE(n);
    EXPECT_EQ(*n, 8U);
}

TEST(Udp, ZeroAndNegativeTimeoutPoll) {
    Pair p;
    std::array<std::byte, 16> buf{};
    EXPECT_EQ(p.a.receive(buf, 0ms).error().code, ErrorCode::Timeout);
    EXPECT_EQ(p.a.receive(buf, -5ms).error().code, ErrorCode::Timeout);

    ASSERT_TRUE(p.b.send(pattern(8)));
    // Delivery on loopback is effectively immediate, but poll a few times to stay robust.
    cigi::Result<std::size_t> n = std::unexpected(cigi::Error{.code = ErrorCode::Timeout});
    for (int i = 0; i < 200 && !n; ++i) {
        n = p.a.receive(buf, 0ms);
        if (!n) {
            std::this_thread::sleep_for(5ms);
        }
    }
    ASSERT_TRUE(n);
    EXPECT_EQ(*n, 8U);
}

TEST(Udp, BufferTooSmallDiscardsDatagramAndRecovers) {
    Pair p;
    ASSERT_TRUE(p.a.send(pattern(32)));
    std::array<std::byte, 16> small{};
    auto n = p.b.receive(small, wait);
    ASSERT_FALSE(n);
    EXPECT_EQ(n.error().code, ErrorCode::BufferTooSmall);

    // Exactly-fitting datagram is fine (the spare byte is not confused with data).
    ASSERT_TRUE(p.a.send(pattern(16)));
    auto ok = p.b.receive(small, wait);
    ASSERT_TRUE(ok);
    EXPECT_EQ(*ok, 16U);

    // Zero-sized buffer: any non-empty datagram is too big; an empty one fits.
    ASSERT_TRUE(p.a.send(pattern(1)));
    EXPECT_EQ(p.b.receive({}, wait).error().code, ErrorCode::BufferTooSmall);
}

TEST(Udp, LargeDatagram) {
    Pair p;
    // 8192 is accepted everywhere; macOS caps UDP datagrams at 9216 by default.
    const auto msg = pattern(8192);
    ASSERT_TRUE(p.a.send(msg));
    std::vector<std::byte> buf(cigi::transport::max_udp_payload);
    auto n = p.b.receive(buf, wait);
    ASSERT_TRUE(n);
    ASSERT_EQ(*n, msg.size());
    EXPECT_TRUE(std::equal(msg.begin(), msg.end(), buf.begin()));
}

TEST(Udp, OversizeDatagramFailsToSend) {
    Pair p;
    const std::vector<std::byte> huge(cigi::transport::max_udp_payload + 100);
    auto r = p.a.send(huge);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, ErrorCode::TransportError);
    EXPECT_NE(r.error().os_error, 0);
}

TEST(Udp, BadAddressesRejected) {
    auto bad_local = UdpEndpoint::open(cfg("not-an-address"));
    ASSERT_FALSE(bad_local);
    EXPECT_EQ(bad_local.error().code, ErrorCode::InvalidFieldValue);

    auto bad_remote = UdpEndpoint::open(to(cfg(), "999.1.1.1", 1));
    ASSERT_FALSE(bad_remote);
    EXPECT_EQ(bad_remote.error().code, ErrorCode::InvalidFieldValue);

    auto no_port = UdpEndpoint::open(to(cfg(), "127.0.0.1", 0));
    ASSERT_FALSE(no_port);
    EXPECT_EQ(no_port.error().code, ErrorCode::InvalidFieldValue);

    auto mixed = UdpEndpoint::open(to(cfg("127.0.0.1"), "::1", 9));
    ASSERT_FALSE(mixed);
    EXPECT_EQ(mixed.error().code, ErrorCode::InvalidFieldValue);

    auto e = UdpEndpoint::open(cfg("127.0.0.1"));
    ASSERT_TRUE(e);
    EXPECT_EQ(e->set_remote("junk", 1).error().code, ErrorCode::InvalidFieldValue);
    EXPECT_EQ(e->set_remote("127.0.0.1", 0).error().code, ErrorCode::InvalidFieldValue);
    EXPECT_EQ(e->set_remote("::1", 5).error().code, ErrorCode::InvalidFieldValue);
}

TEST(Udp, BindToUnownedAddressFailsWithOsError) {
    // 203.0.113.0/24 (TEST-NET-3) is never assigned to a local interface.
    auto e = UdpEndpoint::open(cfg("203.0.113.77"));
    ASSERT_FALSE(e);
    EXPECT_EQ(e.error().code, ErrorCode::TransportError);
    EXPECT_NE(e.error().os_error, 0);
}

TEST(Udp, PortAlreadyInUseFails) {
    auto first = UdpEndpoint::open(cfg("127.0.0.1"));
    ASSERT_TRUE(first);
    auto second = UdpEndpoint::open(on_port(cfg("127.0.0.1"), first->local_port()));
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code, ErrorCode::TransportError);
}

TEST(Udp, SendToAndReply) {
    Pair p;
    // reply() before anything was received has no destination.
    EXPECT_EQ(p.a.reply(pattern(8)).error().code, ErrorCode::InvalidFieldValue);

    const cigi::transport::UdpPeer to_b{.address = "127.0.0.1", .port = p.b.local_port()};
    ASSERT_TRUE(p.a.send_to(to_b, pattern(24)));
    std::array<std::byte, 64> buf{};
    auto n = p.b.receive(buf, wait);
    ASSERT_TRUE(n);
    EXPECT_EQ(*n, 24U);

    ASSERT_TRUE(p.b.reply(pattern(16)));
    auto back = p.a.receive(buf, wait);
    ASSERT_TRUE(back);
    EXPECT_EQ(*back, 16U);

    EXPECT_EQ(p.a.send_to({.address = "junk", .port = 5}, pattern(8)).error().code,
              ErrorCode::InvalidFieldValue);
    EXPECT_EQ(p.a.send_to({.address = "127.0.0.1", .port = 0}, pattern(8)).error().code,
              ErrorCode::InvalidFieldValue);
}

TEST(Udp, DroppedDatagramDoesNotChangeLastSender) {
    Pair p;
    auto third = UdpEndpoint::open(cfg("127.0.0.1"));
    ASSERT_TRUE(third);
    ASSERT_TRUE(third->set_remote("127.0.0.1", p.b.local_port()));

    std::array<std::byte, 16> buf{};
    ASSERT_TRUE(p.a.send(pattern(8)));
    ASSERT_TRUE(p.b.receive(buf, wait));
    const auto first = p.b.last_sender();
    ASSERT_TRUE(first);
    ASSERT_EQ(first->port, p.a.local_port());

    ASSERT_TRUE(third->send(pattern(64))); // too big for buf: dropped
    EXPECT_EQ(p.b.receive(buf, wait).error().code, ErrorCode::BufferTooSmall);
    const auto after = p.b.last_sender();
    ASSERT_TRUE(after);
    EXPECT_EQ(after->port, p.a.local_port()); // still the last delivered sender
}

TEST(Udp, SendWithoutRemoteFails) {
    auto e = UdpEndpoint::open(cfg("127.0.0.1"));
    ASSERT_TRUE(e);
    auto r = e->send(pattern(8));
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, ErrorCode::InvalidFieldValue);
}

TEST(Udp, IcmpUnreachableIsIgnoredByReceive) {
    Pair p;
    // Send to a port nobody listens on, provoking ICMP port-unreachable on some stacks.
    {
        auto dead = UdpEndpoint::open(cfg("127.0.0.1"));
        ASSERT_TRUE(dead);
        const auto dead_port = dead->local_port();
        ASSERT_TRUE(p.a.set_remote("127.0.0.1", dead_port));
        // `dead` closes here.
    }
    ASSERT_TRUE(p.a.send(pattern(8)));
    std::this_thread::sleep_for(20ms);
    std::array<std::byte, 16> buf{};
    auto n = p.a.receive(buf, 100ms);
    ASSERT_FALSE(n);
    EXPECT_EQ(n.error().code, ErrorCode::Timeout);

    // Still usable afterwards.
    ASSERT_TRUE(p.b.send(pattern(8)));
    const auto ok = p.a.receive(buf, wait);
    ASSERT_TRUE(ok);
}

TEST(Udp, MovedEndpointAndMovedFromBehaviour) {
    auto e = UdpEndpoint::open(cfg("127.0.0.1"));
    ASSERT_TRUE(e);
    const auto port = e->local_port();
    UdpEndpoint moved = std::move(*e);
    EXPECT_EQ(moved.local_port(), port);
    EXPECT_TRUE(moved.is_open());

    // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(e->is_open());
    EXPECT_EQ(e->local_port(), 0);
    EXPECT_FALSE(e->last_sender());
    std::array<std::byte, 8> buf{};
    EXPECT_EQ(e->send(buf).error().code, ErrorCode::TransportError);
    EXPECT_EQ(e->send_to({.address = "127.0.0.1", .port = 9}, buf).error().code,
              ErrorCode::TransportError);
    EXPECT_EQ(e->reply(buf).error().code, ErrorCode::TransportError);
    EXPECT_EQ(e->receive(buf, 0ms).error().code, ErrorCode::TransportError);
    EXPECT_EQ(e->set_remote("127.0.0.1", 9).error().code, ErrorCode::TransportError);
    // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

    UdpEndpoint assigned = std::move(moved);
    EXPECT_EQ(assigned.local_port(), port);

    auto other = UdpEndpoint::open(cfg("127.0.0.1"));
    ASSERT_TRUE(other);
    *other = std::move(assigned); // move assignment closes the old socket
    EXPECT_EQ(other->local_port(), port);
}

TEST(Udp, Ipv6Loopback) {
    auto a = UdpEndpoint::open(cfg("::1"));
    if (!a) {
        GTEST_SKIP() << "IPv6 loopback not available";
    }
    auto b = UdpEndpoint::open(to(cfg("::1"), "::1", a->local_port()));
    ASSERT_TRUE(b);
    ASSERT_TRUE(b->send(pattern(8)));
    std::array<std::byte, 16> buf{};
    auto n = a->receive(buf, wait);
    ASSERT_TRUE(n);
    EXPECT_EQ(*n, 8U);
    const auto from = a->last_sender();
    ASSERT_TRUE(from);
    EXPECT_EQ(from->address, "::1");
}

} // namespace
