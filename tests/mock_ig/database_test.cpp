#include "../support/temp_dir.hpp"
#include "mock_ig/database.hpp"

#include <algorithm>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using namespace cigi;
using namespace cigi::mock;

/// Lines joined with '\n' (so a test can count manifest lines by counting initialiser items).
std::string toml(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& line : lines) {
        out += line;
        out += '\n';
    }
    return out;
}

const std::filesystem::path& fake_manifest() {
    static const std::filesystem::path path{"/nonexistent-cigi-db/db1/database.toml"};
    return path;
}

std::expected<Database, DbError> parse(const std::string& text) {
    return parse_database(text, fake_manifest());
}

/// The error of a manifest that must not load.
DbError error_of(const std::string& text) {
    const auto db = parse(text);
    EXPECT_FALSE(db.has_value()) << "loaded, but an error was expected";
    return db ? DbError{} : db.error();
}

/// A valid manifest head: [database] with number and name on lines 1-3.
const char* const head[] = {"[database]", "number = 5", "name = \"Test\""};

std::string with_head(const std::vector<std::string>& rest) {
    std::string out = toml({head[0], head[1], head[2]});
    out += toml(rest);
    return out;
}

// ---- the happy path
// ------------------------------------------------------------------------------

TEST(DatabaseLoader, MinimalManifestUsesDefaults) {
    const auto db = parse(with_head({}));
    ASSERT_TRUE(db) << to_string(db.error());
    EXPECT_EQ(db->number, 5);
    EXPECT_EQ(db->name, "Test");
    EXPECT_EQ(db->number_line, 2U);
    EXPECT_EQ(db->load_frames, 30U);
    EXPECT_EQ(db->terrain.kind, TerrainSpec::Kind::Synthetic);
    EXPECT_EQ(db->default_category, EntityCategory::Other);
    EXPECT_TRUE(db->entities.empty());
    EXPECT_TRUE(db->bounds.contains(db->origin.lat_deg, db->origin.lon_deg));
    EXPECT_TRUE(db->warnings.empty());
    EXPECT_EQ(db->root, fake_manifest().parent_path());
    EXPECT_EQ(db->manifest, fake_manifest());
    EXPECT_EQ(db->resolve("models/a.glb"), fake_manifest().parent_path() / "models" / "a.glb");
}

