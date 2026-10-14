#include "../support/temp_dir.hpp"
#include "mock_ig/database.hpp"

#include <format>
#include <gtest/gtest.h>
#include <string>

namespace {

using namespace cigi;
using namespace cigi::mock;
using cigi::test::TempDir;

/// A valid manifest with the given number and name (the number is on line 2).
std::string manifest(int number, const std::string& name, int load_frames = 3) {
    return std::format("[database]\nnumber = {}\nname = \"{}\"\nload_frames = {}\n", number, name,
                       load_frames);
}

TEST(Catalog, ScansSubFoldersInPathOrder) {
    TempDir dir;
    dir.write("b/database.toml", manifest(2, "Second"));
    dir.write("a/database.toml", manifest(9, "Ninth"));
    dir.write("c/readme.txt", "not a database");
    dir.write("loose.toml", manifest(4, "Loose"));
    DatabaseCatalog catalog;
    ASSERT_TRUE(catalog.scan(dir.path()));
    ASSERT_EQ(catalog.items().size(), 2U);
    EXPECT_EQ(catalog.items()[0].database->number, 9); // folder a first
    EXPECT_EQ(catalog.items()[1].database->number, 2);
    EXPECT_EQ(catalog.numbers(), (std::vector<int>{2, 9}));
    EXPECT_EQ(catalog.find(2)->name, "Second");
    EXPECT_EQ(catalog.find(3), nullptr);
    EXPECT_EQ(catalog.root(), dir.path());
    EXPECT_TRUE(catalog.errors().empty());
    EXPECT_TRUE(catalog.duplicates().empty());
}

TEST(Catalog, ARootThatIsItselfADatabase) {
    TempDir dir;
    dir.write("database.toml", manifest(6, "Single"));
    dir.write("sub/database.toml", manifest(7, "Ignored: the root wins"));
    DatabaseCatalog catalog;
    ASSERT_TRUE(catalog.scan(dir.path()));
    EXPECT_EQ(catalog.numbers(), std::vector<int>{6});
}

TEST(Catalog, ScanRejectsAFileOrMissingFolder) {
    TempDir dir;
    const auto file = dir.write("database.toml", manifest(1, "x"));
    DatabaseCatalog catalog;
    const auto as_file = catalog.scan(file);
    ASSERT_FALSE(as_file);
    EXPECT_NE(as_file.error().message.find("not a folder"), std::string::npos);
    EXPECT_FALSE(catalog.scan(dir.path() / "nope"));
}

TEST(Catalog, EmptyRootHasNoItems) {
    TempDir dir;
    DatabaseCatalog catalog;
    ASSERT_TRUE(catalog.scan(dir.path()));
    EXPECT_TRUE(catalog.items().empty());
    EXPECT_TRUE(catalog.numbers().empty());
}

TEST(Catalog, DuplicateNumbersAreReportedAndTheFirstWins) {
    TempDir dir;
    dir.write("a/database.toml", manifest(3, "First"));
    dir.write("b/database.toml", manifest(3, "Second"));
    dir.write("c/database.toml", manifest(4, "Other"));
    DatabaseCatalog catalog;
    ASSERT_TRUE(catalog.scan(dir.path()));
    EXPECT_EQ(catalog.find(3)->name, "First");
    EXPECT_EQ(catalog.numbers(), (std::vector<int>{3, 4}));
    EXPECT_FALSE(catalog.items()[0].duplicate);
    EXPECT_TRUE(catalog.items()[1].duplicate);
    const auto dups = catalog.duplicates();
    ASSERT_EQ(dups.size(), 1U);
    EXPECT_EQ(dups[0].line, 2U); // the `number = 3` line of the second manifest
    EXPECT_NE(dups[0].file.find("/b/"), std::string::npos);
    EXPECT_NE(dups[0].message.find("already used by"), std::string::npos);
    EXPECT_NE(dups[0].message.find("/a/"), std::string::npos);
}

TEST(Catalog, ABrokenManifestIsRecordedAndTheOthersStillLoad) {
    TempDir dir;
    dir.write("a/database.toml", "[database]\nnumber = 0\nname = \"x\"\n");
    dir.write("b/database.toml", manifest(2, "Fine"));
    DatabaseCatalog catalog;
    ASSERT_TRUE(catalog.scan(dir.path()));
    EXPECT_EQ(catalog.numbers(), std::vector<int>{2});
    const auto errors = catalog.errors();
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0].line, 2U);
    EXPECT_EQ(catalog.items()[0].database, nullptr);
}

// ---- hot reload
// ------------------------------------------------------------------------------------

