#include "mock_ig/mock_ig.hpp"

#include <variant>

namespace cigi::mock {

MockIg::MockIg(const SceneConfig& config) : scene_(config) {
    scene_.register_all_handlers(dispatcher_, &hat_hot_);
    scene_.set_ig_control_hook([this](const IgControl& p) {
        selector_.on_ig_control(p.ig_mode, p.database_number);
        sync_database();
    });
}

void MockIg::use_catalog(const DatabaseCatalog* catalog, int initial_number) {
    selector_ = DatabaseSelector(catalog, initial_number,
                                 [this](std::string text) { scene_.add_log(std::move(text)); });
    sync_database();
}

bool MockIg::refresh_database() {
    const bool changed = selector_.refresh();
    sync_database();
    return changed;
}

void MockIg::sync_database() {
    // A catalog-less selector stays on the scene's own database (global).
    if (selector_.reported() != 0 && selector_.current() != scene_.database_ptr()) {
        scene_.set_database(selector_.current());
    }
}

Result<DispatchStats> MockIg::ingest(std::span<const std::byte> datagram) {
    const auto view = MessageView::parse(datagram);
    if (!view) {
        return std::unexpected(view.error());
    }
    return dispatcher_.dispatch(*view);
}

MockIg::Replies MockIg::end_frame(double dt_s) {
    selector_.end_frame();
    sync_database();
    scene_.advance(dt_s);
    Replies replies = hat_hot_.collect(scene_, ig_frame_);
    ++ig_frame_;
    return replies;
}

Result<void> MockIg::build_reply(MessageBuilder& builder, const Replies& replies) const {
    // Operate and Debug are echoed; anything else (Reset/Standby, Offline Maintenance) reads as
    // Reset/Standby from the IG.
    const IgMode host_mode = scene_.ig_control().ig_mode;
    const IgMode mode = (host_mode == IgMode::Operate || host_mode == IgMode::Debug)
                            ? host_mode
                            : IgMode::ResetStandby;
    // The frame just ended carried number ig_frame_ - 1 (ig_frame_ counts ended frames).
    if (auto r = builder.add(StartOfFrame{
            .database_number = selector_.reported(),
            .ig_mode = mode,
            .ig_frame_number = ig_frame_ == 0 ? 0 : ig_frame_ - 1,
            .last_host_frame_number = scene_.host_frame_number(),
        });
        !r) {
        return r;
    }
    for (const auto& reply : replies) {
        const auto added = std::visit([&builder](const auto& p) { return builder.add(p); }, reply);
        if (!added) {
            return added;
        }
    }
    return {};
}

} // namespace cigi::mock
