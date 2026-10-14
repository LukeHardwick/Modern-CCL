#include "../support/temp_dir.hpp"
#include "mock_ig/mock_ig.hpp"

#include <array>
#include <cigi/message/builder.hpp>
#include <format>
#include <gtest/gtest.h>
#include <optional>
#include <set>
#include <vector>

namespace {

using namespace cigi;
using namespace cigi::mock;

/// Host message with an IgControl in `mode`, run through one IG frame; returns the Start of
/// Frame mode of the reply.
IgMode reply_mode(IgMode mode) {
    MockIg ig;
    std::array<std::byte, 256> storage{};
    MessageBuilder host(Direction::HostToIg, storage);
    EXPECT_TRUE(host.add(IgControl{.ig_mode = mode}));
    EXPECT_TRUE(ig.ingest(host.bytes()));
    const auto replies = ig.end_frame(1.0 / 60.0);
    std::array<std::byte, 256> out{};
    MessageBuilder builder(Direction::IgToHost, out);
    EXPECT_TRUE(ig.build_reply(builder, replies));
    const auto view = MessageView::parse(builder.bytes());
    EXPECT_TRUE(view);
    for (const auto& item : *view) {
        if (item && item->opcode == StartOfFrame::opcode) {
            const auto sof = item->decode_as<StartOfFrame>();
            EXPECT_TRUE(sof);
            return sof->ig_mode;
        }
    }
    ADD_FAILURE() << "no Start of Frame";
    return IgMode::ResetStandby;
}

TEST(MockIg, StartOfFrameEchoesOperateAndDebug) {
    EXPECT_EQ(reply_mode(IgMode::Operate), IgMode::Operate);
    EXPECT_EQ(reply_mode(IgMode::Debug), IgMode::Debug);
}

TEST(MockIg, StartOfFrameReportsResetStandbyForEveryOtherMode) {
    EXPECT_EQ(reply_mode(IgMode::ResetStandby), IgMode::ResetStandby);
    EXPECT_EQ(reply_mode(IgMode::OfflineMaintenance), IgMode::ResetStandby);
}

TEST(MockIg, IngestRejectsADatagramThatIsNotCigi) {
    MockIg ig;
    const std::array<std::byte, 3> junk{std::byte{1}, std::byte{2}, std::byte{3}};
    const auto stats = ig.ingest(junk);
    EXPECT_FALSE(stats);
    EXPECT_EQ(ig.ig_frame(), 0U);
}

TEST(MockIg, BuildReplyReportsABufferThatIsTooSmall) {
    MockIg ig;
    const auto replies = ig.end_frame(0.1);
    std::array<std::byte, 4> tiny{};
    MessageBuilder builder(Direction::IgToHost, tiny);
    const auto r = ig.build_reply(builder, replies);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, ErrorCode::BufferTooSmall);
}

TEST(MockIg, BuildReplyReportsAnAnswerThatDoesNotFit) {
    const MockIg ig;
    const MockIg::Replies replies{HatHotResponse{}, HatHotResponse{}};
    // Room for Start of Frame only.
    std::array<std::byte, 24> small{};
    MessageBuilder probe(Direction::IgToHost, small);
    ASSERT_TRUE(probe.add(StartOfFrame{}));
    std::array<std::byte, 24> storage{};
    MessageBuilder builder(Direction::IgToHost, std::span(storage).first(probe.size()));
    const auto r = ig.build_reply(builder, replies);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, ErrorCode::BufferTooSmall);
}

TEST(MockIg, AccessorsExposeTheSceneAndTheResponder) {
    MockIg ig;
    const MockIg& cig = ig;
    EXPECT_EQ(&ig.scene(), &cig.scene());
    EXPECT_EQ(&ig.hat_hot(), &cig.hat_hot());
    EXPECT_TRUE(cig.scene().entities().empty());
}

// ---- Database Number protocol (ICD 4.1.1, 4.2.1)
// ------------------------------------------------------

/// One Host message (IG Control + optional Entity Control) in, one IG frame, the reply's Start of
/// Frame out.
StartOfFrame frame(MockIg& ig, std::int8_t database, IgMode mode = IgMode::Operate,
                   std::optional<EntityControl> entity = std::nullopt) {
    std::array<std::byte, 256> storage{};
    MessageBuilder host(Direction::HostToIg, storage);
    EXPECT_TRUE(host.add(IgControl{.database_number = database, .ig_mode = mode}));
    if (entity) {
        EXPECT_TRUE(host.add(*entity));
    }
    EXPECT_TRUE(ig.ingest(host.bytes()));
    const auto replies = ig.end_frame(1.0 / 60.0);
    std::array<std::byte, 256> out{};
    MessageBuilder builder(Direction::IgToHost, out);
    EXPECT_TRUE(ig.build_reply(builder, replies));
    const auto view = MessageView::parse(builder.bytes());
    EXPECT_TRUE(view);
    for (const auto& item : *view) {
        if (item && item->opcode == StartOfFrame::opcode) {
            return *item->decode_as<StartOfFrame>();
        }
    }
    ADD_FAILURE() << "no Start of Frame";
    return {};
}

