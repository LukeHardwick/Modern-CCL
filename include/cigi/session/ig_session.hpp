#pragma once

// IgSession: the Image Generator's side of one Host relationship (mirror of HostSession; ICD 2.2,
// 2.3, 2.8, 4.1.1, 4.2.1).
//
//   per frame:   session.begin_frame();                        // writes Start of Frame
//                session.add(HatHotResponse{...});             // responses, once operating
//                transport.send(session.finish_frame());
//                ...                                           // later, for each Host datagram:
//                session.receive(datagram_from_host);
//
// The session numbers Start of Frame messages, applies the mode rules of ICD 2.8 (outside
// Operate/Debug every packet but IG Control is ignored and counted), passes mode and database
// requests to the application, and hands the accepted packets to `handlers()`. Not thread-safe.

#include "cigi/dispatch.hpp"
#include "cigi/message/builder.hpp"
#include "cigi/packets.hpp"
#include "cigi/session/detail.hpp"
#include "cigi/session/session.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cigi {

class IgSession {
public:
    explicit IgSession(SessionConfig config = {});
    IgSession(const IgSession&) = delete;
    IgSession& operator=(const IgSession&) = delete;
    IgSession(IgSession&&) noexcept = default;
    IgSession& operator=(IgSession&&) noexcept = default;
    ~IgSession() = default;

    // ---- receiving --------------------------------------------------------------------------

    /// Process one datagram from the Host: parse it, require Host -> IG, record the peer's version
    /// and byte order, track the Host frame number (counting lost messages), apply mode and
    /// database requests, and hand packets to `handlers()`:
    ///  - Reset/Standby: only the IG Control packet; the rest are ignored and counted (ICD 2.8,
    ///    4.2.1).
    ///  - Offline Maintenance: nothing is delivered, IG Control included (ICD 4.2.1), and no mode
    ///    request is honoured.
    ///  - Operate / Debug: the whole message.
    /// Errors (nothing is delivered): the parse errors of MessageView::parse, WrongDirection, and
    /// the IG Control decode errors.
    Result<ReceiveReport> receive(std::span<const std::byte> datagram);

    /// Handlers for the Host -> IG packets. Version masking is applied before a handler runs.
    [[nodiscard]] Dispatcher<HostToIgPackets>& handlers() noexcept { return handlers_; }

    /// Called when a Host asks for a mode different from the IG's current one and different from
    /// its previous request. Return true to accept: the application then calls set_mode() once the
    /// transition has completed. Return false to refuse (counted). Without a callback every
    /// request is accepted and applied at once. Never called for Offline Maintenance (the Host
    /// cannot request it; ICD 2.8) or while the IG is in it.
    void on_mode_request(std::function<bool(IgMode)> callback) {
        mode_request_ = std::move(callback);
    }

    /// Called when the Host asks, in Operate/Debug, for a database load (1..127). Answer with
    /// set_database(-n) when loading starts and set_database(n) when done (ICD 4.1.1); -128 for a
    /// database that cannot be loaded. Called once per request (until the Host sends 0 again).
    void on_database_request(std::function<void(std::int8_t)> callback) {
        database_request_ = std::move(callback);
    }

    // ---- sending ----------------------------------------------------------------------------

    /// Start a Start of Frame message (IG Frame Number, Last Host Frame Number, mode, status,
    /// database, Earth Reference Model, timestamp). Beginning again before finish_frame()
    /// discards the open message.
    Result<void> begin_frame();

