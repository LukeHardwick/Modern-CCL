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

#include "cigi/packets.hpp"
#include "cigi/session/host_session.hpp"
#include "cigi/session/ig_session.hpp"

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
        }
        if (host.ready_to_send() && host.begin_frame()) {
            (void)host.add(EntityControl{});
            (void)host.add(HatHotRequest{});
            (void)host.add(ViewControl{});
            (void)host.finish_frame();
        }
        (void)host.stats();
        host.reset_peer();
        (void)host.receive(datagram);
    }

    {
        IgSession ig(config);
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
        }
        if (ig.begin_frame()) {
            (void)ig.add(HatHotResponse{});
            (void)ig.add(HatHotExtendedResponse{});
            (void)ig.finish_frame();
        }
        (void)ig.stats();
        ig.reset();
        (void)ig.receive(datagram);
    }
    // `delivered` only exists so the handlers have an observable effect.
    static_cast<void>(delivered);
}

} // namespace cigi::fuzz
