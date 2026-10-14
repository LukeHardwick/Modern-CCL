#include "../support/temp_dir.hpp"
#include "mock_ig/database_selector.hpp"

#include <format>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using namespace cigi;
using namespace cigi::mock;
using cigi::test::TempDir;

std::string manifest(int number, int load_frames) {
    return std::format("[database]\nnumber = {}\nname = \"DB {}\"\nload_frames = {}\n", number,
                       number, load_frames);
}

/// Databases 1 (3 load frames), 2 (0) and 3 (5) under a temp root.
struct Fixture {
    Fixture() {
        dir.write("one/database.toml", manifest(1, 3));
        dir.write("two/database.toml", manifest(2, 0));
        dir.write("three/database.toml", manifest(3, 5));
        EXPECT_TRUE(catalog.scan(dir.path()));
    }
    DatabaseSelector make(int initial = 1) {
        return DatabaseSelector(&catalog, initial,
                                [this](std::string text) { log.push_back(std::move(text)); });
    }
    TempDir dir;
    DatabaseCatalog catalog;
    std::vector<std::string> log;
};

constexpr auto operate = IgMode::Operate;

TEST(DatabaseSelector, GlobalDatabaseReportsZeroAndIgnoresRequests) {
    std::vector<std::string> log;
    DatabaseSelector sel(nullptr, 1, [&log](std::string t) { log.push_back(std::move(t)); });
    EXPECT_EQ(sel.reported(), 0);
    EXPECT_EQ(sel.current(), default_database());
    sel.on_ig_control(operate, 5);
    sel.end_frame();
    EXPECT_EQ(sel.reported(), 0);
    EXPECT_FALSE(sel.loading());
    ASSERT_EQ(log.size(), 1U);
    EXPECT_NE(log[0].find("global"), std::string::npos);
    EXPECT_FALSE(sel.refresh());
}

TEST(DatabaseSelector, StartsWithTheInitialDatabaseLoaded) {
    Fixture f;
    EXPECT_EQ(f.make(2).reported(), 2);
    EXPECT_EQ(f.make(1).current()->name, "DB 1");
    // an initial number the catalog lacks falls back to the built-in database
    EXPECT_EQ(f.make(99).current(), default_database());
}

TEST(DatabaseSelector, LoadReportsMinusNForLoadFramesAndUntilTheHostSendsZero) {
    Fixture f;
    auto sel = f.make(1);
    sel.on_ig_control(operate, 3); // database 3 loads for 5 frames
    EXPECT_TRUE(sel.loading());
    EXPECT_EQ(sel.reported(), -3);
    EXPECT_EQ(sel.current()->number, 1); // still the old one
    for (int i = 0; i < 20; ++i) {       // the Host never sends 0: stuck at -3
        sel.on_ig_control(operate, 3);
        sel.end_frame();
        EXPECT_EQ(sel.reported(), -3) << "frame " << i;
    }
    sel.on_ig_control(operate, 0);
    EXPECT_EQ(sel.reported(), -3); // the 0 is seen, completion happens at the frame end
    sel.end_frame();
    EXPECT_EQ(sel.reported(), 3);
    EXPECT_FALSE(sel.loading());
    EXPECT_EQ(sel.current()->number, 3);
}

TEST(DatabaseSelector, AnEarlyZeroStillWaitsForTheLoadFrames) {
    Fixture f;
    auto sel = f.make(1);
    sel.on_ig_control(operate, 3); // 5 load frames
    sel.end_frame();               // frame 1 reports -3
    sel.on_ig_control(operate, 0);
    for (int frame = 2; frame <= 5; ++frame) {
        sel.end_frame();
        EXPECT_EQ(sel.reported(), -3) << "frame " << frame;
    }
    sel.end_frame(); // frame 6: more than load_frames have passed
    EXPECT_EQ(sel.reported(), 3);
}

TEST(DatabaseSelector, ZeroLoadFramesCompletesAtTheFrameAfterTheZero) {
    Fixture f;
    auto sel = f.make(1);
    sel.on_ig_control(operate, 2);
    sel.end_frame();
    EXPECT_EQ(sel.reported(), -2);
    sel.on_ig_control(operate, 0);
    sel.end_frame();
    EXPECT_EQ(sel.reported(), 2);
}

