// The built-in UDP DatagramTransport: learning the peer, filtering other senders (ICD 2.8).

#include "cigi/udp_datagram_transport.hpp"

#include <array>
#include <gtest/gtest.h>

namespace {

using namespace cigi;
using namespace std::chrono_literals;

constexpr std::array<std::byte, 4> payload{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};

std::unique_ptr<UdpDatagramTransport> open_on_loopback(std::uint16_t remote_port = 0,
                                                       bool filter = true) {
    transport::UdpConfig config{
        .local_address = "127.0.0.1",
        .local_port = 0,
        .remote_address = {},
    };
    if (remote_port != 0) {
        config.remote_address = "127.0.0.1";
        config.remote_port = remote_port;
    }
    auto udp = UdpDatagramTransport::open(config, filter);
    EXPECT_TRUE(udp.has_value());
    return std::move(*udp);
}

/// A plain socket that sends to `port` on loopback.
transport::UdpEndpoint talker_to(std::uint16_t port) {
    return transport::UdpEndpoint::open({
                                            .local_address = "127.0.0.1",
                                            .local_port = 0,
                                            .remote_address = "127.0.0.1",
                                            .remote_port = port,
                                        })
        .value();
}

TEST(UdpDatagramTransport, OpenFailsForABadAddress) {
    const auto bad =
        UdpDatagramTransport::open({.local_address = "not an address", .remote_address = {}});
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, ErrorCode::InvalidFieldValue);
}

TEST(UdpDatagramTransport, ATransportWithAConfiguredPeerHasNothingToLearn) {
    std::array<std::byte, 64> buffer{};
    auto sink = open_on_loopback();
    auto fixed = open_on_loopback(sink->local_port());
    EXPECT_TRUE(fixed->has_peer());
    EXPECT_TRUE(fixed->latch_peer()); // nothing to do, nothing wrong
    fixed->forget_peer();             // a configured peer is never forgotten
    EXPECT_TRUE(fixed->has_peer());
    ASSERT_TRUE(fixed->peer().has_value());
    EXPECT_EQ(fixed->peer()->port, sink->local_port());
    EXPECT_NE(fixed->local_port(), 0);
    EXPECT_TRUE(fixed->send(payload).has_value());
    EXPECT_EQ(sink->receive(buffer, 1000ms).value(), payload.size());
}

TEST(UdpDatagramTransport, ALearningTransportSendsNothingUntilItHasAPeer) {
    auto learner = open_on_loopback();
    EXPECT_FALSE(learner->has_peer());
    EXPECT_FALSE(learner->peer().has_value());
    EXPECT_TRUE(learner->send(payload).has_value()); // no peer yet: silently nowhere
    EXPECT_FALSE(learner->latch_peer());             // and nobody has written to it
    EXPECT_FALSE(learner->last_sender().has_value());
}

TEST(UdpDatagramTransport, LatchingAdoptsTheSenderAndOthersAreDroppedAndCounted) {
    std::array<std::byte, 64> buffer{};
    auto learner = open_on_loopback();
    auto host = talker_to(learner->local_port());
    ASSERT_TRUE(host.send(payload).has_value());
    ASSERT_EQ(learner->receive(buffer, 1000ms).value(), payload.size());
    ASSERT_TRUE(learner->last_sender().has_value());
    EXPECT_EQ(learner->last_sender()->port, host.local_port());
    EXPECT_TRUE(learner->latch_peer());
    EXPECT_TRUE(learner->has_peer());
    ASSERT_TRUE(learner->peer().has_value());
    EXPECT_EQ(learner->peer()->port, host.local_port());

    // The learnt Host is answered, a stranger is dropped before the caller sees it.
    EXPECT_TRUE(learner->send(payload).has_value());
    EXPECT_EQ(host.receive(buffer, 1000ms).value(), payload.size());
    auto stranger = talker_to(learner->local_port());
    ASSERT_TRUE(stranger.send(payload).has_value());
    ASSERT_TRUE(host.send(payload).has_value());
    EXPECT_EQ(learner->receive(buffer, 1000ms).value(), payload.size()); // the Host's
    ASSERT_TRUE(learner->last_sender().has_value());
    EXPECT_EQ(learner->last_sender()->port, host.local_port());
    EXPECT_EQ(learner->foreign_datagrams(), 1U);
    // Only strangers waiting: the call times out within its time, having dropped them.
    ASSERT_TRUE(stranger.send(payload).has_value());
    const auto idle = learner->receive(buffer, 50ms);
    ASSERT_FALSE(idle.has_value());
    EXPECT_EQ(idle.error().code, ErrorCode::Timeout);
    EXPECT_EQ(learner->foreign_datagrams(), 2U);
}

TEST(UdpDatagramTransport, ForgettingLetsANewSenderBeLearnt) {
    std::array<std::byte, 64> buffer{};
    auto learner = open_on_loopback();
    auto first = talker_to(learner->local_port());
    ASSERT_TRUE(first.send(payload).has_value());
    ASSERT_TRUE(learner->receive(buffer, 1000ms).has_value());
    ASSERT_TRUE(learner->latch_peer());
    learner->forget_peer();
    EXPECT_FALSE(learner->has_peer());
    auto second = talker_to(learner->local_port());
    ASSERT_TRUE(second.send(payload).has_value());
    ASSERT_TRUE(learner->receive(buffer, 1000ms).has_value());
    ASSERT_TRUE(learner->latch_peer());
    ASSERT_TRUE(learner->peer().has_value());
    EXPECT_EQ(learner->peer()->port, second.local_port());
    EXPECT_EQ(learner->foreign_datagrams(), 0U);
}

TEST(UdpDatagramTransport, AConfiguredPeerHearsNoOneElseUnlessFilteringIsOff) {
    std::array<std::byte, 64> buffer{};
    const auto expected = talker_to(1); // the peer both transports are configured for
    auto filtered = open_on_loopback(expected.local_port());
    auto stranger = talker_to(filtered->local_port());
    ASSERT_TRUE(stranger.send(payload).has_value());
    EXPECT_EQ(filtered->receive(buffer, 100ms).error().code, ErrorCode::Timeout);
    EXPECT_EQ(filtered->foreign_datagrams(), 1U);

    auto unfiltered = open_on_loopback(expected.local_port(), /*filter=*/false);
    auto anyone = talker_to(unfiltered->local_port());
    ASSERT_TRUE(anyone.send(payload).has_value());
    EXPECT_EQ(unfiltered->receive(buffer, 1000ms).value(), payload.size());
    EXPECT_EQ(unfiltered->foreign_datagrams(), 0U);
}

TEST(UdpEndpoint, KnowsItsRemoteAndWhetherTheLastSenderWasIt) {
    std::array<std::byte, 64> buffer{};
    auto a = talker_to(1);
    EXPECT_TRUE(a.remote().has_value());
    EXPECT_FALSE(a.last_sender_is_remote()); // nothing received yet
    const auto unconnected =
        transport::UdpEndpoint::open({.local_address = "127.0.0.1", .remote_address = {}}).value();
    EXPECT_FALSE(unconnected.remote().has_value());
    EXPECT_FALSE(unconnected.last_sender_is_remote());
    auto b = talker_to(a.local_port());
    ASSERT_TRUE(b.send(payload).has_value());
    ASSERT_TRUE(a.receive(buffer, 1000ms).has_value());
    EXPECT_FALSE(a.last_sender_is_remote()); // a's remote is port 1, b is someone else
    ASSERT_TRUE(a.set_remote("127.0.0.1", b.local_port()).has_value());
    EXPECT_TRUE(a.last_sender_is_remote());
}

} // namespace