    /// Append an IG -> Host packet. Errors: NotOperational (the IG sends only Start of Frame in
    /// Reset/Standby and Offline Maintenance, ICD 2.8, 4.2.1; when enforced),
    /// UnexpectedFirstPacket (no message open), a validate() error (when `validate_outgoing`) and
    /// the builder's errors. A failed add() leaves the message unchanged.
    template <Packet P> Result<void> add(const P& packet) {
        if (!frame_open_) {
            return reject(
                Error{.code = ErrorCode::UnexpectedFirstPacket, .offset = 0, .opcode = P::opcode});
        }
        if constexpr (P::opcode != start_of_frame_opcode) {
            if (config_.enforce_startup_sequence && !detail::is_operational(mode_)) {
                return reject(Error{
                    .code = ErrorCode::NotOperational,
                    .offset = builder_.size(),
                    .opcode = P::opcode,
                });
            }
        }
        if (config_.validate_outgoing) {
            if (auto ok = validate(packet); !ok) {
                return reject(ok.error());
            }
        }
        if (auto ok = builder_.add(packet); !ok) {
            return reject(ok.error());
        }
        return {};
    }

    /// Close the open message and return its bytes (valid until the next begin_frame()).
    [[nodiscard]] std::span<const std::byte> finish_frame() noexcept;

    [[nodiscard]] bool frame_open() const noexcept { return frame_open_; }

    // ---- state the application owns ---------------------------------------------------------

    /// The mode reported in Start of Frame. Call when a transition (accepted via on_mode_request,
    /// or the IG's own UI) has completed. Any mode, Offline Maintenance included.
    void set_mode(IgMode mode);
    [[nodiscard]] IgMode mode() const noexcept { return mode_; }

    /// The Database Number reported in Start of Frame (ICD 4.2.1): -128 unavailable, -n loading,
    /// 0 the IG controls loading, n loaded.
    void set_database(std::int8_t database) noexcept { database_ = database; }
    [[nodiscard]] std::int8_t database() const noexcept { return database_; }

    void set_status(std::uint8_t ig_status) noexcept { status_ = ig_status; }
    void set_earth_reference_model(EarthReferenceModel model) noexcept { erm_ = model; }

    // ---- state ------------------------------------------------------------------------------

    /// The last IG Control received (nullopt before the first).
    [[nodiscard]] const std::optional<IgControl>& last_ig_control() const noexcept {
        return last_control_;
    }
    [[nodiscard]] const std::optional<PeerInfo>& peer() const noexcept { return peer_; }
    /// Host Frame Number of the last IG Control received (0 before the first).
    [[nodiscard]] std::uint32_t last_host_frame_number() const noexcept {
        return host_frames_.last();
    }
    /// Host Frame Number LSN to put in responses to requests received in the last message
    /// (ICD 2.3, 4.2.2).
    [[nodiscard]] std::uint8_t host_frame_lsn() const noexcept {
        return cigi::host_frame_lsn(host_frames_.last());
    }
    /// Number of the most recent Start of Frame begun.
    [[nodiscard]] std::uint32_t ig_frame_number() const noexcept { return next_frame_ - 1U; }

    [[nodiscard]] const SessionStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const SessionConfig& config() const noexcept { return config_; }

private:
    [[nodiscard]] ProtocolVersion peer_version() const noexcept {
        return peer_ ? peer_->version : protocol_version;
    }
    Result<void> reject(const Error& error);
    void track_control(const IgControl& control, ReceiveReport& report);

    SessionConfig config_;
    ClockFn clock_;
    std::shared_ptr<spdlog::logger> log_;
    std::vector<std::byte> storage_;
    MessageBuilder builder_;
    bool frame_open_ = false;
    std::uint32_t next_frame_;
    IgMode mode_ = IgMode::ResetStandby;
    std::int8_t database_ = 0;
    std::uint8_t status_ = 0;
    EarthReferenceModel erm_ = EarthReferenceModel::Wgs84;
    std::optional<IgControl> last_control_;
    std::optional<PeerInfo> peer_;
    std::optional<IgMode> last_requested_mode_;
    std::int8_t last_requested_database_ = 0;
    detail::FrameTracker host_frames_;
    SessionStats stats_;
    std::function<bool(IgMode)> mode_request_;
    std::function<void(std::int8_t)> database_request_;
    Dispatcher<HostToIgPackets> handlers_;
};

} // namespace cigi
