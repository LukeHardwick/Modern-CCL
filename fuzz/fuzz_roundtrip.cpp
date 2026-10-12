// libFuzzer target 2: every packet that decodes must survive encode -> decode unchanged.

#include "harness.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    cigi::fuzz::exercise_roundtrip(std::as_bytes(std::span{data, size}));
    return 0;
}
