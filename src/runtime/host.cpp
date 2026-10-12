#include "cigi/host.hpp"

#include "internal.hpp"
#include "udp_transport.hpp"

#include <algorithm>
#include <cmath>
#include <spdlog/logger.h>
#include <vector>

namespace cigi {

namespace detail {

namespace {

using std::chrono::milliseconds;

constexpr bool is_active(LinkState s) noexcept {
    return s == LinkState::Standby || s == LinkState::LoadingDatabase || s == LinkState::Operate ||
           s == LinkState::Debug;
}

constexpr bool is_flowing(LinkState s) noexcept {
    return s == LinkState::Operate || s == LinkState::Debug;
}

Result<void> validate(const HostConfig& c) {
    const bool bad_mode = c.requested_mode == IgMode::OfflineMaintenance ||
                          !EnumTraits<IgMode>::is_defined(c.requested_mode);
    const bool bad_database = c.database && (*c.database < 0 || *c.database > 127);
    const bool bad_rate =
        !c.synchronous && (!std::isfinite(c.async_rate_hz) || c.async_rate_hz <= 0.0);
    const bool bad_time = c.peer_timeout.count() <= 0 || c.probe_interval.count() <= 0;
    if (bad_mode || bad_database || bad_rate || bad_time || c.max_message_bytes < frame_size) {
        return std::unexpected(invalid_config());
    }
    return {};
}

} // namespace

/// The state of a Host. The public Host owns it through a pointer so that Host stays small,
/// movable and free of the details.
class HostCore {
public:
    HostCore(HostConfig config, std::unique_ptr<DatagramTransport> transport)
        : config_(std::move(config)), transport_(std::move(transport)), session_(config_.session),
          clock_(config_.session.clock), log_(resolve_logger(config_.session)),
          pacing_(config_.synchronous ? 60.0 : config_.async_rate_hz, clock_.now()),
          rx_(receive_buffer_bytes) {
        const TimePoint start = clock_.now();
        state_since_ = start;
        last_rx_ = start;
        last_probe_ = start - config_.probe_interval; // probe at once
        // Validated by Host::over: these cannot fail.
        // NOLINTNEXTLINE(bugprone-unused-return-value): validated by Host::over.
        static_cast<void>(session_.request_mode(config_.requested_mode));
        if (config_.database) {
            // NOLINTNEXTLINE(bugprone-unused-return-value): validated by Host::over.
            static_cast<void>(request_database(*config_.database));
        }
        session_.handlers().on_error([this](const Error& e) { report(e); });
        session_.handlers().on_unknown(
            [this](const UnknownPacket& u) { users_.dispatch(u, [this](const Error& e) { report(e); }); });
    }
    HostCore(const HostCore&) = delete;
    HostCore& operator=(const HostCore&) = delete;
    HostCore(HostCore&&) = delete;
    HostCore& operator=(HostCore&&) = delete;
    ~HostCore() { close(false); }

    Result<PollStatus> poll(milliseconds timeout);
    void close(bool notify);

    Result<void> request_database(int n) {
        auto requested = session_.request_database(n);
        if (requested) {
            database_ = n == 0 ? std::nullopt : std::optional<int>{n};
        }
        return requested;
    }

    void note_add_failure(const Error& error) {
        if (error.code == ErrorCode::BufferTooSmall) {
            ++stats_.oversize_adds;
            log_->warn("frame {} full: packet {} did not fit", session_.host_frame_number(),
                       static_cast<unsigned>(error.opcode));
            report(error);
        }
    }

    [[nodiscard]] LinkState state() const noexcept { return state_; }
    [[nodiscard]] HostStats stats() const {
        HostStats s = stats_;
        s.session = session_.stats();
        s.time_in_state = in_state_;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-*): LinkState values index the array.
        s.time_in_state[static_cast<std::size_t>(state_)] +=
            std::chrono::duration_cast<std::chrono::microseconds>(clock_.now() - state_since_);
        return s;
    }
    [[nodiscard]] HostSession& session() noexcept { return session_; }
    [[nodiscard]] const HostSession& session() const noexcept { return session_; }

    void on_error(std::function<void(const Error&)> f) { error_cb_ = std::move(f); }
    void on_unknown(std::function<void(const UnknownPacket&)> f) { users_.set_fallback(std::move(f)); }
    void set_user_handler(std::uint8_t opcode, UserHandlers::Slot slot) {
        users_.set(opcode, std::move(slot));
    }
    void on_state_change(std::function<void(LinkState, LinkState)> f) { state_cb_ = std::move(f); }
    void on_frame(std::function<void(HostFrame&)> f) { frame_cb_ = std::move(f); }

private:
    void report(const Error& error) const {
        if (error_cb_) {
            error_cb_(error);
        }
    }
    void set_state(LinkState to, TimePoint now, bool notify = true);
    void refresh_state(TimePoint now);
    void on_datagram(std::span<const std::byte> datagram);
    void ig_restarted(TimePoint now, const char* why);
    void reset_link();
    void rearm_database_if_forgotten();
    void check_timeout(TimePoint now);
    [[nodiscard]] TimePoint next_wake(TimePoint now) const;
    Result<bool> maybe_send(TimePoint now);
    Result<void> send_message(TimePoint now, bool with_user_content);

