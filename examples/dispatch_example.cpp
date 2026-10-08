// Handle an incoming IG -> Host message two ways: a compile-time visitor and a runtime Dispatcher.
#include "cigi/dispatch.hpp"
#include "cigi/message/builder.hpp"

#include <cstdlib>
#include <print>

// NOLINTNEXTLINE(bugprone-exception-escape): std::print may throw; an example may terminate.
int main() {
    // An IG would send this; here we build it ourselves.
    cigi::StaticMessageBuilder<128> builder(cigi::Direction::IgToHost);
    const bool built =
        builder.add(cigi::StartOfFrame{.ig_frame_number = 7}).has_value() &&
        builder.add(cigi::HatHotResponse{.hat_hot_id = 3, .valid = true, .height = 12.5})
            .has_value();
    if (!built) {
        return EXIT_FAILURE;
    }
    const auto view = cigi::MessageView::parse(builder.bytes());
    if (!view) {
        std::println("parse failed: {}", cigi::to_string(view.error().code));
        return EXIT_FAILURE;
    }

    // 1. Visitor: packets with no matching overload are skipped.
    const auto stats = cigi::dispatch<cigi::IgToHostPackets>(
        *view,
        cigi::overloaded{
            [](const cigi::StartOfFrame& s) { std::println("frame {}", s.ig_frame_number); },
            [](const cigi::HatHotResponse& r) { std::println("height {} m", r.height); },
            [](const cigi::UnknownPacket& u) { std::println("unknown opcode {}", u.opcode); },
        },
        [](const cigi::Error& e) { std::println("error: {}", cigi::to_string(e.code)); });
    std::println("handled {} ignored {} unknown {} errors {}", stats.handled, stats.ignored,
                 stats.unknown, stats.errors);

    // 2. Dispatcher: handlers registered at run time (allocates here, never while dispatching).
    cigi::Dispatcher<cigi::IgToHostPackets> handlers;
    handlers.on<cigi::StartOfFrame>(
        [](const cigi::StartOfFrame& s) { std::println("again: frame {}", s.ig_frame_number); });
    handlers.dispatch(*view);
    return EXIT_SUCCESS;
}