TEST(DatabaseSelector, ReloadOfTheSameNumber) {
    Fixture f;
    auto sel = f.make(1);
    // first load of 2, completed
    sel.on_ig_control(operate, 2);
    sel.on_ig_control(operate, 0);
    sel.end_frame();
    ASSERT_EQ(sel.reported(), 2);
    // 2 again after the 0: a reload
    sel.on_ig_control(operate, 2);
    EXPECT_TRUE(sel.loading());
    EXPECT_EQ(sel.reported(), -2);
    sel.on_ig_control(operate, 0);
    sel.end_frame();
    EXPECT_EQ(sel.reported(), 2);
}

TEST(DatabaseSelector, RequestsAreIgnoredOutsideOperateAndDebug) {
    Fixture f;
    auto sel = f.make(1);
    for (const auto mode : {IgMode::ResetStandby, IgMode::OfflineMaintenance}) {
        sel.on_ig_control(mode, 2);
        sel.end_frame();
        EXPECT_FALSE(sel.loading());
        EXPECT_EQ(sel.reported(), 1);
    }
    // Standby does not count as "the Host sent 0" either
    sel.on_ig_control(operate, 3);
    sel.on_ig_control(IgMode::ResetStandby, 0);
    for (int i = 0; i < 10; ++i) {
        sel.end_frame();
    }
    EXPECT_EQ(sel.reported(), -3);
    // the same number is accepted once the IG operates (the Standby request was not "held")
    DatabaseSelector second = f.make(1);
    second.on_ig_control(IgMode::ResetStandby, 2);
    second.on_ig_control(IgMode::Debug, 2);
    EXPECT_TRUE(second.loading()); // Debug counts as operating
}

TEST(DatabaseSelector, AnUnknownNumberKeepsTheCurrentDatabase) {
    Fixture f;
    auto sel = f.make(1);
    sel.on_ig_control(operate, 99);
    sel.on_ig_control(operate, 99); // held: logged once
    sel.end_frame();
    EXPECT_FALSE(sel.loading());
    EXPECT_EQ(sel.reported(), 1);
    EXPECT_EQ(sel.current()->number, 1);
    ASSERT_EQ(f.log.size(), 1U);
    EXPECT_NE(f.log[0].find("99"), std::string::npos);
    EXPECT_NE(f.log[0].find("not found"), std::string::npos);
    // after a 0 the same unknown number is a new request and logs again
    sel.on_ig_control(operate, 0);
    sel.on_ig_control(operate, 99);
    EXPECT_EQ(f.log.size(), 2U);
}

TEST(DatabaseSelector, AnotherNumberWhileLoadingRestartsTheLoad) {
    Fixture f;
    auto sel = f.make(1);
    sel.on_ig_control(operate, 3);
    sel.end_frame();
    sel.end_frame();
    sel.on_ig_control(operate, 2);
    EXPECT_EQ(sel.reported(), -2);
    sel.on_ig_control(operate, 0);
    sel.end_frame();
    EXPECT_EQ(sel.reported(), 2); // database 2 has no load frames; the restart reset the count
}

TEST(DatabaseSelector, RefreshFollowsAHotReloadedCatalog) {
    Fixture f;
    auto sel = f.make(1);
    const auto before = sel.current();
    EXPECT_FALSE(sel.refresh());
    const auto file = f.dir.path() / "one" / "database.toml";
    f.dir.write("one/database.toml", manifest(1, 3) + "# edited\n");
    TempDir::bump_mtime(file, 5);
    ASSERT_TRUE(f.catalog.reload_if_changed().changed());
    EXPECT_TRUE(sel.refresh());
    EXPECT_NE(sel.current(), before);
    EXPECT_FALSE(sel.refresh());

    // a database being loaded is refreshed too
    sel.on_ig_control(operate, 3);
    const auto three = f.dir.path() / "three" / "database.toml";
    f.dir.write("three/database.toml", manifest(3, 0));
    TempDir::bump_mtime(three, 5);
    ASSERT_TRUE(f.catalog.reload_if_changed().changed());
    EXPECT_TRUE(sel.refresh());
    sel.on_ig_control(operate, 0);
    sel.end_frame(); // the new version has 0 load frames
    EXPECT_EQ(sel.reported(), 3);
}

} // namespace
