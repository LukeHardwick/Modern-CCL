#include "cigi/version.hpp"

namespace cigi {

std::string_view get_library_version() noexcept {
    return library_version;
}

} // namespace cigi