TEST(CatalogReload, NothingChangedMeansNothingHappens) {
    TempDir dir;
    dir.write("a/database.toml", manifest(1, "One"));
    DatabaseCatalog catalog;
    ASSERT_TRUE(catalog.scan(dir.path()));
    const auto before = catalog.find(1);
    const auto r = catalog.reload_if_changed();
    EXPECT_FALSE(r.changed());
    EXPECT_EQ(catalog.find(1), before); // the very same object
}

TEST(CatalogReload, AnEditedManifestIsReloaded) {
    TempDir dir;
    const auto file = dir.write("a/database.toml", manifest(1, "One"));
    DatabaseCatalog catalog;
    ASSERT_TRUE(catalog.scan(dir.path()));
    const auto before = catalog.find(1);
    dir.write("a/database.toml", manifest(1, "One, renamed"));
    TempDir::bump_mtime(file, 5);
    const auto r = catalog.reload_if_changed();
    EXPECT_EQ(r.updated, std::vector<int>{1});
    EXPECT_TRUE(r.errors.empty());
    EXPECT_TRUE(r.changed());
    EXPECT_NE(catalog.find(1), before);
    EXPECT_EQ(catalog.find(1)->name, "One, renamed");
    EXPECT_EQ(before->name, "One"); // holders of the old version keep a valid object
    EXPECT_FALSE(catalog.reload_if_changed().changed());
}

TEST(CatalogReload, AManifestThatNoLongerParsesKeepsThePreviousVersion) {
    TempDir dir;
    const auto file = dir.write("a/database.toml", manifest(1, "Good"));
    DatabaseCatalog catalog;
    ASSERT_TRUE(catalog.scan(dir.path()));
    const auto good = catalog.find(1);

    dir.write("a/database.toml", "[database]\nnumber = 1\nname = 5\n");
    TempDir::bump_mtime(file, 5);
    const auto r = catalog.reload_if_changed();
    EXPECT_TRUE(r.updated.empty());
    ASSERT_EQ(r.errors.size(), 1U);
    EXPECT_EQ(r.errors[0].line, 3U);
    EXPECT_EQ(catalog.find(1), good); // previous version still served
    ASSERT_EQ(catalog.errors().size(), 1U);
    EXPECT_EQ(catalog.items()[0].error->line, 3U);
    // the same broken file is not reported again
    EXPECT_FALSE(catalog.reload_if_changed().changed());
    EXPECT_EQ(catalog.errors().size(), 1U);

    // fixing it clears the error
    dir.write("a/database.toml", manifest(1, "Fixed"));
    TempDir::bump_mtime(file, 10);
    const auto fixed = catalog.reload_if_changed();
    EXPECT_EQ(fixed.updated, std::vector<int>{1});
    EXPECT_TRUE(catalog.errors().empty());
    EXPECT_EQ(catalog.find(1)->name, "Fixed");
}

TEST(CatalogReload, NewAndRemovedManifestsAreNoticed) {
    TempDir dir;
    dir.write("a/database.toml", manifest(1, "One"));
    DatabaseCatalog catalog;
    ASSERT_TRUE(catalog.scan(dir.path()));

    dir.write("b/database.toml", manifest(2, "Two"));
    dir.write("c/database.toml", "garbage = [");
    const auto added = catalog.reload_if_changed();
    EXPECT_EQ(added.updated, std::vector<int>{2});
    EXPECT_EQ(added.errors.size(), 1U);
    EXPECT_FALSE(added.removed);
    EXPECT_EQ(catalog.numbers(), (std::vector<int>{1, 2}));

    std::error_code ec;
    std::filesystem::remove_all(dir.path() / "a", ec);
    const auto removed = catalog.reload_if_changed();
    EXPECT_TRUE(removed.removed);
    EXPECT_TRUE(removed.changed());
    EXPECT_EQ(catalog.numbers(), std::vector<int>{2});
    EXPECT_EQ(catalog.find(1), nullptr);
}

TEST(CatalogReload, ChangingANumberToADuplicateIsFlagged) {
    TempDir dir;
    dir.write("a/database.toml", manifest(1, "One"));
    const auto b = dir.write("b/database.toml", manifest(2, "Two"));
    DatabaseCatalog catalog;
    ASSERT_TRUE(catalog.scan(dir.path()));
    dir.write("b/database.toml", manifest(1, "Two, now 1"));
    TempDir::bump_mtime(b, 5);
    const auto r = catalog.reload_if_changed();
    EXPECT_EQ(r.updated, std::vector<int>{1});
    EXPECT_EQ(catalog.duplicates().size(), 1U);
    EXPECT_EQ(catalog.find(1)->name, "One");
}

} // namespace