TEST(DatabaseLoader, ParsesEveryField) {
    const auto db = parse(toml({
        "[database]",
        "number = 7",
        "name = \"Full\"",
        "origin = { lat = 10.5, lon = 20, alt = 30 }",
        "bounds = { lat_min = 10, lat_max = 11, lon_min = 19.5, lon_max = 21 }",
        "load_frames = 12",
        "[terrain]",
        "kind = \"heightmap\"",
        "file = \"terrain/h.png\"",
        "[default]",
        "category = \"sea\"",
        "[[material]]",
        "code = 4294967295",
        "name = \"ice\"",
        "colour = \"#A0b1C2\"",
        "[[material]]",
        "code = 3",
        "colour = \"#000000\"",
        "[[entity]]",
        "types = [100, 199]",
        "name = \"jets\"",
        "category = \"aircraft\"",
        R"(lods = ["jet.glb", "jet1.glb", "jet2.glb"])",
        "scale = 0.5",
        "offset = [1, 2.5, -3]",
        "rotation = [90, 0, 180]",
        "attached_view = \"hidden\"",
        "eyepoint_front = \"eye_f\"",
        "eyepoint_rear = \"eye_r\"",
        "lod_thresholds = [0.5, 0.2]",
        "cull_below = 0.01",
        "[[entity.part]]",
        "id = 1",
        "node = \"turret\"",
        "axis = \"z\"",
        "[[entity.part]]",
        "id = 2",
        "node = \"wheel\"",
        "[[entity.component]]",
        "id = 3",
        "node = \"gear\"",
        "kind = \"rotation\"",
        "[[entity.component]]",
        "id = 4",
        "node = \"lamp\"",
        "[[entity]]",
        "types = 150",
        "category = \"lifeform\"",
        "model = \"man.glb\"",
    }));
    ASSERT_TRUE(db) << to_string(db.error());
    EXPECT_EQ(db->number, 7);
    EXPECT_EQ(db->origin, (Geodetic{10.5, 20.0, 30.0}));
    EXPECT_EQ(db->bounds.lat_min_deg, 10.0);
    EXPECT_EQ(db->bounds.lon_max_deg, 21.0);
    EXPECT_EQ(db->load_frames, 12U);
    EXPECT_EQ(db->terrain.kind, TerrainSpec::Kind::Heightmap);
    EXPECT_EQ(db->terrain.file, "terrain/h.png");
    EXPECT_EQ(db->default_category, EntityCategory::Sea);
    ASSERT_EQ(db->materials.size(), 2U);
    EXPECT_EQ(db->materials[0].code, 4294967295U);
    EXPECT_EQ(db->materials[0].colour, (Rgb8{0xA0, 0xB1, 0xC2}));
    EXPECT_EQ(to_hex(db->materials[0].colour), "#a0b1c2");
    EXPECT_EQ(db->materials[1].name, "material 3");
    ASSERT_NE(db->material(3), nullptr);
    EXPECT_EQ(db->material(99), nullptr);

    ASSERT_EQ(db->entities.size(), 2U);
    const EntityModel& jet = db->entities[0];
    EXPECT_EQ(jet.first_type, 100);
    EXPECT_EQ(jet.last_type, 199);
    EXPECT_EQ(jet.name, "jets");
    EXPECT_EQ(jet.category, EntityCategory::Aircraft);
    EXPECT_EQ(jet.lods.size(), 3U);
    EXPECT_EQ(jet.scale, 0.5);
    EXPECT_EQ(jet.offset, (Vec3d{1, 2.5, -3}));
    EXPECT_EQ(jet.rotation, (Attitude{90, 0, 180}));
    EXPECT_EQ(jet.attached_view, AttachedView::Hidden);
    EXPECT_EQ(to_string(jet.attached_view), "hidden");
    EXPECT_EQ(jet.eyepoint_front, "eye_f");
    EXPECT_EQ(jet.eyepoint_rear, "eye_r");
    EXPECT_EQ(jet.lod_screen_heights, (std::vector<double>{0.5, 0.2}));
    EXPECT_EQ(jet.cull_screen_height, 0.01);
    ASSERT_EQ(jet.parts.size(), 2U);
    EXPECT_EQ(jet.parts[0].axis, "z");
    EXPECT_EQ(jet.parts[1].axis, "");
    ASSERT_EQ(jet.components.size(), 2U);
    EXPECT_EQ(jet.components[0].kind, ComponentKind::Rotation);
    EXPECT_EQ(to_string(jet.components[0].kind), "rotation");
    EXPECT_EQ(jet.components[1].kind, ComponentKind::Visibility);
    EXPECT_EQ(db->entities[1].lods, std::vector<std::string>{"man.glb"}); // model = lods[0]
    EXPECT_EQ(db->entities[1].name, "lifeform 150");                      // default name
}

TEST(DatabaseLoader, ShippedDefaultManifestEqualsTheBuiltInDatabase) {
    const auto db =
        load_database(std::filesystem::path(CIGI_DATABASES_DIR) / "default" / "database.toml");
    ASSERT_TRUE(db) << to_string(db.error());
    EXPECT_TRUE(db->same_content(*default_database()));
    EXPECT_TRUE(db->warnings.empty());
}

TEST(DatabaseLoader, LoadFromDiskAndReportMissingFile) {
    test::TempDir dir;
    const auto file = dir.write("a/database.toml", with_head({}));
    const auto ok = load_database(file);
    ASSERT_TRUE(ok) << to_string(ok.error());
    EXPECT_EQ(ok->root, file.parent_path());
    const auto missing = load_database(dir.path() / "none.toml");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().line, 0U);
    EXPECT_NE(missing.error().message.find("cannot open"), std::string::npos);
    EXPECT_NE(to_string(missing.error()).find("none.toml"), std::string::npos);
}

// ---- lookup
// ---------------------------------------------------------------------------------------

TEST(DatabaseLookup, ExactBeatsRangeAndUnmappedUsesTheDefault) {
    const auto db = parse(with_head({
        "[default]",
        "category = \"munition\"",
        "[[entity]]",
        "types = [100, 199]",
        "category = \"aircraft\"",
        "[[entity]]",
        "types = 150",
        "category = \"sea\"",
    }));
    ASSERT_TRUE(db) << to_string(db.error());
    EXPECT_EQ(db->category_of(100), EntityCategory::Aircraft);
    EXPECT_EQ(db->category_of(199), EntityCategory::Aircraft);
    EXPECT_EQ(db->category_of(150), EntityCategory::Sea);
    EXPECT_EQ(db->lookup(150)->first_type, 150);
    EXPECT_EQ(db->lookup(99), nullptr);
    EXPECT_EQ(db->category_of(99), EntityCategory::Munition);
    EXPECT_EQ(db->category_of(65535), EntityCategory::Munition);
}

