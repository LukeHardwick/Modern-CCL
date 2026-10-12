#include "mock_ig/panel_layout.hpp"

#include <algorithm>
#include <span>
#include <vector>

namespace cigi::mock {

// Index arithmetic below is bounded by panel_count (every index comes from a PanelId).
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index)
namespace {

constexpr float margin = 8.0F;
constexpr float gap = 6.0F;
constexpr float two_column_min_width = 1100.0F;

struct Spec {
    float pref_h; ///< Height when there is plenty of room.
    float min_h;  ///< Smallest useful height before the panel collapses.
};

constexpr std::array<Spec, panel_count> specs{{
    {270.0F, 140.0F}, // Link
    {190.0F, 120.0F}, // View
    {280.0F, 120.0F}, // Entities
    {300.0F, 140.0F}, // Selected
    {230.0F, 110.0F}, // Inspector
    {200.0F, 90.0F},  // HatHot
    {160.0F, 80.0F},  // Log
    {380.0F, 150.0F}, // Map
}};

/// Most important first: the last panels of a column are the first to collapse.
constexpr std::array<PanelId, panel_count> priority{
    PanelId::Map,  PanelId::Link,      PanelId::Entities, PanelId::Selected,
    PanelId::View, PanelId::Inspector, PanelId::HatHot,   PanelId::Log};

[[nodiscard]] constexpr float clampf(float v, float lo, float hi) noexcept {
    return std::max(lo, std::min(v, hi));
}

/// Stack `ids` (top to bottom) in a column; heights shrink, then the least important collapse.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void stack_column(PanelLayout& out, std::span<const PanelId> ids, float x, float col_w,
                  float height, float bar_h) {
    std::vector<PanelId> shown(ids.begin(), ids.end());
    if (shown.empty()) {
        return;
    }
    const float avail = std::max(height - (2.0F * margin), bar_h);
    // Collapse state by order index in `shown`; least important = highest priority index.
    std::vector<bool> collapsed(shown.size(), false);
    const auto rank = [&](PanelId id) {
        return std::ranges::find(priority, id) - priority.begin();
    };
    const auto fixed_and_min = [&] {
        float total = gap * static_cast<float>(shown.size() - 1);
        for (std::size_t i = 0; i < shown.size(); ++i) {
            total += collapsed[i] ? bar_h : specs[static_cast<std::size_t>(shown[i])].min_h;
        }
        return total;
    };
    while (fixed_and_min() > avail) {
        // Collapse the least important panel still open; stop when none is left.
        std::size_t worst = shown.size();
        for (std::size_t i = 0; i < shown.size(); ++i) {
            if (!collapsed[i] && (worst == shown.size() || rank(shown[i]) > rank(shown[worst]))) {
                worst = i;
            }
        }
        if (worst == shown.size()) {
            break;
        }
        collapsed[worst] = true;
    }
    // Share the room above the minimums in proportion to each panel's wish, capped at its wish.
    float wish_total = 0.0F;
    for (std::size_t i = 0; i < shown.size(); ++i) {
        if (!collapsed[i]) {
            const Spec& s = specs[static_cast<std::size_t>(shown[i])];
            wish_total += s.pref_h - s.min_h;
        }
    }
    const float spare = avail - fixed_and_min();
    const float frac = wish_total > 0.0F ? clampf(spare / wish_total, 0.0F, 1.0F) : 0.0F;
    float y = margin;
    for (std::size_t i = 0; i < shown.size(); ++i) {
        const Spec& s = specs[static_cast<std::size_t>(shown[i])];
        PanelRect& r = out.panels[static_cast<std::size_t>(shown[i])];
        r.visible = true;
        r.collapsed = collapsed[i];
        r.x = x;
        r.y = y;
        r.w = col_w;
        r.h = collapsed[i] ? s.min_h : s.min_h + ((s.pref_h - s.min_h) * frac);
        r.occupied_h = collapsed[i] ? bar_h : r.h;
        y += r.occupied_h + gap;
    }
}

} // namespace

PanelLayout compute_panel_layout(const LayoutInput& in) noexcept {
    PanelLayout out;
    const float width = std::max(in.width, 1.0F);
    const float height = std::max(in.height, 1.0F);
    const float bar_h = std::max(in.title_bar_h, 1.0F);
    const auto wanted = [&](PanelId id) {
        const bool shown = in.visible[static_cast<std::size_t>(id)];
        return in.map_only ? (id == PanelId::Map && shown) : shown;
    };
    const auto column_of = [&](std::initializer_list<PanelId> list) {
        std::vector<PanelId> ids;
        for (const PanelId id : list) {
            if (wanted(id)) {
                ids.push_back(id);
            }
        }
        return ids;
    };

    if (in.map_only) {
        const float col_w =
            std::min(clampf(0.3F * width, 300.0F, 560.0F), std::max(width - (2.0F * margin), 1.0F));
        const float x = width - margin - col_w;
        const auto ids = column_of({PanelId::Map});
        stack_column(out, ids, x, col_w, height, bar_h);
        if (wanted(PanelId::Map)) {
            // The map is the only panel: let it use the room (square-ish, but not the full height).
            PanelRect& m = out.panels[static_cast<std::size_t>(PanelId::Map)];
            if (!m.collapsed) {
                m.h = std::min(std::max(col_w * 1.1F, m.h),
                               std::max(height - (2.0F * margin), bar_h));
                m.occupied_h = m.h;
            }
        }
    } else if (width >= two_column_min_width) {
        const float col_w = clampf(0.24F * width, 300.0F, 460.0F);
        stack_column(
            out, column_of({PanelId::Link, PanelId::View, PanelId::Entities, PanelId::Selected}),
            margin, col_w, height, bar_h);
        stack_column(out,
                     column_of({PanelId::Map, PanelId::Inspector, PanelId::HatHot, PanelId::Log}),
                     width - margin - col_w, col_w, height, bar_h);
    } else {
        const float col_w = std::min(width < 500.0F ? width : clampf(0.42F * width, 280.0F, 440.0F),
                                     std::max(width - (2.0F * margin), 1.0F));
        stack_column(out,
                     column_of({PanelId::Map, PanelId::Link, PanelId::Entities, PanelId::Selected,
                                PanelId::View, PanelId::Inspector, PanelId::HatHot, PanelId::Log}),
                     margin, col_w, height, bar_h);
    }
    // Final guarantee: nothing hangs over the window edge (only possible in absurdly small
    // windows).
    for (PanelRect& r : out.panels) {
        if (!r.visible) {
            continue;
        }
        r.w = std::min(r.w, std::max(width - (2.0F * margin), 1.0F));
        r.x = clampf(r.x, 0.0F, std::max(width - r.w, 0.0F));
        r.h = std::min(r.h, height);
        r.y = clampf(r.y, 0.0F, std::max(height - r.occupied_h, 0.0F));
    }
    return out;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index)

} // namespace cigi::mock
