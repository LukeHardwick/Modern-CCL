#include "demo_common/support.hpp"

#include "mock_ig/describe.hpp"

#include <chrono>
#include <cigi/message/view.hpp>
#include <cigi/packet_set.hpp>
#include <csignal>
#include <format>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace cigi::demo {

namespace {

std::atomic<int> g_signalled{0}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static_assert(std::atomic<int>::is_always_lock_free);

spdlog::level::level_enum to_spdlog(std::string_view level) {
    if (level == "trace") {
        return spdlog::level::trace;
    }
    if (level == "debug") {
        return spdlog::level::debug;
    }
    if (level == "warn") {
        return spdlog::level::warn;
    }
    if (level == "error") {
        return spdlog::level::err;
    }
    if (level == "off") {
        return spdlog::level::off;
    }
    return spdlog::level::info;
}

template <class Set> void describe_items(const MessageView& view, std::vector<std::string>& lines) {
    for (const auto& item : view) {
        if (!item) {
            lines.push_back(std::format("(malformed packet: {})", to_string(item.error().code)));
            return;
        }
        const auto decoded = decode_any<Set>(*item, view.direction());
        lines.push_back(
            decoded ? mock::describe_any(*decoded)
                    : std::format("opcode {}: {}", item->opcode, to_string(decoded.error().code)));
    }
}

} // namespace

std::shared_ptr<spdlog::logger> make_logger(const std::string& name, std::string_view level) {
    const auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>(name, sink);
    logger->set_level(to_spdlog(level));
    logger->flush_on(spdlog::level::trace);
    logger->set_pattern("%H:%M:%S.%e [%n] %^%l%$ %v");
    return logger;
}

std::vector<std::string> describe_datagram(std::span<const std::byte> datagram) {
    std::vector<std::string> lines;
    const auto view = MessageView::parse(datagram);
    if (!view) {
        lines.push_back(std::format("(not a CIGI message: {})", to_string(view.error().code)));
        return lines;
    }
    if (view->direction() == Direction::HostToIg) {
        describe_items<HostToIgPackets>(*view, lines);
    } else {
        describe_items<IgToHostPackets>(*view, lines);
    }
    return lines;
}

RunControl::RunControl() {
    g_signalled.store(0);
    static_cast<void>(std::signal(SIGINT, [](int) { g_signalled.store(1); }));
    static_cast<void>(std::signal(SIGTERM, [](int) { g_signalled.store(1); }));
    watcher_ = std::jthread([this](const std::stop_token& st) {
        while (!st.stop_requested() && g_signalled.load() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        source_.request_stop();
    });
}

RunControl::~RunControl() {
    watcher_.request_stop();
}

bool RunControl::interrupted() noexcept {
    return g_signalled.load() != 0;
}

} // namespace cigi::demo
