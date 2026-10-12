#pragma once

// HostSession: the Host's side of one IG relationship (ICD 2.1, 2.2, 2.3, 2.8, 4.1.1, 4.2.1).
//
//   per frame:   session.receive(datagram_from_ig);            // Start of Frame
//                if (session.ready_to_send()) {
//                    session.begin_frame();                    // writes IG Control
//                    session.add(EntityControl{...});          // any Host -> IG packet
//                    transport.send(session.finish_frame());
//                }
//
// The session numbers frames, runs the IG mode start-up sequence and the database-load
// handshake, remembers the peer's version and byte order, masks content the peer's version does
// not have, and hands received packets to `handlers()`. Not thread-safe.

#include "cigi/dispatch.hpp"
#include "cigi/message/builder.hpp"
#include "cigi/packets.hpp"
#include "cigi/session/detail.hpp"
#include "cigi/session/session.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace cigi {

class HostSession {
public:
    explicit HostSession(SessionConfig config = {});
    HostSession(const HostSession&) = delete;
    HostSession& operator=(const HostSession&) = delete;
    HostSession(HostSession&&) noexcept = default;
    HostSession& operator=(HostSession&&) noexcept = default;
    ~HostSession() = default;

    // ---- receiving --------------------------------------------------------------------------

    /// Process one datagram from the IG: parse it, require IG -> Host, record the peer's version,
    /// byte order, mode and database, track the IG frame number (counting lost messages), run the
    /// database handshake, and then give the other packets to `handlers()`. The Start of Frame
    /// itself is not delivered: read it with last_start_of_frame().
    /// A Start of Frame repeating the previous IG frame number (`duplicate`) or a few numbers
    /// behind it (`stale`, see SessionConfig::stale_window) changes no state and does not make a
    /// synchronous Host ready to send, but the message's other packets are still delivered.
    /// Errors (nothing is delivered): the parse errors of MessageView::parse, WrongDirection,
    /// and the Start of Frame decode errors.
    Result<ReceiveReport> receive(std::span<const std::byte> datagram);

    /// Handlers for the IG -> Host packets (replaces per-packet event processors). Version
    /// masking is applied before a handler runs. A `UnknownPacket` (its data points into the
    /// receive buffer) is valid during the call only.
    [[nodiscard]] Dispatcher<IgToHostPackets>& handlers() noexcept { return handlers_; }

    /// Forget the IG, as after a link loss or an IG restart: the last Start of Frame, the peer's
    /// version and byte order, the IG frame tracking and the synchronous ready state are cleared
    /// (operational() is false, add() fails with NotOperational until a new operational Start of
    /// Frame). Config, handlers, statistics, the Host's own frame numbering and the requested mode
    /// and database are kept, so the start-up can be replayed. An open message is discarded.
    void reset_peer() noexcept;

    // ---- sending ----------------------------------------------------------------------------

    /// Synchronous mode: true once a Start of Frame has arrived and no message has been sent
    /// since (ICD 2.2.1 - answer each Start of Frame once). Asynchronous mode: always true.
    [[nodiscard]] bool ready_to_send() const noexcept;

    /// Start a message: writes the IG Control packet (Host Frame Number, Last IG Frame Number,
    /// timestamp, requested mode, database number, extrapolation flag). Beginning again before
    /// finish_frame() discards the open message (its frame number is not reused).
    Result<void> begin_frame();

    /// Append a Host -> IG packet to the open message. Errors: NotOperational (start-up sequence
    /// not complete, when enforced), UnexpectedFirstPacket (no message open),
    /// UnsupportedVersion (content the peer's version cannot carry), a validate() error (when
    /// `validate_outgoing`), and the builder's (WrongDirection, DuplicateFramePacket,
    /// BufferTooSmall). A failed add() leaves the message unchanged.
    template <Packet P> Result<void> add(const P& packet) {
        if (!frame_open_) {
            return reject(
                Error{.code = ErrorCode::UnexpectedFirstPacket, .offset = 0, .opcode = P::opcode});
        }
        if constexpr (P::opcode != ig_control_opcode) {
            if (config_.enforce_startup_sequence && !operational()) {
                return reject(Error{
                    .code = ErrorCode::NotOperational,
                    .offset = builder_.size(),
                    .opcode = P::opcode,
                });
            }
        }
        const auto adapted = detail::adapt_outgoing(packet, peer_version());
        if (!adapted) {
            return reject(adapted.error());
        }
        if (config_.validate_outgoing) {
            if (auto ok = validate(*adapted); !ok) {
                return reject(ok.error());
            }
        }
        if (auto ok = builder_.add(*adapted); !ok) {
            return reject(ok.error());
        }
        return {};
    }

