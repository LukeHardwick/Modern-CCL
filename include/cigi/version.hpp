#pragma once

#include <string_view>

namespace cigi {

/// CIGI protocol version implemented for sending.
inline constexpr int protocol_version_major = 3;
inline constexpr int protocol_version_minor = 3;

/// Library version string.
inline constexpr std::string_view library_version = "0.1.0";

/// Returns the library version string (out-of-line so the static library has an object).
[[nodiscard]] std::string_view get_library_version() noexcept;

} // namespace cigi
