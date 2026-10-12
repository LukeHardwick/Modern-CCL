#pragma once

// Logic of fuzz target 3 (fuzz_sessions): arbitrary datagrams into HostSession::receive and
// IgSession::receive, for every IG mode, with every combination of the session options. Sessions
// must never crash, assert or read out of bounds; after a receive they must still be usable
// (begin_frame / add / finish_frame / reset).
//
// Input layout: byte 0 = options (bit 0 synchronous, bit 1 enforce start-up sequence, bit 2
// validate outgoing, bit 3 the IG accepts mode requests, bit 4 receive the datagram a second
// time), byte 1 bits 0-1 = the IG mode (0..3) the sessions are put in before the datagram
// arrives, the rest = the datagram.

#include "cigi/message/view.hpp"
#include "cigi/packets.hpp"
#include "cigi/session/host_session.hpp"
#include "cigi/session/ig_session.hpp"
#include "harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cigi::fuzz {

namespace detail_sessions {

/// Register a trivial handler for every packet of a set, plus unknown and error handlers, so that
/// delivery runs.
template <class... Ps>
void register_handlers(Dispatcher<PacketSet<Ps...>>& d, std::size_t& delivered) {
    (d.template on<Ps>([&delivered](const Ps& p) {
        ++delivered;
        (void)validate(p);
    }),
     ...);
    d.on_unknown([&delivered](const UnknownPacket& p) { delivered += p.bytes.size(); });
    d.on_error([&delivered](const Error& e) { delivered += e.offset; });
}

/// Statistics counters only ever grow.
class StatsWatch {
public:
    void update(const SessionStats& s) {
        const std::array<std::uint64_t, 11> now{
            s.messages_sent,    s.messages_received, s.lost_frames,           s.frame_regressions,
            s.duplicate_frames, s.stale_frames,      s.decode_errors,         s.ignored_packets,
            s.unknown_packets,  s.rejected_packets,  s.mode_requests_refused,
        };
        for (std::size_t i = 0; i < now.size(); ++i) {
            check(now.at(i) >= last_.at(i), "session statistics are monotonic");
        }
        last_ = now;
    }

private:
    std::array<std::uint64_t, 11> last_{};
};

/// A finished message is a well-formed CIGI message.
inline void check_finished(std::span<const std::byte> message) {
    if (message.empty()) {
        return;
    }
    const auto view = MessageView::parse(message);
    check(view.has_value() && view->validate_all().has_value(), "finish_frame output is valid");
}

template <class P> std::array<std::byte, P::wire_size> encoded_array(const P& p) {
    std::array<std::byte, P::wire_size> out{};
    encode(p, out);
    return out;
}

} // namespace detail_sessions

inline void exercise_sessions(std::span<const std::byte> data) {
    if (data.size() < 2) {
        return;
    }
    const auto options = std::to_integer<unsigned>(data[0]);
    const auto mode = static_cast<IgMode>(std::to_integer<unsigned>(data[1]) & 3U);
    const auto datagram = data.subspan(2);

    SessionConfig config;
    config.synchronous = (options & 1U) != 0;
    config.enforce_startup_sequence = (options & 2U) != 0;
    config.validate_outgoing = (options & 4U) != 0;
    config.max_message_bytes = 2048;
    config.clock = [] { return Timestamp{12345}; };
    const bool accept_modes = (options & 8U) != 0;
    const unsigned receives = (options & 16U) != 0 ? 2U : 1U;
    std::size_t delivered = 0;

    {
        HostSession host(config);
        detail_sessions::StatsWatch watch;
        detail_sessions::register_handlers(host.handlers(), delivered);
        // Put the Host in the chosen IG mode first, with a database being loaded, so that the
        // database handshake and the operational paths are reachable.
        StartOfFrame sof;
        sof.ig_mode = mode;
        sof.database_number = -1;
        sof.ig_frame_number = 1;
        (void)host.receive(detail_sessions::encoded_array(sof));
        (void)host.request_mode(IgMode::Operate);
        (void)host.request_database(1);
        for (unsigned i = 0; i < receives; ++i) {
            (void)host.receive(datagram);
            watch.update(host.stats());
        }
        if (host.ready_to_send() && host.begin_frame()) {
            (void)host.add(EntityControl{});
            (void)host.add(HatHotRequest{});
            (void)host.add(ViewControl{});
            detail_sessions::check_finished(host.finish_frame());
        }
        watch.update(host.stats());
        host.reset_peer();
        check(!host.last_start_of_frame() && !host.peer() && !host.operational() &&
                  host.ig_mode() == IgMode::ResetStandby && host.ig_database() == 0 &&
                  host.last_ig_frame_number() == 0,
              "reset_peer restores the no-peer state");
        watch.update(host.stats());
        (void)host.receive(datagram);
        watch.update(host.stats());
    }

    {
        IgSession ig(config);
        detail_sessions::StatsWatch watch;
        detail_sessions::register_handlers(ig.handlers(), delivered);
        ig.on_mode_request([accept_modes](IgMode) { return accept_modes; });
        ig.on_database_request([&delivered](std::int8_t n) {
            delivered += static_cast<std::size_t>(static_cast<std::uint8_t>(n));
        });
        ig.set_mode(mode);
        IgControl control;
        control.ig_mode = mode == IgMode::OfflineMaintenance ? IgMode::Operate : mode;
        control.host_frame_number = 1;
        (void)ig.receive(detail_sessions::encoded_array(control));
        for (unsigned i = 0; i < receives; ++i) {
            (void)ig.receive(datagram);
            watch.update(ig.stats());
        }
        if (ig.begin_frame()) {
            (void)ig.add(HatHotResponse{});
            (void)ig.add(HatHotExtendedResponse{});
            detail_sessions::check_finished(ig.finish_frame());
        }
        watch.update(ig.stats());
        ig.reset();
        check(!ig.last_ig_control() && !ig.peer() && ig.mode() == IgMode::ResetStandby &&
                  ig.database() == 0 && ig.last_host_frame_number() == 0 &&
                  ig.ig_frame_number() == config.initial_frame_number - 1U,
              "reset restores the start-up state");
        watch.update(ig.stats());
        (void)ig.receive(datagram);
        watch.update(ig.stats());
    }
    // `delivered` only exists so the handlers have an observable effect.
    static_cast<void>(delivered);
}

} // namespace cigi::fuzz
