#pragma once

#include <cassert>

/// Debug-only precondition check; a no-op when NDEBUG is defined.
/// Usable in constant expressions (a failed check there is a compile error).
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CIGI_ASSERT(condition) assert(condition)
