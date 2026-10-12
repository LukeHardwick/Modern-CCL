#include "cigi/ig.hpp"

#include "internal.hpp"
#include "udp_transport.hpp"

#include <cmath>
#include <spdlog/logger.h>
#include <vector>

namespace cigi {

namespace detail {

namespace {

using std::chrono::milliseconds;

Result<void> validate(const IgConfig& c) {
    if (!std::isfinite(c.rate_hz) || c.rate_hz <= 0.0 || c.peer_timeout.count() <= 0 ||
        c.max_message_bytes < frame_size) {
        return std::unexpected(invalid_config());
    }
    return {};
}

} // namespace

class IgCore {
public:
    IgCore(IgConfig config, std::unique_ptr<DatagramTransport> transport, bool host_known,
           UdpDatagramTransport* learner = nullptr)
        : config_(std::move(config)), transport_(std::move(transport)), learner_(learner),
          session_(config_.session), clock_(config_.session.clock),
          log_(resolve_logger(config_.session)), pacing_(config_.rate_hz, clock_.now()),
          rx_(receive_buffer_bytes), host_known_(host_known), configured_known_(host_known) {
        last_rx_ = clock_.now();
        session_.handlers().on_error([this](const Error& e) { report(e); });
        session_.handlers().on_unknown(
            [this](const UnknownPacket& u) { users_.dispatch(u, [this](const Error& e) { report(e); }); });
    }
    IgCore(const IgCore&) = delete;
    IgCore& operator=(const IgCore&) = delete;
    IgCore(IgCore&&) = delete;
    IgCore& operator=(IgCore&&) = delete;
    ~IgCore() = default;

    Result<PollStatus> poll(milliseconds timeout);
    void close() {
        learner_ = nullptr;
        transport_.reset();
    }

    void restart() {
        ++stats_.restarts;
        log_->warn("IG restart: back to Reset/Standby, frame counter reset");
        session_.reset();
        pacing_.reset(clock_.now());
        connected_ = false;
        forget_host();
        last_rx_ = clock_.now();
    }

    /// A Host learnt from its traffic is forgotten (and may be a different one next time).
    void forget_host() {
        host_known_ = configured_known_;
        if (learner_ != nullptr) {
            learner_->forget_peer();
        }
    }

    [[nodiscard]] bool host_connected() const {
        return host_known_ && connected_ && clock_.now() - last_rx_ <= config_.peer_timeout;
    }
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    void set_paused(bool paused) noexcept { paused_ = paused; }
    [[nodiscard]] bool closed() const noexcept { return !transport_; }
    [[nodiscard]] IgStats stats() const {
        IgStats s = stats_;
        s.session = session_.stats();
        return s;
    }
    [[nodiscard]] IgSession& session() noexcept { return session_; }
    [[nodiscard]] const IgSession& session() const noexcept { return session_; }

    void on_error(std::function<void(const Error&)> f) { error_cb_ = std::move(f); }
    void on_unknown(std::function<void(const UnknownPacket&)> f) { users_.set_fallback(std::move(f)); }
    void set_user_handler(std::uint8_t opcode, UserHandlers::Slot slot) {
        users_.set(opcode, std::move(slot));
    }
    void on_frame(std::function<void(IgFrame&)> f) { frame_cb_ = std::move(f); }

private:
    void report(const Error& error) const {
        if (error_cb_) {
            error_cb_(error);
        }
    }

    IgConfig config_;
    std::unique_ptr<DatagramTransport> transport_;
    UdpDatagramTransport* learner_ = nullptr; ///< Into transport_ when the Host is learnt.
    IgSession session_;
    mutable TickClock clock_;
    std::shared_ptr<spdlog::logger> log_;
    Pacing pacing_;
    std::vector<std::byte> rx_;
    bool host_known_;
    bool configured_known_;
    bool connected_ = false;
    bool paused_ = false;
    TimePoint last_rx_;
    IgStats stats_;
    UserHandlers users_;
    std::function<void(const Error&)> error_cb_;
    std::function<void(IgFrame&)> frame_cb_;
};

Result<PollStatus> IgCore::poll(milliseconds timeout) {
    PollStatus status;
    if (!transport_) {
        return status;
    }
    TimePoint now = clock_.now();
    const milliseconds wait = wait_for(now, pacing_.next(), timeout);
    for (std::size_t i = 0; i < max_drain_per_poll; ++i) {
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
            if (!transport_) {
                return status; // on_error closed the IG
            }
            continue;
        }
        ++status.datagrams_received;
        ++stats_.datagrams_received;
        const auto handled = session_.receive(std::span<const std::byte>{rx_}.first(*received));
        if (!handled) {
            ++stats_.receive_errors;
            report(handled.error());
            if (!transport_) {
                return status; // on_error closed the IG
            }
            continue;
        }
        if (!transport_) {
            return status; // a handler closed the IG
        }
        if (!host_known_ || !connected_) {
            log_->info("Host connected");
        }
        if (!host_known_ && learner_ != nullptr && !learner_->latch_peer()) {
            log_->warn("cannot adopt the sender of the Host message as the Host");
            continue; // stay unknown: the next message tries again
        }
        host_known_ = true;
        connected_ = true;
        last_rx_ = clock_.now();
    }

