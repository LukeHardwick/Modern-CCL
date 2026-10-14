// Reads database.toml (toml++, built without exceptions) into a Database. The format is described
// field by field in docs/guide/databases.md. Every error carries the line and column of the
// offending value; unknown keys are warnings so newer manifests load in older tools.

#include "mock_ig/database.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <toml++/toml.hpp>

namespace cigi::mock {

namespace {

template <typename T> using Res = std::expected<T, DbError>;

constexpr std::int64_t max_u16 = std::numeric_limits<std::uint16_t>::max();
constexpr std::int64_t max_u32 = std::numeric_limits<std::uint32_t>::max();

struct Ctx {
    std::string file;
    std::filesystem::path root;
    std::vector<DbWarning>* warnings = nullptr;

    [[nodiscard]] DbError error_at(const toml::source_position& p, std::string message) const {
        return DbError{file, p.line, p.column, std::move(message)};
    }
    [[nodiscard]] std::unexpected<DbError> fail(const toml::node& at, std::string message) const {
        return std::unexpected(error_at(at.source().begin, std::move(message)));
    }
    void warn(const toml::source_position& p, std::string message) const {
        warnings->push_back(DbWarning{file, p.line, p.column, std::move(message)});
    }
};

std::string_view type_name(const toml::node& n) noexcept {
    switch (n.type()) {
    case toml::node_type::table:
        return "table";
    case toml::node_type::array:
        return "array";
    case toml::node_type::string:
        return "string";
    case toml::node_type::integer:
        return "integer";
    case toml::node_type::floating_point:
        return "float";
    case toml::node_type::boolean:
        return "boolean";
    default:
        return "date or time";
    }
}

std::unexpected<DbError> wrong_type(const Ctx& c, const toml::node& n, std::string_view key,
                                    std::string_view wanted) {
    return c.fail(n, std::format("'{}' must be {}, not {}", key, wanted, type_name(n)));
}

std::unexpected<DbError> missing(const Ctx& c, const toml::table& in, std::string_view key,
                                 std::string_view where) {
    return c.fail(in, std::format("missing '{}' in {}", key, where));
}

void warn_unknown(const Ctx& c, const toml::table& t, std::initializer_list<std::string_view> known,
                  std::string_view where) {
    for (const auto& [key, value] : t) {
        if (std::ranges::find(known, key.str()) == known.end()) {
            c.warn(key.source().begin,
                   std::format("unknown key '{}' in {} (ignored)", key.str(), where));
        }
    }
}

// ---- scalar getters ---------------------------------------------------------------------------

Res<std::optional<std::int64_t>> opt_int(const Ctx& c, const toml::table& t, std::string_view key,
                                         std::int64_t lo, std::int64_t hi) {
    const toml::node* n = t.get(key);
    if (n == nullptr) {
        return std::nullopt;
    }
    const auto* i = n->as_integer();
    if (i == nullptr) {
        return wrong_type(c, *n, key, "an integer");
    }
    if (i->get() < lo || i->get() > hi) {
        return c.fail(*n,
                      std::format("'{}' must be between {} and {}, not {}", key, lo, hi, i->get()));
    }
    return i->get();
}

Res<std::int64_t> req_int(const Ctx& c, const toml::table& t, std::string_view key, std::int64_t lo,
                          std::int64_t hi, std::string_view where) {
    auto v = opt_int(c, t, key, lo, hi);
    if (!v) {
        return std::unexpected(v.error());
    }
    if (!*v) {
        return missing(c, t, key, where);
    }
    return **v;
}

Res<std::optional<double>> opt_double(const Ctx& c, const toml::table& t, std::string_view key,
                                      double lo, double hi) {
    const toml::node* n = t.get(key);
    if (n == nullptr) {
        return std::nullopt;
    }
    double v = 0;
    if (const auto* d = n->as_floating_point()) {
        v = d->get();
    } else if (const auto* i = n->as_integer()) {
        v = static_cast<double>(i->get());
    } else {
        return wrong_type(c, *n, key, "a number");
    }
    if (!std::isfinite(v) || v < lo || v > hi) {
        return c.fail(*n, std::format("'{}' must be between {} and {}, not {}", key, lo, hi, v));
    }
    return v;
}

Res<double> req_double(const Ctx& c, const toml::table& t, std::string_view key, double lo,
                       double hi, std::string_view where) {
    auto v = opt_double(c, t, key, lo, hi);
    if (!v) {
        return std::unexpected(v.error());
    }
    if (!*v) {
        return missing(c, t, key, where);
    }
    return **v;
}

Res<std::optional<std::string>> opt_string(const Ctx& c, const toml::table& t,
                                           std::string_view key) {
    const toml::node* n = t.get(key);
    if (n == nullptr) {
        return std::nullopt;
    }
    const auto* s = n->as_string();
    if (s == nullptr) {
        return wrong_type(c, *n, key, "a string");
    }
    if (s->get().empty()) {
        return c.fail(*n, std::format("'{}' must not be empty", key));
    }
    return s->get();
}

Res<std::string> req_string(const Ctx& c, const toml::table& t, std::string_view key,
                            std::string_view where) {
    auto v = opt_string(c, t, key);
    if (!v) {
        return std::unexpected(v.error());
    }
    if (!*v) {
        return missing(c, t, key, where);
    }
    return std::move(**v);
}

/// A sub-table (inline or `[header]`); absent is fine, a non-table is an error.
Res<const toml::table*> opt_table(const Ctx& c, const toml::table& t, std::string_view key) {
    const toml::node* n = t.get(key);
    if (n == nullptr) {
        return nullptr;
    }
    const auto* sub = n->as_table();
    if (sub == nullptr) {
        return wrong_type(c, *n, key, "a table");
    }
    return sub;
}

/// `[[key]]` tables; absent gives an empty list.
Res<std::vector<const toml::table*>> table_array(const Ctx& c, const toml::table& t,
                                                 std::string_view key) {
    std::vector<const toml::table*> out;
    const toml::node* n = t.get(key);
    if (n == nullptr) {
        return out;
    }
    const auto* arr = n->as_array();
    if (arr == nullptr || !arr->is_homogeneous(toml::node_type::table)) {
        return c.fail(*n, std::format("'{}' must be an array of tables ([[{}]])", key, key));
    }
    for (const auto& el : *arr) {
        out.push_back(el.as_table());
    }
    return out;
}

Res<std::optional<std::vector<double>>> opt_numbers(const Ctx& c, const toml::table& t,
                                                    std::string_view key, std::size_t count_min,
                                                    std::size_t count_max) {
    const toml::node* n = t.get(key);
    if (n == nullptr) {
        return std::nullopt;
    }
    const auto* arr = n->as_array();
    if (arr == nullptr) {
        return wrong_type(c, *n, key, "an array of numbers");
    }
    if (arr->size() < count_min || arr->size() > count_max) {
        return c.fail(
            *n, count_min == count_max
                    ? std::format("'{}' must have {} numbers, not {}", key, count_min, arr->size())
                    : std::format("'{}' must have {} to {} numbers, not {}", key, count_min,
                                  count_max, arr->size()));
    }
    std::vector<double> out;
    for (const auto& el : *arr) {
        const auto v = el.value<double>();
        if (!el.is_number() || !v || !std::isfinite(*v)) {
            return c.fail(el, std::format("'{}' must contain only finite numbers", key));
        }
        out.push_back(*v);
    }
    return out;
}

Res<std::optional<Vec3d>> opt_vec3(const Ctx& c, const toml::table& t, std::string_view key) {
    auto v = opt_numbers(c, t, key, 3, 3);
    if (!v) {
        return std::unexpected(v.error());
    }
    if (!*v) {
        return std::nullopt;
    }
    return Vec3d{(**v)[0], (**v)[1], (**v)[2]};
}

// ---- sections ---------------------------------------------------------------------------------

Res<void> parse_database_table(const Ctx& c, const toml::table& root, Database& db) {
    auto section = opt_table(c, root, "database");
    if (!section) {
        return std::unexpected(section.error());
    }
    if (*section == nullptr) {
        return std::unexpected(c.error_at(root.source().begin, "missing [database] table"));
    }
    const toml::table& t = **section;
    warn_unknown(c, t, {"number", "name", "origin", "bounds", "load_frames"}, "[database]");

    auto number = req_int(c, t, "number", 1, 127, "[database]");
    if (!number) {
        return std::unexpected(number.error());
    }
    db.number = static_cast<int>(*number);
    db.number_line = t.get("number")->source().begin.line;

    auto name = req_string(c, t, "name", "[database]");
    if (!name) {
        return std::unexpected(name.error());
    }
    db.name = std::move(*name);

    auto frames = opt_int(c, t, "load_frames", 0, max_u16);
    if (!frames) {
        return std::unexpected(frames.error());
    }
    if (*frames) {
        db.load_frames = static_cast<std::uint32_t>(**frames);
    }

    auto origin = opt_table(c, t, "origin");
    if (!origin) {
        return std::unexpected(origin.error());
    }
    if (*origin != nullptr) {
        const toml::table& o = **origin;
        warn_unknown(c, o, {"lat", "lon", "alt"}, "origin");
        auto lat = req_double(c, o, "lat", -90.0, 90.0, "origin");
        if (!lat) {
            return std::unexpected(lat.error());
        }
        auto lon = req_double(c, o, "lon", -180.0, 180.0, "origin");
        if (!lon) {
            return std::unexpected(lon.error());
        }
        auto alt = opt_double(c, o, "alt", -1.0e4, 1.0e5);
        if (!alt) {
            return std::unexpected(alt.error());
        }
        db.origin = {*lat, *lon, alt->value_or(0.0)};
    }

    auto bounds = opt_table(c, t, "bounds");
    if (!bounds) {
        return std::unexpected(bounds.error());
    }
    if (*bounds != nullptr) {
        const toml::table& b = **bounds;
        warn_unknown(c, b, {"lat_min", "lat_max", "lon_min", "lon_max"}, "bounds");
        auto lat_min = req_double(c, b, "lat_min", -90.0, 90.0, "bounds");
        if (!lat_min) {
            return std::unexpected(lat_min.error());
        }
        auto lat_max = req_double(c, b, "lat_max", -90.0, 90.0, "bounds");
        if (!lat_max) {
            return std::unexpected(lat_max.error());
        }
        auto lon_min = req_double(c, b, "lon_min", -180.0, 180.0, "bounds");
        if (!lon_min) {
            return std::unexpected(lon_min.error());
        }
        auto lon_max = req_double(c, b, "lon_max", -180.0, 180.0, "bounds");
        if (!lon_max) {
            return std::unexpected(lon_max.error());
        }
        if (*lat_min >= *lat_max || *lon_min >= *lon_max) {
            return std::unexpected(c.error_at(
                b.source().begin, "bounds: lat_min/lon_min must be below lat_max/lon_max"));
        }
        db.bounds = {*lat_min, *lat_max, *lon_min, *lon_max};
    } else {
        db.bounds = DatabaseBounds::around(db.origin, 0.25);
    }
    if (!db.bounds.contains(db.origin.lat_deg, db.origin.lon_deg)) {
        return std::unexpected(
            c.error_at((*origin != nullptr ? (*origin)->source() : t.source()).begin,
                       "origin lies outside the database bounds"));
    }
    return {};
}

Res<void> parse_terrain_table(const Ctx& c, const toml::table& root, Database& db) {
    auto section = opt_table(c, root, "terrain");
    if (!section) {
        return std::unexpected(section.error());
    }
    if (*section == nullptr) {
        return {};
    }
    const toml::table& t = **section;
    warn_unknown(c, t, {"kind", "file"}, "[terrain]");
    auto kind = req_string(c, t, "kind", "[terrain]");
    if (!kind) {
        return std::unexpected(kind.error());
    }
    if (*kind == "synthetic") {
        db.terrain.kind = TerrainSpec::Kind::Synthetic;
    } else if (*kind == "heightmap") {
        db.terrain.kind = TerrainSpec::Kind::Heightmap;
        auto file = req_string(c, t, "file", "[terrain] (kind = \"heightmap\")");
        if (!file) {
            return std::unexpected(file.error());
        }
        db.terrain.file = std::move(*file);
        std::error_code ec;
        if (!std::filesystem::exists(c.root / db.terrain.file, ec)) {
            c.warn(t.get("file")->source().begin,
                   std::format("terrain file '{}' not found", db.terrain.file));
        }
    } else {
        return c.fail(
            *t.get("kind"),
            std::format("terrain kind must be \"synthetic\" or \"heightmap\", not \"{}\"", *kind));
    }
    return {};
}

Res<std::optional<Rgb8>> parse_colour(const Ctx& c, const toml::table& t, std::string_view key) {
    auto text = opt_string(c, t, key);
    if (!text) {
        return std::unexpected(text.error());
    }
    if (!*text) {
        return std::nullopt;
    }
    const std::string& s = **text;
    const bool shape =
        s.size() == 7 && s[0] == '#' && std::ranges::all_of(s.begin() + 1, s.end(), [](char ch) {
            return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
        });
    if (!shape) {
        return c.fail(*t.get(key),
                      std::format("'{}' must be a colour like \"#295c99\", not \"{}\"", key, s));
    }
    const auto byte = [&s](std::size_t at) {
        unsigned value = 0;
        std::from_chars(s.data() + at, s.data() + at + 2, value, 16); // validated above
        return static_cast<std::uint8_t>(value);
    };
    return Rgb8{byte(1), byte(3), byte(5)};
}

Res<void> parse_materials(const Ctx& c, const toml::table& root, Database& db) {
    auto tables = table_array(c, root, "material");
    if (!tables) {
        return std::unexpected(tables.error());
    }
    for (const toml::table* t : *tables) {
        warn_unknown(c, *t, {"code", "name", "colour"}, "[[material]]");
        MaterialEntry m;
        m.line = t->source().begin.line;
        auto code = req_int(c, *t, "code", 0, max_u32, "[[material]]");
        if (!code) {
            return std::unexpected(code.error());
        }
        m.code = static_cast<std::uint32_t>(*code);
        auto name = opt_string(c, *t, "name");
        if (!name) {
            return std::unexpected(name.error());
        }
        m.name = name->value_or(std::format("material {}", m.code));
        auto colour = parse_colour(c, *t, "colour");
        if (!colour) {
            return std::unexpected(colour.error());
        }
        if (!*colour) {
            return missing(c, *t, "colour", "[[material]]");
        }
        m.colour = **colour;
        if (const MaterialEntry* prior = db.material(m.code)) {
            return c.fail(
                *t->get("code"),
                std::format("material code {} is already defined at line {}", m.code, prior->line));
        }
        db.materials.push_back(std::move(m));
    }
    return {};
}

Res<void> parse_default_table(const Ctx& c, const toml::table& root, Database& db) {
    auto section = opt_table(c, root, "default");
    if (!section) {
        return std::unexpected(section.error());
    }
    if (*section == nullptr) {
        return {};
    }
    warn_unknown(c, **section, {"category"}, "[default]");
    auto category = opt_string(c, **section, "category");
    if (!category) {
        return std::unexpected(category.error());
    }
    if (*category) {
        const auto parsed = parse_entity_category(**category);
        if (!parsed) {
            return c.fail(*(*section)->get("category"),
                          std::format("unknown category \"{}\"", **category));
        }
        db.default_category = *parsed;
    }
    return {};
}

/// `types = 100` or `types = [100, 199]` (inclusive).
Res<std::pair<std::uint16_t, std::uint16_t>> parse_types(const Ctx& c, const toml::table& t) {
    const toml::node* n = t.get("types");
    if (n == nullptr) {
        return missing(c, t, "types", "[[entity]]");
    }
    const auto in_range = [&c](const toml::node& v) -> Res<std::uint16_t> {
        const auto* i = v.as_integer();
        if (i == nullptr) {
            return c.fail(v, "'types' must be an integer or a [first, last] pair of integers");
        }
        if (i->get() < 0 || i->get() > max_u16) {
            return c.fail(
                v, std::format("'types' values must be between 0 and 65535, not {}", i->get()));
        }
        return static_cast<std::uint16_t>(i->get());
    };
    if (const auto* arr = n->as_array()) {
        if (arr->size() != 2) {
            return c.fail(*n,
                          std::format("'types' range needs exactly [first, last], not {} values",
                                      arr->size()));
        }
        const auto first = in_range(*arr->get(0));
        if (!first) {
            return std::unexpected(first.error());
        }
        const auto last = in_range(*arr->get(1));
        if (!last) {
            return std::unexpected(last.error());
        }
        if (*first > *last) {
            return c.fail(
                *n, std::format("'types' range [{}, {}] starts after it ends", *first, *last));
        }
        return std::pair{*first, *last};
    }
    const auto single = in_range(*n);
    if (!single) {
        return std::unexpected(single.error());
    }
    return std::pair{*single, *single};
}

Res<void> parse_parts(const Ctx& c, const toml::table& t, EntityModel& e) {
    auto tables = table_array(c, t, "part");
    if (!tables) {
        return std::unexpected(tables.error());
    }
    for (const toml::table* p : *tables) {
        warn_unknown(c, *p, {"id", "node", "axis"}, "[[entity.part]]");
        PartMapping m;
        auto id = req_int(c, *p, "id", 0, 255, "[[entity.part]]");
        if (!id) {
            return std::unexpected(id.error());
        }
        m.id = static_cast<std::uint8_t>(*id);
        auto node = req_string(c, *p, "node", "[[entity.part]]");
        if (!node) {
            return std::unexpected(node.error());
        }
        m.node = std::move(*node);
        auto axis = opt_string(c, *p, "axis");
        if (!axis) {
            return std::unexpected(axis.error());
        }
        if (*axis) {
            if (**axis != "x" && **axis != "y" && **axis != "z") {
                return c.fail(
                    *p->get("axis"),
                    std::format("part axis must be \"x\", \"y\" or \"z\", not \"{}\"", **axis));
            }
            m.axis = std::move(**axis);
        }
        const auto dup = std::ranges::find(e.parts, m.id, &PartMapping::id);
        if (dup != e.parts.end()) {
            return c.fail(*p->get("id"),
                          std::format("part id {} is mapped twice in this entity", m.id));
        }
        e.parts.push_back(std::move(m));
    }
    return {};
}

Res<void> parse_components(const Ctx& c, const toml::table& t, EntityModel& e) {
    auto tables = table_array(c, t, "component");
    if (!tables) {
        return std::unexpected(tables.error());
    }
    for (const toml::table* p : *tables) {
        warn_unknown(c, *p, {"id", "node", "kind"}, "[[entity.component]]");
        ComponentMapping m;
        auto id = req_int(c, *p, "id", 0, max_u16, "[[entity.component]]");
        if (!id) {
            return std::unexpected(id.error());
        }
        m.id = static_cast<std::uint16_t>(*id);
        auto node = req_string(c, *p, "node", "[[entity.component]]");
        if (!node) {
            return std::unexpected(node.error());
        }
        m.node = std::move(*node);
        auto kind = opt_string(c, *p, "kind");
        if (!kind) {
            return std::unexpected(kind.error());
        }
        if (*kind) {
            if (**kind == "visibility") {
                m.kind = ComponentKind::Visibility;
            } else if (**kind == "rotation") {
                m.kind = ComponentKind::Rotation;
            } else if (**kind == "translation") {
                m.kind = ComponentKind::Translation;
            } else {
                return c.fail(*p->get("kind"),
                              std::format("component kind must be \"visibility\", \"rotation\" or "
                                          "\"translation\", not \"{}\"",
                                          **kind));
            }
        }
        const auto dup = std::ranges::find(e.components, m.id, &ComponentMapping::id);
        if (dup != e.components.end()) {
            return c.fail(*p->get("id"),
                          std::format("component id {} is mapped twice in this entity", m.id));
        }
        e.components.push_back(std::move(m));
    }
    return {};
}

Res<void> parse_models(const Ctx& c, const toml::table& t, EntityModel& e) {
    const toml::node* model_node = t.get("model");
    const toml::node* lods_node = t.get("lods");
    if (model_node != nullptr && lods_node != nullptr) {
        return c.fail(*lods_node, "use either 'model' or 'lods', not both");
    }
    auto model = opt_string(c, t, "model");
    if (!model) {
        return std::unexpected(model.error());
    }
    if (*model) {
        e.lods.push_back(std::move(**model));
    }
    if (lods_node != nullptr) {
        const auto* arr = lods_node->as_array();
        if (arr == nullptr || arr->empty()) {
            return c.fail(*lods_node, "'lods' must be a non-empty array of file names");
        }
        for (const auto& el : *arr) {
            const auto* s = el.as_string();
            if (s == nullptr || s->get().empty()) {
                return c.fail(el, "'lods' must contain only file names (strings)");
            }
            e.lods.push_back(s->get());
        }
    }
    const toml::node* where = model_node != nullptr ? model_node : lods_node;
    for (std::size_t i = 0; i < e.lods.size(); ++i) {
        std::error_code ec;
        if (!c.root.empty() && !std::filesystem::exists(c.root / e.lods[i], ec)) {
            c.warn(where->source().begin,
                   std::format(
                       "model file '{}' not found (the viewer falls back to the category shape)",
                       e.lods[i]));
        }
    }
    return {};
}

Res<void> parse_lod_thresholds(const Ctx& c, const toml::table& t, EntityModel& e) {
    auto thresholds = opt_numbers(c, t, "lod_thresholds", 1, 8);
    if (!thresholds) {
        return std::unexpected(thresholds.error());
    }
    if (*thresholds) {
        const toml::node& at = *t.get("lod_thresholds");
        double previous = 2.0;
        for (const double v : **thresholds) {
            if (v <= 0.0 || v > 1.0 || v >= previous) {
                return c.fail(at, "'lod_thresholds' must be fractions of the screen height, above "
                                  "0, up to 1 and strictly descending");
            }
            previous = v;
        }
        e.lod_screen_heights = std::move(**thresholds);
    }
    auto cull = opt_double(c, t, "cull_below", 0.0, 1.0);
    if (!cull) {
        return std::unexpected(cull.error());
    }
    if (*cull) {
        e.cull_screen_height = **cull;
    }
    if (!e.lod_screen_heights.empty() && e.cull_screen_height >= e.lod_screen_heights.back()) {
        return c.fail(*(cull->has_value() ? t.get("cull_below") : t.get("lod_thresholds")),
                      "'cull_below' must be smaller than the smallest LOD threshold");
    }
    if (e.lods.size() > e.lod_screen_heights.size() + 1) {
        return c.fail(*(t.get("lods") != nullptr ? t.get("lods") : t.get("model")),
                      std::format("{} LODs need {} 'lod_thresholds', not {}", e.lods.size(),
                                  e.lods.size() - 1, e.lod_screen_heights.size()));
    }
    return {};
}

Res<EntityModel> parse_entity(const Ctx& c, const toml::table& t) {
    warn_unknown(c, t,
                 {"types", "name", "category", "model", "lods", "scale", "offset", "rotation",
                  "attached_view", "eyepoint_front", "eyepoint_rear", "lod_thresholds",
                  "cull_below", "part", "component"},
                 "[[entity]]");
    EntityModel e;
    e.line = t.source().begin.line;

    auto types = parse_types(c, t);
    if (!types) {
        return std::unexpected(types.error());
    }
    e.first_type = types->first;
    e.last_type = types->second;

    auto category = req_string(c, t, "category", "[[entity]]");
    if (!category) {
        return std::unexpected(category.error());
    }
    const auto parsed = parse_entity_category(*category);
    if (!parsed) {
        return c.fail(*t.get("category"),
                      std::format("unknown category \"{}\" (aircraft, ground_vehicle, sea, "
                                  "lifeform, munition, other or none)",
                                  *category));
    }
    e.category = *parsed;

    auto name = opt_string(c, t, "name");
    if (!name) {
        return std::unexpected(name.error());
    }
    e.name = name->value_or(std::format("{} {}", to_string(e.category), e.first_type));

    if (auto r = parse_models(c, t, e); !r) {
        return std::unexpected(r.error());
    }

    auto scale = opt_double(c, t, "scale", 1.0e-6, 1.0e6);
    if (!scale) {
        return std::unexpected(scale.error());
    }
    e.scale = scale->value_or(1.0);
    auto offset = opt_vec3(c, t, "offset");
    if (!offset) {
        return std::unexpected(offset.error());
    }
    e.offset = offset->value_or(Vec3d{});
    auto rotation = opt_vec3(c, t, "rotation");
    if (!rotation) {
        return std::unexpected(rotation.error());
    }
    if (*rotation) {
        e.rotation = {(**rotation).x, (**rotation).y, (**rotation).z};
    }

    auto view = opt_string(c, t, "attached_view");
    if (!view) {
        return std::unexpected(view.error());
    }
    if (*view) {
        if (**view == "cockpit") {
            e.attached_view = AttachedView::Cockpit;
        } else if (**view == "full") {
            e.attached_view = AttachedView::Full;
        } else if (**view == "hidden") {
            e.attached_view = AttachedView::Hidden;
        } else {
            return c.fail(
                *t.get("attached_view"),
                std::format("attached_view must be \"cockpit\", \"full\" or \"hidden\", not \"{}\"",
                            **view));
        }
    }
    auto front = opt_string(c, t, "eyepoint_front");
    auto rear = front ? opt_string(c, t, "eyepoint_rear") : front;
    if (!rear) {
        return std::unexpected(rear.error());
    }
    e.eyepoint_front = front->value_or(e.eyepoint_front);
    e.eyepoint_rear = rear->value_or(e.eyepoint_rear);

    if (auto r = parse_lod_thresholds(c, t, e); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = parse_parts(c, t, e); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = parse_components(c, t, e); !r) {
        return std::unexpected(r.error());
    }
    return e;
}

/// Overlap rules: an exact type beats a range, so an exact type may sit inside a range; two
/// entries with the same exact type, or two overlapping ranges, are an error.
Res<void> check_overlaps(const Ctx& c, const std::vector<const toml::table*>& tables,
                         const std::vector<EntityModel>& entities) {
    for (std::size_t j = 1; j < entities.size(); ++j) {
        for (std::size_t i = 0; i < j; ++i) {
            const EntityModel& a = entities[i];
            const EntityModel& b = entities[j];
            const bool overlap = a.first_type <= b.last_type && b.first_type <= a.last_type;
            if (overlap && a.is_exact() == b.is_exact()) {
                return c.fail(
                    *tables[j]->get("types"),
                    std::format("entity types [{}, {}] overlap the entry at line {} ([{}, {}])",
                                b.first_type, b.last_type, a.line, a.first_type, a.last_type));
            }
        }
    }
    return {};
}

Res<void> parse_entities(const Ctx& c, const toml::table& root, Database& db) {
    auto tables = table_array(c, root, "entity");
    if (!tables) {
        return std::unexpected(tables.error());
    }
    for (const toml::table* t : *tables) {
        auto entity = parse_entity(c, *t);
        if (!entity) {
            return std::unexpected(entity.error());
        }
        db.entities.push_back(std::move(*entity));
    }
    return check_overlaps(c, *tables, db.entities);
}

} // namespace

std::expected<Database, DbError> parse_database(std::string_view text,
                                                const std::filesystem::path& manifest_path) {
    Database db;
    db.manifest = manifest_path;
    db.root = manifest_path.parent_path();
    const Ctx c{manifest_path.string(), db.root, &db.warnings};

    toml::parse_result parsed = toml::parse(text, c.file);
    if (!parsed) {
        const auto& e = parsed.error();
        return std::unexpected(DbError{c.file, e.source().begin.line, e.source().begin.column,
                                       std::string(e.description())});
    }
    const toml::table& root = parsed.table();
    warn_unknown(c, root, {"database", "terrain", "material", "default", "entity"}, "the manifest");

    if (auto r = parse_database_table(c, root, db); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = parse_terrain_table(c, root, db); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = parse_default_table(c, root, db); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = parse_materials(c, root, db); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = parse_entities(c, root, db); !r) {
        return std::unexpected(r.error());
    }
    return db;
}

std::expected<Database, DbError> load_database(const std::filesystem::path& manifest) {
    std::ifstream in(manifest, std::ios::binary);
    if (!in) {
        return std::unexpected(DbError{manifest.string(), 0, 0, "cannot open the manifest"});
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        return std::unexpected(DbError{manifest.string(), 0, 0, "cannot read the manifest"});
    }
    return parse_database(buffer.str(), manifest);
}

} // namespace cigi::mock
