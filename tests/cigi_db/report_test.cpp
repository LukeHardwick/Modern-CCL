#include "../support/temp_dir.hpp"
#include "cigi_db/report.hpp"

#include <gtest/gtest.h>
#include <string>

namespace {

using namespace cigi;
using cigi::test::TempDir;
using db::Report;

Report sample_report() {
    TempDir dir;
    dir.write("a/database.toml",
              "[database]\nnumber = 3\nname = \"Quote \\\" and \\\\ and \\u00e9\"\n"
              "[terrain]\nkind = \"heightmap\"\nfile = \"h.png\"\n"
              "[[material]]\ncode = 9\ncolour = \"#010203\"\n"
              "[[entity]]\ntypes = [1, 2]\ncategory = \"sea\"\nlods = [\"a.glb\", "
              "\"b.glb\"]\nunknown = 1\n");
    mock::DatabaseCatalog catalog;
    EXPECT_TRUE(catalog.scan(dir.path()));
    return db::report_for_catalog(catalog, dir.path());
}

TEST(CigiDbReport, ContentFollowsTheDatabase) {
    const Report report = sample_report();
    EXPECT_TRUE(report.ok);
    ASSERT_EQ(report.databases.size(), 1U);
    const auto& d = report.databases[0];
    EXPECT_EQ(d.number, 3);
    EXPECT_EQ(d.terrain, "heightmap");
    EXPECT_EQ(d.default_category, "other");
    ASSERT_EQ(d.materials.size(), 1U);
    EXPECT_EQ(d.materials[0].colour, "#010203");
    ASSERT_EQ(d.entities.size(), 1U);
    EXPECT_EQ(d.entities[0].category, "sea");
    EXPECT_EQ(d.entities[0].lods.size(), 2U);
    EXPECT_EQ(d.entities[0].line, 10U);
    EXPECT_GE(report.warnings.size(), 1U); // unknown key + missing files
    EXPECT_FALSE(d.duplicate);
}

TEST(CigiDbReport, GlazeRoundTrip) {
    const Report report = sample_report();
    const std::string json = db::to_json(report);
    ASSERT_FALSE(json.empty());
    EXPECT_NE(json.find("\"tool\""), std::string::npos);
    EXPECT_NE(json.find("\"databases\""), std::string::npos);
    const auto back = db::report_from_json(json);
    ASSERT_TRUE(back) << back.error();
    EXPECT_EQ(*back, report);
    EXPECT_EQ(back->databases[0].name, report.databases[0].name); // quotes and unicode survive
}

TEST(CigiDbReport, ErrorsCarryTheirLocations) {
    TempDir dir;
    const auto file = dir.write("a/database.toml", "[database]\nnumber = 500\nname = \"x\"\n");
    const Report report = db::report_for_manifest(file);
    EXPECT_FALSE(report.ok);
    ASSERT_EQ(report.errors.size(), 1U);
    EXPECT_EQ(report.errors[0].line, 2U);
    EXPECT_EQ(report.errors[0].column, 10U);
    const auto back = db::report_from_json(db::to_json(report));
    ASSERT_TRUE(back);
    EXPECT_EQ(back->errors, report.errors);
    EXPECT_FALSE(back->ok);

    const Report failure =
        db::report_for_failure("x", {.file = "x", .line = 0, .column = 0, .message = "nope"});
    EXPECT_FALSE(failure.ok);
    EXPECT_EQ(failure.errors[0].message, "nope");
}

TEST(CigiDbReport, RejectsBrokenJson) {
    EXPECT_FALSE(db::report_from_json("{ not json"));
    EXPECT_FALSE(db::report_from_json("{\"databases\": 3}"));
    const auto parsed = db::report_from_json("{\"databases\": 3}");
    EXPECT_FALSE(parsed.error().empty());
}

} // namespace