    now = clock_.now();
    if (connected_ && now - last_rx_ > config_.peer_timeout) {
        connected_ = false;
        log_->warn("no message from the Host for {} ms", config_.peer_timeout.count());
        forget_host();
    }
    if (!pacing_.due(now)) {
        return status;
    }
    stats_.late_frames += pacing_.advance(now);
    if (paused_) {
        ++stats_.frames_skipped;
        return status;
    }
    if (!host_known_) {
        return status; // nobody to send to yet
    }
    // NOLINTNEXTLINE(bugprone-unused-return-value): Start of Frame always fits.
    static_cast<void>(session_.begin_frame());
    if (detail::is_operational(session_.mode()) && frame_cb_) {
        IgFrame frame{session_};
        frame_cb_(frame);
        if (!transport_) {
            return status; // the callback closed the IG
        }
    }
    if (auto sent = transport_->send(session_.finish_frame()); !sent) {
        return std::unexpected(sent.error());
    }
    ++stats_.frames_sent;
    status.frame_sent = true;
    return status;
}

} // namespace detail

Ig::Ig(std::unique_ptr<detail::IgCore> core) noexcept : core_(std::move(core)) {}
Ig::Ig(Ig&&) noexcept = default;
Ig& Ig::operator=(Ig&&) noexcept = default;
Ig::~Ig() = default;

Result<Ig> Ig::over(IgConfig config, std::unique_ptr<DatagramTransport> transport) {
    if (const auto ok = detail::validate(config); !ok || !transport) {
        return std::unexpected(detail::invalid_config());
    }
    config.session.max_message_bytes = config.max_message_bytes;
    if (config.logger) {
        config.session.logger = config.logger;
    }
    return Ig{std::make_unique<detail::IgCore>(std::move(config), std::move(transport), true)};
}

Result<Ig> Ig::connect(IgConfig config) {
    if (auto ok = detail::validate(config); !ok) {
        return std::unexpected(ok.error());
    }
    const bool host_known = !config.host_address.empty();
    if (host_known && config.host_port == 0) {
        return std::unexpected(detail::invalid_config());
    }
    auto endpoint = transport::UdpEndpoint::open({
        .local_address = config.listen_address,
        .local_port = config.listen_port,
        .remote_address = config.host_address,
        .remote_port = host_known ? config.host_port : std::uint16_t{0},
    });
    if (!endpoint) {
        return std::unexpected(endpoint.error());
    }
    config.session.max_message_bytes = config.max_message_bytes;
    if (config.logger) {
        config.session.logger = config.logger;
    }
    auto udp = std::make_unique<detail::UdpDatagramTransport>(std::move(*endpoint), !host_known);
    auto* learner = host_known ? nullptr : udp.get(); // the IgCore owns udp and drops this first
    return Ig{
        std::make_unique<detail::IgCore>(std::move(config), std::move(udp), host_known, learner)};
}

void Ig::on_unknown(std::function<void(const UnknownPacket&)> handler) {
    core_->on_unknown(std::move(handler));
}
void Ig::set_user_handler(std::uint8_t opcode, detail::UserHandlers::Slot slot) {
    core_->set_user_handler(opcode, std::move(slot));
}
void Ig::on_error(std::function<void(const Error&)> handler) {
    core_->on_error(std::move(handler));
}
void Ig::on_mode_request(std::function<bool(IgMode)> handler) {
    core_->session().on_mode_request(std::move(handler));
}
void Ig::on_database_request(std::function<void(std::int8_t)> handler) {
    core_->session().on_database_request(std::move(handler));
}
void Ig::on_frame(std::function<void(IgFrame&)> handler) {
    core_->on_frame(std::move(handler));
}

Result<PollStatus> Ig::poll(std::chrono::milliseconds timeout) {
    return core_->poll(timeout);
}

Result<void> Ig::run(const std::stop_token& stop, std::chrono::milliseconds poll_interval) {
    while (!stop.stop_requested() && !core_->closed()) {
        if (const auto polled = core_->poll(poll_interval); !polled) {
            return std::unexpected(polled.error());
        }
    }
    return {};
}

void Ig::close() {
    core_->close();
}
void Ig::set_mode(IgMode mode) {
    core_->session().set_mode(mode);
}
void Ig::set_database(std::int8_t database) {
    core_->session().set_database(database);
}
IgMode Ig::mode() const noexcept {
    return core_->session().mode();
}
bool Ig::host_connected() const noexcept {
    return core_->host_connected();
}
void Ig::set_paused(bool paused) noexcept {
    core_->set_paused(paused);
}
bool Ig::paused() const noexcept {
    return core_->paused();
}
void Ig::restart() {
    core_->restart();
}
IgStats Ig::stats() const {
    return core_->stats();
}
IgSession& Ig::session() noexcept {
    return core_->session();
}
const IgSession& Ig::session() const noexcept {
    return core_->session();
}

} // namespace cigi
