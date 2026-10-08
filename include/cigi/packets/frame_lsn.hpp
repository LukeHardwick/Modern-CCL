#pragma once

// Host Frame Number LSN (ICD 2.3, 4.2.2, 4.2.3): the least significant nybble of the Host Frame
// Number of the last IG Control packet the IG received. HAT/HOT responses carry it so a Host can
// match a periodic response to the frame in which it asked.

#include <cstdint>

namespace cigi {

/// The low 4 bits of `host_frame`, as carried in the Host Frame Number LSN fields.
[[nodiscard]] constexpr std::uint8_t host_frame_lsn(std::uint32_t host_frame) noexcept {
    return static_cast<std::uint8_t>(host_frame & 0x0FU);
}

} // namespace cigi
