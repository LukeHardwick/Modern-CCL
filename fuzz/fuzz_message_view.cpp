// libFuzzer target 1: parse -> validate_all -> iterate -> decode every known packet -> validate().

#include "harness.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    cigi::fuzz::exercise_message_view(std::as_bytes(std::span{data, size}));
    return 0;
}
