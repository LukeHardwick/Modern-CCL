#pragma once

// Window-relative placement of the ig_viewer panels. Pure arithmetic (no raylib / ImGui) so it can
// be unit tested without a window. The viewer calls compute_panel_layout() whenever the window
// size, the set of visible panels or the UI mode changes, and applies the result once.

#include <array>
#include <cstddef>

namespace cigi::mock {

enum class PanelId : std::size_t { Link, View, Entities, Selected, Inspector, HatHot, Log, Map };

inline constexpr std::size_t panel_count = 8;

struct PanelRect {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;          ///< Size the panel has when expanded.
    float occupied_h = 0.0F; ///< Height it takes in the stack (title bar only if collapsed).
    bool visible = false;    ///< False: not laid out (hidden by the user or by the UI mode).
    bool collapsed = false;  ///< Did not fit: show only the title bar until the user opens it.
};

struct PanelLayout {
    std::array<PanelRect, panel_count> panels{};

    [[nodiscard]] const PanelRect& operator[](PanelId id) const noexcept {
        return panels[static_cast<std::size_t>(id)];
    }
};

struct LayoutInput {
    float width = 1280.0F; ///< Window size in logical (ImGui) pixels.
    float height = 720.0F;
    float title_bar_h = 20.0F;
    bool map_only = false; ///< UiMode::Map: only the map panel.
    std::array<bool, panel_count> visible{true, true, true, true, true, true, true, true};
};

/// Place the visible panels in one or two columns along the window edges, leaving the middle for
/// the 3-D view. Panels shrink towards their minimum height to fit; if even that does not fit,
/// the least important ones collapse to a title bar. Every rectangle stays inside the window.
[[nodiscard]] PanelLayout compute_panel_layout(const LayoutInput& in) noexcept;

} // namespace cigi::mock
