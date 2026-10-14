#pragma once

// The mock IG's database: what IG Control's Database Number selects (ICD 4.1.1, 4.2.1). On disk
// it is a folder with a hand-edited TOML manifest (database.toml, documented in
// docs/guide/databases.md); in memory it is the plain value type below.
//
//   auto db = load_database("databases/default/database.toml");   // std::expected<Database,
//   DbError> const EntityModel* m = db->lookup(150);                       // nullptr: not mapped
//   EntityCategory c = db->category_of(150);                      // falls back to [default]
//
// Entity Type values are database-specific in CIGI (ICD 4.1.2): which numbers mean what is up to
// the database, so the manifest maps them. An exact type beats a range; overlapping ranges are an
// error. The folder holds the assets (models, terrain) the manifest refers to; paths in it are
// relative to the manifest.

#include "mock_ig/geo.hpp"
#include "mock_ig/terrain.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cigi::mock {

/// Where a problem was found. `line` and `column` are 1-based; 0 means "not known" (for example
/// a file that cannot be opened).
struct Diagnostic {
    std::string file;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::string message;

    friend bool operator==(const Diagnostic&, const Diagnostic&) noexcept = default;
};
using DbError = Diagnostic;   ///< A problem that stops a database from loading.
using DbWarning = Diagnostic; ///< A problem that does not (e.g. an unknown key).

/// "file:line:column: message" (line and column left out when unknown).
[[nodiscard]] std::string to_string(const Diagnostic& d);

/// How the viewer draws an entity of a type and how the map labels it. Replaces the old
/// hard-coded convention (0 none, 1-99 other, 100-199 aircraft, 200-299 ground vehicle).
enum class EntityCategory : std::uint8_t {
    None,
    Aircraft,
    GroundVehicle,
    Sea,
    Lifeform,
    Munition,
    Other,
};
[[nodiscard]] std::string_view to_string(EntityCategory c) noexcept;
[[nodiscard]] std::optional<EntityCategory> parse_entity_category(std::string_view name) noexcept;

/// What the attached (cockpit) view shows of the entity it is attached to (read by T22).
enum class AttachedView : std::uint8_t {
    Cockpit,
    Full,
    Hidden,
};
[[nodiscard]] std::string_view to_string(AttachedView v) noexcept;

/// Colour as 8-bit sRGB, written "#rrggbb" in the manifest.
struct Rgb8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    friend constexpr bool operator==(const Rgb8&, const Rgb8&) noexcept = default;
};
[[nodiscard]] std::string to_hex(const Rgb8& c);

/// A HAT/HOT material code (ICD 4.2.3, Material Code) and how maps and terrain colour it.
struct MaterialEntry {
    std::uint32_t code = 0;
    std::string name;
    Rgb8 colour;
    std::uint32_t line = 0; ///< Manifest line of the table (0: built in).
    friend bool operator==(const MaterialEntry&, const MaterialEntry&) noexcept = default;
};

/// `[[entity.part]]`: Articulated Part ID -> model node (ICD 4.1.6). Used by T22.
struct PartMapping {
    std::uint8_t id = 0;
    std::string node;
    std::string axis; ///< "", "x", "y" or "z": rotation axis of a spinning part.
    friend bool operator==(const PartMapping&, const PartMapping&) noexcept = default;
};

/// What a Component Control drives on a model node (ICD 4.1.4). Used by T22.
enum class ComponentKind : std::uint8_t {
    Visibility,
    Rotation,
    Translation,
};
[[nodiscard]] std::string_view to_string(ComponentKind k) noexcept;

/// `[[entity.component]]`: Component ID -> model node. Used by T22.
struct ComponentMapping {
    std::uint16_t id = 0;
    std::string node;
    ComponentKind kind = ComponentKind::Visibility;
    friend bool operator==(const ComponentMapping&, const ComponentMapping&) noexcept = default;
};

/// One `[[entity]]` table: an inclusive range of Entity Type values and what they look like.
struct EntityModel {
    std::uint16_t first_type = 0; ///< Inclusive range; a single number has first == last.
    std::uint16_t last_type = 0;
    std::string name;
    EntityCategory category = EntityCategory::Other;
    /// Model files, LOD0 first (`model = "x.glb"` is `lods = ["x.glb"]`); paths relative to the
    /// manifest. Empty: the viewer draws the category's primitive shape.
    std::vector<std::string> lods;
    /// Model -> CIGI body axes (x forward, y right, z down) correction (T22).
    double scale = 1.0;
    Vec3d offset;      ///< Metres.
    Attitude rotation; ///< yaw, pitch, roll in degrees.
    /// Screen-height fractions where LOD1, LOD2, ... take over, and the one below which the
    /// entity is culled (research/03 Addendum A). Applies to `lods`.
    std::vector<double> lod_screen_heights{0.25, 0.08, 0.02};
    double cull_screen_height = 0.001;
    AttachedView attached_view = AttachedView::Cockpit;
    std::string eyepoint_front = "eyepoint_front"; ///< Default eyepoint node names.
    std::string eyepoint_rear = "eyepoint_rear";
    std::vector<PartMapping> parts;
    std::vector<ComponentMapping> components;
    std::uint32_t line = 0; ///< Manifest line of the table (0: built in).

    [[nodiscard]] constexpr bool contains(std::uint16_t type) const noexcept {
        return type >= first_type && type <= last_type;
    }
    [[nodiscard]] constexpr bool is_exact() const noexcept { return first_type == last_type; }
    friend bool operator==(const EntityModel&, const EntityModel&) noexcept = default;
};