struct Databases {
    Databases() {
        dir.write("one/database.toml", "[database]\nnumber = 1\nname = \"One\"\nload_frames = 2\n"
                                       "[[entity]]\ntypes = 1\ncategory = \"aircraft\"\n");
        dir.write("two/database.toml",
                  "[database]\nnumber = 2\nname = \"Two\"\nload_frames = 1\n"
                  "origin = { lat = 40, lon = -100 }\n"
                  "bounds = { lat_min = 39.5, lat_max = 40.5, lon_min = -100.5, lon_max = -99.5 }\n"
                  "[[entity]]\ntypes = [1, 5]\ncategory = \"sea\"\n");
        EXPECT_TRUE(catalog.scan(dir.path()));
    }
    cigi::test::TempDir dir;
    DatabaseCatalog catalog;
};

TEST(MockIgDatabase, WithoutACatalogTheIgReportsTheGlobalDatabase) {
    MockIg ig;
    EXPECT_EQ(frame(ig, 0).database_number, 0);
    EXPECT_EQ(frame(ig, 5).database_number, 0); // ignored: one global database
    EXPECT_EQ(ig.scene().database_ptr(), default_database());
}

TEST(MockIgDatabase, LoadReloadStandbyUnknownAndAHostThatNeverSendsZero) {
    Databases dbs;
    MockIg ig;
    ig.use_catalog(&dbs.catalog, 1);
    EXPECT_EQ(ig.scene().database().name, "One");
    EXPECT_EQ(frame(ig, 0).database_number, 1);

    // Standby: ignored
    EXPECT_EQ(frame(ig, 2, IgMode::ResetStandby).database_number, 1);
    EXPECT_EQ(ig.scene().database().number, 1);

    // Operate, database 2 (1 load frame): -2 while the Host holds 2 ...
    EXPECT_EQ(frame(ig, 2).database_number, -2);
    EXPECT_EQ(ig.scene().database().number, 1); // the old database stays in force while loading
    EXPECT_EQ(frame(ig, 2).database_number, -2);
    EXPECT_EQ(frame(ig, 2).database_number, -2); // ... a Host that never sends 0 sees -2 forever
    // the Host sends 0: +2, and the new database is in force
    const auto done = frame(ig, 0);
    EXPECT_EQ(done.database_number, 2);
    EXPECT_EQ(ig.scene().database().name, "Two");
    EXPECT_EQ(ig.scene().config().origin, (Geodetic{40.0, -100.0, 0.0}));
    EXPECT_TRUE(ig.scene().config().bounds.contains(40.0, -100.0));
    EXPECT_FALSE(ig.scene().config().bounds.contains(51.5, -0.12));

    // reload: 2 again after the 0
    EXPECT_EQ(frame(ig, 2).database_number, -2);
    EXPECT_EQ(frame(ig, 0).database_number, 2);

    // unknown number: error logged, database and its number kept
    EXPECT_EQ(frame(ig, 99).database_number, 2);
    EXPECT_EQ(ig.scene().database().number, 2);
    bool logged = false;
    for (const auto& line : ig.scene().log()) {
        logged = logged || line.find("99") != std::string::npos;
    }
    EXPECT_TRUE(logged);
    EXPECT_EQ(ig.database_selector().reported(), 2);
}

TEST(MockIgDatabase, EntitiesSurviveALoadAndUnmappedTypesAreTracked) {
    Databases dbs;
    MockIg ig;
    ig.use_catalog(&dbs.catalog, 1);
    EntityControl e;
    e.entity_id = 7;
    e.entity_type = 3; // database 1 maps only type 1
    e.entity_state = EntityState::Active;
    e.latitude_or_x = 51.5;
    e.longitude_or_y = -0.12;
    (void)frame(ig, 0, IgMode::Operate, e);
    EXPECT_EQ(ig.scene().stats().unmapped_entity_types, std::set<std::uint16_t>{3});
    EXPECT_EQ(ig.scene().category_of(3), EntityCategory::Other);
    EXPECT_EQ(ig.scene().category_of(1), EntityCategory::Aircraft);

    (void)frame(ig, 2);
    (void)frame(ig, 0);                                            // database 2 now in force
    ASSERT_NE(ig.scene().find(7), nullptr);                        // the entity was kept
    EXPECT_TRUE(ig.scene().stats().unmapped_entity_types.empty()); // type 3 is mapped by database 2
    EXPECT_EQ(ig.scene().category_of(3), EntityCategory::Sea);
}

TEST(MockIgDatabase, RefreshDatabasePicksUpAHotReload) {
    Databases dbs;
    MockIg ig;
    ig.use_catalog(&dbs.catalog, 1);
    EXPECT_FALSE(ig.refresh_database());
    const auto file = dbs.dir.path() / "one" / "database.toml";
    dbs.dir.write("one/database.toml", "[database]\nnumber = 1\nname = \"One v2\"\n");
    cigi::test::TempDir::bump_mtime(file, 5);
    ASSERT_TRUE(dbs.catalog.reload_if_changed().changed());
    EXPECT_TRUE(ig.refresh_database());
    EXPECT_EQ(ig.scene().database().name, "One v2");
    EXPECT_EQ(frame(ig, 0).database_number, 1); // no flicker: still reported as loaded
}

TEST(MockIgDatabase, SceneConfigCanCarryADatabase) {
    Databases dbs;
    SceneConfig config;
    config.database = dbs.catalog.find(2);
    MockIg ig(config);
    EXPECT_EQ(ig.scene().config().origin, (Geodetic{40.0, -100.0, 0.0}));
    EXPECT_EQ(ig.scene().database().number, 2);
    EXPECT_EQ(ig.scene().frame().origin().lat_deg, 40.0);
    ig.scene().set_database(nullptr); // ignored
    EXPECT_EQ(ig.scene().database().number, 2);
}

} // namespace
