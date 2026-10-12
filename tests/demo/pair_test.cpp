// demo_ig and demo_host against each other over loopback UDP, in one process (label: network).
// Ports are ephemeral, so parallel test runs never collide. Durations are short; assertions are
// about what happened, never about exact rates.

#include "demo_common/demo_host.hpp"
#include "demo_common/demo_ig.hpp"
#include "demo_common/support.hpp"
#include "demo_common/udp_link.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <random>
#include <spdlog/logger.h>
#include <spdlog/sinks/null_sink.h>
#include <string>
#include <thread>

namespace cigi::demo {
namespace {

struct PairResult {
    IgSummary ig;
    HostSummary host;
};

bool contains(const std::vector<std::string>& v, const std::string& item) {
    return std::ranges::find(v, item) != v.end();
}

/// Run a demo IG and a demo Host against each other for `seconds` of Host time.
PairResult run_pair(DemoIgOptions ig_options, DemoHostOptions host_options, double seconds) {
    ig_options.listen_port = 0;
    ig_options.listen_addr = "127.0.0.1";
    ig_options.log_level = "off";
    ig_options.duration_s = 0; // stopped explicitly
    host_options.log_level = "off";
    host_options.listen_port = 0;
    host_options.duration_s = seconds;

    auto ig = DemoIg::create(ig_options, make_logger("ig", "off"));
    EXPECT_TRUE(ig.has_value()) << (ig ? "" : ig.error());
    if (!ig) {
        return {};
    }
    host_options.ig_port = (*ig)->bound_port();
    auto host = DemoHost::create(host_options, make_logger("host", "off"));
    EXPECT_TRUE(host.has_value()) << (host ? "" : host.error());
    if (!host) {
        return {};
    }

    PairResult result;
    std::stop_source stop;
    std::thread ig_thread([&] { result.ig = (*ig)->run(stop.get_token()); });
    result.host = (*host)->run(std::stop_token{});
    stop.request_stop();
    ig_thread.join();
    return result;
}

PairResult run_default(double seconds) {
    const DemoIgOptions ig;
    const DemoHostOptions host;
    return run_pair(ig, host, seconds);
}

PairResult run_with_ig(const DemoIgOptions& ig, double seconds) {
    const DemoHostOptions host;
    return run_pair(ig, host, seconds);
}

PairResult run_with_host(const DemoHostOptions& host, double seconds) {
    const DemoIgOptions ig;
    return run_pair(ig, host, seconds);
}

std::filesystem::path unique_temp_dir() {
    std::random_device random;
    for (;;) {
        const auto dir = std::filesystem::temp_directory_path() /
                         std::format("cigi_demo_pair_{:08x}{:08x}", random(), random());
        if (std::filesystem::create_directory(dir)) {
            return dir;
        }
    }
}

TEST(DemoPair, HealthyRunExitsZero) {
    const auto r = run_default(3.0);
    EXPECT_EQ(r.host.exit_code(), 0);
    EXPECT_EQ(r.ig.exit_code(), 0);
    EXPECT_TRUE(r.host.reached_operate);
    EXPECT_EQ(r.host.final_state, LinkState::Operate);
    EXPECT_EQ(r.host.protocol_errors, 0U);
    EXPECT_EQ(r.host.host.link_losses, 0U);
    // Responses: one-shot HAT and HOT, then the periodic extended request every 30th frame.
    EXPECT_GE(r.host.hat_responses, 2U);
    EXPECT_GE(r.host.extended_responses, 2U);
    EXPECT_EQ(r.host.invalid_responses, 0U);
    // The scene the IG built from the Host's packets.
    EXPECT_EQ(r.ig.scene.entities_created, 1U);
    EXPECT_EQ(r.ig.entities, 1U);
    // The IG may send one more periodic answer after the Host's last poll.
    EXPECT_GE(r.ig.hat_hot_answers, r.host.responses());
    EXPECT_LE(r.ig.hat_hot_answers - r.host.responses(), 1U);
    EXPECT_GT(r.ig.operate_frames, 100U);
    EXPECT_EQ(r.ig.ig.restarts, 0U);
    EXPECT_EQ(r.ig.operate_refused, 0U);
    EXPECT_FALSE(r.ig.faults_active);
}

TEST(DemoPair, RefusingOperateKeepsTheHostInStandby) {
    DemoIgOptions ig;
    ig.refuse_operate = true;
    const auto r = run_with_ig(ig, 1.5);
    EXPECT_FALSE(r.host.reached_operate);
    EXPECT_EQ(r.host.responses(), 0U);
    EXPECT_EQ(r.host.exit_code(), 1);
    EXPECT_GT(r.ig.operate_refused, 0U);
    EXPECT_EQ(r.ig.operate_frames, 0U);
    EXPECT_EQ(r.host.final_state, LinkState::Standby);
    EXPECT_NE(format_summary(r.host).find("NO (never)"), std::string::npos);
    EXPECT_EQ(r.ig.exit_code(), 0);
}

TEST(DemoPair, LoadFramesDelayOperate) {
    DemoIgOptions ig;
    ig.load_frames = 60; // one second at 60 Hz
    const auto r = run_with_ig(ig, 3.0);
    EXPECT_EQ(r.host.exit_code(), 0);
    EXPECT_TRUE(contains(r.host.transitions, "WaitingForIg -> Standby"));
    EXPECT_TRUE(contains(r.host.transitions, "Standby -> Operate"));
    // About one second of Start of Frame before Operate: well under the whole run is Operate.
    EXPECT_LT(r.ig.operate_frames, r.ig.ig.frames_sent - 40);
    EXPECT_GT(r.host.host.time_in(LinkState::Standby), std::chrono::milliseconds{700});
}

TEST(DemoPair, PausedStartOfFrameLosesTheLinkAndItRecovers) {
    DemoIgOptions ig;
    ig.pause_sof_after_s = 1.0;
    ig.resume_sof_after_s = 2.0;
    DemoHostOptions host;
    host.peer_timeout_ms = 400;
    const auto r = run_pair(ig, host, 4.0);
    EXPECT_GE(r.host.host.link_losses, 1U);
    EXPECT_TRUE(contains(r.host.transitions, "Operate -> Lost"));
    EXPECT_EQ(r.host.final_state, LinkState::Operate);
    EXPECT_GT(r.ig.ig.frames_skipped, 30U);
    EXPECT_EQ(r.host.exit_code(), 0);
    EXPECT_GE(r.host.host.time_in(LinkState::Lost), std::chrono::milliseconds{300});
}

TEST(DemoPair, DroppedDatagramsAreCountedAndTheRunStillSucceeds) {
    DemoIgOptions ig;
    ig.drop = 0.2;
    ig.seed = 7;
    const auto r = run_with_ig(ig, 3.0);
    EXPECT_EQ(r.host.exit_code(), 0);
    ASSERT_TRUE(r.ig.faults_active);
    EXPECT_GT(r.ig.faults.dropped, 10U);
    EXPECT_GT(r.host.host.session.lost_frames, 5U);
    EXPECT_EQ(r.host.protocol_errors, 0U);
}

TEST(DemoPair, DuplicatedAndDelayedDatagramsAreTolerated) {
    DemoIgOptions ig;
    ig.duplicate = 0.3;
    ig.latency_ms = 20;
    ig.seed = 3;
    const auto r = run_with_ig(ig, 3.0);
    EXPECT_EQ(r.host.exit_code(), 0);
    EXPECT_GT(r.ig.faults.duplicated, 10U);
    EXPECT_GT(r.ig.faults.delayed, 10U);
    EXPECT_GT(r.host.host.session.duplicate_frames, 5U);
}

TEST(DemoPair, RestartedIgIsDetectedAndOperateResumes) {
    DemoIgOptions ig;
    ig.restart_after_s = 1.5;
    const auto r = run_with_ig(ig, 4.0);
    EXPECT_EQ(r.ig.ig.restarts, 1U);
    EXPECT_GE(r.host.host.ig_restarts + r.host.host.link_losses, 1U);
    EXPECT_EQ(r.host.final_state, LinkState::Operate);
    EXPECT_EQ(r.host.exit_code(), 0);
    // The restarted IG built its scene again from the Host's repeated entity updates.
    EXPECT_EQ(r.ig.scene.entities_created, 1U);
    EXPECT_EQ(r.ig.entities, 1U);
}

TEST(DemoPair, AsynchronousHostWorksToo) {
    DemoHostOptions host;
    host.synchronous = false;
    host.rate_hz = 30;
    const auto r = run_with_host(host, 2.0);
    EXPECT_EQ(r.host.exit_code(), 0);
    EXPECT_GT(r.host.responses(), 0U);
}

TEST(DemoPair, BothSidesRecordPcapFiles) {
    // A directory of our own: concurrent test runs (debug, asan, ...) must not share files.
    const auto dir = unique_temp_dir();
    const auto ig_file = dir / "ig.pcap";
    const auto host_file = dir / "host.pcap";
    DemoIgOptions ig;
    ig.pcap = ig_file.string();
    DemoHostOptions host;
    host.pcap = host_file.string();
    const auto r = run_pair(ig, host, 1.5);
    EXPECT_EQ(r.host.exit_code(), 0);
    EXPECT_TRUE(r.ig.pcap_ok);
    EXPECT_TRUE(r.host.pcap_ok);
    // Each side captured what it sent plus what it received; the datagrams are the same ones.
    // (Within a couple: the stats are read a moment before the pcap's final count.)
    EXPECT_NEAR(static_cast<double>(r.host.pcap_frames),
                static_cast<double>(r.host.host.frames_sent + r.host.host.datagrams_received), 2.0);
    EXPECT_EQ(r.ig.pcap_frames, r.ig.datagrams_sent + r.ig.ig.datagrams_received);
    EXPECT_GT(r.ig.pcap_frames, 100U);
    for (const auto& file : {ig_file, host_file}) {
        std::ifstream in(file, std::ios::binary);
        std::string magic(4, '\0');
        in.read(magic.data(), 4);
        EXPECT_EQ(static_cast<unsigned char>(magic.at(0)), 0xD4U) << file;
        EXPECT_EQ(static_cast<unsigned char>(magic.at(1)), 0xC3U) << file;
        EXPECT_GT(std::filesystem::file_size(file), 1000U);
        std::filesystem::remove(file);
    }
    std::filesystem::remove(dir);
}

TEST(DemoPair, AStraySenderCannotDriveTheScene) {
    DemoIgOptions ig_options;
    ig_options.listen_port = 0;
    ig_options.listen_addr = "127.0.0.1";
    auto ig = DemoIg::create(ig_options, make_logger("ig", "off"));
    ASSERT_TRUE(ig.has_value());
    DemoHostOptions host_options;
    host_options.ig_port = (*ig)->bound_port();
    host_options.listen_port = 0;
    host_options.duration_s = 2.0;
    auto host = DemoHost::create(host_options, make_logger("host", "off"));
    ASSERT_TRUE(host.has_value());

    // A stranger sends Host messages (creating entity 7) to the IG once the real Host is known.
    UdpLinkConfig stray_config;
    stray_config.local_address = "127.0.0.1";
    stray_config.remote_address = "127.0.0.1";
    stray_config.remote_port = (*ig)->bound_port();
    auto stray = UdpLink::open(std::move(stray_config));
    ASSERT_TRUE(stray.has_value());
    std::vector<std::byte> storage(256);
    MessageBuilder builder(Direction::HostToIg, storage);
    IgControl control;
    control.ig_mode = IgMode::Operate;
    ASSERT_TRUE(builder.add(control));
    EntityControl entity;
    entity.entity_id = 7;
    entity.entity_state = EntityState::Active;
    ASSERT_TRUE(builder.add(entity));

    std::stop_source stop;
    IgSummary ig_summary;
    std::thread ig_thread([&] { ig_summary = (*ig)->run(stop.get_token()); });
    std::thread stray_thread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds{500});
        for (int i = 0; i < 30; ++i) {
            static_cast<void>((*stray)->send(builder.bytes()));
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
    });
    const auto host_summary = (*host)->run(std::stop_token{});
    stray_thread.join();
    stop.request_stop();
    ig_thread.join();

