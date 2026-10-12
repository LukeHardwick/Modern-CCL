#include "demo_common/demo_ig.hpp"
#include "demo_common/support.hpp"
#include "demo_common/udp_link.hpp"

#include <array>
#include <cigi/message/builder.hpp>
#include <csignal>
#include <cstddef>
#include <gtest/gtest.h>
#include <spdlog/logger.h>
#include <sstream>
#include <string>

namespace cigi::demo {
namespace {

using namespace std::chrono_literals;

std::vector<std::byte> host_message() {
    std::vector<std::byte> storage(256);
    MessageBuilder builder(Direction::HostToIg, storage);
    IgControl control;
    control.ig_mode = IgMode::Operate;
    EXPECT_TRUE(builder.add(control));
    EntityControl entity;
    entity.entity_id = 7;
    entity.entity_state = EntityState::Active;
    entity.entity_type = 101;
    EXPECT_TRUE(builder.add(entity));
    const auto bytes = builder.bytes();
    return {bytes.begin(), bytes.end()};
}

std::vector<std::byte> ig_message() {
    std::vector<std::byte> storage(64);
    MessageBuilder builder(Direction::IgToHost, storage);
    EXPECT_TRUE(builder.add(StartOfFrame{.ig_mode = IgMode::Operate}));
    const auto bytes = builder.bytes();
    return {bytes.begin(), bytes.end()};
}

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

TEST(UdpLink, LearnsItsPeerFromTheFirstHostMessageOnly) {
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

    std::array<std::byte, 1024> buffer{};
    // A stray datagram does not become the peer.
    const std::array<std::byte, 4> junk{};
    EXPECT_TRUE(host->send(junk));
    ASSERT_TRUE(ig->receive(buffer, 1s));
    EXPECT_FALSE(ig->has_peer());
    // An IG -> Host message does not either.
    EXPECT_TRUE(host->send(reply));
    ASSERT_TRUE(ig->receive(buffer, 1s));
    EXPECT_FALSE(ig->has_peer());

    EXPECT_TRUE(host->send(host_message()));
    ASSERT_TRUE(ig->receive(buffer, 1s));
    EXPECT_TRUE(ig->has_peer());

    EXPECT_TRUE(ig->send(reply));
    EXPECT_EQ(ig->datagrams_sent(), 1U);
    const auto got = host->receive(buffer, 1s);
    ASSERT_TRUE(got);
    EXPECT_EQ(*got, reply.size());

    ig->forget_peer();
    EXPECT_FALSE(ig->has_peer());
    EXPECT_EQ(host->datagrams_received(), 1U);
}

TEST(UdpLink, ConfiguredPeerIsNeverForgotten) {
    auto a = open_link(UdpLinkConfig{});
    ASSERT_NE(a, nullptr);
    auto b = open_link(towards(a->local_port()));
    ASSERT_NE(b, nullptr);
    b->forget_peer();
    EXPECT_TRUE(b->has_peer());
}

TEST(UdpLink, WritesPcapAndCallsTheTap) {
    std::ostringstream pcap_bytes;
    auto pcap = std::make_shared<tools::PcapWriter>(pcap_bytes);
    int tapped_sent = 0;
    int tapped_received = 0;

    UdpLinkConfig ig_config;
    ig_config.pcap = pcap;
    ig_config.tap = [&](bool sent, std::span<const std::byte> b) {
        (sent ? tapped_sent : tapped_received) += 1;
        EXPECT_FALSE(b.empty());
    };
    auto ig = open_link(std::move(ig_config));
    ASSERT_NE(ig, nullptr);
    auto host = open_link(towards(ig->local_port()));
    ASSERT_NE(host, nullptr);

    std::array<std::byte, 1024> buffer{};
    EXPECT_TRUE(host->send(host_message()));
    ASSERT_TRUE(ig->receive(buffer, 1s));
    EXPECT_TRUE(ig->send(ig_message()));
    ASSERT_TRUE(host->receive(buffer, 1s));

    EXPECT_EQ(pcap->frames(), 2U);
    EXPECT_EQ(tapped_sent, 1);
    EXPECT_EQ(tapped_received, 1);
    // 24-byte file header + two records (16 + 20 + 8 + payload).
    const auto host_len = host_message().size();
    const auto ig_len = ig_message().size();
    EXPECT_EQ(pcap_bytes.str().size(), 24 + (16 + 28 + host_len) + (16 + 28 + ig_len));
}

TEST(UdpLink, OpenPcapReportsWhyItFailed) {
    std::string error;
    EXPECT_EQ(open_pcap("/nonexistent-dir/x.pcap", error), nullptr);
    EXPECT_NE(error.find("/nonexistent-dir/x.pcap"), std::string::npos);
}

TEST(Support, DescribesDatagramsPacketByPacket) {
    const auto lines = describe_datagram(host_message());
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_NE(lines[0].find("Operate"), std::string::npos);
    EXPECT_NE(lines[1].find("7"), std::string::npos);

    const auto reply = describe_datagram(ig_message());
    ASSERT_EQ(reply.size(), 1U);

    const std::array<std::byte, 3> junk{};
    const auto bad = describe_datagram(junk);
    ASSERT_EQ(bad.size(), 1U);
    EXPECT_NE(bad[0].find("not a CIGI message"), std::string::npos);
}

TEST(Support, LoggerHonoursItsLevel) {
    const auto logger = make_logger("test", "warn");
    EXPECT_TRUE(logger->should_log(spdlog::level::warn));
    EXPECT_FALSE(logger->should_log(spdlog::level::info));
    EXPECT_FALSE(make_logger("off", "off")->should_log(spdlog::level::err));
    EXPECT_TRUE(make_logger("dbg", "debug")->should_log(spdlog::level::debug));
    EXPECT_TRUE(make_logger("trc", "trace")->should_log(spdlog::level::trace));
    EXPECT_TRUE(make_logger("err", "error")->should_log(spdlog::level::err));
    EXPECT_TRUE(make_logger("def", "bogus")->should_log(spdlog::level::info));
}

TEST(Support, RunControlStopsOnSigint) {
    const RunControl control;
    EXPECT_FALSE(control.token().stop_requested());
    EXPECT_FALSE(control.interrupted());
    std::raise(SIGINT);
    for (int i = 0; i < 100 && !control.token().stop_requested(); ++i) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_TRUE(control.token().stop_requested());
    EXPECT_TRUE(control.interrupted());
}

TEST(Support, EntityTableListsEveryEntity) {
    mock::SceneModel scene;
    EXPECT_NE(format_entity_table(scene).find("(no entities)"), std::string::npos);

    EntityControl e;
    e.entity_id = 12;
    e.entity_state = EntityState::Active;
    e.entity_type = 101;
    e.latitude_or_x = 51.5;
    e.longitude_or_y = -0.12;
    e.altitude_or_z = 300;
    scene.apply(e);
    scene.advance(0.016);
    const auto table = format_entity_table(scene);
    EXPECT_NE(table.find("Active"), std::string::npos);
    EXPECT_NE(table.find("51.5"), std::string::npos);
    EXPECT_EQ(table.find("(no entities)"), std::string::npos);
}

TEST(Support, SummariesMentionTheNumbersAFaultTestReads) {
    IgSummary ig;
    ig.faults_active = true;
    ig.faults.dropped = 17;
    ig.datagrams_unrouted = 3;
    ig.pcap_frames = 5;
    ig.fatal = true;
    ig.error = "boom";
    const auto text = format_summary(ig);
    EXPECT_NE(text.find("dropped 17"), std::string::npos);
    EXPECT_NE(text.find("3 datagrams not sent"), std::string::npos);
    EXPECT_NE(text.find("pcap: 5"), std::string::npos);
    EXPECT_NE(text.find("FATAL: boom"), std::string::npos);
    EXPECT_EQ(ig.exit_code(), 1);
}

} // namespace
} // namespace cigi::demo
