#include "demo_common/options.hpp"

#include <CLI/CLI.hpp>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace cigi::demo {
namespace {

// CLI11 parses a vector in reverse order of arguments, as from the command line.
bool parse_ig(DemoIgOptions& options, std::vector<std::string> args) {
    CLI::App app;
    add_demo_ig_options(app, options);
    try {
        std::vector<std::string> reversed(args.rbegin(), args.rend());
        app.parse(reversed);
    } catch (const CLI::ParseError&) {
        return false;
    }
    return true;
}

bool parse_host(DemoHostOptions& options, std::vector<std::string> args) {
    CLI::App app;
    add_demo_host_options(app, options);
    try {
        std::vector<std::string> reversed(args.rbegin(), args.rend());
        app.parse(reversed);
    } catch (const CLI::ParseError&) {
        return false;
    }
    return true;
}

TEST(IgOptions, DefaultsAreTheDocumentedOnes) {
    DemoIgOptions o;
    ASSERT_TRUE(parse_ig(o, {}));
    EXPECT_EQ(o.listen_port, 8004);
    EXPECT_EQ(o.host_port, 8005);
    EXPECT_TRUE(o.host_addr.empty());
    EXPECT_DOUBLE_EQ(o.rate_hz, 60.0);
    EXPECT_EQ(o.load_frames, 0U);
    EXPECT_EQ(o.log_level, "info");
    EXPECT_FALSE(o.refuse_operate);
    EXPECT_FALSE(o.has_network_faults());
    EXPECT_TRUE(validate(o).empty());
}

TEST(IgOptions, EveryFlagIsParsed) {
    DemoIgOptions o;
    ASSERT_TRUE(parse_ig(o, {"--listen-addr",
                             "127.0.0.1",
                             "--listen-port",
                             "9000",
                             "--host-addr",
                             "10.0.0.5",
                             "--host-port",
                             "9001",
                             "--rate",
                             "30",
                             "--load-frames",
                             "12",
                             "--pcap",
                             "x.pcap",
                             "--duration",
                             "5",
                             "--log-level",
                             "debug",
                             "--refuse-operate",
                             "--pause-sof-after",
                             "1",
                             "--resume-sof-after",
                             "2.5",
                             "--restart-after",
                             "7",
                             "--drop",
                             "0.2",
                             "--duplicate",
                             "0.1",
                             "--latency",
                             "15",
                             "--seed",
                             "42",
                             "--table"}));
    EXPECT_EQ(o.listen_addr, "127.0.0.1");
    EXPECT_EQ(o.listen_port, 9000);
    EXPECT_EQ(o.host_addr, "10.0.0.5");
    EXPECT_EQ(o.host_port, 9001);
    EXPECT_DOUBLE_EQ(o.rate_hz, 30.0);
    EXPECT_EQ(o.load_frames, 12U);
    EXPECT_EQ(o.pcap, "x.pcap");
    EXPECT_DOUBLE_EQ(o.duration_s, 5.0);
    EXPECT_EQ(o.log_level, "debug");
    EXPECT_TRUE(o.refuse_operate);
    EXPECT_DOUBLE_EQ(o.pause_sof_after_s, 1.0);
    EXPECT_DOUBLE_EQ(o.resume_sof_after_s, 2.5);
    EXPECT_DOUBLE_EQ(o.restart_after_s, 7.0);
    EXPECT_DOUBLE_EQ(o.drop, 0.2);
    EXPECT_DOUBLE_EQ(o.duplicate, 0.1);
    EXPECT_DOUBLE_EQ(o.latency_ms, 15.0);
    EXPECT_EQ(o.seed, 42U);
    EXPECT_TRUE(o.table);
    EXPECT_TRUE(o.has_network_faults());
    EXPECT_TRUE(validate(o).empty());
}

TEST(IgOptions, BadValuesAreRejectedByTheParser) {
    DemoIgOptions o;
    EXPECT_FALSE(parse_ig(o, {"--drop", "1.5"}));
    EXPECT_FALSE(parse_ig(o, {"--duplicate", "-0.1"}));
    EXPECT_FALSE(parse_ig(o, {"--rate", "0"}));
    EXPECT_FALSE(parse_ig(o, {"--latency", "-1"}));
    EXPECT_FALSE(parse_ig(o, {"--log-level", "chatty"}));
    EXPECT_FALSE(parse_ig(o, {"--listen-port", "70000"}));
    EXPECT_FALSE(parse_ig(o, {"--no-such-flag"}));
}

TEST(IgOptions, ValidateChecksCombinations) {
    MockIgOptions o;
    o.resume_sof_after_s = 2;
    EXPECT_NE(validate(o).find("--pause-sof-after"), std::string::npos);
    o.pause_sof_after_s = 3;
    EXPECT_NE(validate(o).find("later"), std::string::npos);
    o.pause_sof_after_s = 1;
    EXPECT_TRUE(validate(o).empty());

    o = {};
    o.host_addr = "127.0.0.1";
    o.host_port = 0;
    EXPECT_NE(validate(o).find("--host-port"), std::string::npos);

    o = {};
    o.rate_hz = 0;
    EXPECT_FALSE(validate(o).empty());
    o = {};
    o.drop = 2;
    EXPECT_FALSE(validate(o).empty());
    o = {};
    o.log_level = "loud";
    EXPECT_FALSE(validate(o).empty());
}

TEST(HostOptions, DefaultsAndFlags) {
    DemoHostOptions o;
    ASSERT_TRUE(parse_host(o, {}));
    EXPECT_EQ(o.ig_addr, "127.0.0.1");
    EXPECT_EQ(o.ig_port, 8004);
    EXPECT_EQ(o.listen_port, 8005);
    EXPECT_TRUE(o.synchronous);
    EXPECT_TRUE(validate(o).empty());

    ASSERT_TRUE(parse_host(o, {"--async", "--rate", "20", "--ig-addr", "10.1.2.3", "--ig-port",
                               "9004", "--listen-port", "9005", "--peer-timeout", "300",
                               "--duration", "2", "--pcap", "h.pcap", "--log-level", "warn"}));
    EXPECT_FALSE(o.synchronous);
    EXPECT_DOUBLE_EQ(o.rate_hz, 20.0);
    EXPECT_EQ(o.ig_addr, "10.1.2.3");
    EXPECT_EQ(o.ig_port, 9004);
    EXPECT_EQ(o.listen_port, 9005);
    EXPECT_EQ(o.peer_timeout_ms, 300U);
    EXPECT_DOUBLE_EQ(o.duration_s, 2.0);
    EXPECT_EQ(o.pcap, "h.pcap");
    EXPECT_EQ(o.log_level, "warn");

    ASSERT_TRUE(parse_host(o, {"--sync"}));
    EXPECT_TRUE(o.synchronous);
}

TEST(HostOptions, ValidateRejectsNonsense) {
    DemoHostOptions o;
    o.ig_addr.clear();
    EXPECT_FALSE(validate(o).empty());
    o = {};
    o.ig_port = 0;
    EXPECT_FALSE(validate(o).empty());
    o = {};
    o.peer_timeout_ms = 0;
    EXPECT_FALSE(validate(o).empty());
    o = {};
    o.log_level = "x";
    EXPECT_FALSE(validate(o).empty());
}

TEST(LogLevel, KnownNames) {
    for (const char* name : {"trace", "debug", "info", "warn", "error", "off"}) {
        EXPECT_TRUE(is_log_level(name)) << name;
    }
    EXPECT_FALSE(is_log_level("warning"));
    EXPECT_FALSE(is_log_level(""));
}

} // namespace
} // namespace cigi::demo
