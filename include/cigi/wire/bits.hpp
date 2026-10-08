#pragma once

#include <cstdint>

namespace cigi::wire {

// Bit fields never cross a byte boundary; bit 0 is the least significant bit (ICD 2.7.3).

namespace detail {
[[nodiscard]] constexpr std::uint8_t mask(unsigned width) noexcept {
    return static_cast<std::uint8_t>((1U << width) - 1U);
}
} // namespace detail

/// Extract `width` bits starting at bit `lsb`. Precondition: width >= 1, lsb + width <= 8.
[[nodiscard]] constexpr std::uint8_t get_bits(std::uint8_t byte, unsigned lsb,
                                              unsigned width) noexcept {
    return static_cast<std::uint8_t>((byte >> lsb) & detail::mask(width));
}

/// Replace `width` bits at `lsb` with `value` (masked to width); other bits are preserved.
[[nodiscard]] constexpr std::uint8_t set_bits(std::uint8_t byte, unsigned lsb, unsigned width,
                                              std::uint8_t value) noexcept {
    const auto field_mask = static_cast<std::uint8_t>(detail::mask(width) << lsb);
    const auto shifted = static_cast<std::uint8_t>((value << lsb) & field_mask);
    return static_cast<std::uint8_t>((byte & ~field_mask) | shifted);
}

/// Compile-time described bit field; cannot be declared across a byte boundary.
template <unsigned Lsb, unsigned Width> struct BitField {
    static_assert(Width >= 1 && Lsb + Width <= 8, "a CIGI bit field must lie within one byte");

    [[nodiscard]] static constexpr std::uint8_t get(std::uint8_t byte) noexcept {
        return get_bits(byte, Lsb, Width);
    }
    [[nodiscard]] static constexpr std::uint8_t set(std::uint8_t byte,
                                                    std::uint8_t value) noexcept {
        return set_bits(byte, Lsb, Width, value);
    }
};

/// Single-bit read.
[[nodiscard]] constexpr bool get_flag(std::uint8_t byte, unsigned bit) noexcept {
    return get_bits(byte, bit, 1) != 0;
}

/// Single-bit write.
[[nodiscard]] constexpr std::uint8_t set_flag(std::uint8_t byte, unsigned bit,
                                              bool value) noexcept {
    return set_bits(byte, bit, 1, value ? 1 : 0);
}

} // namespace cigi::wire