    HostConfig config_;
    std::unique_ptr<DatagramTransport> transport_;
    HostSession session_;
    mutable TickClock clock_;
    std::shared_ptr<spdlog::logger> log_;
    Pacing pacing_;
    std::vector<std::byte> rx_;

    LinkState state_ = LinkState::WaitingForIg;
    TimePoint state_since_;
    std::array<std::chrono::microseconds, link_state_count> in_state_{};
    TimePoint last_rx_;
    TimePoint last_probe_;
    std::optional<TimePoint> last_sent_;
    std::uint32_t stale_run_ = 0; ///< Consecutive stale or duplicate Start of Frames.
    bool saw_stale_ = false; ///< A real stale frame (not only duplicates) is in the current run.
    bool check_database_ = false; ///< After Lost: re-request the database if the IG forgot it.
    std::optional<int> database_; ///< Last database asked for, replayed after a restart.
    bool warned_fragmentation_ = false;
    HostStats stats_;

    UserHandlers users_;
    std::function<void(const Error&)> error_cb_;
    std::function<void(LinkState, LinkState)> state_cb_;
    std::function<void(HostFrame&)> frame_cb_;
};

void HostCore::set_state(LinkState to, TimePoint now, bool notify) {
    if (to == state_ || state_ == LinkState::Closed) {
        return; // Closed is final: a callback that closed the Host must not be undone
    }
    const LinkState from = state_;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-*): LinkState values index the array.
    in_state_[static_cast<std::size_t>(from)] +=
        std::chrono::duration_cast<std::chrono::microseconds>(now - state_since_);
    state_ = to;
    state_since_ = now;
    log_->info("link {} -> {}", to_string(from), to_string(to));
    if (notify && state_cb_) {
        state_cb_(from, to);
    }
}

void HostCore::refresh_state(TimePoint now) {
    if (!session_.last_start_of_frame()) {
        return;
    }
    LinkState next = LinkState::Standby; // Reset/Standby and Offline Maintenance
    switch (session_.ig_mode()) {
    case IgMode::Operate:
        next = session_.database_loading() ? LinkState::LoadingDatabase : LinkState::Operate;
        break;
    case IgMode::Debug:
        next = session_.database_loading() ? LinkState::LoadingDatabase : LinkState::Debug;
        break;
    case IgMode::ResetStandby:
    case IgMode::OfflineMaintenance:
        break;
    }
    set_state(next, now);
}

void HostCore::ig_restarted(TimePoint now, const char* why) {
    ++stats_.ig_restarts;
    log_->warn("IG restarted ({}): replaying start-up", why);
    reset_link();
    check_database_ = false;
    if (database_) { // the session forgets a request once the IG accepted it
        // NOLINTNEXTLINE(bugprone-unused-return-value): the number was validated when stored.
        static_cast<void>(session_.request_database(*database_));
    }
    set_state(LinkState::WaitingForIg, now);
}

void HostCore::reset_link() {
    session_.reset_peer();
    stale_run_ = 0;
    saw_stale_ = false;
}

/// After Lost the IG may have been power-cycled, or may just have been out of reach. A later
/// non-zero database number is a NEW load request (ICD 4.1.1), so ask again only if the first
/// fresh Start of Frame shows the IG is not on the database we asked for (n, or -n while loading).
void HostCore::rearm_database_if_forgotten() {
    if (!check_database_) {
        return;
    }
    check_database_ = false;
    const auto& sof = session_.last_start_of_frame();
    if (database_ && sof && sof->database_number != *database_ &&
        sof->database_number != -*database_) {
        log_->info("IG reports database {} after the link loss: requesting {} again",
                   static_cast<int>(sof->database_number), *database_);
        // NOLINTNEXTLINE(bugprone-unused-return-value): the number was validated when stored.
        static_cast<void>(session_.request_database(*database_));
    }
}

