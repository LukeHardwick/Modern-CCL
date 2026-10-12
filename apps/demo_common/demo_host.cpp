#include "demo_common/demo_host.hpp"

#include "demo_common/support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>
#include <spdlog/logger.h>
#include <utility>

namespace cigi::demo {

namespace {

using Clock = std::chrono::steady_clock;

// The scenario flies over the mock IG's default database (around 51.5 N, 0.12 W).
constexpr double origin_lat = 51.5;
constexpr double origin_lon = -0.12;
constexpr double circle_radius_m = 1500.0;
constexpr double speed_mps = 80.0;
constexpr double altitude_m = 500.0;
constexpr double pi = std::numbers::pi;
constexpr double metres_per_degree_lat = 111'320.0;
constexpr std::uint16_t ownship_id = 0;
constexpr std::uint16_t ownship_type = 101; // the mock IG's convention: 100-199 are aircraft

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

bool operational(LinkState s) noexcept {
    return s == LinkState::Operate || s == LinkState::Debug;
}

EntityControl ownship_at(double t_s) {
    const double angle = speed_mps / circle_radius_m * t_s; // radians, clockwise from north
    const double east = circle_radius_m * std::sin(angle);
    const double north = circle_radius_m * std::cos(angle);
    const double heading = std::fmod((angle * 180.0 / pi) + 90.0, 360.0);
    EntityControl e;
    e.entity_id = ownship_id;
    e.entity_state = EntityState::Active;
    e.entity_type = ownship_type;
    e.latitude_or_x = origin_lat + (north / metres_per_degree_lat);
    e.longitude_or_y =
        origin_lon + (east / (metres_per_degree_lat * std::cos(origin_lat * pi / 180.0)));
    e.altitude_or_z = altitude_m;
    e.yaw = static_cast<float>(heading);
    e.roll = 20.0F; // banked into the turn
    return e;
}

} // namespace

std::string format_summary(const HostSummary& s) {
    std::string out = std::format("Host summary after {:.1f} s\n", s.seconds);
    out += std::format("  reached Operate: {}   final link state: {}\n",
                       s.reached_operate ? "yes" : "NO (never)", to_string(s.final_state));
    std::string path;
    for (const auto& t : s.transitions) {
        path += (path.empty() ? "" : "; ") + t;
    }
    out += std::format("  link: {}\n", path.empty() ? "(no change)" : path);
    out +=
        std::format("  link losses {}, IG restarts {}\n", s.host.link_losses, s.host.ig_restarts);
    out += std::format("  messages sent {} ({} frame callbacks), received {}, unusable {}\n",
                       s.host.frames_sent, s.host.frame_callbacks, s.host.session.messages_received,
                       s.host.receive_errors);
    out += std::format("  Start of Frame lost {}, duplicate {}, stale {}\n",
                       s.host.session.lost_frames, s.host.session.duplicate_frames,
                       s.host.session.stale_frames);
    out += std::format("  HAT/HOT responses {} (+{} extended, {} not valid)\n", s.hat_responses,
                       s.extended_responses, s.invalid_responses);
    out += std::format("  protocol errors {}, decode errors {}\n", s.protocol_errors,
                       s.host.session.decode_errors);
    if (s.pcap_frames > 0 || !s.pcap_ok) {
        out += std::format("  pcap: {} frames written{}\n", s.pcap_frames,
                           s.pcap_ok ? "" : " (WRITE ERROR)");
    }
    if (s.fatal) {
        out += std::format("  FATAL: {}\n", s.error);
    }
    out += std::format("  result: {}\n", s.exit_code() == 0 ? "OK" : "FAILED");
    return out;
}

std::expected<std::unique_ptr<DemoHost>, std::string>
DemoHost::create(const DemoHostOptions& options, std::shared_ptr<spdlog::logger> logger) {
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
        .local_address = {},
        .local_port = options.listen_port,
        .remote_address = options.ig_addr,
        .remote_port = options.ig_port,
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
        return std::unexpected(std::format("cannot use IG address {}:{} / listen port {}: {}",
                                           options.ig_addr, options.ig_port, options.listen_port,
                                           to_string(link.error().code)));
    }
    UdpLink* raw_link = link->get();

