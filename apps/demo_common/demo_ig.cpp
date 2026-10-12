#include "demo_common/demo_ig.hpp"

#include "demo_common/support.hpp"
#include "mock_ig/describe.hpp"

#include <chrono>
#include <format>
#include <spdlog/logger.h>
#include <utility>
#include <variant>

namespace cigi::demo {

namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

bool operational(IgMode m) noexcept {
    return m == IgMode::Operate || m == IgMode::Debug;
}

} // namespace

std::string format_entity_table(const mock::SceneModel& scene) {
    std::string out = std::format("{:>5} {:>5} {:<8} {:>6} {:>10} {:>11} {:>8} {:>6} {:>7}\n", "id",
                                  "type", "state", "parent", "lat", "lon", "alt_m", "yaw", "agl_m");
    for (const auto& [id, e] : scene.entities()) {
        out += std::format(
            "{:>5} {:>5} {:<8} {:>6} {:>10.5f} {:>11.5f} {:>8.1f} {:>6.1f} {:>7.1f}\n", id,
            e.entity_type, mock::enum_name(e.state),
            e.attached ? std::to_string(e.parent_id) : std::string{"-"}, e.position.lat_deg,
            e.position.lon_deg, e.position.alt_m, e.world_attitude.yaw_deg, e.agl_m);
    }
    if (scene.entities().empty()) {
        out += "(no entities)\n";
    }
    return out;
}

std::string format_summary(const IgSummary& s) {
    std::string out = std::format("IG summary after {:.1f} s\n", s.seconds);
    out += std::format("  Start of Frame sent {} ({} in Operate), skipped while paused {}, "
                       "late {}\n",
                       s.ig.frames_sent, s.operate_frames, s.ig.frames_skipped, s.ig.late_frames);
    out += std::format("  datagrams received {}, unusable {}, restarts {}\n",
                       s.ig.datagrams_received, s.ig.receive_errors, s.ig.restarts);
    out += std::format("  Host messages lost {}, duplicate {}, stale {}, ignored packets {}\n",
                       s.ig.session.lost_frames, s.ig.session.duplicate_frames,
                       s.ig.session.stale_frames, s.ig.session.ignored_packets);
    out += std::format("  mode requests refused {}\n", s.operate_refused);
    out += std::format("  entities created {}, destroyed {}, alive {}\n", s.scene.entities_created,
                       s.scene.entities_destroyed, s.entities);
    out += std::format("  HAT/HOT responses sent {}\n", s.hat_hot_answers);
    if (s.faults_active) {
        out += std::format("  faults: sent {}, dropped {}, duplicated {}, delayed {}\n",
                           s.faults.sent, s.faults.dropped, s.faults.duplicated, s.faults.delayed);
    }
    if (s.datagrams_unrouted > 0) {
        out += std::format("  {} Start of Frame not sent: no Host known (before the first one, or "
                           "after it timed out)\n",
                           s.datagrams_unrouted);
    }
    if (s.datagrams_foreign > 0) {
        out += std::format("  {} datagrams ignored: not from the Host\n", s.datagrams_foreign);
    }
    if (s.pcap_frames > 0 || !s.pcap_ok) {
        out += std::format("  pcap: {} frames written{}\n", s.pcap_frames,
                           s.pcap_ok ? "" : " (WRITE ERROR)");
    }
    if (s.fatal) {
        out += std::format("  FATAL: {}\n", s.error);
    }
    return out;
}

