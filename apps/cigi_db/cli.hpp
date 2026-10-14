#pragma once

// The cigi_db command line (a minimal hand-written parser: two commands, one option).
//
//   cigi_db validate <root-or-manifest> [--json FILE]
//   cigi_db list <root>
//
// Exit codes: 0 fine, 1 the databases have errors (or cannot be read), 2 bad usage.

#include <iosfwd>
#include <span>
#include <string_view>

namespace cigi::db {

[[nodiscard]] int run(std::span<const std::string_view> args, std::ostream& out, std::ostream& err);

[[nodiscard]] std::string_view usage() noexcept;

} // namespace cigi::db