void HostCore::on_datagram(std::span<const std::byte> datagram) {
    const TimePoint now = clock_.now();
    const auto received = session_.receive(datagram);
    if (!received) {
        ++stats_.receive_errors;
        report(received.error());
        return;
    }
    if (!transport_) {
        return; // a handler or callback closed the Host
    }
    last_rx_ = now;
    if (received->stale || received->duplicate) {
        // Reordering or a repeat - unless it goes on for longer than the stale window: then the IG
        // restarted and is counting up to its old number again (docs/design/04-sessions.md,
        // "Limitation").
        saw_stale_ = saw_stale_ || received->stale;
        if (++stale_run_ > config_.session.stale_window) {
            ig_restarted(now, "stale run");
        }
        return;
    }
    const bool after_stale_run = saw_stale_; // duplicates alone are not a restart
    stale_run_ = 0;
    saw_stale_ = false;
    if (received->frame_regressed) {
        ig_restarted(now, "frame number regressed");
        return;
    }
    // An IG restarted within the stale window of its old number counts 0..N-1 (stale), N
    // (duplicate) and then fresh numbers, now in Reset/Standby: it is not an IG-side mode change.
    if (after_stale_run && is_flowing(state_) && session_.ig_mode() == IgMode::ResetStandby) {
        ig_restarted(now, "reset after stale frames");
        return;
    }
    rearm_database_if_forgotten();
    refresh_state(now);
}

void HostCore::check_timeout(TimePoint now) {
    if (is_active(state_) && now - last_rx_ > config_.peer_timeout) {
        ++stats_.link_losses;
        log_->warn("no message from the IG for {} ms: link lost",
                   std::chrono::duration_cast<milliseconds>(now - last_rx_).count());
        reset_link();
        check_database_ = database_.has_value();
        set_state(LinkState::Lost, now);
    }
}

TimePoint HostCore::next_wake(TimePoint now) const {
    TimePoint wake = now + std::chrono::hours{1};
    if (!config_.synchronous) {
        wake = std::min(wake, pacing_.next());
    } else if (state_ == LinkState::WaitingForIg || state_ == LinkState::Lost) {
        wake = std::min(wake, last_probe_ + config_.probe_interval);
    }
    if (is_active(state_)) {
        wake = std::min(wake, last_rx_ + config_.peer_timeout + milliseconds{1});
    }
    return wake;
}

Result<PollStatus> HostCore::poll(milliseconds timeout) {
    PollStatus status;
    if (state_ == LinkState::Closed) {
        return status;
    }
    const milliseconds wait = wait_for(clock_.now(), next_wake(clock_.now()), timeout);
    for (std::size_t i = 0; i < max_drain_per_poll && state_ != LinkState::Closed; ++i) {
        const auto received = transport_->receive(rx_, i == 0 ? wait : milliseconds{0});
        if (!received) {
            const Error error = received.error();
            if (error.code == ErrorCode::Timeout) {
                break;
            }
            if (error.code != ErrorCode::BufferTooSmall) {
                return std::unexpected(error);
            }
            ++stats_.receive_errors; // an oversize datagram was discarded
            report(error);
            continue;
        }
        ++status.datagrams_received;
        ++stats_.datagrams_received;
        on_datagram(std::span<const std::byte>{rx_}.first(*received));
    }
    if (state_ == LinkState::Closed) {
        return status;
    }
    const TimePoint now = clock_.now();
    check_timeout(now);
    if (state_ == LinkState::Closed) {
        return status; // on_state_change closed the Host
    }
    const auto sent = maybe_send(now);
    if (!sent) {
        return std::unexpected(sent.error());
    }
    status.frame_sent = *sent;
    return status;
}

Result<bool> HostCore::maybe_send(TimePoint now) {
    if (!transport_) {
        return false;
    }
    bool with_user_content = false;
    bool probe = false;
    if (config_.synchronous) {
        if (session_.ready_to_send()) {
            with_user_content = true; // one message per Start of Frame (ICD 2.2.1)
        } else if ((state_ == LinkState::WaitingForIg || state_ == LinkState::Lost) &&
                   now - last_probe_ >= config_.probe_interval) {
            probe = true;
        }
    } else if (pacing_.due(now)) {
        stats_.late_frames += pacing_.advance(now);
        with_user_content = true;
    }
    if (!with_user_content && !probe) {
        return false;
    }
    if (probe) {
        last_probe_ = now;
    }
    if (auto sent = send_message(now, with_user_content); !sent) {
        return std::unexpected(sent.error());
    }
    return true;
}

