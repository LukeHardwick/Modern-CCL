#include "cigi/session/host_session.hpp"

#include <algorithm>
#include <spdlog/logger.h>
#include <utility>

namespace cigi {

namespace {

constexpr std::size_t min_message_bytes = detail::frame_size;

} // namespace

HostSession::HostSession(SessionConfig config)
    : config_(std::move(config)), clock_(config_.clock ? config_.clock : ClockFn{steady_timestamp}),
      log_(detail::resolve_logger(config_)),
      storage_(std::max(config_.max_message_bytes, min_message_bytes)),
      builder_(Direction::HostToIg, storage_), next_frame_(config_.initial_frame_number) {}

bool HostSession::ready_to_send() const noexcept {
    return !config_.synchronous || ready_;
}

Result<void> HostSession::reject(const Error& error) {
    ++stats_.rejected_packets;
    log_->debug("add() rejected packet {}: {}", static_cast<unsigned>(error.opcode),
                to_string(error.code));
    return std::unexpected(error);
}

Result<void> HostSession::begin_frame() {
    builder_.clear();
    const std::uint32_t frame = next_frame_++;
    const bool legacy = peer_version().minor < detail::first_modern_minor;

    IgControl control;
    control.database_number = database_request_;
    control.ig_mode = requested_mode_;
    control.timestamp_valid = true;
    control.timestamp = clock_();
    control.extrapolation_enabled = extrapolation_ && peer_version().minor >= 3;
    // CIGI 3.0/3.1 peers expect the IG's own Frame Counter echoed back (ICD 2.3); from 3.2 the Host
    // numbers its own frames and acknowledges the IG's in Last IG Frame Number.
    control.host_frame_number = legacy ? ig_frames_.last() : frame;
    control.last_ig_frame_number = legacy ? 0U : ig_frames_.last();

    frame_open_ = true;
    return builder_.add(control); // cannot fail: the storage holds at least one frame packet
}

std::span<const std::byte> HostSession::finish_frame() noexcept {
    if (!frame_open_) {
        return {};
    }
    frame_open_ = false;
    ready_ = false;
    ++stats_.messages_sent;
    return builder_.bytes();
}

Result<void> HostSession::request_mode(IgMode mode) {
    if (mode == IgMode::OfflineMaintenance || !EnumTraits<IgMode>::is_defined(mode)) {
        return std::unexpected(
            Error{.code = ErrorCode::InvalidFieldValue, .offset = 4, .opcode = ig_control_opcode});
    }
    requested_mode_ = mode;
    return {};
}

Result<void> HostSession::request_database(int n) {
    if (n < 0 || n > 127) {
        return std::unexpected(
            Error{.code = ErrorCode::InvalidFieldValue, .offset = 3, .opcode = ig_control_opcode});
    }
    database_request_ = static_cast<std::int8_t>(n);
    return {};
}

void HostSession::track_start_of_frame(const StartOfFrame& sof, ReceiveReport& report) {
    const IgMode old_mode = ig_mode();
    const std::int8_t old_database = ig_database();
    const bool first = !last_sof_;

    // Frame numbering (ICD 2.3). A 3.0/3.1 IG's "Frame Counter" is its frame number too.
    const auto seen = ig_frames_.observe(sof.ig_frame_number);
    report.lost_frames = seen.lost;
    report.frame_regressed = seen.regressed;
    stats_.lost_frames += seen.lost;
    if (seen.lost != 0) {
        log_->warn("lost {} IG message(s) before IG frame {}", seen.lost, sof.ig_frame_number);
    }
    if (seen.regressed) {
        ++stats_.frame_regressions;
        log_->warn("IG frame number went back to {} (reordered, duplicated or IG restarted)",
                   sof.ig_frame_number);
    }

    last_sof_ = sof;
    report.mode_changed = !first && sof.ig_mode != old_mode;
    report.database_changed = !first && sof.database_number != old_database;
    if (first || sof.ig_mode != old_mode) {
        log_->info("IG mode is {}", detail::mode_name(sof.ig_mode));
    }
    if (!first && sof.database_number != old_database) {
        log_->info("IG database number {} -> {}", static_cast<int>(old_database),
                   static_cast<int>(sof.database_number));
    }

    // Database handshake (ICD 4.1.1): -n acknowledges the request, so go back to 0; -128 means the
    // IG cannot load it.
    if (database_request_ != 0) {
        if (sof.database_number == -database_request_) {
            log_->debug("IG is loading database {}", static_cast<int>(database_request_));
            database_request_ = 0;
        } else if (sof.database_number == database_unavailable) {
            log_->warn("IG cannot load database {}", static_cast<int>(database_request_));
            database_request_ = 0;
        }
    }
}

Result<ReceiveReport> HostSession::receive(std::span<const std::byte> datagram) {
    const auto view = MessageView::parse(datagram);
    if (!view) {
        ++stats_.decode_errors;
        log_->warn("dropped datagram: {}", to_string(view.error().code));
        return std::unexpected(view.error());
    }
    if (view->direction() != Direction::IgToHost) {
        ++stats_.decode_errors;
        log_->warn("dropped Host-to-IG message received by a Host");
        return std::unexpected(
            Error{.code = ErrorCode::WrongDirection, .offset = 0, .opcode = ig_control_opcode});
    }
    const auto sof = (*view->begin()).and_then([](const RawPacket& raw) {
        return raw.decode_as<StartOfFrame>();
    });
    if (!sof) {
        ++stats_.decode_errors;
        log_->warn("dropped Start of Frame: {}", to_string(sof.error().code));
        return std::unexpected(sof.error());
    }

    ++stats_.messages_received;
    const PeerInfo info{.version = view->version(), .byte_order = view->byte_order()};
    if (peer_ != info) {
        log_->info("IG speaks CIGI {}.{}, {} byte order", static_cast<int>(info.version.major),
                   static_cast<int>(info.version.minor),
                   info.byte_order == wire::ByteOrder::Native ? "native" : "swapped");
        peer_ = info;
    }

    ReceiveReport report;
    track_start_of_frame(*sof, report);
    ready_ = true;

    const DispatchStats walked = detail::deliver_message(handlers_, *view);
    stats_.unknown_packets += walked.unknown;
    stats_.decode_errors += walked.errors;
    return report;
}

} // namespace cigi
