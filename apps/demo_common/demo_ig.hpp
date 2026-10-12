#pragma once

// The headless mock IG: a cigi::Ig whose application logic is the shared cigi_mock_ig library
// (SceneModel + HatHotResponder), plus the start-up behaviour and fault flags of T09. The
// viewer builds on the same options and logic, so both behave identically.
//
//   auto ig = DemoIg::create(options, make_logger("ig", options.log_level));
//   if (!ig) { ...ig.error() is a message... }
//   IgSummary s = (*ig)->run(stop_token);

#include "demo_common/options.hpp"
#include "demo_common/udp_link.hpp"

#include <cigi/fault_transport.hpp>
#include <cigi/ig.hpp>
#include <cstdint>
#include <expected>
#include <memory>
#include <mock_ig/hat_hot.hpp>
#include <mock_ig/scene.hpp>
#include <optional>
#include <stop_token>
#include <string>

namespace spdlog {
class logger;
}

namespace cigi::demo {

struct IgSummary {
    double seconds = 0;
    IgStats ig;
    mock::SceneStats scene;
    std::size_t entities = 0;          ///< Entities alive at the end.
    std::uint64_t hat_hot_answers = 0; ///< HAT/HOT responses sent.
    std::uint64_t operate_frames = 0;  ///< Frames sent while the IG was in Operate/Debug.
    std::uint64_t operate_refused = 0; ///< Mode requests refused by --refuse-operate.
    std::uint64_t datagrams_sent = 0;  ///< Datagrams that reached the socket.
    std::uint64_t datagrams_unrouted = 0;
    std::uint64_t datagrams_foreign = 0; ///< Datagrams from a sender other than the Host.
    bool faults_active = false;
    FaultInjectingTransport::Stats faults;
    std::size_t pcap_frames = 0;
    bool pcap_ok = true;
    bool fatal = false; ///< run() ended on a transport error.
    std::string error;

    /// 0 unless the socket failed or the pcap could not be written.
    [[nodiscard]] int exit_code() const noexcept { return fatal || !pcap_ok ? 1 : 0; }
};

/// Multi-line text of a summary (what the apps print at exit).
[[nodiscard]] std::string format_summary(const IgSummary& summary);

/// A plain-text table of the scene's entities (id, type, state, parent, position, attitude, AGL).
[[nodiscard]] std::string format_entity_table(const mock::SceneModel& scene);

class DemoIg {
public:
    /// Open the socket (and the pcap file). The error is a message for the user.
    [[nodiscard]] static std::expected<std::unique_ptr<DemoIg>, std::string>
    create(const DemoIgOptions& options, std::shared_ptr<spdlog::logger> logger);

    DemoIg(const DemoIg&) = delete;
    DemoIg& operator=(const DemoIg&) = delete;
    DemoIg(DemoIg&&) = delete;
    DemoIg& operator=(DemoIg&&) = delete;
    ~DemoIg();

    /// The UDP port the IG listens on (useful with --listen-port 0).
    [[nodiscard]] std::uint16_t bound_port() const { return ig_.local_port(); }

    /// Serve until `stop` is requested, `duration_s` has passed or the transport fails. Logs a
    /// status line every second and the summary at the end.
    IgSummary run(const std::stop_token& stop);

    [[nodiscard]] const mock::SceneModel& scene() const noexcept { return *scene_; }

private:
    DemoIg(DemoIgOptions options, std::shared_ptr<spdlog::logger> logger,
           std::shared_ptr<tools::PcapWriter> pcap, UdpLink* link, FaultInjectingTransport* faults,
           Ig ig);

    void wire_scene();
    void on_frame(IgFrame& frame);
    bool on_mode_request(IgMode mode);
    void timed_faults(double elapsed_s);
    void after_poll();
    void report(double elapsed_s);
    [[nodiscard]] IgSummary summarise(double elapsed_s) const;

    DemoIgOptions options_;
    std::shared_ptr<spdlog::logger> logger_;
    std::shared_ptr<tools::PcapWriter> pcap_;
    UdpLink* link_;                   // owned by ig_
    FaultInjectingTransport* faults_; // owned by ig_; null without network faults
    std::unique_ptr<mock::SceneModel> scene_;
    mock::HatHotResponder responder_;
    Ig ig_;
    double dt_s_;

    struct PendingMode {
        IgMode mode;
        std::uint64_t frames_left;
    };
    std::optional<PendingMode> pending_mode_;
    std::uint64_t last_frames_sent_ = 0;
    std::uint64_t hat_hot_answers_ = 0;
    std::uint64_t operate_frames_ = 0;
    std::uint64_t operate_refused_ = 0;
    bool seen_host_ = false;
    bool paused_done_ = false;
    bool resumed_done_ = false;
    bool restarted_done_ = false;
    std::uint64_t last_report_frames_ = 0;
};

} // namespace cigi::demo
