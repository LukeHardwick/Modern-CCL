#pragma once

// Dear ImGui panels: Link, Entities, Selected entity, Packet inspector, HAT/HOT log and the
// top-down map. Call between rlImGuiBegin() and rlImGuiEnd().

#include "app.hpp"
#include "renderer.hpp"

namespace cigi::viewer {

/// Draw the panels selected by app.ui.mode. `fps` is the measured frame rate.
void draw_ui(AppState& app, const CameraResult& cam, const SkyState& sky, float fps);

} // namespace cigi::viewer