TEST(DatabaseLookup, ExactEntryWinsRegardlessOfOrder) {
    const auto db = parse(with_head({
        "[[entity]]",
        "types = 150",
        "category = \"sea\"",
        "[[entity]]",
        "types = [100, 199]",
        "category = \"aircraft\"",
    }));
    ASSERT_TRUE(db) << to_string(db.error());
    EXPECT_EQ(db->category_of(150), EntityCategory::Sea);
    EXPECT_EQ(db->category_of(151), EntityCategory::Aircraft);
}

TEST(DatabaseLookup, DefaultDatabaseKeepsTheOldConvention) {
    const auto db = default_database();
    EXPECT_EQ(db->number, 1);
    EXPECT_EQ(db->category_of(0), EntityCategory::None);
    EXPECT_EQ(db->category_of(1), EntityCategory::Other);
    EXPECT_EQ(db->category_of(99), EntityCategory::Other);
    EXPECT_EQ(db->category_of(100), EntityCategory::Aircraft);
    EXPECT_EQ(db->category_of(199), EntityCategory::Aircraft);
    EXPECT_EQ(db->category_of(200), EntityCategory::GroundVehicle);
    EXPECT_EQ(db->category_of(299), EntityCategory::GroundVehicle);
    EXPECT_EQ(db->category_of(300), EntityCategory::Other);
    EXPECT_EQ(db->category_of(65535), EntityCategory::Other);
    EXPECT_EQ(db->lookup(300), nullptr);
    EXPECT_EQ(default_database(), db); // one shared instance
}

TEST(DatabaseLookup, CategoryNamesRoundTrip) {
    for (const auto c :
         {EntityCategory::None, EntityCategory::Aircraft, EntityCategory::GroundVehicle,
          EntityCategory::Sea, EntityCategory::Lifeform, EntityCategory::Munition,
          EntityCategory::Other}) {
        EXPECT_EQ(parse_entity_category(to_string(c)), c);
    }
    EXPECT_FALSE(parse_entity_category("tank"));
}

TEST(DatabaseLookup, SameContentIgnoresOriginAndWarnings) {
    const auto a = parse(with_head({}));
    auto b = parse_database(with_head({"bogus = 1"}), "/elsewhere/database.toml");
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_FALSE(b->warnings.empty());
    EXPECT_TRUE(a->same_content(*b));
    b->name = "other";
    EXPECT_FALSE(a->same_content(*b));
}

// ---- errors, each with its line
// -------------------------------------------------------------------

struct ErrorCase {
    const char* name;
    std::vector<std::string> lines;
    std::uint32_t line;
    const char* fragment; ///< Must occur in the message.
};

void expect_error(const ErrorCase& c) {
    SCOPED_TRACE(c.name);
    const DbError e = error_of(toml(c.lines));
    EXPECT_EQ(e.line, c.line) << to_string(e);
    EXPECT_GT(e.column, 0U) << to_string(e);
    EXPECT_NE(e.message.find(c.fragment), std::string::npos) << to_string(e);
    EXPECT_EQ(e.file, fake_manifest().string());
}

TEST(DatabaseErrors, Syntax) {
    expect_error({"unterminated string", {"[database]", "number = 1", "name = \"x"}, 3, ""});
    expect_error({"bad table header", {"[database", "number = 1"}, 1, ""});
}

TEST(DatabaseErrors, DatabaseTable) {
    expect_error({"no [database]", {"[terrain]", "kind = \"synthetic\""}, 1, "missing [database]"});
    expect_error({"[database] is not a table", {"database = 3"}, 1, "must be a table"});
    expect_error({"no number", {"", "[database]", "name = \"x\""}, 2, "missing 'number'"});
    expect_error(
        {"number 0", {"[database]", "number = 0", "name = \"x\""}, 2, "between 1 and 127"});
    expect_error(
        {"number 128", {"[database]", "name = \"x\"", "number = 128"}, 3, "between 1 and 127"});
    expect_error(
        {"number as string", {"[database]", "number = \"1\"", "name = \"x\""}, 2, "an integer"});
    expect_error({"no name", {"[database]", "number = 1"}, 1, "missing 'name'"});
    expect_error(
        {"empty name", {"[database]", "number = 1", "name = \"\""}, 3, "must not be empty"});
    expect_error({"name is a number", {"[database]", "number = 1", "name = 4"}, 3, "a string"});
    expect_error({"negative load_frames",
                  {"[database]", "number = 1", "name = \"x\"", "load_frames = -1"},
                  4,
                  "load_frames"});
    expect_error({"load_frames is a float",
                  {"[database]", "number = 1", "name = \"x\"", "load_frames = 1.5"},
                  4,
                  "an integer"});
}

