#pragma once

// The pieces of a mock IG wired together: a SceneModel, a HatHotResponder and a Dispatcher.
// Shared by the headless demo IG and the viewer so both behave identically.
//
//   MockIg ig;
//   ig.ingest(datagram);              // parse + dispatch one Host message into the scene
//   auto replies = ig.end_frame(dt);  // advance the scene, collect due HAT/HOT answers
//   ig.build_reply(builder, replies); // StartOfFrame + the answers, ready to send

#include "mock_ig/database_selector.hpp"
#include "mock_ig/hat_hot.hpp"
#include "mock_ig/scene.hpp"

#include <cigi/dispatch.hpp>
#include <cigi/message/builder.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cigi::mock {

class MockIg {
public:
    using Replies = std::vector<HatHotResponder::Reply>;

    explicit MockIg(const SceneConfig& config = {});
    // The dispatcher's handlers point into this object.
    MockIg(const MockIg&) = delete;
    MockIg& operator=(const MockIg&) = delete;
    MockIg(MockIg&&) = delete;
    MockIg& operator=(MockIg&&) = delete;
    ~MockIg() = default;

    /// Serve databases from `catalog` (not owned; it must outlive this): `initial_number` is in
    /// force at once and the Database Number protocol (database_selector.hpp) answers the
    /// Host's requests in the Start of Frame. Without a catalog the IG has one global database
    /// (the one in the SceneConfig, else the built-in default) and reports Database Number 0.
    void use_catalog(const DatabaseCatalog* catalog, int initial_number = 1);
    /// After the catalog was hot-reloaded: switch to the newer version of the database in force.
    /// True if it changed.
    bool refresh_database();
    [[nodiscard]] const DatabaseSelector& database_selector() const noexcept { return selector_; }

    /// Parse one Host datagram and dispatch every packet into the scene. Errors are those of
    /// MessageView::parse (the datagram is not a CIGI message at all); problems inside a message
    /// are counted in the scene's stats and the returned DispatchStats.
    [[nodiscard]] Result<DispatchStats> ingest(std::span<const std::byte> datagram);

    /// End of one IG frame: advance the scene by `dt_s`, answer due HAT/HOT requests, count the
    /// frame. The answers are returned for build_reply().
    [[nodiscard]] Replies end_frame(double dt_s);

    /// Start of Frame (IG mode follows the Host's, last Host frame echoed) followed by `replies`,
    /// for the frame just ended. Errors as MessageBuilder::add.
    [[nodiscard]] Result<void> build_reply(MessageBuilder& builder, const Replies& replies) const;

    [[nodiscard]] SceneModel& scene() noexcept { return scene_; }
    [[nodiscard]] const SceneModel& scene() const noexcept { return scene_; }
    [[nodiscard]] HatHotResponder& hat_hot() noexcept { return hat_hot_; }
    [[nodiscard]] const HatHotResponder& hat_hot() const noexcept { return hat_hot_; }
    /// Number of frames ended so far = the next frame's IG Frame Number.
    [[nodiscard]] std::uint32_t ig_frame() const noexcept { return ig_frame_; }

private:
    void sync_database();

    SceneModel scene_;
    DatabaseSelector selector_;
    HatHotResponder hat_hot_;
    SceneModel::Handlers dispatcher_;
    std::uint32_t ig_frame_ = 0;
};

} // namespace cigi::mock
