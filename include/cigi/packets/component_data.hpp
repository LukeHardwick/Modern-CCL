#pragma once

// Typed access to the six 32-bit user-defined words of a Component Control packet (ICD 4.1.4,
// Figs. 44-46). The packet is byte-swapped word by word, so anything narrower or wider than 32 bits
// must be composed arithmetically from whole words; this class does exactly that, and every
// accessor works on host-order values, so it is independent of the byte order on the wire.
//
// Layout conventions (documented here because the ICD only shows them by example):
//   * 16- and 8-bit values share a word like the ICD's `i * 2^16 + j`: the FIRST value is the
//     most significant (u16 slot 0 = bits 31-16; u8 slot 0 = bits 31-24, slot 3 = bits 7-0).
//   * A 64-bit value occupies words i and i + 1 with the LEAST significant 32 bits in word i and
//     the most significant in word i + 1. This is what the ICD's worked example puts on the wire
//     (Fig. 46: m = 100.0 = 0x4059000000000000 gives word 3 = 0, word 4 = 0x40590000), although
//     its labels "m (MSW)" / "m (LSW)" and prose call the first word the MSW. See
//     docs/interop/discrepancies.md.
//   * Floats travel as their IEEE-754 bit pattern in one word (doubles in two).

#include "cigi/detail/assert.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace cigi {

class ComponentData {
public:
    static constexpr std::size_t word_count = 6;

    constexpr ComponentData() noexcept = default;

    /// Raw words (host order), index 0 = Component Data 1.
    [[nodiscard]] constexpr const std::array<std::uint32_t, word_count>& words() const noexcept {
        return words_;
    }
    constexpr void set_words(const std::array<std::uint32_t, word_count>& w) noexcept {
        words_ = w;
    }

    // All `word` indices are 0-based and must be < word_count (64-bit values: < word_count - 1);
    // violations are debug-asserted.

    constexpr void set_u32(std::size_t word, std::uint32_t v) noexcept {
        CIGI_ASSERT(word < word_count);
        at(word) = v;
    }
    [[nodiscard]] constexpr std::uint32_t u32(std::size_t word) const noexcept {
        CIGI_ASSERT(word < word_count);
        return at(word);
    }

    constexpr void set_i32(std::size_t word, std::int32_t v) noexcept {
        set_u32(word, std::bit_cast<std::uint32_t>(v));
    }
    [[nodiscard]] constexpr std::int32_t i32(std::size_t word) const noexcept {
        return std::bit_cast<std::int32_t>(u32(word));
    }

    constexpr void set_f32(std::size_t word, float v) noexcept {
        set_u32(word, std::bit_cast<std::uint32_t>(v));
    }
    [[nodiscard]] constexpr float f32(std::size_t word) const noexcept {
        return std::bit_cast<float>(u32(word));
    }

    /// Words `word` (low 32 bits) and `word + 1` (high 32 bits).
    constexpr void set_u64(std::size_t word, std::uint64_t v) noexcept {
        CIGI_ASSERT(word + 1 < word_count);
        at(word) = static_cast<std::uint32_t>(v);
        at(word + 1) = static_cast<std::uint32_t>(v >> 32U);
    }
    [[nodiscard]] constexpr std::uint64_t u64(std::size_t word) const noexcept {
        CIGI_ASSERT(word + 1 < word_count);
        return (static_cast<std::uint64_t>(at(word + 1)) << 32U) | at(word);
    }

    constexpr void set_i64(std::size_t word, std::int64_t v) noexcept {
        set_u64(word, std::bit_cast<std::uint64_t>(v));
    }
    [[nodiscard]] constexpr std::int64_t i64(std::size_t word) const noexcept {
        return std::bit_cast<std::int64_t>(u64(word));
    }

    constexpr void set_f64(std::size_t word, double v) noexcept {
        set_u64(word, std::bit_cast<std::uint64_t>(v));
    }
    [[nodiscard]] constexpr double f64(std::size_t word) const noexcept {
        return std::bit_cast<double>(u64(word));
    }

    /// Two 16-bit values in one word; `first` is the most significant half.
    constexpr void set_u16x2(std::size_t word, std::uint16_t first, std::uint16_t second) noexcept {
        set_u32(word, (static_cast<std::uint32_t>(first) << 16U) | second);
    }
    /// Slot 0 (most significant) or 1.
    [[nodiscard]] constexpr std::uint16_t u16(std::size_t word, std::size_t slot) const noexcept {
        CIGI_ASSERT(slot < 2);
        return static_cast<std::uint16_t>(u32(word) >> (16U * (1U - slot)));
    }

    /// Four 8-bit values in one word; `b0` is the most significant byte.
    // NOLINTBEGIN(bugprone-easily-swappable-parameters)
    constexpr void set_u8x4(std::size_t word, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2,
                            std::uint8_t b3) noexcept {
        set_u32(word, (static_cast<std::uint32_t>(b0) << 24U) |
                          (static_cast<std::uint32_t>(b1) << 16U) |
                          (static_cast<std::uint32_t>(b2) << 8U) | b3);
    }
    // NOLINTEND(bugprone-easily-swappable-parameters)
    /// Slot 0 (most significant) to 3.
    [[nodiscard]] constexpr std::uint8_t u8(std::size_t word, std::size_t slot) const noexcept {
        CIGI_ASSERT(slot < 4);
        return static_cast<std::uint8_t>(u32(word) >> (8U * (3U - slot)));
    }

    friend constexpr bool operator==(const ComponentData&, const ComponentData&) noexcept = default;

private:
    // The single place that indexes the array; callers assert their bounds beforehand.
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
    //             cppcoreguidelines-pro-bounds-constant-array-index)
    [[nodiscard]] constexpr std::uint32_t& at(std::size_t i) noexcept { return words_[i]; }
    [[nodiscard]] constexpr const std::uint32_t& at(std::size_t i) const noexcept {
        return words_[i];
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
    //           cppcoreguidelines-pro-bounds-constant-array-index)

    std::array<std::uint32_t, word_count> words_{};
};

} // namespace cigi
