#pragma once

// Command-line options of the demo apps (CLI11). The mock-IG options are one struct with one
// registration function so that apps/demo_ig and the viewer (apps/ig_viewer) accept the same
// flags and behave identically.

#include <cstdint>
#include <string>
#include <string_view>

namespace CLI {
class App;
}

namespace cigi::demo {

/// Everything that configures a mock IG (network, start-up behaviour, faults, logging, pcap).
struct MockIgOptions {
    std::string listen_addr;          ///< Interface to bind; empty = any.
    std::uint16_t listen_port = 8004; ///< 0 = ephemeral (tests).
    std::string host_addr;            ///< Empty: learn the Host from its first message.
    std::uint16_t host_port = 8005;   ///< Used with host_addr.
    double rate_hz = 60.0;            ///< Start of Frame rate.
    unsigned load_frames = 0;         ///< Frames spent in Standby before accepting Operate.
    std::string pcap;                 ///< Pcap file for all datagrams; empty = none.
    std::string log_level = "info";   ///< trace, debug, info, warn, error, off.
    // ---- fault injection ----
    bool refuse_operate = false;    ///< Refuse every request for Operate/Debug.
    double pause_sof_after_s = -1;  ///< Stop sending Start of Frame after this many seconds.
    double resume_sof_after_s = -1; ///< ... and resume after this many.
    double restart_after_s = -1;    ///< Behave as if the IG restarted, once.
    double drop = 0;                ///< Chance 0..1 that an outgoing datagram is lost.
    double duplicate = 0;           ///< Chance 0..1 that an outgoing datagram is sent twice.
    double latency_ms = 0;          ///< Delay of every outgoing datagram.
    std::uint64_t seed = 1;         ///< Seed of the fault dice.

    [[nodiscard]] bool has_network_faults() const noexcept {
        return drop > 0 || duplicate > 0 || latency_ms > 0;
    }
};

/// The headless demo IG adds a run length and a live table.
struct DemoIgOptions : MockIgOptions {
    double duration_s = 0; ///< 0 = until interrupted.
    bool table = false;    ///< Print the entity table every second.
};

struct DemoHostOptions {
    std::string ig_addr = "127.0.0.1";
    std::uint16_t ig_port = 8004;
    std::uint16_t listen_port = 8005; ///< 0 = ephemeral (tests).
    bool synchronous = true;          ///< --sync (default) / --async.
    double rate_hz = 60.0;            ///< Message rate of --async.
    unsigned peer_timeout_ms = 1000;  ///< No Start of Frame for this long: the link is Lost.
    double duration_s = 0;            ///< 0 = until interrupted.
    std::string pcap;
    std::string log_level = "info";
};

/// Register the mock-IG flags (--listen-addr, --listen-port, --host-addr, --host-port, --rate,
/// --load-frames, --pcap, --log-level and the fault flags) writing into `options`.
void add_mock_ig_options(CLI::App& app, MockIgOptions& options);
void add_demo_ig_options(CLI::App& app, DemoIgOptions& options);
void add_demo_host_options(CLI::App& app, DemoHostOptions& options);

/// Cross-field checks CLI11 cannot express. Empty string when valid, else the problem.
[[nodiscard]] std::string validate(const MockIgOptions& options);
[[nodiscard]] std::string validate(const DemoHostOptions& options);

/// True if `name` is one of trace, debug, info, warn, error, off.
[[nodiscard]] bool is_log_level(std::string_view name) noexcept;

} // namespace cigi::demo
