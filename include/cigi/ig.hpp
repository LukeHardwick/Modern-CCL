#pragma once

// cigi::Ig: the mirror of cigi::Host, small on purpose. It listens for a Host, sends a Start of
// Frame at a fixed rate, applies the mode rules of the ICD (2.8) through its IgSession and lets
// the application answer: mode and database requests, responses added to each frame. Meant for
// the mock IGs (T09, T09b) and tests, not as a renderer's integration layer.
//
//   auto ig = cigi::Ig::connect({.listen_port = 8004});
//   ig->on<cigi::EntityControl>([](const cigi::EntityControl& e) { ... });
//   ig->on_frame([](cigi::IgFrame& f) { f.add(cigi::HatHotResponse{...}); });
//   ig->run(stop_token);
//
// THREADING: an Ig is NOT thread-safe; every callback runs on the thread calling poll()/run().

#include "cigi/datagram_transport.hpp"
#include "cigi/link_state.hpp"
#include "cigi/packet_set.hpp"
#include "cigi/packets.hpp"
#include "cigi/session/ig_session.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <type_traits>
#include <utility>

namespace cigi {

namespace detail {
class IgCore;
}

struct IgConfig {
    /// Interface to bind; empty means any.
    std::string listen_address = {};
    std::uint16_t listen_port = 8004;
    /// Where Start of Frame goes (numeric address). Empty: reply to the sender of the most recent
    /// datagram, and send nothing until the first one arrives.
    std::string host_address = {};
    std::uint16_t host_port = 8005;
    /// Start of Frame rate (ICD 2.4; typically the display frame rate).
    double rate_hz = 60.0;
    /// No message from the Host for this long: host_connected() turns false.
    std::chrono::milliseconds peer_timeout{1000};
    /// Size of the buffer a message is built in. Overrides `session.max_message_bytes`.
    std::size_t max_message_bytes = 16384;
    SessionConfig session = {};
    /// Where the facade and its session log; null (the default) logs nothing.
    std::shared_ptr<spdlog::logger> logger = nullptr;
};

struct IgStats {
    SessionStats session;
    std::uint64_t frames_sent = 0;        ///< Start of Frame messages sent.
    std::uint64_t frames_skipped = 0;     ///< Ticks not sent because the IG was paused.
    std::uint64_t late_frames = 0;        ///< Ticks missed because poll ran late.
    std::uint64_t datagrams_received = 0; ///< Datagrams taken from the transport.
    std::uint64_t receive_errors = 0;     ///< Datagrams that could not be used.
    std::uint64_t restarts = 0;           ///< restart() calls.
};

/// One Start of Frame message being built by Ig::on_frame. Valid during the callback only.
class IgFrame {
public:
    /// Append an IG -> Host packet (the session's rules: HostSession::add counterpart).
    template <Packet P> Result<void> add(const P& packet) { return session_->add(packet); }

    /// Host Frame Number LSN to put in responses to requests of the last message (ICD 2.3, 4.2.2).
    [[nodiscard]] std::uint8_t host_frame_lsn() const noexcept { return session_->host_frame_lsn(); }
    [[nodiscard]] std::uint32_t host_frame_number() const noexcept {
        return session_->last_host_frame_number();
    }
    [[nodiscard]] std::uint32_t ig_frame_number() const noexcept {
        return session_->ig_frame_number();
    }
    [[nodiscard]] IgMode mode() const noexcept { return session_->mode(); }

private:
    friend class detail::IgCore;
    explicit IgFrame(IgSession& session) noexcept : session_(&session) {}

    IgSession* session_;
};

/// An IG talking to one Host. Move-only; not thread-safe.
class Ig {
public:
    /// Open the UDP socket. Errors: InvalidFieldValue (bad configuration or address),
    /// TransportError (bind failed).
    [[nodiscard]] static Result<Ig> connect(IgConfig config);
    /// Same, over a transport of your own. Start of Frame is sent from the first poll() on.
    [[nodiscard]] static Result<Ig> over(IgConfig config,
                                         std::unique_ptr<DatagramTransport> transport);

    Ig(Ig&&) noexcept;
    Ig& operator=(Ig&&) noexcept;
    Ig(const Ig&) = delete;
    Ig& operator=(const Ig&) = delete;
    ~Ig();

    // ---- receiving --------------------------------------------------------------------------

    /// Handler for Host -> IG packets of type P (IG Control is consumed by the session; read it
    /// with session().last_ig_control()). Only called in Operate/Debug (ICD 2.8).
    template <class P, class F>
        requires(HostToIgPackets::contains<P> && !std::is_same_v<P, IgControl>)
    void on(F&& handler) {
        session().handlers().template on<P>(std::forward<F>(handler));
    }
    void on_unknown(std::function<void(const UnknownPacket&)> handler);
    void on_error(std::function<void(const Error&)> handler);
    /// Accept (true) or refuse (false) a Host's request for a different mode. On true the
    /// application calls set_mode() when the transition is done. Without it every request is
    /// accepted at once.
    void on_mode_request(std::function<bool(IgMode)> handler);
    /// A Host asks (in Operate/Debug) for database n: answer with set_database(-n), then (n).
    void on_database_request(std::function<void(std::int8_t)> handler);
    /// Called once per Start of Frame while the IG mode is Operate or Debug, after the facade has
    /// written the Start of Frame: add the responses of this frame.
    void on_frame(std::function<void(IgFrame&)> handler);

    // ---- driving ----------------------------------------------------------------------------

    /// Drain the datagrams available within `timeout`, feed the session and send a Start of Frame
    /// if one is due. A zero timeout never waits.
    Result<PollStatus> poll(std::chrono::milliseconds timeout);
    Result<void> run(std::stop_token stop,
                     std::chrono::milliseconds poll_interval = std::chrono::milliseconds{20});
    /// Close the transport. Idempotent; poll() then does nothing.
    void close();

    // ---- state and fault hooks --------------------------------------------------------------

    /// Report mode / database in the Start of Frame (IgSession::set_mode / set_database).
    void set_mode(IgMode mode);
    void set_database(std::int8_t database);
    [[nodiscard]] IgMode mode() const noexcept;
    /// True while a message from the Host arrived within `peer_timeout`.
    [[nodiscard]] bool host_connected() const noexcept;

    /// Fault hook: stop sending Start of Frame (the IG still receives). A synchronous Host sees
    /// the link go Lost after its `peer_timeout`.
    void set_paused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept;
    /// Fault hook: behave as if the IG process restarted: frame counter and session back to
    /// Reset/Standby (IgSession::reset()), pacing restarted.
    void restart();

    [[nodiscard]] IgStats stats() const;
    /// The session underneath (escape hatch).
    [[nodiscard]] IgSession& session() noexcept;
    [[nodiscard]] const IgSession& session() const noexcept;

private:
    explicit Ig(std::unique_ptr<detail::IgCore> core) noexcept;

    std::unique_ptr<detail::IgCore> core_;
};

} // namespace cigi