std::expected<std::unique_ptr<DemoIg>, std::string>
DemoIg::create(const DemoIgOptions& options, std::shared_ptr<spdlog::logger> logger) {
    if (auto problem = validate(options); !problem.empty()) {
        return std::unexpected(std::move(problem));
    }
    std::shared_ptr<tools::PcapWriter> pcap;
    if (!options.pcap.empty()) {
        std::string error;
        pcap = open_pcap(options.pcap, error);
        if (!pcap) {
            return std::unexpected(std::move(error));
        }
    }
    UdpLinkConfig link_config{
        .local_address = options.listen_addr,
        .local_port = options.listen_port,
        .remote_address = options.host_addr,
        .remote_port = options.host_port,
        .pcap = pcap,
        .tap = {},
    };
    if (logger->should_log(spdlog::level::debug)) {
        link_config.tap = [logger](bool sent, std::span<const std::byte> bytes) {
            for (const auto& line : describe_datagram(bytes)) {
                logger->debug("{} {}", sent ? "->" : "<-", line);
            }
        };
    }
    auto link = UdpLink::open(std::move(link_config));
    if (!link) {
        return std::unexpected(std::format("cannot listen on port {}: {}", options.listen_port,
                                           to_string(link.error().code)));
    }
    UdpLink* raw_link = link->get();

    FaultInjectingTransport* raw_faults = nullptr;
    std::unique_ptr<DatagramTransport> transport = std::move(*link);
    if (options.has_network_faults()) {
        auto faults = std::make_unique<FaultInjectingTransport>(std::move(transport), ClockFn{},
                                                                options.seed);
        faults->set_drop_probability(options.drop);
        faults->set_duplicate_probability(options.duplicate);
        faults->set_added_latency(
            std::chrono::microseconds{static_cast<std::int64_t>(options.latency_ms * 1000.0)});
        raw_faults = faults.get();
        transport = std::move(faults);
    }

    IgConfig config;
    config.rate_hz = options.rate_hz;
    config.logger = logger;
    auto ig = Ig::over(std::move(config), std::move(transport));
    if (!ig) {
        return std::unexpected(std::format("cannot start the IG: {}", to_string(ig.error().code)));
    }
    return std::unique_ptr<DemoIg>(new DemoIg(options, std::move(logger), std::move(pcap), raw_link,
                                              raw_faults, std::move(*ig)));
}

DemoIg::DemoIg(DemoIgOptions options, std::shared_ptr<spdlog::logger> logger,
               std::shared_ptr<tools::PcapWriter> pcap, UdpLink* link,
               FaultInjectingTransport* faults, Ig ig)
    : options_(std::move(options)), logger_(std::move(logger)), pcap_(std::move(pcap)), link_(link),
      faults_(faults), ig_(std::move(ig)), dt_s_(1.0 / options_.rate_hz) {
    wire_scene();
    ig_.on_mode_request([this](IgMode mode) { return on_mode_request(mode); });
    ig_.on_frame([this](IgFrame& frame) { on_frame(frame); });
    // The session consumes IG Control; the scene wants its frame number (HAT/HOT LSN).
    ig_.on_ig_control([this](const IgControl& control) { scene_->apply(control); });
    // Unknown opcodes and errors go through the facade, so the scene counts them and the
    // facade's own error reporting stays intact. (The scene is replaced on a restart; these
    // hooks always use the current one.)
    ig_.on_unknown([this](const UnknownPacket& p) { scene_->note_unknown(p); });
    ig_.on_error([this](const Error& e) {
        scene_->note_error(e);
        logger_->warn("bad datagram or packet: {}", to_string(e.code));
    });
}

DemoIg::~DemoIg() = default;

void DemoIg::wire_scene() {
    scene_ = std::make_unique<mock::SceneModel>();
    responder_ = mock::HatHotResponder{};
    scene_->register_handlers(ig_.session().handlers(), &responder_);
    scene_->set_log_sink([this](std::string_view line) { logger_->info("scene: {}", line); });
}

bool DemoIg::on_mode_request(IgMode mode) {
    if (options_.refuse_operate && operational(mode)) {
        ++operate_refused_;
        logger_->warn("refusing the request for {} (--refuse-operate)", mock::enum_name(mode));
        return false;
    }
    // Loading is for leaving Standby; a Debug <-> Operate switch needs none.
    const std::uint64_t loading =
        operational(mode) && !operational(ig_.mode()) ? options_.load_frames : 0;
    pending_mode_ = PendingMode{mode, loading};
    if (loading > 0) {
        logger_->info("loading for {} frames before {}", loading, mock::enum_name(mode));
    }
    return true;
}

void DemoIg::on_frame(IgFrame& frame) {
    scene_->advance(dt_s_);
    ++operate_frames_;
    for (const auto& reply : responder_.collect(*scene_, frame.ig_frame_number())) {
        const auto added = std::visit([&frame](const auto& p) { return frame.add(p); }, reply);
        if (added) {
            ++hat_hot_answers_;
        } else {
            logger_->warn("cannot add a HAT/HOT response: {}", to_string(added.error().code));
        }
    }
}