TEST(DatabaseErrors, OriginAndBounds) {
    const auto base = [](const char* l4) {
        return std::vector<std::string>{"[database]", "number = 1", "name = \"x\"", l4};
    };
    expect_error({"lat too big", base("origin = { lat = 91, lon = 0 }"), 4, "'lat'"});
    expect_error({"lon too small", base("origin = { lat = 0, lon = -181 }"), 4, "'lon'"});
    expect_error({"no lon", base("origin = { lat = 0 }"), 4, "missing 'lon'"});
    expect_error({"no lat", base("origin = { lon = 0 }"), 4, "missing 'lat'"});
    expect_error({"origin is a number", base("origin = 4"), 4, "a table"});
    expect_error({"lat is a string", base("origin = { lat = \"1\", lon = 0 }"), 4, "a number"});
    expect_error({"alt too high", base("origin = { lat = 0, lon = 0, alt = 1e9 }"), 4, "'alt'"});
    expect_error({"bounds inverted",
                  base("bounds = { lat_min = 2, lat_max = 1, lon_min = 0, lon_max = 1 }"), 4,
                  "below"});
    expect_error({"bounds lon inverted",
                  base("bounds = { lat_min = 0, lat_max = 1, lon_min = 5, lon_max = 1 }"), 4,
                  "below"});
    expect_error({"bounds missing key", base("bounds = { lat_min = 0, lat_max = 1, lon_min = 5 }"),
                  4, "missing 'lon_max'"});
    expect_error({"bounds lat_min missing",
                  base("bounds = { lat_max = 1, lon_min = 5, lon_max = 6 }"), 4,
                  "missing 'lat_min'"});
    expect_error({"bounds lat_max missing",
                  base("bounds = { lat_min = 0, lon_min = 5, lon_max = 6 }"), 4,
                  "missing 'lat_max'"});
    expect_error({"bounds lon_min missing",
                  base("bounds = { lat_min = 0, lat_max = 1, lon_max = 6 }"), 4,
                  "missing 'lon_min'"});
    expect_error({"origin outside explicit bounds",
                  {"[database]", "number = 1", "name = \"x\"", "origin = { lat = 50, lon = 0 }",
                   "bounds = { lat_min = 0, lat_max = 1, lon_min = 0, lon_max = 1 }"},
                  4,
                  "outside the database bounds"});
    expect_error({"default origin outside bounds",
                  {"[database]", "number = 1", "name = \"x\"",
                   "bounds = { lat_min = 0, lat_max = 1, lon_min = 0, lon_max = 1 }"},
                  1,
                  "outside the database bounds"});
    expect_error({"bounds is a string", base("bounds = \"x\""), 4, "a table"});
}

TEST(DatabaseErrors, Terrain) {
    expect_error({"unknown kind",
                  {"[database]", "number = 1", "name = \"x\"", "[terrain]", "kind = \"voxel\""},
                  5,
                  "synthetic"});
    expect_error({"no kind",
                  {"[database]", "number = 1", "name = \"x\"", "[terrain]"},
                  4,
                  "missing 'kind'"});
    expect_error({"heightmap without file",
                  {"[database]", "number = 1", "name = \"x\"", "[terrain]", "kind = \"heightmap\""},
                  4,
                  "missing 'file'"});
    expect_error({"terrain is not a table",
                  {"terrain = 1", "[database]", "number = 1", "name = \"x\""},
                  1,
                  "a table"});
}

