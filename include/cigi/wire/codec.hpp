#pragma once

#include "cigi/detail/assert.hpp"
#include "cigi/wire/byte_order.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cigi::wire {

/// The scalar types that appear on the wire (ICD 2.7.1).
template <class T>
concept WireScalar = std::same_as<T, std::uint8_t> || std::same_as<T, std::int8_t> ||
                     std::same_as<T, std::uint16_t> || std::same_as<T, std::int16_t> ||
                     std::same_as<T, std::uint32_t> || std::same_as<T, std::int32_t> ||
                     std::same_as<T, std::uint64_t> || std::same_as<T, std::int64_t> ||
                     std::same_as<T, float> || std::same_as<T, double>;

/// True if `n` bytes starting at `offset` lie inside `bytes` (overflow-safe).
[[nodiscard]] constexpr bool fits(std::span<const std::byte> bytes, std::size_t offset,
                                  std::size_t n) noexcept {
    return offset <= bytes.size() && n <= bytes.size() - offset;
}

/// Read a T at `offset`. Precondition: fits(bytes, offset, sizeof(T)) (debug-asserted); packet
/// codecs validate the whole packet span once up front. Alignment is not required. Values are
/// byte-reversed when `order == Swapped`; floating-point values are reversed as raw bytes so
/// NaN payloads survive bit-exactly.
template <WireScalar T>
[[nodiscard]] constexpr T load(std::span<const std::byte> bytes, std::size_t offset,
                               ByteOrder order) noexcept {
    CIGI_ASSERT(fits(bytes, offset, sizeof(T)));
    std::array<std::byte, sizeof(T)> raw{};
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        raw[i] = bytes[offset + i];
    }
    if (order == ByteOrder::Swapped) {
        std::ranges::reverse(raw);
    }
    return std::bit_cast<T>(raw);
}

/// Write `value` at `offset` in the given byte order (default: host order, which is what a
/// CIGI sender always uses; `Swapped` exists to fabricate a foreign-endian peer in tests).
/// Precondition: fits(bytes, offset, sizeof(T)) (debug-asserted).
template <WireScalar T>
constexpr void store(std::span<std::byte> bytes, std::size_t offset, T value,
                     ByteOrder order = ByteOrder::Native) noexcept {
    CIGI_ASSERT(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset);
    auto raw = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    if (order == ByteOrder::Swapped) {
        std::ranges::reverse(raw);
    }
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bytes[offset + i] = raw[i];
    }
}

} // namespace cigi::wire
