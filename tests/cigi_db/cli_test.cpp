#include "../support/temp_dir.hpp"
#include "cigi_db/cli.hpp"
#include "cigi_db/report.hpp"

#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace cigi;
using cigi::test::TempDir;

struct Result {
    int code = 0;
    std::string out;
    std::string err;
};

Result run(const std::vector<std::string>& args) {
    const std::vector<std::string_view> views(args.begin(), args.end());
    std::ostringstream out;
    std::ostringstream err;
    Result r;
    r.code = db::run(views, out, err);
    r.out = out.str();
    r.err = err.str();
    return r;
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const char* const good = "[database]\nnumber = 4\nname = \"Good\"\nbogus = 1\n";
const char* const bad = "[database]\nnumber = 4\nname = \"Bad\"\n[[entity]]\ntypes = 1\n";

TEST(CigiDb, HelpAndUsage) {
    EXPECT_EQ(run({"--help"}).code, 0);
    EXPECT_NE(run({"help"}).out.find("validate"), std::string::npos);
    EXPECT_EQ(run({"-h"}).code, 0);
    const auto none = run({});
    EXPECT_EQ(none.code, 2);
    EXPECT_NE(none.err.find("usage"), std::string::npos);
    const auto unknown = run({"frobnicate"});
    EXPECT_EQ(unknown.code, 2);
    EXPECT_NE(unknown.err.find("frobnicate"), std::string::npos);
    EXPECT_FALSE(db::usage().empty());
}

TEST(CigiDb, ValidateRejectsBadArguments) {
    EXPECT_EQ(run({"validate"}).code, 2);
    EXPECT_EQ(run({"validate", "a", "b"}).code, 2);
    EXPECT_EQ(run({"validate", "a", "--json"}).code, 2);
    EXPECT_EQ(run({"validate", "a", "--json", "x", "--json", "y"}).code, 2);
    EXPECT_EQ(run({"validate", "a", "--frob"}).code, 2);
}

TEST(CigiDb, ValidateAManifestWithWarnings) {
    TempDir dir;
    const auto file = dir.write("db/database.toml", good);
    const auto r = run({"validate", file.string()});
    EXPECT_EQ(r.code, 0);
    EXPECT_NE(r.out.find("database 4 \"Good\""), std::string::npos);
    EXPECT_NE(r.out.find("OK: 1 database(s), 0 error(s), 1 warning(s)"), std::string::npos);
    EXPECT_NE(r.err.find("warning: "), std::string::npos);
    EXPECT_NE(r.err.find(":4:1: unknown key 'bogus'"), std::string::npos);
}

TEST(CigiDb, ValidateAManifestWithAnErrorExitsOne) {
    TempDir dir;
    const auto file = dir.write("db/database.toml", bad);
    const auto r = run({"validate", file.string()});
    EXPECT_EQ(r.code, 1);
    EXPECT_NE(r.err.find("error: "), std::string::npos);
    EXPECT_NE(r.err.find(":4:1: missing 'category'"), std::string::npos) << r.err;
    EXPECT_NE(r.out.find("FAILED"), std::string::npos);
}

TEST(CigiDb, ValidateARootAndWriteTheJsonReport) {
    TempDir dir;
    dir.write("a/database.toml", good);
    dir.write("b/database.toml", "[database]\nnumber = 4\nname = \"Twin\"\n");
    dir.write("c/database.toml", bad);
    const auto json = dir.path() / "out" / "report.json";
    std::filesystem::create_directories(json.parent_path());
    const auto r = run({"validate", dir.path().string(), "--json", json.string()});
    EXPECT_EQ(r.code, 1);
    EXPECT_NE(r.out.find("[DUPLICATE NUMBER]"), std::string::npos);
    const auto report = db::report_from_json(read_file(json));
    ASSERT_TRUE(report) << report.error();
    EXPECT_FALSE(report->ok);
    EXPECT_EQ(report->databases.size(), 2U);
    EXPECT_EQ(report->errors.size(), 2U); // the broken c/ and the duplicate number
    EXPECT_EQ(report->warnings.size(), 1U);
    EXPECT_EQ(report->warnings[0].line, 4U);
}

TEST(CigiDb, AnUnwritableJsonPathIsAnError) {
    TempDir dir;
    const auto file = dir.write("db/database.toml", "[database]\nnumber = 1\nname = \"x\"\n");
    const auto r = run(
        {"validate", file.string(), "--json", (dir.path() / "no" / "such" / "r.json").string()});
    EXPECT_EQ(r.code, 1);
    EXPECT_NE(r.err.find("cannot write"), std::string::npos);
}

TEST(CigiDb, ValidateAMissingPathOrEmptyRoot) {
    TempDir dir;
    const auto missing = run({"validate", (dir.path() / "nope").string()});
    EXPECT_EQ(missing.code, 1);
    EXPECT_NE(missing.err.find("no such file or folder"), std::string::npos);
    const auto empty = run({"validate", dir.path().string()});
    EXPECT_EQ(empty.code, 1);
    EXPECT_NE(empty.err.find("no database.toml found"), std::string::npos);
}

TEST(CigiDb, ValidatesTheShippedDatabases) {
    const auto r = run({"validate", CIGI_DATABASES_DIR});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_NE(r.out.find("Default (synthetic)"), std::string::npos);
}

TEST(CigiDb, ListShowsDatabasesDuplicatesAndErrors) {
    TempDir dir;
    dir.write("a/database.toml", good);
    dir.write("b/database.toml", "[database]\nnumber = 4\nname = \"Twin\"\n");
    dir.write("c/database.toml", bad);
    const auto r = run({"list", dir.path().string()});
    EXPECT_EQ(r.code, 0);
    EXPECT_NE(r.out.find("Good"), std::string::npos);
    EXPECT_NE(r.out.find("duplicate number, unused"), std::string::npos);
    EXPECT_NE(r.out.find("[ERROR: "), std::string::npos);

    TempDir empty;
    EXPECT_NE(run({"list", empty.path().string()}).out.find("(no databases)"), std::string::npos);
    EXPECT_EQ(run({"list"}).code, 2);
    EXPECT_EQ(run({"list", "a", "b"}).code, 2);
    EXPECT_EQ(run({"list", "--x"}).code, 2);
    const auto nofolder = run({"list", (dir.path() / "nope").string()});
    EXPECT_EQ(nofolder.code, 1);
    EXPECT_NE(nofolder.err.find("not a folder"), std::string::npos);
}

} // namespace