TEST(DatabaseErrors, Materials) {
    const auto base = [](const std::vector<std::string>& rest) { return with_head(rest); };
    EXPECT_EQ(error_of(base({"[[material]]", "name = \"w\"", "colour = \"#000000\""})).line, 4U);
    expect_error({"bad colour",
                  {"[database]", "number = 1", "name = \"x\"", "[[material]]", "code = 0",
                   "colour = \"blue\""},
                  6,
                  "#295c99"});
    expect_error({"colour too short",
                  {"[database]", "number = 1", "name = \"x\"", "[[material]]", "code = 0",
                   "colour = \"#abc\""},
                  6,
                  "colour"});
    expect_error({"colour not hex",
                  {"[database]", "number = 1", "name = \"x\"", "[[material]]", "code = 0",
                   "colour = \"#12345g\""},
                  6,
                  "colour"});
    expect_error({"no colour",
                  {"[database]", "number = 1", "name = \"x\"", "[[material]]", "code = 0"},
                  4,
                  "missing 'colour'"});
    expect_error(
        {"colour not a string",
         {"[database]", "number = 1", "name = \"x\"", "[[material]]", "code = 0", "colour = 5"},
         6,
         "a string"});
    expect_error({"negative code",
                  {"[database]", "number = 1", "name = \"x\"", "[[material]]", "code = -1",
                   "colour = \"#000000\""},
                  5,
                  "between 0"});
    expect_error({"duplicate code",
                  {"[database]", "number = 1", "name = \"x\"", "[[material]]", "code = 1",
                   "colour = \"#000000\"", "[[material]]", "code = 1", "colour = \"#ffffff\""},
                  8,
                  "already defined at line 4"});
    expect_error({"material is not an array of tables",
                  {"material = 3", "[database]", "number = 1", "name = \"x\""},
                  1,
                  "array of tables"});
}

TEST(DatabaseErrors, DefaultCategory) {
    expect_error({"unknown default",
                  {"[database]", "number = 1", "name = \"x\"", "[default]", "category = \"tank\""},
                  5,
                  "unknown category"});
    expect_error({"default is not a table",
                  {"default = 1", "[database]", "number = 1", "name = \"x\""},
                  1,
                  "a table"});
}

TEST(DatabaseErrors, EntityTypes) {
    const auto at = [](const char* line) {
        return std::vector<std::string>{"[database]", "number = 1", "name = \"x\"",
                                        "[[entity]]", line,         "category = \"other\""};
    };
    expect_error(
        {"no types",
         {"[database]", "number = 1", "name = \"x\"", "[[entity]]", "category = \"other\""},
         4,
         "missing 'types'"});
    expect_error({"types negative", at("types = -1"), 5, "between 0 and 65535"});
    expect_error({"types too big", at("types = 65536"), 5, "between 0 and 65535"});
    expect_error({"types a string", at("types = \"a\""), 5, "integer"});
    expect_error({"range reversed", at("types = [9, 3]"), 5, "starts after"});
    expect_error({"range of three", at("types = [1, 2, 3]"), 5, "exactly"});
    expect_error({"range of one", at("types = [1]"), 5, "exactly"});
    expect_error({"range with a string", at("types = [1, \"b\"]"), 5, "integer"});
    expect_error({"range with a string first", at("types = [\"a\", 2]"), 5, "integer"});
    expect_error({"range out of u16", at("types = [1, 70000]"), 5, "between 0 and 65535"});
}

TEST(DatabaseErrors, EntityOverlaps) {
    expect_error(
        {"ranges overlap",
         {"[database]", "number = 1", "name = \"x\"", "[[entity]]", "types = [1, 10]",
          "category = \"other\"", "[[entity]]", "types = [10, 20]", "category = \"other\""},
         8,
         "overlap the entry at line 4"});
    expect_error({"same exact type twice",
                  {"[database]", "number = 1", "name = \"x\"", "[[entity]]", "types = 5",
                   "category = \"other\"", "[[entity]]", "types = 5", "category = \"aircraft\""},
                  8,
                  "overlap"});
    expect_error({"range inside a range",
                  {"[database]", "number = 1", "name = \"x\"", "[[entity]]", "types = [1, 100]",
                   "category = \"other\"", "[[entity]]", "types = [5, 6]", "category = \"other\""},
                  8,
                  "overlap"});
    // an exact type inside a range is the "exception" form and is fine
    EXPECT_TRUE(
        parse(toml({"[database]", "number = 1", "name = \"x\"", "[[entity]]", "types = [1, 100]",
                    "category = \"other\"", "[[entity]]", "types = 5", "category = \"sea\""})));
    // adjacent ranges are fine
    EXPECT_TRUE(parse(
        toml({"[database]", "number = 1", "name = \"x\"", "[[entity]]", "types = [1, 10]",
              "category = \"other\"", "[[entity]]", "types = [11, 20]", "category = \"sea\""})));
}

