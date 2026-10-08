#pragma once

#include "cigi/error.hpp"

#include <bit>
#include <cstdint>
#include <limits>

namespace cigi::wire {

static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
              "CIGI requires IEEE-754 single and double precision reals (ICD 2.7.1)");
static_assert(std::endian::native == std::endian::little || std::endian::native == std::endian::big,
              "mixed-endian hosts are not supported");

/// How a reader must decode multi-byte values relative to the host. CIGI 3 is
/// receiver-makes-right: a sender always writes its own native order (ICD 2.5).
enum class ByteOrder : std::uint8_t {
    Native,  ///< Peer's byte order equals the host's.
    Swapped, ///< Peer's byte order differs; reverse every multi-byte value.
};

/// Value of the Byte Swap Magic Number as written by the sender (ICD 2.5).
inline constexpr std::uint16_t byte_swap_magic = 0x8000;

/// Decide the byte order from the magic number read with the host's native order.
[[nodiscard]] constexpr Result<ByteOrder>
detect_byte_order(std::uint16_t magic_as_read_natively) noexcept {
    if (magic_as_read_natively == byte_swap_magic) {
        return ByteOrder::Native;
    }
    if (magic_as_read_natively == std::byteswap(byte_swap_magic)) {
        return ByteOrder::Swapped;
    }
    return std::unexpected(Error{ErrorCode::InvalidByteSwapMagic});
}

} // namespace cigi::wire
