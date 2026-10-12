#pragma once

// State of the Host's link to its IG, as seen by the cigi::Host facade, and the outcome of one
// poll() of either facade.

#include "cigi/wire/fields.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cigi {

/// Link state of a Host (derived from the IG Mode and Database Number of the received Start of
/// Frame messages, ICD 4.2.1, plus the peer timeout; see docs/design/04-sessions.md).
enum class LinkState : std::uint8_t {
    WaitingForIg,    ///< No Start of Frame yet (or the IG restarted and has not been heard since).
    Standby,         ///< The IG reports Reset/Standby (or Offline Maintenance).
    LoadingDatabase, ///< The IG is operating but reports a database being loaded (ICD 4.2.1).
    Operate,         ///< The IG reports Operate: mission data flows.
    Debug,           ///< The IG reports Debug: mission data flows.
    Lost,            ///< The IG was heard before, but nothing has arrived for `peer_timeout`.
    Closed,          ///< close() was called.
};

inline constexpr std::size_t link_state_count = 7;

template <> struct EnumTraits<LinkState> {
    [[nodiscard]] static constexpr bool is_defined(LinkState s) noexcept {
        return enum_between(s, LinkState::WaitingForIg, LinkState::Closed);
    }
};

/// Short, stable name of a link state ("?" for a value outside the enum).
[[nodiscard]] constexpr std::string_view to_string(LinkState s) noexcept {
    switch (s) {
    case LinkState::WaitingForIg:
        return "WaitingForIg";
    case LinkState::Standby:
        return "Standby";
    case LinkState::LoadingDatabase:
        return "LoadingDatabase";
    case LinkState::Operate:
        return "Operate";
    case LinkState::Debug:
        return "Debug";
    case LinkState::Lost:
        return "Lost";
    case LinkState::Closed:
        return "Closed";
    }
    return "?";
}

/// What one poll() did.
struct PollStatus {
    std::size_t datagrams_received = 0; ///< Datagrams taken from the transport (valid or not).
    bool frame_sent = false;            ///< A message (frame or start-up message) was sent.

    friend constexpr bool operator==(const PollStatus&, const PollStatus&) noexcept = default;
};

} // namespace cigi