TEST(DatabaseErrors, EntityFields) {
    const auto with = [](const char* line) {
        return std::vector<std::string>{"[database]", "number = 1", "name = \"x\"",
                                        "[[entity]]", "types = 1",  "category = \"other\"",
                                        line};
    };
    expect_error({"entity is not an array of tables",
                  {"entity = 3", "[database]", "number = 1", "name = \"x\""},
                  1,
                  "array of tables"});
    expect_error({"no category",
                  {"[database]", "number = 1", "name = \"x\"", "[[entity]]", "types = 1"},
                  4,
                  "missing 'category'"});
    expect_error({"unknown category",
                  {"[database]", "number = 1", "name = \"x\"", "[[entity]]", "types = 1",
                   "category = \"tank\""},
                  6,
                  "unknown category"});
    expect_error({"model and lods",
                  {"[database]", "number = 1", "name = \"x\"", "[[entity]]", "types = 1",
                   "category = \"other\"", "model = \"a.glb\"", "lods = [\"b.glb\"]"},
                  8,
                  "either"});
    expect_error({"empty lods", with("lods = []"), 7, "non-empty"});
    expect_error({"lods not an array", with("lods = \"a.glb\""), 7, "non-empty array"});
    expect_error({"lods with a number", with("lods = [\"a.glb\", 3]"), 7, "file names"});
    expect_error({"model not a string", with("model = 4"), 7, "a string"});
    expect_error({"scale zero", with("scale = 0"), 7, "'scale'"});
    expect_error({"scale a string", with("scale = \"2\""), 7, "a number"});
    expect_error({"offset of two", with("offset = [1, 2]"), 7, "3 numbers"});
    expect_error({"offset not numbers", with("offset = [1, 2, \"z\"]"), 7, "finite numbers"});
    expect_error({"offset not an array", with("offset = 1"), 7, "array of numbers"});
    expect_error({"rotation bad", with("rotation = [1, 2]"), 7, "3 numbers"});
    expect_error({"attached_view bad", with("attached_view = \"tail\""), 7, "cockpit"});
    expect_error({"eyepoint_front bad", with("eyepoint_front = 4"), 7, "a string"});
    expect_error({"eyepoint_rear bad", with("eyepoint_rear = 4"), 7, "a string"});
}

TEST(DatabaseErrors, LodThresholds) {
    const auto with = [](const char* a, const char* b = "") {
        return std::vector<std::string>{"[database]",
                                        "number = 1",
                                        "name = \"x\"",
                                        "[[entity]]",
                                        "types = 1",
                                        "category = \"other\"",
                                        a,
                                        b};
    };
    expect_error({"ascending", with("lod_thresholds = [0.1, 0.2]"), 7, "descending"});
    expect_error({"equal", with("lod_thresholds = [0.2, 0.2]"), 7, "descending"});
    expect_error({"zero", with("lod_thresholds = [0.2, 0]"), 7, "descending"});
    expect_error({"above one", with("lod_thresholds = [1.5]"), 7, "descending"});
    expect_error({"empty", with("lod_thresholds = []"), 7, "1 to 8"});
    expect_error({"cull above the smallest",
                  with("lod_thresholds = [0.2, 0.05]", "cull_below = 0.05"), 8, "smaller"});
    expect_error(
        {"cull default above a tiny threshold", with("lod_thresholds = [0.0005]"), 7, "smaller"});
    expect_error({"cull out of range", with("cull_below = 2"), 7, "'cull_below'"});
    expect_error({"too many lods",
                  {"[database]", "number = 1", "name = \"x\"", "[[entity]]", "types = 1",
                   "category = \"other\"", R"(lods = ["a", "b", "c", "d", "e"])"},
                  7,
                  "need 4"});
}

