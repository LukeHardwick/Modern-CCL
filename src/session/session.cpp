#include "cigi/session/detail.hpp"

#include <chrono>
#include <spdlog/logger.h>
#include <spdlog/sinks/null_sink.h>

namespace cigi {

Timestamp steady_timestamp() noexcept {
    using Ticks = std::chrono::duration<std::int64_t, Timestamp::period>;
    const auto ticks =
        std::chrono::duration_cast<Ticks>(std::chrono::steady_clock::now().time_since_epoch());
    return Timestamp{static_cast<std::uint32_t>(static_cast<std::uint64_t>(ticks.count()))};
}

namespace detail {

std::shared_ptr<spdlog::logger> resolve_logger(const SessionConfig& config) {
    if (config.logger) {
        return config.logger;
    }
    auto silent = std::make_shared<spdlog::logger>("cigi-null",
                                                   std::make_shared<spdlog::sinks::null_sink_st>());
    silent->set_level(spdlog::level::off);
    return silent;
}

} // namespace detail
} // namespace cigi
