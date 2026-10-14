#include "mock_ig/database.hpp"

#include <algorithm>
#include <format>
#include <set>

namespace cigi::mock {

namespace fs = std::filesystem;

namespace {

constexpr const char* manifest_name = "database.toml";

/// The manifests under `root`: its own, or one per sub-folder; sorted.
std::vector<fs::path> find_manifests(const fs::path& root) {
    std::vector<fs::path> found;
    std::error_code ec;
    const fs::path own = root / manifest_name;
    if (fs::is_regular_file(own, ec)) {
        found.push_back(own);
        return found;
    }
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entry_ec;
        if (!it->is_directory(entry_ec)) {
            continue;
        }
        const fs::path manifest = it->path() / manifest_name;
        if (fs::is_regular_file(manifest, entry_ec)) {
            found.push_back(manifest);
        }
    }
    std::ranges::sort(found);
    return found;
}

fs::file_time_type mtime_of(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    return ec ? fs::file_time_type{} : t;
}

DatabaseCatalog::Item load_item(const fs::path& manifest) {
    DatabaseCatalog::Item item;
    item.manifest = manifest;
    item.mtime = mtime_of(manifest);
    auto db = load_database(manifest);
    if (db) {
        item.database = std::make_shared<const Database>(std::move(*db));
    } else {
        item.error = std::move(db.error());
    }
    return item;
}

} // namespace

std::expected<void, DbError> DatabaseCatalog::scan(const fs::path& root) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return std::unexpected(
            DbError{root.string(), 0, 0, "not a folder (expected a database root)"});
    }
    root_ = root;
    items_.clear();
    for (const auto& manifest : find_manifests(root)) {
        items_.push_back(load_item(manifest));
    }
    mark_duplicates();
    return {};
}

void DatabaseCatalog::mark_duplicates() {
    std::set<int> seen;
    for (auto& item : items_) {
        item.duplicate = item.database != nullptr && !seen.insert(item.database->number).second;
    }
}

DatabaseCatalog::Reload DatabaseCatalog::reload_if_changed() {
    Reload result;
    const std::vector<fs::path> now = find_manifests(root_);

    const auto before = items_.size();
    std::erase_if(items_, [&now](const Item& item) {
        return !std::ranges::binary_search(now, item.manifest);
    });
    result.removed = items_.size() != before;

    for (const auto& manifest : now) {
        const auto it = std::ranges::find(items_, manifest, &Item::manifest);
        if (it == items_.end()) {
            Item item = load_item(manifest);
            if (item.database) {
                result.updated.push_back(item.database->number);
            } else if (item.error) {
                result.errors.push_back(*item.error);
            }
            items_.push_back(std::move(item));
            continue;
        }
        const auto stamp = mtime_of(manifest);
        if (stamp == it->mtime) {
            continue;
        }
        Item fresh = load_item(manifest);
        it->mtime = fresh.mtime;
        if (fresh.database) {
            it->database = std::move(fresh.database);
            it->error.reset();
            result.updated.push_back(it->database->number);
        } else {
            // Keep the last good version; the error is reported once and stays on the item.
            it->error = fresh.error;
            if (fresh.error) {
                result.errors.push_back(*fresh.error);
            }
        }
    }
    std::ranges::sort(items_, {}, &Item::manifest);
    mark_duplicates();
    return result;
}

std::shared_ptr<const Database> DatabaseCatalog::find(int number) const {
    for (const auto& item : items_) {
        if (item.database && !item.duplicate && item.database->number == number) {
            return item.database;
        }
    }
    return nullptr;
}

std::vector<int> DatabaseCatalog::numbers() const {
    std::vector<int> out;
    for (const auto& item : items_) {
        if (item.database && !item.duplicate) {
            out.push_back(item.database->number);
        }
    }
    std::ranges::sort(out);
    return out;
}

std::vector<DbError> DatabaseCatalog::duplicates() const {
    std::vector<DbError> out;
    for (const auto& item : items_) {
        if (!item.duplicate) {
            continue;
        }
        const auto first = std::ranges::find_if(items_, [&item](const Item& other) {
            return other.database && !other.duplicate &&
                   other.database->number == item.database->number;
        });
        out.push_back(DbError{
            item.manifest.string(), item.database->number_line, 0,
            std::format("database number {} is already used by {}", item.database->number,
                        first != items_.end() ? first->manifest.string() : std::string{"?"})});
    }
    return out;
}

std::vector<DbError> DatabaseCatalog::errors() const {
    std::vector<DbError> out;
    for (const auto& item : items_) {
        if (item.error) {
            out.push_back(*item.error);
        }
    }
    return out;
}

} // namespace cigi::mock
