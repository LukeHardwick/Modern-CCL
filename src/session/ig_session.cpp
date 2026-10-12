#include "cigi/session/ig_session.hpp"

#include <algorithm>
#include <optional>
#include <spdlog/logger.h>
#include <utility>

namespace cigi {

namespace {

constexpr std::size_t min_message_bytes = detail::frame_size;

/// Packets of `view` after the first, up to the first structural error (returned in `error`).
std::uint64_t count_after_first(const MessageView& view, std::optional<Error>& error) {
    std::uint64_t n = 0;
    bool first = true;
    for (const auto& item : view) {
        if (!item) {
            error = item.error();
            break;
        }
        if (!std::exchange(first, false)) {
            ++n;
        }
    }
    return n;
}

} // namespace

IgSession::IgSession(SessionConfig config)
    : config_(std::move(config)), clock_(config_.clock ? config_.clock : ClockFn{steady_timestamp}),
      log_(detail::resolve_logger(config_)),
      storage_(std::max(config_.max_message_bytes, min_message_bytes)),
      builder_(Direction::IgToHost, storage_), next_frame_(config_.initial_frame_number),
      host_frames_(config_.stale_window) {
    handlers_.ignore_user_packets(config_.ignore_user_packets);
}

Result<void> IgSession::reject(const Error& error) {
    ++stats_.rejected_packets;
    log_->debug("add() rejected packet {}: {}", static_cast<unsigned>(error.opcode),
                to_string(error.code));
    return std::unexpected(error);
}

Result<void> IgSession::begin_frame() {
    builder_.clear();
    StartOfFrame sof;
    sof.database_number = database_;
    sof.ig_status = status_;
    sof.ig_mode = mode_;
    sof.timestamp_valid = true;
    sof.earth_reference_model = erm_;
    sof.ig_frame_number = next_frame_++;
    sof.timestamp = clock_();
    // A 3.0/3.1 Host has no Last Host Frame field; it echoes the IG's own counter instead.
    sof.last_host_frame_number = host_frames_.last();
    frame_open_ = true;
    return builder_.add(sof); // cannot fail: the storage holds at least one frame packet
}

std::span<const std::byte> IgSession::finish_frame() noexcept {
    if (!frame_open_) {
        return {};
    }
    frame_open_ = false;
    ++stats_.messages_sent;
    return builder_.bytes();
}

void IgSession::set_mode(IgMode mode) {
    if (mode != mode_) {
        log_->info("IG mode {} -> {}", detail::mode_name(mode_), detail::mode_name(mode));
    }
    mode_ = mode;
    // Let the Host's next request be judged afresh against the new mode.
    last_requested_mode_.reset();
}

void IgSession::reset() noexcept {
    builder_.clear();
    frame_open_ = false;
    next_frame_ = config_.initial_frame_number;
    mode_ = IgMode::ResetStandby;
    database_ = 0;
    last_control_.reset();
    peer_.reset();
    last_requested_mode_.reset();
    last_requested_database_ = 0;
    host_frames_.reset();
}

/// Returns true if the IG Control is current (not a duplicate or stale one).
bool IgSession::track_control(const IgControl& control, ReceiveReport& report) {
    // A 3.0/3.1 Host echoes the IG's counter in this field (ICD 2.3): its gaps and repeats say
    // nothing about the Host's messages, so only remember it.
    if (peer_version().minor < detail::first_modern_minor) {
        host_frames_.record(control.host_frame_number);
        last_control_ = control;
        return true;
    }
    const auto seen = host_frames_.observe(control.host_frame_number);
    report.duplicate = seen.duplicate;
    report.stale = seen.stale;
    if (!seen.accepted()) {
        if (seen.duplicate) {
            ++stats_.duplicate_frames;
            log_->debug("duplicate IG Control {}", control.host_frame_number);
        } else {
            ++stats_.stale_frames;
            log_->debug("stale IG Control {} (newest is {})", control.host_frame_number,
                        host_frames_.last());
        }
        return false;
    }
    report.lost_frames = seen.lost;
    report.frame_regressed = seen.regressed;
    stats_.lost_frames += seen.lost;
    if (seen.lost != 0) {
        log_->warn("lost {} Host message(s) before Host frame {}", seen.lost,
                   control.host_frame_number);
    }
    if (seen.regressed) {
        ++stats_.frame_regressions;
        log_->warn("Host frame number jumped to {} (Host restarted)", control.host_frame_number);
    }
    last_control_ = control;
    return true;
}

Result<ReceiveReport> IgSession::receive(std::span<const std::byte> datagram) {
    const auto view = MessageView::parse(datagram);
    if (!view) {
        ++stats_.decode_errors;
        log_->warn("dropped datagram: {}", to_string(view.error().code));
        return std::unexpected(view.error());
    }
    if (view->direction() != Direction::HostToIg) {
        ++stats_.decode_errors;
        log_->warn("dropped IG-to-Host message received by an IG");
        return std::unexpected(
            Error{.code = ErrorCode::WrongDirection, .offset = 0, .opcode = start_of_frame_opcode});
    }
    const auto control =
        (*view->begin()).and_then([](const RawPacket& raw) { return raw.decode_as<IgControl>(); });
    if (!control) {
        ++stats_.decode_errors;
        log_->warn("dropped IG Control: {}", to_string(control.error().code));
        return std::unexpected(control.error());
    }

    ++stats_.messages_received;
    const PeerInfo info{.version = view->version(), .byte_order = view->byte_order()};
    if (peer_ != info) {
        log_->info("Host speaks CIGI {}.{}, {} byte order", static_cast<int>(info.version.major),
                   static_cast<int>(info.version.minor),
                   info.byte_order == wire::ByteOrder::Native ? "native" : "swapped");
        peer_ = info;
    }

    ReceiveReport report;
    const bool current = track_control(*control, report);

    // Offline Maintenance: the IG ignores all Host data (ICD 4.2.1).
    if (mode_ == IgMode::OfflineMaintenance) {
        std::optional<Error> error;
        stats_.ignored_packets += 1 + count_after_first(*view, error);
        return report;
    }

    if (current) {
        apply_requests(*control, report);
    }

    if (mode_ == IgMode::ResetStandby) {
        // Nothing but IG Control gets through in Reset/Standby (ICD 2.8, 4.2.1), and the session
        // consumes that itself.
        std::optional<Error> error;
        stats_.ignored_packets += count_after_first(*view, error);
        if (error) {
            ++stats_.decode_errors;
            static_cast<void>(handlers_.deliver(*error));
        }
        return report;
    }
    const DispatchStats walked = detail::deliver_message<IgControl>(handlers_, *view);
    stats_.unknown_packets += walked.unknown;
    stats_.decode_errors += walked.errors;
    return report;
}

void IgSession::apply_requests(const IgControl& control, ReceiveReport& report) {
    // Mode request (ICD 4.1.1): judged once per change of the Host's request.
    const IgMode requested = control.ig_mode;
    if (requested != last_requested_mode_ && requested != mode_) {
        report.mode_changed = true;
        log_->info("Host requests mode {}", detail::mode_name(requested));
        bool accept = requested != IgMode::OfflineMaintenance; // the Host cannot command it
        if (accept && mode_request_) {
            accept = mode_request_(requested);
        } else if (accept) {
            set_mode(requested);
        }
        if (!accept) {
            ++stats_.mode_requests_refused;
            log_->info("mode request {} refused", detail::mode_name(requested));
        }
    }
    last_requested_mode_ = requested;

    // Database request (ICD 4.1.1): ignored in Reset/Standby, then judged once per change. The
    // ICD defines only 0 and 1..127 for the Host to send; a negative number is counted and ignored.
    if (detail::is_operational(mode_)) {
        const std::int8_t database = control.database_number;
        if (database != last_requested_database_) {
            last_requested_database_ = database;
            if (database < 0) {
                ++stats_.rejected_packets;
                log_->warn("Host requests invalid database {}; ignored",
                           static_cast<int>(database));
            } else if (database != 0) {
                report.database_changed = true;
                log_->info("Host requests database {}", static_cast<int>(database));
                if (database_request_) {
                    database_request_(database);
                }
            }
        }
    }
}

} // namespace cigi
