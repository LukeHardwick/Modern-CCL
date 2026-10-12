// The demo UDP link over loopback sockets (label: network).
#include "demo_common/udp_link.hpp"
#include "messages.hpp"

#include <array>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

namespace cigi::demo {
namespace {

using namespace std::chrono_literals;
using test::host_message;
using test::ig_message;

UdpLinkConfig towards(std::uint16_t port) {
    UdpLinkConfig config;
    config.remote_address = "127.0.0.1";
    config.remote_port = port;
    return config;
}

std::unique_ptr<UdpLink> open_link(UdpLinkConfig config) {
    config.local_address = "127.0.0.1";
    auto link = UdpLink::open(std::move(config));
    EXPECT_TRUE(link.has_value());
    return link ? std::move(*link) : nullptr;
}

std::array<std::byte, 1024>& buffer() {
    static std::array<std::byte, 1024> b{};
    return b;
}

TEST(UdpLink, NominatesACandidateFromAHostMessageAndLatchesOnRequest) {
    auto ig = open_link(UdpLinkConfig{});
    ASSERT_NE(ig, nullptr);
    EXPECT_FALSE(ig->has_peer());
    auto host = open_link(towards(ig->local_port()));
    ASSERT_NE(host, nullptr);
    EXPECT_TRUE(host->has_peer());

    // Nowhere to send yet: counted, not an error.
    const auto reply = ig_message();
    EXPECT_TRUE(ig->send(reply));
    EXPECT_EQ(ig->datagrams_unrouted(), 1U);
    EXPECT_EQ(ig->datagrams_sent(), 0U);

    // Neither junk nor an IG -> Host message nominates a candidate.
    const std::array<std::byte, 4> junk{};
    EXPECT_TRUE(host->send(junk));
    ASSERT_TRUE(ig->receive(buffer(), 1s));
    EXPECT_TRUE(host->send(reply));
    ASSERT_TRUE(ig->receive(buffer(), 1s));
    EXPECT_FALSE(ig->latch_candidate());
    EXPECT_FALSE(ig->has_peer());

    // A Host message does, but the peer is only taken when the owner latches.
    EXPECT_TRUE(host->send(host_message()));
    ASSERT_TRUE(ig->receive(buffer(), 1s));
    EXPECT_FALSE(ig->has_peer());
    EXPECT_TRUE(ig->latch_candidate());
    EXPECT_TRUE(ig->has_peer());

    EXPECT_TRUE(ig->send(reply));
    EXPECT_EQ(ig->datagrams_sent(), 1U);
    const auto got = host->receive(buffer(), 1s);
    ASSERT_TRUE(got);
    EXPECT_EQ(*got, reply.size());
}

TEST(UdpLink, DatagramsFromOtherSendersAreDroppedOnceThePeerIsKnown) {
    auto ig = open_link(UdpLinkConfig{});
    ASSERT_NE(ig, nullptr);
    auto host = open_link(towards(ig->local_port()));
    auto stray = open_link(towards(ig->local_port()));
    ASSERT_NE(host, nullptr);
    ASSERT_NE(stray, nullptr);

    EXPECT_TRUE(host->send(host_message()));
    ASSERT_TRUE(ig->receive(buffer(), 1s));
    ASSERT_TRUE(ig->latch_candidate());

    // The stray datagram is swallowed; the Host's one behind it is delivered.
    EXPECT_TRUE(stray->send(host_message()));
    EXPECT_TRUE(host->send(host_message()));
    ASSERT_TRUE(ig->receive(buffer(), 1s));
    EXPECT_EQ(ig->datagrams_foreign(), 1U);
    EXPECT_EQ(ig->datagrams_received(), 2U);
    // Only strays: a timeout, and still the same peer.
    EXPECT_TRUE(stray->send(host_message()));
    const auto none = ig->receive(buffer(), 100ms);
    ASSERT_FALSE(none);
    EXPECT_EQ(none.error().code, ErrorCode::Timeout);
    EXPECT_EQ(ig->datagrams_foreign(), 2U);
}

TEST(UdpLink, ARestartedHostIsLearntAgainAfterForgetting) {
    auto ig = open_link(UdpLinkConfig{});
    ASSERT_NE(ig, nullptr);
    {
        auto first = open_link(towards(ig->local_port()));
        EXPECT_TRUE(first->send(host_message()));
        ASSERT_TRUE(ig->receive(buffer(), 1s));
        ASSERT_TRUE(ig->latch_candidate());
    } // the Host process ends
    ig->forget_peer();
    EXPECT_FALSE(ig->has_peer());

    auto second = open_link(towards(ig->local_port())); // new port
    EXPECT_TRUE(second->send(host_message()));
    ASSERT_TRUE(ig->receive(buffer(), 1s));
    ASSERT_TRUE(ig->latch_candidate());
    EXPECT_TRUE(ig->send(ig_message()));
    EXPECT_TRUE(second->receive(buffer(), 1s));
    EXPECT_EQ(ig->datagrams_foreign(), 0U);
}

TEST(UdpLink, ConfiguredPeerIsNeverForgottenAndOthersAreIgnored) {
    auto ig = open_link(UdpLinkConfig{});
    ASSERT_NE(ig, nullptr);
    auto host = open_link(towards(ig->local_port()));
    ASSERT_NE(host, nullptr);
    host->forget_peer();
    EXPECT_TRUE(host->has_peer());

    // A stray sender to the Host's port does not reach it (e.g. a second IG).
    UdpLinkConfig back = towards(host->local_port());
    auto stray = open_link(std::move(back));
    ASSERT_NE(stray, nullptr);
    EXPECT_TRUE(stray->send(ig_message()));
    const auto none = host->receive(buffer(), 100ms);
    ASSERT_FALSE(none);
    EXPECT_EQ(host->datagrams_foreign(), 1U);
    EXPECT_EQ(host->datagrams_received(), 0U);
}

/// A record of a pcap written by PcapWriter: addresses and ports of the IPv4/UDP frame.
struct Frame {
    std::array<int, 4> src_ip{};
    std::array<int, 4> dst_ip{};
    int src_port = 0;
    int dst_port = 0;
    std::size_t payload = 0;
};

std::vector<Frame> read_frames(const std::string& pcap) {
    std::vector<Frame> frames;
    std::size_t pos = 24; // file header
    const auto at = [&](std::size_t i) {
        return static_cast<int>(static_cast<unsigned char>(pcap.at(i)));
    };
    while (pos + 16 <= pcap.size()) {
        const std::size_t len =
            static_cast<std::size_t>(at(pos + 8)) + (static_cast<std::size_t>(at(pos + 9)) * 256U);
        const std::size_t ip = pos + 16;
        Frame f;
        for (std::size_t i = 0; i < 4; ++i) {
            f.src_ip.at(i) = at(ip + 12 + i);
            f.dst_ip.at(i) = at(ip + 16 + i);
        }
        f.src_port = (at(ip + 20) * 256) + at(ip + 21);
        f.dst_port = (at(ip + 22) * 256) + at(ip + 23);
        f.payload = len - 28;
        frames.push_back(f);
        pos = ip + len;
    }
    return frames;
}

TEST(UdpLink, WritesPcapWithRealAddressesAndPortsAndCallsTheTap) {
    std::ostringstream pcap_bytes;
    const auto pcap = std::make_shared<tools::PcapWriter>(pcap_bytes);
    int tapped_sent = 0;
    int tapped_received = 0;

    UdpLinkConfig ig_config;
    ig_config.pcap = pcap;
    ig_config.tap = [&](bool sent, std::span<const std::byte> b) {
        (sent ? tapped_sent : tapped_received) += 1;
        EXPECT_FALSE(b.empty());
    };
    ig_config.local_address = ""; // bound to any: the capture must still show 127.0.0.1
    auto ig_link = UdpLink::open(std::move(ig_config));
    ASSERT_TRUE(ig_link.has_value());
    auto& ig = *ig_link;
    auto host = open_link(towards(ig->local_port()));
    ASSERT_NE(host, nullptr);

    EXPECT_TRUE(host->send(host_message()));
    ASSERT_TRUE(ig->receive(buffer(), 1s));
    ASSERT_TRUE(ig->latch_candidate());
    EXPECT_TRUE(ig->send(ig_message()));
    ASSERT_TRUE(host->receive(buffer(), 1s));

    EXPECT_EQ(pcap->frames(), 2U);
    EXPECT_EQ(tapped_sent, 1);
    EXPECT_EQ(tapped_received, 1);
    const auto frames = read_frames(pcap_bytes.str());
    ASSERT_EQ(frames.size(), 2U);
    const std::array<int, 4> loopback{127, 0, 0, 1};
    // Received from the Host: Host port -> IG port.
    EXPECT_EQ(frames.at(0).src_ip, loopback);
    EXPECT_EQ(frames.at(0).dst_ip, loopback);
    EXPECT_EQ(frames.at(0).src_port, host->local_port());
    EXPECT_EQ(frames.at(0).dst_port, ig->local_port());
    EXPECT_EQ(frames.at(0).payload, host_message().size());
    // Sent to the Host: IG port -> Host port.
    EXPECT_EQ(frames.at(1).src_ip, loopback);
    EXPECT_EQ(frames.at(1).dst_ip, loopback);
    EXPECT_EQ(frames.at(1).src_port, ig->local_port());
    EXPECT_EQ(frames.at(1).dst_port, host->local_port());
    EXPECT_EQ(frames.at(1).payload, ig_message().size());
}

TEST(UdpLink, OpenPcapReportsWhyItFailed) {
    std::string error;
    EXPECT_EQ(open_pcap("/nonexistent-dir/x.pcap", error), nullptr);
    EXPECT_NE(error.find("/nonexistent-dir/x.pcap"), std::string::npos);
}

} // namespace
} // namespace cigi::demo