    EXPECT_EQ(host_summary.exit_code(), 0);
    EXPECT_GE(ig_summary.datagrams_foreign, 25U);
    EXPECT_EQ(ig_summary.scene.entities_created, 1U); // the Host's ownship only, never entity 7
}

TEST(DemoPair, CreateReportsProblemsAsMessages) {
    DemoIgOptions ig;
    ig.pcap = "/nonexistent-dir/ig.pcap";
    auto created = DemoIg::create(ig, make_logger("ig", "off"));
    ASSERT_FALSE(created.has_value());
    EXPECT_NE(created.error().find("/nonexistent-dir/ig.pcap"), std::string::npos);

    ig = {};
    ig.rate_hz = -1;
    EXPECT_FALSE(DemoIg::create(ig, make_logger("ig", "off")).has_value());

    DemoHostOptions host;
    host.ig_addr = "not-an-address";
    EXPECT_FALSE(DemoHost::create(host, make_logger("host", "off")).has_value());
    host = {};
    host.pcap = "/nonexistent-dir/h.pcap";
    EXPECT_FALSE(DemoHost::create(host, make_logger("host", "off")).has_value());
    host = {};
    host.ig_port = 0;
    EXPECT_FALSE(DemoHost::create(host, make_logger("host", "off")).has_value());
}

