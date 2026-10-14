#include "mock_ig/database_selector.hpp"

#include <format>

namespace cigi::mock {

namespace {

bool operational(IgMode m) noexcept {
    return m == IgMode::Operate || m == IgMode::Debug;
}

} // namespace

DatabaseSelector::DatabaseSelector(const DatabaseCatalog* catalog, int initial_number, Log log)
    : catalog_(catalog), log_(std::move(log)), current_(default_database()) {
    if (catalog_ != nullptr) {
        if (auto db = catalog_->find(initial_number)) {
            current_ = std::move(db);
        }
    }
}

void DatabaseSelector::log(std::string text) const {
    if (log_) {
        log_(std::move(text));
    }
}

void DatabaseSelector::on_ig_control(IgMode mode, std::int8_t database_number) {
    if (!operational(mode)) {
        return; // ICD 4.1.1: only in Operate / Debug
    }
    if (database_number <= 0) {
        last_request_ = 0;
        if (loading_) {
            host_released_ = true; // the Host has seen -n and let go (Figure 38)
        }
        return;
    }
    if (database_number == last_request_) {
        return; // the same request, still being held
    }
    last_request_ = database_number;
    if (catalog_ == nullptr) {
        log(std::format("database {} requested, but this IG has one global database (ignored)",
                        database_number));
        return;
    }
    auto db = catalog_->find(database_number);
    if (!db) {
        log(std::format("database {} requested but not found; keeping database {}", database_number,
                        current_->number));
        return;
    }
    log(std::format("loading database {} \"{}\" ({} frames)", db->number, db->name,
                    db->load_frames));
    target_ = std::move(db);
    loading_ = true;
    host_released_ = false;
    loading_frames_ = 0;
}

void DatabaseSelector::end_frame() {
    if (!loading_) {
        return;
    }
    ++loading_frames_;
    if (loading_frames_ > target_->load_frames && host_released_) {
        current_ = std::move(target_);
        target_.reset();
        loading_ = false;
        log(std::format("database {} loaded", current_->number));
    }
}

bool DatabaseSelector::refresh() {
    if (catalog_ == nullptr) {
        return false;
    }
    bool changed = false;
    if (auto fresh = catalog_->find(current_->number); fresh && fresh != current_) {
        current_ = std::move(fresh);
        changed = true;
    }
    if (loading_) {
        if (auto fresh = catalog_->find(target_->number); fresh && fresh != target_) {
            target_ = std::move(fresh);
            changed = true;
        }
    }
    return changed;
}

std::int8_t DatabaseSelector::reported() const noexcept {
    if (catalog_ == nullptr) {
        return 0;
    }
    const int number = loading_ ? -target_->number : current_->number;
    return static_cast<std::int8_t>(number);
}

} // namespace cigi::mock
