#include "demo_common/options.hpp"

#include <CLI/CLI.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace cigi::demo {

namespace {

constexpr std::array<std::string_view, 6> log_levels{"trace", "debug", "info",
                                                     "warn",  "error", "off"};

void add_log_level(CLI::App& app, std::string& level) {
    app.add_option("--log-level", level, "trace, debug, info, warn, error or off")
        ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error", "off"}))
        ->capture_default_str();
}

} // namespace

bool is_log_level(std::string_view name) noexcept {
    return std::ranges::find(log_levels, name) != log_levels.end();
}

void add_mock_ig_options(CLI::App& app, MockIgOptions& o) {
    app.add_option("--listen-addr", o.listen_addr, "Interface to bind (default: all)");
    app.add_option("--listen-port", o.listen_port, "UDP port the IG listens on")
        ->capture_default_str();
    app.add_option("--host-addr", o.host_addr,
                   "Host address Start of Frame is sent to (default: learn it from the Host)");
    app.add_option("--host-port", o.host_port, "Host UDP port (with --host-addr)")
        ->capture_default_str();
    app.add_option("--rate", o.rate_hz, "Start of Frame rate in Hz")
        ->check(CLI::PositiveNumber)
        ->capture_default_str();
    app.add_option("--load-frames", o.load_frames,
                   "Frames spent loading in Standby before accepting Operate")
        ->capture_default_str();
    app.add_option("--pcap", o.pcap, "Write every sent and received datagram to this pcap file");
    add_log_level(app, o.log_level);

    auto* faults = app.add_option_group("Fault injection");
    faults->add_flag("--refuse-operate", o.refuse_operate, "Refuse every request for Operate");
    faults
        ->add_option("--pause-sof-after", o.pause_sof_after_s,
                     "Stop sending Start of Frame after this many seconds")
        ->check(CLI::NonNegativeNumber);
    faults
        ->add_option("--resume-sof-after", o.resume_sof_after_s,
                     "Resume sending Start of Frame after this many seconds")
        ->check(CLI::NonNegativeNumber);
    faults
        ->add_option("--restart-after", o.restart_after_s,
                     "Behave as if the IG restarted after this many seconds")
        ->check(CLI::NonNegativeNumber);
    faults->add_option("--drop", o.drop, "Chance (0..1) that a sent datagram is lost")
        ->check(CLI::Range(0.0, 1.0));
    faults
        ->add_option("--duplicate", o.duplicate, "Chance (0..1) that a sent datagram is sent twice")
        ->check(CLI::Range(0.0, 1.0));
    faults->add_option("--latency", o.latency_ms, "Milliseconds every sent datagram is delayed")
        ->check(CLI::NonNegativeNumber);
    faults->add_option("--seed", o.seed, "Seed of the fault dice")->capture_default_str();
}

void add_demo_ig_options(CLI::App& app, DemoIgOptions& o) {
    add_mock_ig_options(app, o);
    app.add_option("--duration", o.duration_s, "Run for this many seconds (default: until Ctrl-C)")
        ->check(CLI::NonNegativeNumber);
    app.add_flag("--table", o.table, "Print the entity table every second");
}

void add_demo_host_options(CLI::App& app, DemoHostOptions& o) {
    app.add_option("--ig-addr", o.ig_addr, "IG address (numeric)")->capture_default_str();
    app.add_option("--ig-port", o.ig_port, "IG UDP port")->capture_default_str();
    app.add_option("--listen-port", o.listen_port, "UDP port the Host listens on")
        ->capture_default_str();
    app.add_flag("--sync,!--async", o.synchronous,
                 "Synchronous (one message per Start of Frame, default) or asynchronous (timed)");
    app.add_option("--rate", o.rate_hz, "Message rate in Hz (--async)")
        ->check(CLI::PositiveNumber)
        ->capture_default_str();
    app.add_option("--peer-timeout", o.peer_timeout_ms,
                   "Milliseconds without Start of Frame before the link counts as Lost")
        ->check(CLI::PositiveNumber)
        ->capture_default_str();
    app.add_option("--duration", o.duration_s, "Run for this many seconds (default: until Ctrl-C)")
        ->check(CLI::NonNegativeNumber);
    app.add_option("--pcap", o.pcap, "Write every sent and received datagram to this pcap file");
    add_log_level(app, o.log_level);
}

std::string validate(const MockIgOptions& o) {
    if (!is_log_level(o.log_level)) {
        return std::format("unknown log level '{}'", o.log_level);
    }
    if (!(o.rate_hz > 0.0)) {
        return "--rate must be positive";
    }
    if (o.resume_sof_after_s >= 0 && o.pause_sof_after_s < 0) {
        return "--resume-sof-after needs --pause-sof-after";
    }
    if (o.pause_sof_after_s >= 0 && o.resume_sof_after_s >= 0 &&
        o.resume_sof_after_s <= o.pause_sof_after_s) {
        return "--resume-sof-after must be later than --pause-sof-after";
    }
    if (!o.host_addr.empty() && o.host_port == 0) {
        return "--host-port must not be 0 with --host-addr";
    }
    const auto in_unit = [](double p) { return p >= 0.0 && p <= 1.0; };
    if (!in_unit(o.drop) || !in_unit(o.duplicate)) {
        return "--drop and --duplicate must be between 0 and 1";
    }
    return {};
}

std::string validate(const DemoHostOptions& o) {
    if (!is_log_level(o.log_level)) {
        return std::format("unknown log level '{}'", o.log_level);
    }
    if (o.ig_addr.empty()) {
        return "--ig-addr must not be empty";
    }
    if (o.ig_port == 0) {
        return "--ig-port must not be 0";
    }
    if (!(o.rate_hz > 0.0) || o.peer_timeout_ms == 0) {
        return "--rate and --peer-timeout must be positive";
    }
    return {};
}

} // namespace cigi::demo