void DemoIg::timed_faults(double elapsed_s) {
    if (!paused_done_ && options_.pause_sof_after_s >= 0 &&
        elapsed_s >= options_.pause_sof_after_s) {
        paused_done_ = true;
        ig_.set_paused(true);
        logger_->warn("fault: Start of Frame paused");
    }
    if (!resumed_done_ && options_.resume_sof_after_s >= 0 &&
        elapsed_s >= options_.resume_sof_after_s) {
        resumed_done_ = true;
        ig_.set_paused(false);
        logger_->warn("fault: Start of Frame resumed");
    }
    if (!restarted_done_ && options_.restart_after_s >= 0 &&
        elapsed_s >= options_.restart_after_s) {
        restarted_done_ = true;
        logger_->warn("fault: IG restart");
        ig_.restart();
        seen_host_ = false;
        pending_mode_.reset();
        wire_scene();
    }
}

void DemoIg::after_poll() {
    // Finish an accepted mode change after its loading frames.
    const auto sent = ig_.stats().frames_sent;
    const auto new_frames = sent - last_frames_sent_;
    last_frames_sent_ = sent;
    if (pending_mode_) {
        if (pending_mode_->frames_left > new_frames) {
            pending_mode_->frames_left -= new_frames;
        } else {
            ig_.set_mode(pending_mode_->mode);
            logger_->info("IG mode is now {}", mock::enum_name(pending_mode_->mode));
            pending_mode_.reset();
        }
    }
    // (The facade learns the Host, and forgets it again when it goes quiet, through the link.)
    const bool connected = ig_.host_connected();
    if (connected) {
        seen_host_ = true;
    } else if (seen_host_) {
        seen_host_ = false;
        logger_->warn("Host lost");
    }
}

void DemoIg::report(double elapsed_s) {
    const auto stats = ig_.stats();
    logger_->info("t={:.0f}s mode={} host={} sof={} (+{}) rx={} entities={} hat/hot={}", elapsed_s,
                  mock::enum_name(ig_.mode()), ig_.host_connected() ? "yes" : "no",
                  stats.frames_sent, stats.frames_sent - last_report_frames_,
                  stats.datagrams_received, scene_->entities().size(), hat_hot_answers_);
    last_report_frames_ = stats.frames_sent;
    if (options_.table) {
        logger_->info("entity table:\n{}", format_entity_table(*scene_));
    }
}

IgSummary DemoIg::summarise(double elapsed_s) const {
    IgSummary s;
    s.seconds = elapsed_s;
    s.ig = ig_.stats();
    s.scene = scene_->stats();
    s.entities = scene_->entities().size();
    s.hat_hot_answers = hat_hot_answers_;
    s.operate_frames = operate_frames_;
    s.operate_refused = operate_refused_;
    s.datagrams_sent = link_->datagrams_sent();
    s.datagrams_unrouted = s.ig.frames_unrouted;
    s.datagrams_foreign = s.ig.foreign_datagrams;
    s.faults_active = faults_ != nullptr;
    if (faults_ != nullptr) {
        s.faults = faults_->stats();
    }
    if (pcap_) {
        s.pcap_frames = pcap_->frames();
        s.pcap_ok = pcap_->ok();
    }
    return s;
}

IgSummary DemoIg::run(const std::stop_token& stop) {
    logger_->info("IG listening on UDP port {} ({:.0f} Hz){}", bound_port(), options_.rate_hz,
                  options_.host_addr.empty() ? ", Host learnt from its first message" : "");
    const auto start = Clock::now();
    auto next_report = start + std::chrono::seconds(1);
    std::string fatal_error;
    bool fatal = false;
    while (!stop.stop_requested()) {
        const double elapsed = seconds_since(start);
        if (options_.duration_s > 0 && elapsed >= options_.duration_s) {
            break;
        }
        timed_faults(elapsed);
        if (const auto polled = ig_.poll(std::chrono::milliseconds{10}); !polled) {
            fatal = true;
            fatal_error = std::string{to_string(polled.error().code)};
            logger_->error("transport failed: {}", fatal_error);
            break;
        }
        after_poll();
        if (Clock::now() >= next_report) {
            next_report += std::chrono::seconds(1);
            report(seconds_since(start));
        }
    }
    IgSummary summary = summarise(seconds_since(start));
    summary.fatal = fatal;
    summary.error = fatal_error;
    ig_.close();
    logger_->info("{}", format_summary(summary));
    return summary;
}

} // namespace cigi::demo
