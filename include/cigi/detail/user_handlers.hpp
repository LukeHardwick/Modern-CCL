#pragma once

// The typed user-packet handlers of a Host or Ig facade (not installed API; see Host::on_user).
// A fixed table indexed by opcode - 201; the facade's internal on_unknown handler decodes the
// packet as the registered type and calls the user's handler, or falls back to the user's own
// on_unknown. Dispatching never allocates (registration may).

#include "cigi/error.hpp"
#include "cigi/message/view.hpp"
#include "cigi/packet_set.hpp"
#include "cigi/user_packet.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <utility>

namespace cigi::detail {

class UserHandlers {
public:
    /// Decodes the packet and calls the user's handler; the error is a decode failure.
    using Slot = std::function<Result<void>(const UnknownPacket&)>;

    static constexpr std::size_t slot_count = user_opcode_max - user_opcode_min + 1U;

    /// Replaces the handler of `opcode` (201-255; anything else is ignored).
    void set(std::uint8_t opcode, Slot slot) {
        if (is_user_opcode(opcode)) {
            slots_.at(opcode - user_opcode_min) = std::move(slot);
        }
    }
    void set_fallback(std::function<void(const UnknownPacket&)> f) { fallback_ = std::move(f); }

    /// The session's on_unknown handler body. `report` receives decode errors.
    template <class Report> void dispatch(const UnknownPacket& packet, Report&& report) const {
        if (is_user_opcode(packet.opcode)) {
            const Slot& slot = slots_.at(packet.opcode - user_opcode_min);
            if (slot) {
                if (const auto result = slot(packet); !result) {
                    report(result.error());
                }
                return;
            }
        }
        if (fallback_) {
            fallback_(packet);
        }
    }

private:
    std::array<Slot, slot_count> slots_;
    std::function<void(const UnknownPacket&)> fallback_;
};

/// Wrap a typed handler into a Slot.
template <class P, class F> UserHandlers::Slot make_user_slot(F&& handler) {
    return [h = std::forward<F>(handler)](const UnknownPacket& u) mutable -> Result<void> {
        const RawPacket raw{
            .opcode = u.opcode, .bytes = u.bytes, .offset = u.offset, .byte_order = u.byte_order};
        const auto decoded = raw.template decode_as<P>();
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        h(*decoded);
        return {};
    };
}

} // namespace cigi::detail
