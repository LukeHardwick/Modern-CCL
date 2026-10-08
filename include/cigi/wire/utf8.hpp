#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cigi::wire {

namespace detail {
[[nodiscard]] constexpr bool is_continuation(char c) noexcept {
    return (static_cast<std::uint8_t>(c) & 0xC0U) == 0x80U;
}
} // namespace detail

/// Length of the longest prefix of `text` that is at most `max_bytes` long and does not end in
/// the middle of a UTF-8 code point (ICD 2.7.2). Malformed input is handled by backing off over
/// continuation bytes only; it never reads out of range and always terminates.
[[nodiscard]] constexpr std::size_t utf8_truncate(std::string_view text,
                                                  std::size_t max_bytes) noexcept {
    if (max_bytes >= text.size()) {
        return text.size();
    }
    std::size_t n = max_bytes;
    // text[n] is the first byte that would be cut off; if it continues a code point, the code
    // point straddles the cut, so drop it entirely.
    while (n > 0 && detail::is_continuation(text[n])) {
        --n;
    }
    return n;
}

/// Strict UTF-8 validation: rejects overlong forms, surrogates (U+D800..U+DFFF), code points
/// above U+10FFFF and truncated sequences. Diagnostic use only; receivers must tolerate any
/// bytes (ICD 2.7.2).
[[nodiscard]] constexpr bool is_valid_utf8(std::string_view text) noexcept {
    std::size_t i = 0;
    const std::size_t size = text.size();
    while (i < size) {
        const auto lead = static_cast<std::uint8_t>(text[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        std::size_t extra = 0;
        std::uint8_t lo = 0x80; // allowed range of the first continuation byte
        std::uint8_t hi = 0xBF;
        if (lead >= 0xC2 && lead <= 0xDF) {
            extra = 1;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            extra = 2;
            if (lead == 0xE0) {
                lo = 0xA0; // no overlongs
            } else if (lead == 0xED) {
                hi = 0x9F; // no surrogates
            }
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            extra = 3;
            if (lead == 0xF0) {
                lo = 0x90; // no overlongs
            } else if (lead == 0xF4) {
                hi = 0x8F; // <= U+10FFFF
            }
        } else {
            return false; // stray continuation, C0, C1, F5..FF
        }
        if (size - i <= extra) {
            return false; // truncated
        }
        const auto first = static_cast<std::uint8_t>(text[i + 1]);
        if (first < lo || first > hi) {
            return false;
        }
        for (std::size_t k = 2; k <= extra; ++k) {
            if (!detail::is_continuation(text[i + k])) {
                return false;
            }
        }
        i += extra + 1;
    }
    return true;
}

} // namespace cigi::wire
