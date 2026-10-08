#pragma once

#include <cstddef>

namespace cigi::wire {

/// Packets start and end on 64-bit boundaries (ICD 2.6), so sizes are multiples of 8.
/// The Packet Size field is a u8, so the largest representable multiple of 8 is 248.
inline constexpr std::size_t max_packet_size = 248;

/// Round `n` up to the next multiple of 8.
[[nodiscard]] constexpr std::size_t pad_to_8(std::size_t n) noexcept {
    return (n + 7U) & ~static_cast<std::size_t>(7U);
}

/// A legal on-the-wire Packet Size: 8 <= n <= 248 and a multiple of 8 (ICD 2.6).
[[nodiscard]] constexpr bool is_valid_packet_size(std::size_t n) noexcept {
    return n >= 8 && n % 8 == 0 && n <= max_packet_size;
}

} // namespace cigi::wire
