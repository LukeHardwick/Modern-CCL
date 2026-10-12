// libFuzzer target 3: arbitrary datagrams into HostSession::receive and IgSession::receive.

#include "session_harness.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    cigi::fuzz::exercise_sessions(std::as_bytes(std::span{data, size}));
    return 0;
}
