#pragma once

// The demo Host: a cigi::Host that waits for the IG, requests Operate and then, every frame,
// flies an ownship entity in a circle, attaches a view to it, sets the Celestial Sphere time,
// animates an articulated part and sends HAT/HOT requests. It doubles as the worked example of
// the Host facade (examples/host_minimal.cpp is the short one).

#include "demo_common/options.hpp"
#include "demo_common/udp_link.hpp"

#include <array>
#include <chrono>
#include <cigi/host.hpp>
#include <cstdint>
#include <expected>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace spdlog {
class logger;
}

namespace cigi::demo {

struct HostSummary {
    double seconds = 0;
    bool reached_operate = false;
    LinkState final_state = LinkState::WaitingForIg;
    HostStats host;
    std::uint64_t hat_responses = 0;      ///< HAT / HOT responses received.
    std::uint64_t extended_responses = 0; ///< Extended HAT/HOT responses received.
    std::uint64_t invalid_responses = 0;  ///< Responses with Valid = 0.
    std::uint64_t protocol_errors = 0;    ///< Problems reported by the facade (on_error).
    std::uint64_t datagrams_sent = 0;
    std::size_t pcap_frames = 0;
    bool pcap_ok = true;
    bool fatal = false;
    std::string error;
    std::vector<std::string> transitions; ///< "WaitingForIg -> Standby" ... in order.

    [[nodiscard]] std::uint64_t responses() const noexcept {
        return hat_responses + extended_responses;
    }
    /// 0 only if Operate was reached, responses arrived, no protocol error occurred and the pcap
    /// (if any) was written without error.
    [[nodiscard]] int exit_code() const noexcept {
        return !fatal && pcap_ok && reached_operate && responses() > 0 && protocol_errors == 0 ? 0
                                                                                               : 1;
    }
};

[[nodiscard]] std::string format_summary(const HostSummary& summary);

class DemoHost {
public:
    [[nodiscard]] static std::expected<std::unique_ptr<DemoHost>, std::string>
    create(const DemoHostOptions& options, std::shared_ptr<spdlog::logger> logger);

    DemoHost(const DemoHost&) = delete;
    DemoHost& operator=(const DemoHost&) = delete;
    DemoHost(DemoHost&&) = delete;
    DemoHost& operator=(DemoHost&&) = delete;
    ~DemoHost();

    /// Run until `stop`, `duration_s` or a fatal transport error. Logs a status line every second
    /// and the summary at the end.
    HostSummary run(const std::stop_token& stop);

private:
    DemoHost(DemoHostOptions options, std::shared_ptr<spdlog::logger> logger,
             std::shared_ptr<tools::PcapWriter> pcap, UdpLink* link, Host host);

    void on_frame(HostFrame& frame);
    void on_state_change(LinkState from, LinkState to);
    void report(double elapsed_s);

    DemoHostOptions options_;
    std::shared_ptr<spdlog::logger> logger_;
    std::shared_ptr<tools::PcapWriter> pcap_;
    UdpLink* link_; // owned by host_
    Host host_;

    HostSummary summary_;
    // Scenario state; reset every time the link enters Operate so a restarted IG gets everything
    // again.
    double t_s_ = 0;             ///< Seconds in Operate.
    std::uint64_t op_frame_ = 0; ///< Frames sent in Operate since entering it.
    std::uint64_t last_report_responses_ = 0;
    std::uint64_t last_report_frames_ = 0;
};

} // namespace cigi::demo