TEST(DatabaseErrors, PartsAndComponents) {
    const auto e = [](const std::vector<std::string>& tail) {
        return toml({"[database]", "number = 1", "name = \"x\"", "[[entity]]", "types = 1",
                     "category = \"other\""}) +
               toml(tail);
    };
    EXPECT_EQ(error_of(e({"[[entity.part]]", "node = \"a\""})).line, 7U);
    EXPECT_EQ(error_of(e({"[[entity.part]]", "id = 256", "node = \"a\""})).line, 8U);
    EXPECT_EQ(error_of(e({"[[entity.part]]", "id = 1"})).line, 7U);
    EXPECT_EQ(error_of(e({"[[entity.part]]", "id = 1", "node = \"a\"", "axis = \"w\""})).line, 10U);
    EXPECT_EQ(error_of(e({"[[entity.part]]", "id = 1", "node = \"a\"", "axis = 3"})).line, 10U);
    EXPECT_EQ(error_of(e({"[[entity.part]]", "id = 1", "node = \"a\"", "[[entity.part]]", "id = 1",
                          "node = \"b\""}))
                  .line,
              11U);
    EXPECT_EQ(error_of(e({"[[entity.component]]", "node = \"a\""})).line, 7U);
    EXPECT_EQ(error_of(e({"[[entity.component]]", "id = 70000", "node = \"a\""})).line, 8U);
    EXPECT_EQ(error_of(e({"[[entity.component]]", "id = 1"})).line, 7U);
    EXPECT_EQ(
        error_of(e({"[[entity.component]]", "id = 1", "node = \"a\"", "kind = \"spin\""})).line,
        10U);
    EXPECT_EQ(error_of(e({"[[entity.component]]", "id = 1", "node = \"a\"", "kind = 2"})).line,
              10U);
    EXPECT_EQ(error_of(e({"[[entity.component]]", "id = 1", "node = \"a\"", "[[entity.component]]",
                          "id = 1", "node = \"b\""}))
                  .line,
              11U);
    const auto db =
        parse(e({"[[entity.component]]", "id = 1", "node = \"a\"", "kind = \"translation\""}));
    ASSERT_TRUE(db);
    EXPECT_EQ(db->entities[0].components[0].kind, ComponentKind::Translation);
    EXPECT_EQ(to_string(ComponentKind::Translation), "translation");
}

TEST(DatabaseErrors, WrongTypesAreNamedInTheMessage) {
    const auto head3 = std::vector<std::string>{"[database]", "name = \"x\""};
    const auto with = [&head3](const char* line) {
        auto lines = head3;
        lines.emplace_back(line);
        return toml(lines);
    };
    EXPECT_NE(error_of(with("number = true")).message.find("not boolean"), std::string::npos);
    EXPECT_NE(error_of(with("number = [1]")).message.find("not array"), std::string::npos);
    EXPECT_NE(error_of(with("number = { a = 1 }")).message.find("not table"), std::string::npos);
    EXPECT_NE(error_of(with("number = 1979-05-27")).message.find("not date or time"),
              std::string::npos);
    EXPECT_NE(error_of(with("number = 1.5")).message.find("not float"), std::string::npos);
}

TEST(DatabaseErrors, MoreFieldTypes) {
    const auto entity = [](const char* line) {
        return toml({"[database]", "number = 1", "name = \"x\"", "[[entity]]", "types = 1",
                     "category = \"other\"", line});
    };
    EXPECT_EQ(error_of(entity("name = 5")).line, 7U);
    EXPECT_EQ(error_of(entity("attached_view = 5")).line, 7U);
    EXPECT_EQ(error_of(entity("part = 3")).line, 7U);
    EXPECT_EQ(error_of(entity("component = 3")).line, 7U);
    EXPECT_EQ(
        error_of(toml({"[database]", "number = 1", "name = \"x\"", "[default]", "category = 5"}))
            .line,
        5U);
    EXPECT_EQ(error_of(toml({"[database]", "number = 1", "name = \"x\"", "[[material]]", "code = 1",
                             "name = 5", "colour = \"#000000\""}))
                  .line,
              6U);
}

TEST(DatabaseLoader, ExplicitEnumValuesAndNames) {
    const auto db =
        parse(toml({"[database]", "number = 1", "name = \"x\"", "[[entity]]", "types = 1",
                    "category = \"other\"", "attached_view = \"cockpit\"", "[[entity.component]]",
                    "id = 1", "node = \"n\"", "kind = \"visibility\"", "[[entity]]", "types = 2",
                    "category = \"other\"", "attached_view = \"full\""}));
    ASSERT_TRUE(db) << to_string(db.error());
    EXPECT_EQ(db->entities[0].attached_view, AttachedView::Cockpit);
    EXPECT_EQ(db->entities[1].attached_view, AttachedView::Full);
    EXPECT_EQ(db->entities[0].components[0].kind, ComponentKind::Visibility);
    EXPECT_EQ(to_string(AttachedView::Cockpit), "cockpit");
    EXPECT_EQ(to_string(AttachedView::Full), "full");
    EXPECT_EQ(to_string(AttachedView::Hidden), "hidden");
    EXPECT_EQ(to_string(ComponentKind::Visibility), "visibility");
    EXPECT_EQ(to_string(ComponentKind::Rotation), "rotation");
    EXPECT_EQ(to_string(EntityCategory::Other), "other");
    EXPECT_EQ(to_string(static_cast<EntityCategory>(200)), "other"); // out-of-range value
    EXPECT_EQ(to_string(static_cast<AttachedView>(200)), "cockpit");
    EXPECT_EQ(to_string(static_cast<ComponentKind>(200)), "visibility");
}

