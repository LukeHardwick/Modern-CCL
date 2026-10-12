// The UDP DatagramTransport the facades use: learning the peer from the first valid datagram.

#include "../../src/runtime/udp_transport.hpp"

#include <array>
#include <gtest/gtest.h>

namespace {

using namespace cigi;
using namespace std::chrono_literals;

transport::UdpEndpoint open_on_loopback() {
    return transport::UdpEndpoint::open({.local_address = "127.0.0.1", .remote_address = {}})
        .value();
}

TEST(UdpDatagramTransport, ATransportWithAConfiguredPeerHasNothingToLearn) {
    detail::UdpDatagramTransport fixed{open_on_loopback(), /*learn_peer=*/false};
    EXPECT_TRUE(fixed.latch_peer()); // nothing to do, nothing wrong
}

TEST(UdpDatagramTransport, ALearningTransportSendsNothingUntilItHasAPeer) {
    detail::UdpDatagramTransport learner{open_on_loopback(), /*learn_peer=*/true};
    const std::array<std::byte, 4> data{};
    EXPECT_TRUE(learner.send(data).has_value()); // no peer yet: silently nowhere
    EXPECT_FALSE(learner.latch_peer());          // and nobody has written to it
}

} // namespace
