#pragma once

// Internals shared by the two session types: frame-number tracking, version masking of packets
// (ICD 2.11) and delivery to the application's handlers.

#include "cigi/dispatch.hpp"
#include "cigi/packets.hpp"
#include "cigi/session/session.hpp"

#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>

namespace cigi::detail {

/// Gap detection on a peer's frame numbers (ICD 2.3). Numbers are unsigned 32-bit and wrap.
///
/// Classification of a number against the last accepted one (all arithmetic modulo 2^32):
///   - the same number                      -> `duplicate` (not accepted),
///   - 1..stale_window behind               -> `stale`, a reordered older message (not accepted),
///   - further behind                       -> `regressed`, the peer restarted (accepted),
///   - ahead by more than max_forward_jump  -> `regressed` too (a restart, not 2^31 lost frames),
///   - otherwise ahead                      -> accepted, the numbers skipped are `lost`.
/// A duplicate or stale number leaves the tracker untouched.
class FrameTracker {
public:
    struct Observation {
        std::uint32_t lost = 0; ///< Numbers skipped (messages lost).
        bool regressed = false; ///< Went far backwards or jumped far ahead: the peer restarted.
        bool duplicate = false; ///< Same number as the last accepted one.
        bool stale = false;     ///< A few numbers behind the last accepted one (reordered).

        /// True if the number was taken as the peer's newest.
        [[nodiscard]] constexpr bool accepted() const noexcept { return !duplicate && !stale; }
    };

    /// The largest forward jump counted as lost frames; anything further is taken as a restart.
    static constexpr std::uint32_t max_forward_jump = 1U << 16U;

    explicit FrameTracker(std::uint32_t stale_window = 8) noexcept : stale_window_(stale_window) {}

    /// Record `number` as the latest frame the peer sent. The first one is never a gap.
    [[nodiscard]] Observation observe(std::uint32_t number) noexcept {
        Observation o;
        if (seen_) {
            const std::uint32_t behind = last_ - number; // wraps modulo 2^32
            if (behind == 0) {
                o.duplicate = true;
                return o;
            }
            if (behind <= stale_window_) {
                o.stale = true;
                return o;
            }
            const std::uint32_t gap = number - (last_ + 1U);
            // Every step back further than the stale window has gap >= 2^31, so it lands here too.
            if (gap > max_forward_jump) {
                o.regressed = true;
            } else {
                o.lost = gap;
            }
        }
        seen_ = true;
        last_ = number;
        return o;
    }

    /// Remember `number` as the latest without judging it.
    void record(std::uint32_t number) noexcept {
        seen_ = true;
        last_ = number;
    }