TEST(DatabaseLoader, AFolderIsNotAManifest) {
    const test::TempDir dir;
    const auto result = load_database(dir.path());
    // Cannot open or cannot read (platform-dependent), or reads as empty: an error either way.
    EXPECT_FALSE(result);
}

TEST(DatabaseErrors, DiagnosticFormatting) {
    EXPECT_EQ(to_string(Diagnostic{"f.toml", 3, 7, "bad"}), "f.toml:3:7: bad");
    EXPECT_EQ(to_string(Diagnostic{"f.toml", 3, 0, "bad"}), "f.toml:3: bad");
    EXPECT_EQ(to_string(Diagnostic{"f.toml", 0, 0, "bad"}), "f.toml: bad");
    EXPECT_EQ(to_string(Diagnostic{"", 0, 0, "bad"}), "bad");
}

// ---- warnings
// ---------------------------------------------------------------------------------------

TEST(DatabaseWarnings, UnknownKeysAreCollectedNotFatal) {
    const auto db = parse(toml({
        "future = 1",                              // 1
        "[database]",                              // 2
        "number = 1",                              // 3
        "name = \"x\"",                            // 4
        "colour = \"red\"",                        // 5
        "origin = { lat = 1, lon = 2, zoom = 3 }", // 6
        "[terrain]",                               // 7
        "kind = \"synthetic\"",                    // 8
        "seed = 4",                                // 9
        "[default]",                               // 10
        "category = \"other\"",                    // 11
        "tint = 1",                                // 12
        "[[material]]",                            // 13
        "code = 0",                                // 14
        "colour = \"#000000\"",                    // 15
        "shine = 1",                               // 16
        "[[entity]]",                              // 17
        "types = 1",                               // 18
        "category = \"other\"",                    // 19
        "armour = 3",                              // 20
        "[[entity.part]]",                         // 21
        "id = 1",                                  // 22
        "node = \"n\"",                            // 23
        "speed = 1",                               // 24
        "[[entity.component]]",                    // 25
        "id = 1",                                  // 26
        "node = \"n\"",                            // 27
        "speed = 1",                               // 28
    }));
    ASSERT_TRUE(db) << to_string(db.error());
    std::vector<std::uint32_t> lines;
    for (const auto& w : db->warnings) {
        lines.push_back(w.line);
        EXPECT_NE(w.message.find("unknown key"), std::string::npos) << to_string(w);
        EXPECT_GT(w.column, 0U);
        EXPECT_EQ(w.file, fake_manifest().string());
    }
    std::ranges::sort(lines);
    EXPECT_EQ(lines, (std::vector<std::uint32_t>{1, 5, 6, 9, 12, 16, 20, 24, 28}));
}

TEST(DatabaseWarnings, MissingFilesAreWarnings) {
    test::TempDir dir;
    dir.write("here.glb", "x");
    const auto file = dir.write("db/database.toml", "");
    std::filesystem::copy_file(dir.path() / "here.glb", dir.path() / "db" / "here.glb");
    const auto db = parse_database(
        toml({"[database]", "number = 1", "name = \"x\"", "[terrain]", "kind = \"heightmap\"",
              "file = \"missing.png\"", "[[entity]]", "types = 1", "category = \"other\"",
              R"(lods = ["here.glb", "gone.glb"])", "[[entity]]", "types = 2",
              "category = \"other\"", "model = \"also_gone.glb\""}),
        file);
    ASSERT_TRUE(db) << to_string(db.error());
    ASSERT_EQ(db->warnings.size(), 3U);
    EXPECT_EQ(db->warnings[0].line, 6U);
    EXPECT_NE(db->warnings[0].message.find("missing.png"), std::string::npos);
    EXPECT_EQ(db->warnings[1].line, 10U);
    EXPECT_NE(db->warnings[1].message.find("gone.glb"), std::string::npos);
    EXPECT_EQ(db->warnings[2].line, 14U);
    // the file that exists is not reported
    for (const auto& w : db->warnings) {
        EXPECT_EQ(w.message.find("'here.glb'"), std::string::npos);
    }
}

} // namespace