    HostConfig config;
    config.ig_address = options.ig_addr;
    config.ig_port = options.ig_port;
    config.local_port = options.listen_port;
    config.synchronous = options.synchronous;
    config.async_rate_hz = options.rate_hz;
    config.peer_timeout = std::chrono::milliseconds{options.peer_timeout_ms};
    config.logger = logger;
    auto host = Host::over(std::move(config), std::move(*link));
    if (!host) {
        return std::unexpected(
            std::format("cannot start the Host: {}", to_string(host.error().code)));
    }
    return std::unique_ptr<DemoHost>(
        new DemoHost(options, std::move(logger), std::move(pcap), raw_link, std::move(*host)));
}

DemoHost::DemoHost(DemoHostOptions options, std::shared_ptr<spdlog::logger> logger,
                   std::shared_ptr<tools::PcapWriter> pcap, UdpLink* link, Host host)
    : options_(std::move(options)), logger_(std::move(logger)), pcap_(std::move(pcap)), link_(link),
      host_(std::move(host)) {
    host_.on<HatHotResponse>([this](const HatHotResponse& r) {
        ++summary_.hat_responses;
        if (!r.valid) {
            ++summary_.invalid_responses;
        }
        logger_->info("{} id={} valid={} height={:.1f} m (LSN {})",
                      r.response_type == HatHotResponseType::Hat ? "HAT" : "HOT", r.hat_hot_id,
                      r.valid, r.height, r.host_frame_lsn);
    });
    host_.on<HatHotExtendedResponse>([this](const HatHotExtendedResponse& r) {
        ++summary_.extended_responses;
        if (!r.valid) {
            ++summary_.invalid_responses;
        }
        logger_->info("HAT/HOT extended id={} valid={} hat={:.1f} m hot={:.1f} m material={} "
                      "(LSN {})",
                      r.hat_hot_id, r.valid, r.hat, r.hot, r.material_code, r.host_frame_lsn);
    });
    host_.on_error([this](const Error& e) {
        ++summary_.protocol_errors;
        logger_->warn("protocol error: {}", to_string(e.code));
    });
    host_.on_state_change([this](LinkState from, LinkState to) { on_state_change(from, to); });
    host_.on_frame([this](HostFrame& frame) { on_frame(frame); });
}

DemoHost::~DemoHost() = default;

void DemoHost::on_state_change(LinkState from, LinkState to) {
    // (The facade logs the transition itself.)
    summary_.transitions.push_back(std::format("{} -> {}", to_string(from), to_string(to)));
    if (operational(to) && !operational(from)) {
        summary_.reached_operate = true;
        // A fresh Operate (first time, after Lost or an IG restart): send the whole scenario again.
        t_s_ = 0;
        op_frame_ = 0;
    }
}