std::shared_ptr<spdlog::logger> silent_debug_logger(const std::string& name) {
    auto logger =
        std::make_shared<spdlog::logger>(name, std::make_shared<spdlog::sinks::null_sink_mt>());
    logger->set_level(spdlog::level::debug); // formats every line, writes none
    return logger;
}

TEST(DemoPair, TableAndDebugLoggingDoNotChangeTheOutcome) {
    DemoIgOptions ig;
    ig.table = true;
    ig.log_level = "debug";
    DemoHostOptions host;
    host.log_level = "debug";
    // run_pair forces the levels off; build this pair by hand so the debug paths run.
    ig.listen_port = 0;
    ig.listen_addr = "127.0.0.1";
    auto ig_app = DemoIg::create(ig, silent_debug_logger("ig"));
    ASSERT_TRUE(ig_app.has_value());
    host.ig_port = (*ig_app)->bound_port();
    host.listen_port = 0;
    host.duration_s = 1.5;
    auto host_app = DemoHost::create(host, silent_debug_logger("host"));
    ASSERT_TRUE(host_app.has_value());
    std::stop_source stop;
    IgSummary ig_summary;
    std::thread t([&] { ig_summary = (*ig_app)->run(stop.get_token()); });
    const auto host_summary = (*host_app)->run(std::stop_token{});
    stop.request_stop();
    t.join();
    EXPECT_EQ(host_summary.exit_code(), 0);
}

} // namespace
} // namespace cigi::demo
