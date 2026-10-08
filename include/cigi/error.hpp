#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

namespace cigi {

/// Reasons an encode or decode operation can fail. Extensible: append new values only.
enum class ErrorCode : std::uint8_t {
    BufferTooSmall,    ///< Output buffer cannot hold the data to be written.
    Truncated,         ///< Input is shorter than required.
    InvalidPacketSize, ///< Size is 0, not a multiple of 8, or below the packet minimum (ICD 2.6).
    InvalidByteSwapMagic,  ///< Byte Swap Magic Number is neither 0x8000 nor 0x0080 (ICD 2.5).
    InvalidFieldValue,     ///< A field holds a value outside its legal range.
    UnexpectedFirstPacket, ///< Message does not start with IG Control / Start of Frame (ICD 2.6).
    UnsupportedVersion,    ///< Peer CIGI version is not supported.
    UnknownOpcode,         ///< Opcode is not recognised.
    WrongDirection,        ///< Packet cannot travel in this message's direction (ICD 2.3).
    DuplicateFramePacket,  ///< A second IG Control / Start of Frame in one message (ICD 2.6).
    OpcodeMismatch,        ///< Packet's opcode byte differs from the type asked for; Error::opcode
                           ///< holds the opcode found in the data.
    TransportError,        ///< A socket operation failed; Error::os_error holds the OS code.
    Timeout,               ///< No datagram arrived within the allowed time.
};

/// Error details.
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init): `code` is always given (aggregate).
struct Error {
    ErrorCode code;          ///< What went wrong.
    std::size_t offset = 0;  ///< Byte offset into the message where it was detected.
    std::uint8_t opcode = 0; ///< Packet opcode if known, otherwise 0.
    int os_error = 0;        ///< Operating-system error number (transport errors), otherwise 0.

    friend constexpr bool operator==(const Error&, const Error&) noexcept = default;
};

/// Result of a fallible operation.
template <class T> using Result = std::expected<T, Error>;

/// Short, stable, human-readable name of an error code.
[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

} // namespace cigi