void DemoHost::on_frame(HostFrame& frame) {
    // Time advances by what elapsed since the previous message (capped, so a stall is not a jump).
    t_s_ += std::min(std::chrono::duration<double>(frame.since_last_frame()).count(), 0.1);
    const auto add = [&frame](const auto& packet) { static_cast<void>(frame.add(packet)); };

    add(ownship_at(t_s_));

    ArticulatedPartControl propeller; // part 1 of the ownship spins at 2 revolutions per second
    propeller.entity_id = ownship_id;
    propeller.articulated_part_id = 1;
    propeller.part_enabled = true;
    propeller.yaw_enabled = true;
    propeller.yaw = static_cast<float>(std::fmod(720.0 * t_s_, 360.0));
    add(propeller);

    if (op_frame_ == 0) {
        ViewDefinition definition; // a 70 x 42 degree perspective view
        definition.view_id = 0;
        definition.near_enabled = definition.far_enabled = true;
        definition.left_enabled = definition.right_enabled = true;
        definition.top_enabled = definition.bottom_enabled = true;
        definition.near_plane = 1.0F;
        definition.far_plane = 20000.0F;
        definition.left = -35.0F;
        definition.right = 35.0F;
        definition.top = 21.0F;
        definition.bottom = -21.0F;
        add(definition);
    }
    if (op_frame_ % 60 == 0) {
        ViewControl view; // the pilot's eye, 1.5 m above the reference point, looking 12 down
        view.view_id = 0;
        view.entity_id = ownship_id;
        view.z_offset_enabled = true;
        view.z_offset = -1.5F;
        view.pitch_enabled = true;
        view.pitch = -12.0F;
        add(view);
    }
    if (op_frame_ % 30 == 0) {
        CelestialSphereControl sky; // 10:00 UTC on midsummer's day, one minute per second
        const auto minutes = static_cast<std::uint32_t>(600.0 + t_s_) % 1440U;
        sky.hour = static_cast<std::uint8_t>(minutes / 60U);
        sky.minute = static_cast<std::uint8_t>(minutes % 60U);
        sky.ephemeris_model_enabled = false;
        sky.sun_enabled = true;
        sky.date_time_valid = true;
        static_cast<void>(sky.set_date(std::chrono::year_month_day{
            std::chrono::year{2026}, std::chrono::June, std::chrono::day{21}}));
        add(sky);
    }
    if (op_frame_ == 1) { // one-shot terrain height at the origin
        HatHotRequest request;
        request.hat_hot_id = 1;
        request.request_type = HatHotRequestType::Hat;
        request.latitude_or_x = origin_lat;
        request.longitude_or_y = origin_lon;
        request.altitude_or_z = 1000.0;
        add(request);
    }
    if (op_frame_ == 2) { // periodic extended request below the ownship, every 30th frame
        HatHotRequest request;
        request.hat_hot_id = 3;
        request.request_type = HatHotRequestType::Extended;
        request.coordinate_system = HatHotCoordinateSystem::Entity;
        request.update_period = 30;
        request.entity_id = ownship_id;
        add(request);
    }
    if (op_frame_ == 30) { // one-shot terrain elevation 1 km east of the origin
        HatHotRequest request;
        request.hat_hot_id = 2;
        request.request_type = HatHotRequestType::Hot;
        request.latitude_or_x = origin_lat;
        request.longitude_or_y = origin_lon + 0.014;
        add(request);
    }
    ++op_frame_;
}

void DemoHost::report(double elapsed_s) {
    const auto stats = host_.stats();
    logger_->info(
        "t={:.0f}s link={} sent={} (+{}) ig_frame={} lost={} responses={} (+{}) errors={}",
        elapsed_s, to_string(host_.state()), stats.frames_sent,
        stats.frames_sent - last_report_frames_, host_.session().last_ig_frame_number(),
        stats.session.lost_frames, summary_.responses(),
        summary_.responses() - last_report_responses_, summary_.protocol_errors);
    last_report_frames_ = stats.frames_sent;
    last_report_responses_ = summary_.responses();
}

HostSummary DemoHost::run(const std::stop_token& stop) {
    logger_->info("Host {} -> IG {}:{} ({})", options_.listen_port, options_.ig_addr,
                  options_.ig_port, options_.synchronous ? "synchronous" : "asynchronous");
    const auto start = Clock::now();
    auto next_report = start + std::chrono::seconds(1);
    while (!stop.stop_requested()) {
        if (options_.duration_s > 0 && seconds_since(start) >= options_.duration_s) {
            break;
        }
        if (const auto polled = host_.poll(std::chrono::milliseconds{20}); !polled) {
            summary_.fatal = true;
            summary_.error = std::string{to_string(polled.error().code)};
            logger_->error("transport failed: {}", summary_.error);
            break;
        }
        if (Clock::now() >= next_report) {
            next_report += std::chrono::seconds(1);
            report(seconds_since(start));
        }
    }
    summary_.final_state = host_.state();
    summary_.datagrams_sent = link_->datagrams_sent();
    host_.close(); // asks the IG for Reset/Standby
    summary_.seconds = seconds_since(start);
    summary_.host = host_.stats();
    if (pcap_) {
        summary_.pcap_frames = pcap_->frames();
        summary_.pcap_ok = pcap_->ok();
    }
    logger_->info("{}", format_summary(summary_));
    return summary_;
}

} // namespace cigi::demo
