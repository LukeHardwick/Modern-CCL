#include "demo_common/demo_host.hpp"
#include "demo_common/demo_ig.hpp"
#include "demo_common/support.hpp"
#include "messages.hpp"

#include <array>
#include <chrono>
#include <csignal>
#include <gtest/gtest.h>
#include <spdlog/logger.h>
#include <string>
#include <thread>

namespace cigi::demo {
namespace {

using namespace std::chrono_literals;
using test::host_message;
using test::ig_message;

TEST(Support, DescribesDatagramsPacketByPacket) {
    const auto lines = describe_datagram(host_message());
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_NE(lines.at(0).find("Operate"), std::string::npos);
    EXPECT_NE(lines.at(1).find('7'), std::string::npos);

    const auto reply = describe_datagram(ig_message());
    ASSERT_EQ(reply.size(), 1U);

    const std::array<std::byte, 3> junk{};
    const auto bad = describe_datagram(junk);
    ASSERT_EQ(bad.size(), 1U);
    EXPECT_NE(bad.at(0).find("not a CIGI message"), std::string::npos);
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
    using Handler = void (*)(int);
    const Handler previous = std::signal(SIGINT, SIG_DFL);
    {
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
    static_cast<void>(std::signal(SIGINT, previous));
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
    ig.datagrams_foreign = 4;
    ig.pcap_frames = 5;
    ig.fatal = true;
    ig.error = "boom";
    const auto text = format_summary(ig);
    EXPECT_NE(text.find("dropped 17"), std::string::npos);
    EXPECT_NE(text.find("3 Start of Frame not sent"), std::string::npos);
    EXPECT_NE(text.find("4 datagrams ignored"), std::string::npos);
    EXPECT_NE(text.find("pcap: 5"), std::string::npos);
    EXPECT_NE(text.find("FATAL: boom"), std::string::npos);
    EXPECT_EQ(ig.exit_code(), 1);
}

TEST(Support, APcapWriteErrorFailsTheExitCode) {
    IgSummary ig;
    EXPECT_EQ(ig.exit_code(), 0);
    ig.pcap_ok = false;
    EXPECT_EQ(ig.exit_code(), 1);

    HostSummary host;
    host.reached_operate = true;
    host.hat_responses = 1;
    EXPECT_EQ(host.exit_code(), 0);
    host.pcap_ok = false;
    EXPECT_EQ(host.exit_code(), 1);
}

} // namespace
} // namespace cigi::demo
