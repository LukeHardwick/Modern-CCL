#include "app.hpp"

#include <array>
#include <cigi/message/view.hpp>

namespace cigi::viewer {

void step_simulation(AppState& app) {
    const auto message = app.host.next_message();
    if (!message) {
        app.last_error =
            std::string("scripted host: ") + std::string(to_string(message.error().code));
        return;
    }
    FrameRecord record;
    record.host_frame = app.host.frame() - 1;
    record.host_message.assign(message->begin(), message->end());

    const auto stats = app.ig.ingest(*message);
    if (stats) {
        app.total_packets += stats->handled + stats->ignored + stats->unknown;
    } else {
        app.last_error = std::string("ingest: ") + std::string(to_string(stats.error().code));
    }
    app.total_bytes += message->size();

    const auto replies = app.ig.end_frame(AppState::dt);
    app.reply_packets += replies.size() + 1;
    std::array<std::byte, 8192> storage{};
    MessageBuilder builder(Direction::IgToHost, storage);
    if (app.ig.build_reply(builder, replies)) {
        record.ig_message.assign(builder.bytes().begin(), builder.bytes().end());
    } else {
        app.last_error = "reply: IG message does not fit its buffer";
    }

    app.history.push_back(std::move(record));
    while (app.history.size() > AppState::history_max) {
        app.history.pop_front();
    }
    app.ui.inspector_cache_valid = false;
}

} // namespace cigi::viewer
