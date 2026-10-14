#include "mock_ig/database.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace cigi::mock {

namespace {

constexpr std::array<std::pair<EntityCategory, std::string_view>, 7> category_names{{
    {EntityCategory::None, "none"},
    {EntityCategory::Aircraft, "aircraft"},
    {EntityCategory::GroundVehicle, "ground_vehicle"},
    {EntityCategory::Sea, "sea"},
    {EntityCategory::Lifeform, "lifeform"},
    {EntityCategory::Munition, "munition"},
    {EntityCategory::Other, "other"},
}};

bool same_bounds(const DatabaseBounds& a, const DatabaseBounds& b) noexcept {
    return a.lat_min_deg == b.lat_min_deg && a.lat_max_deg == b.lat_max_deg &&
           a.lon_min_deg == b.lon_min_deg && a.lon_max_deg == b.lon_max_deg;
}

EntityModel builtin_entity(std::uint16_t first, std::uint16_t last, std::string name,
                           EntityCategory category) {
    EntityModel e;
    e.first_type = first;
    e.last_type = last;
    e.name = std::move(name);
    e.category = category;
    return e;
}

Database make_default_database() {
    Database db;
    db.number = 1;
    db.name = "Default (synthetic)";
    db.origin = {51.5, -0.12, 0.0};
    db.bounds = DatabaseBounds::around(db.origin, 0.25);
    db.load_frames = 30;
    db.terrain.kind = TerrainSpec::Kind::Synthetic;
    // The three height bands of the synthetic terrain (terrain.hpp); colours as the viewer's.
    db.materials = {
        {0, "water", {0x29, 0x5c, 0x99}, 0},
        {1, "grass", {0x4d, 0x8f, 0x42}, 0},
        {2, "rock", {0x94, 0x85, 0x75}, 0},
    };
    db.default_category = EntityCategory::Other;
    db.entities = {
        builtin_entity(0, 0, "null entity", EntityCategory::None),
        builtin_entity(1, 99, "other", EntityCategory::Other),
        builtin_entity(100, 199, "generic aircraft", EntityCategory::Aircraft),
        builtin_entity(200, 299, "generic ground vehicle", EntityCategory::GroundVehicle),
    };
    return db;
}

} // namespace

std::string to_string(const Diagnostic& d) {
    std::string out = d.file;
    if (d.line > 0) {
        out += std::format(":{}", d.line);
        if (d.column > 0) {
            out += std::format(":{}", d.column);
        }
    }
    if (!out.empty()) {
        out += ": ";
    }
    out += d.message;
    return out;
}

std::string_view to_string(EntityCategory c) noexcept {
    for (const auto& [value, name] : category_names) {
        if (value == c) {
            return name;
        }
    }
    return "other";
}

std::optional<EntityCategory> parse_entity_category(std::string_view name) noexcept {
    for (const auto& [value, text] : category_names) {
        if (text == name) {
            return value;
        }
    }
    return std::nullopt;
}

std::string_view to_string(AttachedView v) noexcept {
    switch (v) {
    case AttachedView::Cockpit:
        return "cockpit";
    case AttachedView::Full:
        return "full";
    case AttachedView::Hidden:
        return "hidden";
    }
    return "cockpit";
}

std::string_view to_string(ComponentKind k) noexcept {
    switch (k) {
    case ComponentKind::Visibility:
        return "visibility";
    case ComponentKind::Rotation:
        return "rotation";
    case ComponentKind::Translation:
        return "translation";
    }
    return "visibility";
}

std::string to_hex(const Rgb8& c) {
    return std::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b);
}

const EntityModel* Database::lookup(std::uint16_t type) const noexcept {
    // An exact type beats a range (load_database rejects overlapping ranges, so at most one
    // range matches).
    const EntityModel* range = nullptr;
    for (const auto& e : entities) {
        if (!e.contains(type)) {
            continue;
        }
        if (e.is_exact()) {
            return &e;
        }
        range = &e;
    }
    return range;
}

EntityCategory Database::category_of(std::uint16_t type) const noexcept {
    const EntityModel* e = lookup(type);
    return e != nullptr ? e->category : default_category;
}

const MaterialEntry* Database::material(std::uint32_t code) const noexcept {
    const auto it = std::ranges::find(materials, code, &MaterialEntry::code);
    return it == materials.end() ? nullptr : &*it;
}

std::filesystem::path Database::resolve(std::string_view relative) const {
    return root / std::filesystem::path(relative);
}

bool Database::same_content(const Database& o) const {
    // Source lines say where an entry was written, not what it is.
    auto materials_a = materials;
    auto materials_b = o.materials;
    auto entities_a = entities;
    auto entities_b = o.entities;
    for (auto* m : {&materials_a, &materials_b}) {
        std::ranges::for_each(*m, [](MaterialEntry& e) { e.line = 0; });
    }
    for (auto* m : {&entities_a, &entities_b}) {
        std::ranges::for_each(*m, [](EntityModel& e) { e.line = 0; });
    }
    return number == o.number && name == o.name && origin == o.origin &&
           same_bounds(bounds, o.bounds) && load_frames == o.load_frames && terrain == o.terrain &&
           materials_a == materials_b && default_category == o.default_category &&
           entities_a == entities_b;
}

std::shared_ptr<const Database> default_database() {
    static const std::shared_ptr<const Database> db =
        std::make_shared<const Database>(make_default_database());
    return db;
}

} // namespace cigi::mock
