#pragma once

// The Database Number protocol of an IG (ICD 4.1.1 IG Control, 4.2.1 Start of Frame, Figure 38),
// separated from the rest of the mock IG so a facade-based IG can reuse it.
//
//   DatabaseSelector sel(&catalog, 1);
//   sel.on_ig_control(mode, n);     // per IG Control packet
//   sel.end_frame();                // per IG frame
//   sof.database_number = sel.reported();
//
// The ICD says the Host sets Database Number n (1..127) in Operate/Debug, the IG reports -n while
// it loads and +n when done, and the Host then sends 0. Where the ICD is silent this selector
// decides (recorded as ICD ambiguities in the T20 report):
//  * -n is reported for at least the database's `load_frames` frames AND until the Host has sent
//    0; only then +n. A Host that never sends 0 sees -n forever.
//  * Sending the same number again after a 0 reloads it; a different number while loading
//    restarts the load with that number.
//  * An unknown number logs an error, keeps the current database and keeps reporting its number.
//  * Requests are ignored in Reset/Standby and Offline Maintenance (the Host must retry).
//  * With no catalog the IG has one global database: it reports 0 and ignores requests.
// Entities are the scene's business and are kept across a load.

#include "mock_ig/database.hpp"

#include <cigi/packets.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace cigi::mock {

class DatabaseSelector {
public:
    using Log = std::function<void(std::string)>;

    /// `catalog` (not owned, must outlive this) null: a global database, reporting 0.
    /// Otherwise `initial_number` is loaded from it at start (falling back to the built-in
    /// database when it is not there).
    explicit DatabaseSelector(const DatabaseCatalog* catalog = nullptr, int initial_number = 1,
                              Log log = {});

    /// One IG Control packet (any one the Host sends, in order).
    void on_ig_control(IgMode mode, std::int8_t database_number);
    /// One IG frame has ended (call before reading reported()). Counts load frames.
    void end_frame();
    /// Follow a hot-reloaded catalog: the database in force (or being loaded) is replaced by the
    /// catalog's newer version of the same number. True if it changed.
    bool refresh();

    /// The Start of Frame Database Number: -n loading, +n loaded, 0 global.
    [[nodiscard]] std::int8_t reported() const noexcept;
    [[nodiscard]] bool loading() const noexcept { return loading_; }
    /// The database in force (never null; the old one while loading).
    [[nodiscard]] const std::shared_ptr<const Database>& current() const noexcept {
        return current_;
    }

private:
    void log(std::string text) const;

    const DatabaseCatalog* catalog_;
    Log log_;
    std::shared_ptr<const Database> current_;
    std::shared_ptr<const Database> target_; ///< Being loaded.
    bool loading_ = false;
    bool host_released_ = false;       ///< The Host has sent 0 since this load began.
    std::int8_t last_request_ = 0;     ///< The number the Host is currently holding.
    std::uint32_t loading_frames_ = 0; ///< Frames ended since this load began.
};

} // namespace cigi::mock