Result<void> HostCore::send_message(TimePoint now, bool with_user_content) {
    if (!transport_) {
        return {};
    }
    // NOLINTNEXTLINE(bugprone-unused-return-value): IG Control always fits.
    static_cast<void>(session_.begin_frame());
    if (with_user_content && is_flowing(state_) && frame_cb_) {
        ++stats_.frame_callbacks;
        const auto& sof = session_.last_start_of_frame();
        const auto since =
            last_sent_ ? std::chrono::duration_cast<std::chrono::microseconds>(now - *last_sent_)
                       : std::chrono::microseconds{0};
        HostFrame frame{session_, *this, state_,
                        sof && sof->timestamp_valid ? sof->timestamp : Timestamp{}, since};
        frame_cb_(frame);
        if (!transport_) {
            return {}; // the callback closed the Host
        }
    }
    const auto bytes = session_.finish_frame();
    if (bytes.size() > ethernet_payload_limit && !warned_fragmentation_) {
        warned_fragmentation_ = true;
        log_->warn("message of {} bytes exceeds {}: it will be IP-fragmented on Ethernet",
                   bytes.size(), ethernet_payload_limit);
    }
    if (auto sent = transport_->send(bytes); !sent) {
        return sent;
    }
    ++stats_.frames_sent;
    last_sent_ = now;
    return {};
}

void HostCore::close(bool notify) {
    if (state_ == LinkState::Closed) {
        return;
    }
    if (config_.standby_on_close && transport_) {
        // ICD 2.8: leave the IG in Reset/Standby. Best effort: the socket is closed anyway.
        // NOLINTNEXTLINE(bugprone-unused-return-value): ResetStandby is always valid.
        static_cast<void>(session_.request_mode(IgMode::ResetStandby));
        // NOLINTNEXTLINE(bugprone-unused-return-value): IG Control always fits.
        static_cast<void>(session_.begin_frame());
        if (const auto sent = transport_->send(session_.finish_frame()); !sent) {
            log_->warn("could not send the Standby request on close: {}",
                       to_string(sent.error().code));
        }
    }
    transport_.reset();
    set_state(LinkState::Closed, clock_.now(), notify);
}

} // namespace detail

void HostFrame::note_failure(const Error& error) {
    core_->note_add_failure(error);
}

Host::Host(std::unique_ptr<detail::HostCore> core) noexcept : core_(std::move(core)) {}
Host::Host(Host&&) noexcept = default;
Host& Host::operator=(Host&&) noexcept = default;
Host::~Host() = default;

Result<Host> Host::over(HostConfig config, std::unique_ptr<DatagramTransport> transport) {
    if (const auto ok = detail::validate(config); !ok || !transport) {
        return std::unexpected(detail::invalid_config());
    }
    config.session.synchronous = config.synchronous;
    config.session.max_message_bytes = config.max_message_bytes;
    if (config.logger) {
        config.session.logger = config.logger;
    }
    return Host{std::make_unique<detail::HostCore>(std::move(config), std::move(transport))};
}

Result<Host> Host::connect(HostConfig config) {
    if (config.ig_address.empty() || config.ig_port == 0) {
        return std::unexpected(detail::invalid_config());
    }
    if (auto ok = detail::validate(config); !ok) {
        return std::unexpected(ok.error());
    }
    auto endpoint = transport::UdpEndpoint::open({
        .local_address = config.local_address,
        .local_port = config.local_port,
        .remote_address = config.ig_address,
        .remote_port = config.ig_port,
    });
    if (!endpoint) {
        return std::unexpected(endpoint.error());
    }
    return over(std::move(config),
                std::make_unique<detail::UdpDatagramTransport>(std::move(*endpoint), false));
}

void Host::on_unknown(std::function<void(const UnknownPacket&)> handler) {
    core_->on_unknown(std::move(handler));
}
void Host::set_user_handler(std::uint8_t opcode, detail::UserHandlers::Slot slot) {
    core_->set_user_handler(opcode, std::move(slot));
}
void Host::on_error(std::function<void(const Error&)> handler) {
    core_->on_error(std::move(handler));
}
void Host::on_state_change(std::function<void(LinkState, LinkState)> handler) {
    core_->on_state_change(std::move(handler));
}
void Host::on_frame(std::function<void(HostFrame&)> handler) {
    core_->on_frame(std::move(handler));
}

Result<PollStatus> Host::poll(std::chrono::milliseconds timeout) {
    return core_->poll(timeout);
}

Result<void> Host::run(const std::stop_token& stop, std::chrono::milliseconds poll_interval) {
    while (!stop.stop_requested() && core_->state() != LinkState::Closed) {
        if (const auto polled = core_->poll(poll_interval); !polled) {
            return std::unexpected(polled.error());
        }
    }
    return {};
}

void Host::close() {
    core_->close(true);
}

Result<void> Host::request_mode(IgMode mode) {
    return core_->session().request_mode(mode);
}
Result<void> Host::request_database(int n) {
    return core_->request_database(n);
}
LinkState Host::state() const noexcept {
    return core_->state();
}
HostStats Host::stats() const {
    return core_->stats();
}
HostSession& Host::session() noexcept {
    return core_->session();
}
const HostSession& Host::session() const noexcept {
    return core_->session();
}

} // namespace cigi
