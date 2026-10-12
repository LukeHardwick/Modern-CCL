#pragma once

// cigi::Host: "connect to an IG and send entity updates every frame" in a few lines.
//
//   auto ig = cigi::Host::connect({.ig_address = "10.0.0.2"});
//   if (!ig) return fail(ig.error());
//   ig->on<cigi::HatHotResponse>(handle_terrain);
//   ig->on_state_change([](cigi::LinkState from, cigi::LinkState to) { log(from, to); });
//   ig->on_frame([&](cigi::HostFrame& f) { f.add(entity_control); });  // Operate/Debug only
//   return ig->run(stop_token) ? 0 : 1;                                // or ig->poll(timeout)
//
// The facade owns the receive -> session -> ready? -> build -> send loop of a HostSession and the
// timeouts around it; it adds no protocol rules (those are the session's, ICD 2.2, 2.3, 2.8,
// 4.1.1, 4.2.1). THREADING: a Host is NOT thread-safe. Every callback runs on the thread that
// calls poll()/run()/close(); call everything from that thread, or serialise access yourself.

#include "cigi/datagram_transport.hpp"
#include "cigi/link_state.hpp"
#include "cigi/packet_set.hpp"
#include "cigi/packets.hpp"
#include "cigi/session/host_session.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <type_traits>
#include <utility>

namespace cigi {

namespace detail {
class HostCore;
}

/// Configuration of a Host. Every field has a working default; only `ig_address` is required by
/// Host::connect.
struct HostConfig {
    /// Numeric IPv4/IPv6 address of the IG (no DNS lookup, so connecting never blocks).
    std::string ig_address = {}; // NOLINT(readability-redundant-member-init)
    std::uint16_t ig_port = 8004;
    /// Interface to bind; empty means any.
    std::string local_address = {}; // NOLINT(readability-redundant-member-init)
    /// Port the IG's Start of Frame messages are sent to.
    std::uint16_t local_port = 8005;
    /// Synchronous (ICD 2.2.1): one message per received Start of Frame. Asynchronous (ICD
    /// 2.2.2): one message every 1 / `async_rate_hz` seconds. Overrides `session.synchronous`.
    bool synchronous = true;
    double async_rate_hz = 60.0; ///< Asynchronous mode only; must be positive.
    /// IG mode requested in IG Control (Reset/Standby, Operate or Debug).
    IgMode requested_mode = IgMode::Operate;
    /// Database to load once the IG operates (1..127); nullopt: leave it to the IG.
    std::optional<int> database = std::nullopt;
    /// No message from the IG for this long, after it was heard, means the link is Lost.
    std::chrono::milliseconds peer_timeout{1000};
    /// While no IG is heard (WaitingForIg, Lost) a synchronous Host sends one IG Control this
    /// often, so an IG that learns its Host from the first message can start. (An asynchronous
    /// Host sends at `async_rate_hz`.)
    std::chrono::milliseconds probe_interval{100};
    /// Size of the buffer a message is built in. Overrides `session.max_message_bytes`.
    std::size_t max_message_bytes = 16384;
    /// close() sends one IG Control requesting Reset/Standby first (ICD 2.8).
    bool standby_on_close = true;
    /// Further session settings (clock, stale window, validation, ...).
    SessionConfig session = {};
    /// Where the facade and its session log; null (the default) logs nothing.
    std::shared_ptr<spdlog::logger> logger = nullptr;
};

/// Counters of a Host (all since construction).
struct HostStats {
    SessionStats session;                 ///< The session's counters.
    std::uint64_t frames_sent = 0;        ///< Messages sent, start-up messages included.
    std::uint64_t frame_callbacks = 0;    ///< on_frame calls (Operate/Debug frames).
    std::uint64_t late_frames = 0;        ///< Asynchronous ticks missed because poll ran late.
    std::uint64_t oversize_adds = 0;      ///< HostFrame::add calls that did not fit.
    std::uint64_t datagrams_received = 0; ///< Datagrams taken from the transport.
    std::uint64_t receive_errors = 0;     ///< Datagrams that could not be used.
    std::uint64_t ig_restarts = 0;        ///< IG restarts detected (frame numbers or stale run).
    std::uint64_t link_losses = 0;        ///< Times the link went Lost.
    std::array<std::chrono::microseconds, link_state_count> time_in_state{};

    [[nodiscard]] std::chrono::microseconds time_in(LinkState s) const noexcept {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-*): LinkState values index the array.
        return time_in_state[static_cast<std::size_t>(s)];
    }
};

/// One frame being built by Host::on_frame: the IG Control is already in it; add the Host -> IG
/// packets of this frame. Valid during the callback only.
class HostFrame {
public:
    /// Append a Host -> IG packet. Errors are the session's (HostSession::add): BufferTooSmall
    /// when it does not fit (the frame is still sent with what fit; also reported to on_error),
    /// UnsupportedVersion, a validate() error (when enabled), ...
    template <Packet P> Result<void> add(const P& packet) {
        auto result = session_->add(packet);
        if (!result) {
            note_failure(result.error());
        }
        return result;
    }