/// `[terrain]`: how the ground is made. T21 adds the heightmap rendering and HAT/HOT; the table
/// is parsed and validated here.
struct TerrainSpec {
    enum class Kind : std::uint8_t {
        Synthetic, ///< The built-in sine-wave terrain (terrain.hpp).
        Heightmap, ///< A height image in the database folder (T21).
    };
    Kind kind = Kind::Synthetic;
    std::string file; ///< Heightmap: path relative to the manifest.
    friend bool operator==(const TerrainSpec&, const TerrainSpec&) noexcept = default;
};

struct Database {
    int number = 1; ///< 1..127: what IG Control selects and Start of Frame reports.
    std::string name;
    Geodetic origin{51.5, -0.12, 0.0};
    DatabaseBounds bounds = DatabaseBounds::around(origin, 0.25);
    /// Simulated load time: frames the IG reports the load in progress (Start of Frame -n).
    std::uint32_t load_frames = 30;
    TerrainSpec terrain;
    std::vector<MaterialEntry> materials;
    EntityCategory default_category = EntityCategory::Other; ///< `[default] category`.
    std::vector<EntityModel> entities;

    std::filesystem::path root;      ///< Folder of the manifest; empty for the built-in database.
    std::filesystem::path manifest;  ///< The manifest file; empty for the built-in database.
    std::uint32_t number_line = 0;   ///< Manifest line of `number` (for duplicate reports).
    std::vector<DbWarning> warnings; ///< Collected while loading; never fatal.

    /// The entry for `type`: an exact entry first, otherwise the range that contains it. Null if
    /// no entry maps it. The pointer stays valid as long as this object is unchanged.
    [[nodiscard]] const EntityModel* lookup(std::uint16_t type) const noexcept;
    /// `lookup(type)->category`, or the `[default]` category when it is not mapped.
    [[nodiscard]] EntityCategory category_of(std::uint16_t type) const noexcept;
    [[nodiscard]] const MaterialEntry* material(std::uint32_t code) const noexcept;
    /// `relative` (a path from the manifest) made absolute against the database folder.
    [[nodiscard]] std::filesystem::path resolve(std::string_view relative) const;

    /// Same content (name, entries, ...), ignoring where it was loaded from, source lines and
    /// warnings.
    [[nodiscard]] bool same_content(const Database& other) const;
};

/// The built-in database used when no database root is given: number 1, synthetic terrain, and
/// the old convention (0 none, 1-99 and everything unmapped other, 100-199 aircraft,
/// 200-299 ground vehicle). `databases/default/database.toml` describes the same database.
[[nodiscard]] std::shared_ptr<const Database> default_database();

/// Read and validate one manifest. Stops at the first error. Unknown keys and missing model
/// files are warnings in `Database::warnings`.
[[nodiscard]] std::expected<Database, DbError> load_database(const std::filesystem::path& manifest);

/// Same, from the text of a manifest. `manifest_path` names the file in messages and its folder
/// is where relative paths and model files are looked up. Used by load_database() and the tests.
[[nodiscard]] std::expected<Database, DbError>
parse_database(std::string_view text, const std::filesystem::path& manifest_path);

/// All databases under a root folder, indexed by number. Either the root itself holds a
/// database.toml (one database) or each sub-folder with a database.toml is one. Tolerant: a
/// manifest that fails to load is recorded with its error and the others are still available.
class DatabaseCatalog {
public:
    struct Item {
        std::filesystem::path manifest;
        std::filesystem::file_time_type mtime{};
        std::shared_ptr<const Database> database; ///< Last version that loaded; null if none.
        std::optional<DbError> error;             ///< Error of the newest attempt, if it failed.
        bool duplicate = false; ///< Another item (earlier by path) already has this number.
    };

    /// What reload_if_changed() did.
    struct Reload {
        std::vector<int> updated;    ///< Numbers whose database was replaced (or added).
        std::vector<DbError> errors; ///< New failures; the previous version is kept.
        bool removed = false;        ///< A manifest disappeared.
        [[nodiscard]] bool changed() const noexcept {
            return !updated.empty() || !errors.empty() || removed;
        }
    };

    /// Scan `root` (replaces the previous content). An unreadable root is an error.
    [[nodiscard]] std::expected<void, DbError> scan(const std::filesystem::path& root);

    /// Poll the manifests' modification times (and the folder listing for new / removed
    /// manifests) and reload what changed. A manifest that no longer parses keeps its previous
    /// version and reports the error. The caller decides how often to call this.
    [[nodiscard]] Reload reload_if_changed();

    /// The database with this number, or null. With duplicates, the first manifest (by path).
    [[nodiscard]] std::shared_ptr<const Database> find(int number) const;
    /// Numbers of the usable databases, ascending (each number once).
    [[nodiscard]] std::vector<int> numbers() const;
    [[nodiscard]] const std::vector<Item>& items() const noexcept { return items_; }
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    /// One diagnostic per manifest that shares its number with an earlier one.
    [[nodiscard]] std::vector<DbError> duplicates() const;
    /// The newest load error of every manifest that has one.
    [[nodiscard]] std::vector<DbError> errors() const;

private:
    void mark_duplicates();

    std::filesystem::path root_;
    std::vector<Item> items_; ///< Sorted by manifest path.
};

} // namespace cigi::mock
