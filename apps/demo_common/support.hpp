#pragma once

// Small helpers shared by demo_ig and demo_host: logger, Ctrl-C, datagram descriptions.

#include <atomic>
#include <cstddef>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace spdlog {
class logger;
}

namespace cigi::demo {

/// A console logger (stdout, colour when a terminal) at `level` ("trace".."off"). Flushes every
/// line so the output of two processes started together stays readable.
[[nodiscard]] std::shared_ptr<spdlog::logger> make_logger(const std::string& name,
                                                          std::string_view level);

/// One line per packet of a datagram ("IgControl ...", "EntityControl ..."), decoded for its
/// direction. A datagram that is not a CIGI message gives one line saying why.
[[nodiscard]] std::vector<std::string> describe_datagram(std::span<const std::byte> datagram);

/// A stop token that fires on SIGINT/SIGTERM. Only one at a time.
class RunControl {
public:
    RunControl();
    RunControl(const RunControl&) = delete;
    RunControl& operator=(const RunControl&) = delete;
    ~RunControl();

    [[nodiscard]] std::stop_token token() const { return source_.get_token(); }
    [[nodiscard]] static bool interrupted() noexcept;

private:
    std::stop_source source_;
    std::jthread watcher_;
};

} // namespace cigi::demo