    /// Close the open message and return its bytes (valid until the next begin_frame()). Empty if
    /// none is open.
    [[nodiscard]] std::span<const std::byte> finish_frame() noexcept;

    [[nodiscard]] bool frame_open() const noexcept { return frame_open_; }

    // ---- control ----------------------------------------------------------------------------

    /// IG mode to request in IG Control. Reset/Standby, Operate or Debug; Offline Maintenance
    /// cannot be commanded (ICD 2.8) and fails with InvalidFieldValue. The default is
    /// Reset/Standby; request Operate to start the mission.
    Result<void> request_mode(IgMode mode);
    [[nodiscard]] IgMode requested_mode() const noexcept { return requested_mode_; }

    /// Ask the IG to load database `n` (1..127; 0 cancels). The number is sent in IG Control until
    /// the IG answers with -n (loading) - the acknowledgement of ICD 4.1.1 - or -128 (cannot be
    /// loaded); the session then sends 0 again. An IG with a global database reports 0 instead
    /// (ICD 4.1.1): an operating IG reporting 0 in a Start of Frame that acknowledges the message
    /// which first carried the request also ends it. The IG ignores the request in Reset/Standby,
    /// so it is kept until the IG is operating.
    Result<void> request_database(int n);
    /// The database number the session is currently requesting (0: none).
    [[nodiscard]] std::int8_t pending_database() const noexcept { return database_request_; }

    /// Extrapolation/Interpolation Enable bit of IG Control (3.3 peers only; ICD 4.1.1).
    void set_extrapolation_enabled(bool enabled) noexcept { extrapolation_ = enabled; }

    // ---- state ------------------------------------------------------------------------------

    /// The last Start of Frame received (nullopt before the first).
    [[nodiscard]] const std::optional<StartOfFrame>& last_start_of_frame() const noexcept {
        return last_sof_;
    }
    /// Version and byte order of the last valid message (nullopt before the first).
    [[nodiscard]] const std::optional<PeerInfo>& peer() const noexcept { return peer_; }
    /// The IG's reported mode (Reset/Standby before the first Start of Frame).
    [[nodiscard]] IgMode ig_mode() const noexcept {
        return last_sof_ ? last_sof_->ig_mode : IgMode::ResetStandby;
    }
    /// True once the IG reports Operate or Debug (ICD 2.8): mission data may be sent.
    [[nodiscard]] bool operational() const noexcept {
        return last_sof_ && detail::is_operational(last_sof_->ig_mode);
    }
    /// The IG's reported database number (ICD 4.2.1); 0 before the first Start of Frame.
    [[nodiscard]] std::int8_t ig_database() const noexcept {
        return last_sof_ ? last_sof_->database_number : std::int8_t{0};
    }
    /// True while the IG reports a database being loaded (-127..-1).
    [[nodiscard]] bool database_loading() const noexcept {
        return ig_database() < 0 && ig_database() != database_unavailable;
    }
    /// Number of the most recent message begun (initial_frame_number - 1 before the first).
    [[nodiscard]] std::uint32_t host_frame_number() const noexcept { return next_frame_ - 1U; }
    /// IG Frame Number of the last Start of Frame (0 before the first).
    [[nodiscard]] std::uint32_t last_ig_frame_number() const noexcept { return ig_frames_.last(); }

    [[nodiscard]] const SessionStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const SessionConfig& config() const noexcept { return config_; }

private:
    static constexpr std::int8_t database_unavailable = -128; // ICD 4.2.1

    [[nodiscard]] ProtocolVersion peer_version() const noexcept {
        return peer_ ? peer_->version : protocol_version;
    }
    Result<void> reject(const Error& error);
    void track_start_of_frame(const StartOfFrame& sof, ReceiveReport& report);
    void run_database_handshake(const StartOfFrame& sof);

    SessionConfig config_;
    ClockFn clock_;
    std::shared_ptr<spdlog::logger> log_;
    std::vector<std::byte> storage_;
    MessageBuilder builder_;
    bool frame_open_ = false;
    bool ready_ = false;
    std::uint32_t next_frame_;
    IgMode requested_mode_ = IgMode::ResetStandby;
    std::int8_t database_request_ = 0;
    /// Host frame number of the first message that carried database_request_ (none yet: unset).
    std::optional<std::uint32_t> request_frame_;
    bool extrapolation_ = false;
    std::optional<StartOfFrame> last_sof_;
    std::optional<PeerInfo> peer_;
    detail::FrameTracker ig_frames_;
    SessionStats stats_;
    Dispatcher<IgToHostPackets> handlers_;
};

} // namespace cigi
