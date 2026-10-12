#include "mock_ig/mock_ig.hpp"

#include <variant>

namespace cigi::mock {

MockIg::MockIg(const SceneConfig& config) : scene_(config) {
    scene_.register_handlers(dispatcher_, &hat_hot_);
}

Result<DispatchStats> MockIg::ingest(std::span<const std::byte> datagram) {
    const auto view = MessageView::parse(datagram);
    if (!view) {
        return std::unexpected(view.error());
    }
    return dispatcher_.dispatch(*view);
}

MockIg::Replies MockIg::end_frame(double dt_s) {
    scene_.advance(dt_s);
    Replies replies = hat_hot_.collect(scene_, ig_frame_);
    ++ig_frame_;
    return replies;
}

Result<void> MockIg::build_reply(MessageBuilder& builder, const Replies& replies) const {
    const bool operate = scene_.ig_control().ig_mode == IgMode::Operate;
    // The frame just ended carried number ig_frame_ - 1 (ig_frame_ counts ended frames).
    if (auto r = builder.add(StartOfFrame{
            .ig_mode = operate ? IgMode::Operate : IgMode::ResetStandby,
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