    /// IG Frame Number of the last Start of Frame (0 before the first).
    [[nodiscard]] std::uint32_t ig_frame_number() const noexcept {
        return session_->last_ig_frame_number();
    }
    /// Timestamp of the last Start of Frame (zero if it carried none).
    [[nodiscard]] Timestamp ig_timestamp() const noexcept { return ig_timestamp_; }
    /// Host Frame Number of this frame.
    [[nodiscard]] std::uint32_t host_frame_number() const noexcept {
        return session_->host_frame_number();
    }
    /// Time since the previous message was sent (zero for the first). Start-up probes count as
    /// messages, so the first frame after Operate may follow a probe by less than a frame.
    [[nodiscard]] std::chrono::microseconds since_last_frame() const noexcept {
        return since_last_;
    }
    [[nodiscard]] LinkState link_state() const noexcept { return state_; }

private:
    friend class detail::HostCore;
    HostFrame(HostSession& session, detail::HostCore& core, LinkState state, Timestamp ig_timestamp,
              std::chrono::microseconds since_last) noexcept
        : session_(&session), core_(&core), state_(state), ig_timestamp_(ig_timestamp),
          since_last_(since_last) {}
    void note_failure(const Error& error);

    HostSession* session_;
    detail::HostCore* core_;
    LinkState state_;
    Timestamp ig_timestamp_;
    std::chrono::microseconds since_last_;
};

/// A Host talking to one IG. Move-only; not thread-safe. The destructor closes (without calling
/// the state-change callback). A moved-from Host may only be destroyed or assigned to.
class Host {
public:
    /// Open the UDP socket (never blocks; UDP is connectionless). The link is established by the
    /// start-up sequence that poll()/run() drive. Errors: InvalidFieldValue (bad configuration or
    /// address), TransportError (bind failed).
    [[nodiscard]] static Result<Host> connect(HostConfig config);

    /// Same, over a transport of your own (tests, multicast, fault injection).
    [[nodiscard]] static Result<Host> over(HostConfig config,
                                           std::unique_ptr<DatagramTransport> transport);

    Host(Host&&) noexcept;
    Host& operator=(Host&&) noexcept;
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    ~Host();

    // ---- receiving --------------------------------------------------------------------------

    /// Handler for IG -> Host packets of type P (the Start of Frame is consumed by the facade;
    /// read it with session().last_start_of_frame()). Version masking is applied first.
    template <class P, class F>
        requires(IgToHostPackets::contains<P> && !std::is_same_v<P, StartOfFrame>)
    void on(F&& handler) {
        session().handlers().template on<P>(std::forward<F>(handler));
    }
    /// Handler for packets with an opcode outside the session's set.
    void on_unknown(std::function<void(const UnknownPacket&)> handler);
    /// Handler for non-fatal problems: undecodable datagrams and packets, oversize add().
    void on_error(std::function<void(const Error&)> handler);
    /// Called on every change of link state, after the state has changed.
    void on_state_change(std::function<void(LinkState from, LinkState to)> handler);
    /// Called once per frame the Host sends while the link is Operate or Debug (per Start of
    /// Frame in synchronous mode; at `async_rate_hz` otherwise).
    void on_frame(std::function<void(HostFrame&)> handler);

    // ---- driving ----------------------------------------------------------------------------

    /// Drain the datagrams available within `timeout`, feed the session, check the peer timeout
    /// and send at most one message. A zero timeout never waits. Returns the fatal error of the
    /// transport; non-fatal problems go to on_error and stats(). After close() it does nothing.
    Result<PollStatus> poll(std::chrono::milliseconds timeout);

    /// poll() in a loop until `stop` is requested, close() is called or a fatal error occurs.
    Result<void> run(const std::stop_token& stop,
                     std::chrono::milliseconds poll_interval = std::chrono::milliseconds{20});

    /// Send one IG Control requesting Reset/Standby if `standby_on_close`, then close the
    /// transport. State -> Closed. Idempotent.
    void close();

    // ---- control and state ------------------------------------------------------------------

    Result<void> request_mode(IgMode mode);
    Result<void> request_database(int n);
    [[nodiscard]] LinkState state() const noexcept;
    [[nodiscard]] HostStats stats() const;
    /// The session underneath (escape hatch: handlers, last Start of Frame, peer version, ...).
    /// Do not call begin_frame()/finish_frame() on it while the facade drives it.
    [[nodiscard]] HostSession& session() noexcept;
    [[nodiscard]] const HostSession& session() const noexcept;

private:
    explicit Host(std::unique_ptr<detail::HostCore> core) noexcept;

    std::unique_ptr<detail::HostCore> core_;
};

} // namespace cigi
