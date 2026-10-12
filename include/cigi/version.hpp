#pragma once

#include "cigi/packet.hpp"

#include <string_view>

namespace cigi {

/// CIGI protocol version implemented for sending (see cigi::protocol_version).
inline constexpr int protocol_version_major = protocol_version.major;
inline constexpr int protocol_version_minor = protocol_version.minor;

/// Library version string.
inline constexpr std::string_view library_version = "0.2.0";

/// Returns the library version string (out-of-line so the static library has an object).
[[nodiscard]] std::string_view get_library_version() noexcept;

} // namespace cigi
