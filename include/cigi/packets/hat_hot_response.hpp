#pragma once

// HAT/HOT Response packet (ICD 4.2.2), opcode 102, IG -> Host, 16 bytes.
//
//   bytes 2-3 HAT/HOT ID
//   byte 4 bits 7-4 Host Frame Number LSN, 3-2 reserved, 1 response type, 0 valid
//   bytes 5-7 reserved   bytes 8-15 height
//
// CIGI 3.0/3.1 reserved the LSN nybble (added in 3.2, ICD Appendix B). A conforming 3.0/3.1 peer
// writes 0 there; decode itself is version-agnostic and passes any value through, so sessions
// (T07) mask the field by the peer's minor version. Pure table-driven packet.

#include "cigi/packet.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>

namespace cigi {

/// Which height the packet carries (ICD 4.2.2 Response Type).
enum class HatHotResponseType : std::uint8_t {
    Hat = 0, ///< Height Above Terrain.
    Hot = 1, ///< Height Of Terrain.
};

template <> struct EnumTraits<HatHotResponseType> {
    [[nodiscard]] static constexpr bool is_defined(HatHotResponseType t) noexcept {
        return enum_between(t, HatHotResponseType::Hat, HatHotResponseType::Hot);
    }
};

struct HatHotResponse {
    static constexpr std::uint8_t opcode = 102;
    static constexpr Direction direction = Direction::IgToHost;
    static constexpr std::size_t wire_size = 16;

    std::uint16_t hat_hot_id = 0; ///< HAT/HOT ID of the request being answered.
    bool valid = false; ///< False: the test point was outside the database; `height` is unset.
    HatHotResponseType response_type = HatHotResponseType::Hat;
    std::uint8_t host_frame_lsn = 0; ///< 0..15, see host_frame_lsn(); 3.2+, ignore if one-shot.
    double height = 0;               ///< Metres.

    static constexpr auto fields = std::tuple{
        wire::scalar(&HatHotResponse::hat_hot_id, 2),
        wire::flag(&HatHotResponse::valid, 4, 0),
        wire::bits(&HatHotResponse::response_type, 4, 1, 1),
        wire::bits(&HatHotResponse::host_frame_lsn, 4, 4, 4).range(0, 15),
        wire::scalar(&HatHotResponse::height, 8),
    };

    friend constexpr bool operator==(const HatHotResponse&,
                                     const HatHotResponse&) noexcept = default;
};
static_assert(wire::layout_ok<HatHotResponse>());

} // namespace cigi
