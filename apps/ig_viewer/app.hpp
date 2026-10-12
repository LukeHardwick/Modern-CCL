#pragma once

// State shared by the main loop, the 3-D renderer and the ImGui panels.

#include "args.hpp"
#include "mock_ig/mock_ig.hpp"
#include "mock_ig/panel_layout.hpp"
#include "mock_ig/scripted_host.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <imgui.h>
#include <string>
#include <vector>

namespace cigi::viewer {

/// What one simulated frame carried, kept for the packet inspector.
struct FrameRecord {
    std::uint32_t host_frame = 0;
    std::vector<std::byte> host_message; ///< Host -> IG datagram.
    std::vector<std::byte> ig_message;   ///< IG -> Host datagram (Start of Frame + answers).
};

struct CameraState {
    CameraKind mode = CameraKind::Host;
    // Free camera: position in the local ENU frame, yaw clockwise from north, pitch up.
    mock::Vec3d free_position{350.0, -950.0, 260.0};
    float free_yaw_deg = 25.0F;
    float free_pitch_deg = -6.0F;
    // Chase camera: orbit around `chase_target`, angles relative to that entity's heading.
    std::uint16_t chase_target = 0;
    float chase_yaw_deg = 0.0F; ///< 0: directly behind.
    float chase_pitch_deg = 18.0F;
    float chase_distance_m = 70.0F;
};

struct DrawOptions {
    float shape_scale = 3.0F; ///< Entity meshes are drawn this many times life size.
    bool trails = true;
    bool labels = true;
    bool drop_lines = true;
    bool parts = true;
    bool terrain_grid = true;
};

struct UiState {
    UiMode mode = UiMode::All;
    bool show_link = true;
    bool show_entities = true;
    bool show_selected = true;
    bool show_inspector = true;
    bool show_hat_hot = true;
    bool show_map = true;
    bool show_view = true;
    // Panel layout: recomputed from the window size (see mock::compute_panel_layout) and applied
    // once (ImGuiCond_Always) when the size, the UI mode or the set of panels changes; in between
    // the user can move and resize panels freely.
    mock::PanelLayout layout;
    bool apply_layout = true;       ///< This frame: force the panels to `layout`.
    bool relayout_requested = true; ///< Key L, or the first frame.
    float layout_w = 0.0F;
    float layout_h = 0.0F;
    UiMode layout_mode = UiMode::All;
    std::array<bool, mock::panel_count> layout_visible{};
    // Map
    mock::Vec3d map_center{};
    float map_scale = 0.09F; ///< Pixels per metre.
    bool map_follow = false;
    // Scratch buffers reused every frame by the map.
    std::vector<ImVec2> map_pts;
    std::vector<ImVec4> map_label_rects;
    // Inspector
    int inspector_offset = 0; ///< 0: newest recorded frame; n: n frames older.
    std::string inspector_filter;
    std::vector<std::string> inspector_lines; ///< Cached describe() text of the shown frame.
    std::uint32_t inspector_cached_frame = 0;
    bool inspector_cache_valid = false;
};

struct AppState {
    explicit AppState(const Args& args, const mock::ScriptedHostConfig& host_cfg,
                      const mock::SceneConfig& scene_cfg)
        : host(host_cfg), ig(scene_cfg) {
        cam.mode = args.camera;
        ui.mode = args.ui;
        selected = static_cast<std::uint16_t>(args.select);
        cam.chase_target = selected;
    }

    mock::ScriptedHost host;
    mock::MockIg ig;
    CameraState cam;
    DrawOptions draw;
    UiState ui;
    std::uint16_t selected = 0; ///< Selected Entity ID (ownship at start).

    // Simulation clock
    static constexpr double dt = 1.0 / 60.0;
    bool paused = false;
    int step_requests = 0;
    float speed = 1.0F;
    double accumulator = 0.0;

    std::deque<FrameRecord> history;
    static constexpr std::size_t history_max = 120;

    // Traffic statistics (Host -> IG)
    std::uint64_t total_packets = 0;
    std::uint64_t total_bytes = 0;
    std::uint64_t reply_packets = 0;
    double packets_per_s = 0.0;
    double bytes_per_s = 0.0;
    double rate_timer = 0.0;
    std::uint64_t rate_packets_mark = 0;
    std::uint64_t rate_bytes_mark = 0;
    std::string last_error;
};

/// Run one IG frame: next scripted Host message -> codec -> dispatch -> scene -> replies.
void step_simulation(AppState& app);

} // namespace cigi::viewer