    [[nodiscard]] bool seen() const noexcept { return seen_; }
    [[nodiscard]] std::uint32_t last() const noexcept { return last_; }
    void reset() noexcept {
        seen_ = false;
        last_ = 0;
    }

private:
    std::uint32_t stale_window_;
    bool seen_ = false;
    std::uint32_t last_ = 0;
};

[[nodiscard]] constexpr std::string_view mode_name(IgMode m) noexcept {
    switch (m) {
    case IgMode::ResetStandby:
        return "Reset/Standby";
    case IgMode::Operate:
        return "Operate";
    case IgMode::Debug:
        return "Debug";
    case IgMode::OfflineMaintenance:
        return "Offline Maintenance";
    }
    return "undefined";
}

[[nodiscard]] constexpr bool is_operational(IgMode m) noexcept {
    return m == IgMode::Operate || m == IgMode::Debug;
}

// ---- Version masking --------------------------------------------------------------------------
// Packet decode is version-agnostic. Fields a peer's version does not have are reserved there, so
// the session clears them before the application sees a packet (adapt_received) and before a
// packet is sent to that peer (adapt_outgoing). Sessions always send CIGI 3.3 frames; only the
// content is masked. Exactly what is masked (ICD Appendix B / the packet sections):
//   peer minor < 3: Entity Control extrapolation bit; Component Control classes 14 and 15
//                   (Symbol Surface, Symbol) cannot be sent (UnsupportedVersion).
//   peer minor < 2: HAT/HOT Request update period; HAT/HOT (Extended) Response host frame LSN
//                   (both directions); Rate Control coordinate-system bit.

template <class P> [[nodiscard]] Result<P> adapt_outgoing(const P& p, ProtocolVersion /*peer*/) {
    return p;
}

[[nodiscard]] inline Result<EntityControl> adapt_outgoing(const EntityControl& p,
                                                          ProtocolVersion peer) {
    EntityControl q = p;
    q.extrapolation_enabled = q.extrapolation_enabled && peer.minor >= 3;
    return q;
}

[[nodiscard]] inline Result<RateControl> adapt_outgoing(const RateControl& p,
                                                        ProtocolVersion peer) {
    RateControl q = p;
    if (peer.minor < 2) {
        q.coordinate_system = RateCoordinateSystem::WorldParent;
    }
    return q;
}

[[nodiscard]] inline Result<HatHotRequest> adapt_outgoing(const HatHotRequest& p,
                                                          ProtocolVersion peer) {
    HatHotRequest q = p;
    if (peer.minor < 2) {
        q.update_period = 0;
    }
    return q;
}

[[nodiscard]] inline Result<ComponentControl> adapt_outgoing(const ComponentControl& p,
                                                             ProtocolVersion peer) {
    if (peer.minor < 3 && (p.component_class == ComponentClass::SymbolSurface ||
                           p.component_class == ComponentClass::Symbol)) {
        return std::unexpected(Error{
            .code = ErrorCode::UnsupportedVersion,
            .offset = 6,
            .opcode = ComponentControl::opcode,
        });
    }
    return p;
}

[[nodiscard]] inline Result<HatHotResponse> adapt_outgoing(const HatHotResponse& p,
                                                           ProtocolVersion peer) {
    HatHotResponse q = p;
    if (peer.minor < 2) {
        q.host_frame_lsn = 0;
    }
    return q;
}

[[nodiscard]] inline Result<HatHotExtendedResponse> adapt_outgoing(const HatHotExtendedResponse& p,
                                                                   ProtocolVersion peer) {
    HatHotExtendedResponse q = p;
    if (peer.minor < 2) {
        q.host_frame_lsn = 0;
    }
    return q;
}

template <class P> [[nodiscard]] P adapt_received(const P& p, ProtocolVersion /*peer*/) {
    return p;
}

[[nodiscard]] inline EntityControl adapt_received(const EntityControl& p, ProtocolVersion peer) {
    EntityControl q = p;
    q.extrapolation_enabled = q.extrapolation_enabled && peer.minor >= 3;
    return q;
}

[[nodiscard]] inline RateControl adapt_received(const RateControl& p, ProtocolVersion peer) {
    RateControl q = p;
    if (peer.minor < 2) {
        q.coordinate_system = RateCoordinateSystem::WorldParent;
    }
    return q;
}

[[nodiscard]] inline HatHotRequest adapt_received(const HatHotRequest& p, ProtocolVersion peer) {
    HatHotRequest q = p;
    if (peer.minor < 2) {
        q.update_period = 0;
    }
    return q;
}

[[nodiscard]] inline HatHotResponse adapt_received(const HatHotResponse& p, ProtocolVersion peer) {
    HatHotResponse q = p;
    if (peer.minor < 2) {
        q.host_frame_lsn = 0;
    }
    return q;
}

[[nodiscard]] inline HatHotExtendedResponse adapt_received(const HatHotExtendedResponse& p,
                                                           ProtocolVersion peer) {
    HatHotExtendedResponse q = p;
    if (peer.minor < 2) {
        q.host_frame_lsn = 0;
    }
    return q;
}

/// Hand every packet of `view` to `handlers` after masking it for the peer's version, except the
/// session's own frame packet `Frame` (Start of Frame for a Host, IG Control for an IG): the
/// session consumes that one and applications read it through the session's accessors. Returns
/// what the walk did (the withheld frame packet counts as ignored).
template <class Frame, class Set>
DispatchStats deliver_message(const Dispatcher<Set>& handlers, const MessageView& view) {
    const ProtocolVersion peer = view.version();
    const auto visitor = [&](const auto& p) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(p)>, UnknownPacket>) {
            handlers.deliver(p);
        } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(p)>, Frame>) {
            // withheld: see above
        } else {
            handlers.deliver(adapt_received(p, peer));
        }
    };
    return dispatch<Set>(
        view, visitor, [&](const Error& e) { handlers.deliver(e); },
        handlers.ignoring_user_packets());
}

/// The logger to use: the configured one, or a silent one.
[[nodiscard]] std::shared_ptr<spdlog::logger> resolve_logger(const SessionConfig& config);

} // namespace cigi::detail
