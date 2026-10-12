#include "ui.hpp"

#include "gl_coords.hpp"
#include "mock_ig/describe.hpp"
#include "terrain_colors.hpp"

#include <algorithm>
#include <array>
#include <cigi/packet_set.hpp>
#include <cmath>
#include <format>
#include <imgui.h>
#include <raylib.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace cigi::viewer {

namespace {

using mock::Entity;
using mock::Vec3d;

ImU32 to_im(Color c) {
    return IM_COL32(c.r, c.g, c.b, c.a);
}

ImU32 entity_im_color(std::uint16_t id, float alpha = 1.0F) {
    return to_im(Fade(id_color(id), alpha));
}

const char* shape_name(std::uint16_t type) {
    switch (mock::classify_entity_type(type)) {
    case mock::ShapeClass::None:
        return "none";
    case mock::ShapeClass::Aircraft:
        return "aircraft";
    case mock::ShapeClass::GroundVehicle:
        return "vehicle";
    case mock::ShapeClass::Other:
        break;
    }
    return "other";
}

void text(const std::string& s) {
    ImGui::TextUnformatted(s.c_str());
}

void select_entity(AppState& app, std::uint16_t id) {
    app.selected = id;
    app.cam.chase_target = id;
}

// ---- Link ---------------------------------------------------------------------------------

void draw_link(AppState& app, const CameraResult& cam, const SkyState& sky, float fps) {
    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({400, 270}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Link", &app.ui.show_link)) {
        ImGui::End();
        return;
    }
    const mock::SceneModel& scene = app.ig.scene();
    ImGui::TextColored({0.4F, 0.9F, 0.5F, 1.0F}, "Mode: scripted (offline, no network)");
    text(std::format("IG frame {}   Host frame {}", app.ig.ig_frame(), scene.host_frame_number()));
    text(std::format("IG mode (Host asked): {}", mock::enum_name(scene.ig_control().ig_mode)));
    text(std::format("Render {:.0f} fps   sim {:.1f} s", static_cast<double>(fps),
                     scene.sim_time_s()));
    text(std::format("Host -> IG: {:.0f} packets/s, {:.1f} kB/s", app.packets_per_s,
                     app.bytes_per_s / 1000.0));
    text(std::format("Totals: {} packets, {:.1f} kB; {} IG packets", app.total_packets,
                     static_cast<double>(app.total_bytes) / 1000.0, app.reply_packets));
    const mock::SceneStats& st = scene.stats();
    text(std::format("Entities {} (created {}, destroyed {})", scene.entities().size(),
                     st.entities_created, st.entities_destroyed));
    text(std::format(
        "Ignored: unknown entity {}, bad parent {}, unknown opcode {}, decode errors {}",
        st.unknown_entity, st.invalid_parent, st.unknown_packets, st.decode_errors));
    text(std::format("Sun {:.1f} deg elevation, {:.0f} deg azimuth", sky.sun.elevation_deg,
                     sky.sun.azimuth_deg));
    text(std::format("Camera: {}{}",
                     app.cam.mode == CameraKind::Host    ? "Host view"
                     : app.cam.mode == CameraKind::Chase ? "Chase"
                                                         : "Free",
                     app.cam.mode == CameraKind::Host && !cam.host_view ? " (no view yet)" : ""));
    ImGui::Separator();
    ImGui::Checkbox("Pause simulation", &app.paused);
    ImGui::SameLine();
    if (ImGui::Button("Step")) {
        app.step_requests += 1;
    }
    ImGui::SliderFloat("Speed", &app.speed, 0.1F, 4.0F, "%.1fx");
    if (!app.last_error.empty()) {
        ImGui::TextColored({1.0F, 0.4F, 0.3F, 1.0F}, "%s", app.last_error.c_str());
    }
    ImGui::End();
}

// ---- Entities -----------------------------------------------------------------------------

void draw_entities(AppState& app) {
    ImGui::SetNextWindowPos({10, 290}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({640, 280}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Entities", &app.ui.show_entities)) {
        ImGui::End();
        return;
    }
    const mock::SceneModel& scene = app.ig.scene();
    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("entities", 11, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(
            "ID", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Type", 0, 100);
        ImGui::TableSetupColumn("State", 0, 55);
        ImGui::TableSetupColumn("Parent", 0, 45);
        ImGui::TableSetupColumn("Lat", 0, 80);
        ImGui::TableSetupColumn("Lon", 0, 80);
        ImGui::TableSetupColumn("Alt m", 0, 55);
        ImGui::TableSetupColumn("AGL m", 0, 55);
        ImGui::TableSetupColumn("Yaw", 0, 45);
        ImGui::TableSetupColumn("Pitch", 0, 45);
        ImGui::TableSetupColumn("Roll", 0, 45);
        ImGui::TableHeadersRow();

        std::vector<const Entity*> rows;
        rows.reserve(scene.entities().size());
        for (const auto& [id, e] : scene.entities()) {
            rows.push_back(&e);
        }
        if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
            specs != nullptr && specs->SpecsCount > 0) {
            const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
            const auto key = [&](const Entity* e) {
                switch (spec.ColumnIndex) {
                case 1:
                    return static_cast<double>(e->entity_type);
                case 2:
                    return static_cast<double>(static_cast<int>(e->state));
                case 3:
                    return e->attached ? static_cast<double>(e->parent_id) : -1.0;
                case 4:
                    return e->position.lat_deg;
                case 5:
                    return e->position.lon_deg;
                case 6:
                    return e->position.alt_m;
                case 7:
                    return e->agl_m;
                case 8:
                    return e->world_attitude.yaw_deg;
                case 9:
                    return e->world_attitude.pitch_deg;
                case 10:
                    return e->world_attitude.roll_deg;
                default:
                    return static_cast<double>(e->id);
                }
            };
            const bool ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
            std::ranges::stable_sort(rows, [&](const Entity* a, const Entity* b) {
                return ascending ? key(a) < key(b) : key(a) > key(b);
            });
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const Entity& e = *rows.at(static_cast<std::size_t>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, entity_im_color(e.id, 1.0F));
                const std::string label = std::format("{}##row{}", e.id, e.id);
                if (ImGui::Selectable(label.c_str(), app.selected == e.id,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    select_entity(app, e.id);
                }
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                text(std::format("{} {}", e.entity_type, shape_name(e.entity_type)));
                ImGui::TableNextColumn();
                text(e.state == EntityState::Active            ? "Active"
                     : e.state == EntityState::InactiveStandby ? "Standby"
                                                               : "?");
                ImGui::TableNextColumn();
                text(e.attached ? std::to_string(e.parent_id) : "-");
                ImGui::TableNextColumn();
                text(std::format("{:.6f}", e.position.lat_deg));
                ImGui::TableNextColumn();
                text(std::format("{:.6f}", e.position.lon_deg));
                ImGui::TableNextColumn();
                text(std::format("{:.1f}", e.position.alt_m));
                ImGui::TableNextColumn();
                text(std::format("{:.1f}", e.agl_m));
                ImGui::TableNextColumn();
                text(std::format("{:.1f}", e.world_attitude.yaw_deg));
                ImGui::TableNextColumn();
                text(std::format("{:.1f}", e.world_attitude.pitch_deg));
                ImGui::TableNextColumn();
                text(std::format("{:.1f}", e.world_attitude.roll_deg));
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// ---- Selected entity ----------------------------------------------------------------------

void draw_selected(AppState& app) {
    ImGui::SetNextWindowPos({10, 580}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({640, 310}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Selected entity", &app.ui.show_selected)) {
        ImGui::End();
        return;
    }
    const mock::SceneModel& scene = app.ig.scene();
    const Entity* e = scene.find(app.selected);
    if (e == nullptr) {
        ImGui::TextDisabled("Entity %u does not exist (click a row in Entities)",
                            static_cast<unsigned>(app.selected));
        ImGui::End();
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, entity_im_color(e->id));
    text(std::format("Entity {}  type {} ({})", e->id, e->entity_type, shape_name(e->entity_type)));
    ImGui::PopStyleColor();
    ImGui::TextWrapped("%s", mock::describe(e->last_control).c_str());
    ImGui::Separator();
    text(std::format("Visible {}   alpha {} (apparent {})   {}", e->visible ? "yes" : "no",
                     e->alpha, e->apparent_alpha,
                     e->attached ? std::format("attached to {}", e->parent_id) : "top level"));
    if (!e->attached) {
        text(std::format("Clamp {}   commanded alt {:.2f} m", mock::enum_name(e->ground_clamp),
                         e->commanded.alt_m));
    } else {
        text(std::format("Offset in parent body frame: x {:.2f}  y {:.2f}  z {:.2f} m", e->offset.x,
                         e->offset.y, e->offset.z));
    }
    text(std::format("Position {:.6f}, {:.6f}, {:.2f} m MSL", e->position.lat_deg,
                     e->position.lon_deg, e->position.alt_m));
    text(std::format("Local ENU {:.1f} E, {:.1f} N, {:.1f} U m", e->enu.x, e->enu.y, e->enu.z));
    text(std::format("Terrain {:.1f} m   AGL {:.1f} m", e->terrain_m, e->agl_m));
    text(std::format("World attitude: yaw {:.1f}  pitch {:.1f}  roll {:.1f} deg",
                     e->world_attitude.yaw_deg, e->world_attitude.pitch_deg,
                     e->world_attitude.roll_deg));
    if (e->rates.active()) {
        text(std::format("Rates ({}): lin {:.1f}, {:.1f}, {:.1f} m/s; ang yaw {:.1f} pitch {:.1f} "
                         "roll {:.1f} deg/s",
                         mock::enum_name(e->rates.frame), e->rates.linear.x, e->rates.linear.y,
                         e->rates.linear.z, e->rates.angular.yaw_deg, e->rates.angular.pitch_deg,
                         e->rates.angular.roll_deg));
    }
    const auto children = scene.children_of(e->id);
    if (!children.empty()) {
        std::string list;
        for (const std::uint16_t c : children) {
            list += std::format(" {}", c);
        }
        text("Children:" + list);
    }
    if (ImGui::CollapsingHeader(std::format("Articulated parts ({})", e->parts.size()).c_str(),
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("parts", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            for (const char* h : {"Part", "On", "x", "y", "z", "yaw", "pitch", "roll"}) {
                ImGui::TableSetupColumn(h);
            }
            ImGui::TableHeadersRow();
            for (const auto& [pid, p] : e->parts) {
                ImGui::TableNextRow();
                const std::array<std::string, 8> cells{std::to_string(pid),
                                                       p.enabled ? "yes" : "no",
                                                       std::format("{:.2f}", p.offset.x),
                                                       std::format("{:.2f}", p.offset.y),
                                                       std::format("{:.2f}", p.offset.z),
                                                       std::format("{:.1f}", p.attitude.yaw_deg),
                                                       std::format("{:.1f}", p.attitude.pitch_deg),
                                                       std::format("{:.1f}", p.attitude.roll_deg)};
                for (const std::string& cell : cells) {
                    ImGui::TableNextColumn();
                    text(cell);
                }
            }
            ImGui::EndTable();
        }
    }
    if (ImGui::CollapsingHeader(
            std::format("Components ({}, stored not interpreted)", scene.components().size())
                .c_str())) {
        for (const auto& [key, c] : scene.components()) {
            text(std::format("class {} id {} instance {}: state {}", std::get<0>(key),
                             std::get<1>(key), std::get<2>(key), c.state));
        }
    }
    ImGui::End();
}

// ---- Packet inspector ---------------------------------------------------------------------

template <class Set>
void describe_message(std::span<const std::byte> bytes, const char* tag,
                      std::vector<std::string>& out) {
    if (bytes.empty()) {
        return;
    }
    const auto view = MessageView::parse(bytes);
    if (!view) {
        out.push_back(std::format("{} <unparsable: {}>", tag, to_string(view.error().code)));
        return;
    }
    for (const auto& item : *view) {
        if (!item) {
            out.push_back(std::format("{} <bad packet: {}>", tag, to_string(item.error().code)));
            break;
        }
        const auto decoded = decode_any<Set>(*item, view->direction());
        out.push_back(decoded ? std::format("{} {}", tag, mock::describe_any(*decoded))
                              : std::format("{} <opcode {}: {}>", tag, item->opcode,
                                            to_string(decoded.error().code)));
    }
}

void draw_inspector(AppState& app) {
    ImGui::SetNextWindowPos({1180, 440}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({420, 230}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Packet inspector", &app.ui.show_inspector)) {
        ImGui::End();
        return;
    }
    UiState& ui = app.ui;
    ImGui::Checkbox("Pause", &app.paused);
    ImGui::SameLine();
    if (ImGui::Button("Step")) {
        app.step_requests += 1;
    }
    ImGui::SameLine();
    const int max_back = std::max(0, static_cast<int>(app.history.size()) - 1);
    ImGui::SetNextItemWidth(130);
    ImGui::SliderInt("frames back", &ui.inspector_offset, 0, max_back);
    ui.inspector_offset = std::clamp(ui.inspector_offset, 0, max_back);
    static std::array<char, 64> filter{};
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputTextWithHint("##filter", "filter (e.g. Entity)", filter.data(),
                                 filter.size())) {
        ui.inspector_filter = filter.data();
    }
    if (app.history.empty()) {
        ImGui::TextDisabled("no frames yet");
        ImGui::End();
        return;
    }
    const FrameRecord& rec =
        app.history.at(app.history.size() - 1 - static_cast<std::size_t>(ui.inspector_offset));
    if (!ui.inspector_cache_valid || ui.inspector_cached_frame != rec.host_frame) {
        ui.inspector_lines.clear();
        describe_message<HostToIgPackets>(rec.host_message, "H>", ui.inspector_lines);
        describe_message<IgToHostPackets>(rec.ig_message, "<I", ui.inspector_lines);
        ui.inspector_cached_frame = rec.host_frame;
        ui.inspector_cache_valid = true;
    }
    text(std::format(
        "Host frame {}: Host message {} bytes, IG reply {} bytes   (H> Host to IG, <I IG to Host)",
        rec.host_frame, rec.host_message.size(), rec.ig_message.size()));
    ImGui::BeginChild("packets", {0, 0}, ImGuiChildFlags_Borders);
    for (const std::string& line : ui.inspector_lines) {
        if (!ui.inspector_filter.empty() && !line.contains(ui.inspector_filter)) {
            continue;
        }
        if (line.starts_with("<I")) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.55F, 0.85F, 1.0F, 1.0F});
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.85F, 0.85F, 0.85F, 1.0F});
        }
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::End();
}

// ---- HAT/HOT log --------------------------------------------------------------------------

void draw_hat_hot(AppState& app) {
    ImGui::SetNextWindowPos({1180, 680}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({420, 210}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("HAT/HOT log", &app.ui.show_hat_hot)) {
        ImGui::End();
        return;
    }
    const mock::SceneModel& scene = app.ig.scene();
    text(std::format("Periodic requests active: {}   waiting one-shots: {}",
                     app.ig.hat_hot().periodic_count(), app.ig.hat_hot().pending_count()));
    if (ImGui::BeginTable("probes", 9,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit |
                              ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        for (const char* h : {"IG fr", "ID", "Type", "Kind", "Valid", "Point", "HAT m", "HOT m",
                              "Material / normal"}) {
            ImGui::TableSetupColumn(h);
        }
        ImGui::TableHeadersRow();
        const auto& probes = scene.probes();
        for (auto it = probes.rbegin(); it != probes.rend(); ++it) {
            const mock::HatHotProbe& p = *it;
            ImGui::TableNextRow();
            const std::array<std::string, 9> cells{
                std::to_string(p.ig_frame),
                std::to_string(p.request.hat_hot_id),
                mock::enum_name(p.request.request_type),
                p.periodic ? std::format("every {}", p.request.update_period) : "one-shot",
                "",
                std::format("{:.5f}, {:.5f}, {:.1f}", p.point.lat_deg, p.point.lon_deg,
                            p.point.alt_m),
                p.valid ? std::format("{:.2f}", p.hat_m) : "-",
                p.valid ? std::format("{:.2f}", p.hot_m) : "-",
                p.valid
                    ? std::format("mat {}  az {:.0f} el {:.0f}", static_cast<unsigned>(p.material),
                                  p.normal.azimuth_deg, p.normal.elevation_deg)
                    : "-"};
            for (std::size_t c = 0; c < cells.size(); ++c) {
                ImGui::TableNextColumn();
                if (c == 4) {
                    ImGui::TextColored(p.valid ? ImVec4{0.4F, 1.0F, 0.4F, 1.0F}
                                               : ImVec4{1.0F, 0.4F, 0.4F, 1.0F},
                                       p.valid ? "valid" : "INVALID");
                } else {
                    text(cells.at(c));
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void draw_log(AppState& app) {
    ImGui::SetNextWindowPos({660, 700}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({510, 190}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("IG log")) {
        ImGui::End();
        return;
    }
    for (const std::string& line : app.ig.scene().log()) {
        text(line);
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0F);
    }
    ImGui::End();
}

// ---- View options ---------------------------------------------------------------------------

void draw_view_options(AppState& app) {
    ImGui::SetNextWindowPos({420, 10}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({210, 260}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("View", &app.ui.show_view)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("C cycles Host / Chase / Free");
    int mode = static_cast<int>(app.cam.mode);
    ImGui::RadioButton("Host view", &mode, 0);
    ImGui::RadioButton("Chase (drag, wheel)", &mode, 1);
    ImGui::RadioButton("Free (WASD, RMB)", &mode, 2);
    app.cam.mode = static_cast<CameraKind>(mode);
    ImGui::SliderFloat("Model scale", &app.draw.shape_scale, 1.0F, 10.0F, "%.1fx");
    ImGui::Checkbox("Trails", &app.draw.trails);
    ImGui::Checkbox("Labels", &app.draw.labels);
    ImGui::Checkbox("Drop lines", &app.draw.drop_lines);
    ImGui::Checkbox("Parts", &app.draw.parts);
    ImGui::Checkbox("Terrain grid (100 m)", &app.draw.terrain_grid);
    ImGui::End();
}

// ---- Map ------------------------------------------------------------------------------------

void draw_map(AppState& app) {
    ImGui::SetNextWindowPos({1180, 10}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({420, 420}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Map", &app.ui.show_map)) {
        ImGui::End();
        return;
    }
    UiState& ui = app.ui;
    const mock::SceneModel& scene = app.ig.scene();
    ImGui::Checkbox("Follow selected", &ui.map_follow);
    ImGui::SameLine();
    ImGui::TextDisabled("drag = pan, wheel = zoom, click = select");
    if (ui.map_follow) {
        if (const Entity* e = scene.find(app.selected)) {
            ui.map_center = {e->enu.x, e->enu.y, 0.0};
        }
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 60.0F || avail.y < 60.0F) {
        ImGui::End();
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("map_area", avail, ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0F)) {
        ui.map_center.x -= static_cast<double>(io.MouseDelta.x / ui.map_scale);
        ui.map_center.y += static_cast<double>(io.MouseDelta.y / ui.map_scale);
        ui.map_follow = false;
    }
    if (hovered && io.MouseWheel != 0.0F) {
        ui.map_scale = std::clamp(ui.map_scale * std::pow(1.15F, io.MouseWheel), 0.01F, 3.0F);
    }
    const ImVec2 centre{origin.x + (avail.x / 2.0F), origin.y + (avail.y / 2.0F)};
    const float scale = ui.map_scale;
    const auto to_screen = [&](double east, double north) {
        return ImVec2{centre.x + (static_cast<float>(east - ui.map_center.x) * scale),
                      centre.y - (static_cast<float>(north - ui.map_center.y) * scale)};
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, {origin.x + avail.x, origin.y + avail.y}, true);

    // Terrain shading, hill-shaded from the north-west. Cells sit on a world-aligned grid whose
    // size is a power of two metres (5..10 px on screen), so panning and following reuse cached
    // colours instead of sampling the terrain again every frame.
    static std::unordered_map<std::uint64_t, ImU32> shade_cache;
    static int shade_k = 1000;
    const int k = static_cast<int>(std::ceil(std::log2(5.0F / scale)));
    if (k != shade_k || shade_cache.size() > 30000) {
        shade_cache.clear();
        shade_k = k;
    }
    const double cell_m = std::exp2(static_cast<double>(k));
    const float cell_px = static_cast<float>(cell_m) * scale;
    const double half_w = static_cast<double>(avail.x) / 2.0 / static_cast<double>(scale);
    const double half_h = static_cast<double>(avail.y) / 2.0 / static_cast<double>(scale);
    const auto first_ix =
        static_cast<std::int64_t>(std::floor((ui.map_center.x - half_w) / cell_m));
    const auto last_ix = static_cast<std::int64_t>(std::floor((ui.map_center.x + half_w) / cell_m));
    const auto first_iy =
        static_cast<std::int64_t>(std::floor((ui.map_center.y - half_h) / cell_m));
    const auto last_iy = static_cast<std::int64_t>(std::floor((ui.map_center.y + half_h) / cell_m));
    for (std::int64_t iy = first_iy; iy <= last_iy; ++iy) {
        for (std::int64_t ix = first_ix; ix <= last_ix; ++ix) {
            const std::uint64_t key = (static_cast<std::uint64_t>(ix + 2147483648LL) << 32U) |
                                      static_cast<std::uint64_t>(iy + 2147483648LL);
            auto it = shade_cache.find(key);
            if (it == shade_cache.end()) {
                const double east = (static_cast<double>(ix) + 0.5) * cell_m;
                const double north = (static_cast<double>(iy) + 0.5) * cell_m;
                const mock::Geodetic g = scene.frame().to_geodetic({east, north, 0.0});
                Rgb rgb = terrain_rgb(mock::terrain_height(g.lat_deg, g.lon_deg));
                const Vec3d grad = mock::terrain_gradient(g.lat_deg, g.lon_deg);
                const auto shade =
                    std::clamp(0.8F + static_cast<float>(1.6 * (grad.x - grad.y)), 0.55F, 1.1F);
                const float dim =
                    scene.config().bounds.contains(g.lat_deg, g.lon_deg) ? 1.0F : 0.45F;
                for (float& channel : rgb) {
                    channel = std::clamp(channel * shade * dim, 0.0F, 1.0F);
                }
                const ImU32 col =
                    IM_COL32(rgb.at(0) * 255.0F, rgb.at(1) * 255.0F, rgb.at(2) * 255.0F, 255);
                it = shade_cache.emplace(key, col).first;
            }
            const ImVec2 p0 =
                to_screen(static_cast<double>(ix) * cell_m, static_cast<double>(iy + 1) * cell_m);
            dl->AddRectFilled(p0, {p0.x + cell_px + 0.5F, p0.y + cell_px + 0.5F}, it->second);
        }
    }

    // 1 km grid lines and the scenario origin.
    const double grid_m = 1000.0;
    const double first_e = std::floor((ui.map_center.x - (static_cast<double>(avail.x) / 2.0 /
                                                          static_cast<double>(scale))) /
                                      grid_m) *
                           grid_m;
    for (double e = first_e;
         e < ui.map_center.x + (static_cast<double>(avail.x) / static_cast<double>(scale));
         e += grid_m) {
        dl->AddLine(to_screen(e, ui.map_center.y - 1.0e5), to_screen(e, ui.map_center.y + 1.0e5),
                    IM_COL32(0, 0, 0, 40));
    }
    const double first_n = std::floor((ui.map_center.y - (static_cast<double>(avail.y) / 2.0 /
                                                          static_cast<double>(scale))) /
                                      grid_m) *
                           grid_m;
    for (double n = first_n;
         n < ui.map_center.y + (static_cast<double>(avail.y) / static_cast<double>(scale));
         n += grid_m) {
        dl->AddLine(to_screen(ui.map_center.x - 1.0e5, n), to_screen(ui.map_center.x + 1.0e5, n),
                    IM_COL32(0, 0, 0, 40));
    }
    dl->AddCircle(to_screen(0.0, 0.0), 4.0F, IM_COL32(255, 255, 255, 200));

    // Host-view frustum wedge.
    std::uint16_t view_id = 0;
    if (find_host_view(scene, view_id)) {
        if (const auto pose = scene.view_pose(view_id)) {
            const mock::ViewState& v = *scene.find_view(view_id);
            const double yaw = std::atan2(pose->body_to_enu.c0.x, pose->body_to_enu.c0.y);
            const double half = v.horizontal_fov_deg() * mock::deg_to_rad / 2.0;
            const double range = std::min(static_cast<double>(v.far_plane), 1500.0);
            std::array<ImVec2, 8> poly;
            poly.at(0) = to_screen(pose->position_enu.x, pose->position_enu.y);
            for (std::size_t i = 0; i < 7; ++i) {
                const double a = yaw - half + (2.0 * half * static_cast<double>(i) / 6.0);
                poly.at(i + 1) = to_screen(pose->position_enu.x + (range * std::sin(a)),
                                           pose->position_enu.y + (range * std::cos(a)));
            }
            dl->AddConvexPolyFilled(poly.data(), static_cast<int>(poly.size()),
                                    IM_COL32(255, 255, 255, 45));
            dl->AddPolyline(poly.data(), static_cast<int>(poly.size()),
                            IM_COL32(255, 255, 255, 130), ImDrawFlags_Closed, 1.0F);
        }
    }

    // HAT/HOT probes of the last two seconds.
    for (const mock::HatHotProbe& p : scene.probes()) {
        if (scene.frame_count() > 120 && p.ig_frame + 120 < scene.frame_count()) {
            continue;
        }
        const Vec3d at = scene.frame().to_enu(p.point);
        const ImVec2 s = to_screen(at.x, at.y);
        const ImU32 col = p.valid ? IM_COL32(60, 255, 90, 255) : IM_COL32(255, 60, 60, 255);
        dl->AddCircle(s, 5.0F, col, 12, 2.0F);
        dl->AddText({s.x + 6.0F, s.y - 6.0F}, col,
                    std::format("#{}", p.request.hat_hot_id).c_str());
    }

    // Entities.
    const Entity* nearest = nullptr;
    float nearest_dist = 14.0F;
    std::vector<ImVec4> label_rects; // placed ID labels (min.x, min.y, max.x, max.y)
    for (const auto& [id, e] : scene.entities()) {
        if (!e.visible) {
            continue;
        }
        if (app.draw.trails && e.trail.size() > 1) {
            std::vector<ImVec2> pts;
            // ~24 points per entity keeps the map cheap (ImDrawIdx is 32-bit, see Dependencies.cmake).
            const std::size_t first = e.trail.size() > 192 ? e.trail.size() - 192 : 0;
            for (std::size_t i = first; i < e.trail.size(); i += 8) {
                pts.push_back(to_screen(e.trail.at(i).x, e.trail.at(i).y));
            }
            pts.push_back(to_screen(e.enu.x, e.enu.y));
            dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), entity_im_color(id, 0.55F),
                            ImDrawFlags_None, 1.5F);
        }
        const ImVec2 s = to_screen(e.enu.x, e.enu.y);
        const double yaw = e.world_attitude.yaw_deg * mock::deg_to_rad;
        const ImVec2 fwd{static_cast<float>(std::sin(yaw)), static_cast<float>(-std::cos(yaw))};
        const ImVec2 right{-fwd.y, fwd.x};
        const ImU32 col = entity_im_color(id);
        const float r = id == app.selected ? 9.0F : 7.0F;
        switch (mock::classify_entity_type(e.entity_type)) {
        case mock::ShapeClass::Aircraft:
            dl->AddTriangleFilled({s.x + (fwd.x * r * 1.4F), s.y + (fwd.y * r * 1.4F)},
                                  {s.x - (fwd.x * r) + (right.x * r * 0.9F),
                                   s.y - (fwd.y * r) + (right.y * r * 0.9F)},
                                  {s.x - (fwd.x * r) - (right.x * r * 0.9F),
                                   s.y - (fwd.y * r) - (right.y * r * 0.9F)},
                                  col);
            break;
        case mock::ShapeClass::GroundVehicle:
            dl->AddQuadFilled(
                {s.x + ((fwd.x + right.x * 0.6F) * r), s.y + ((fwd.y + right.y * 0.6F) * r)},
                {s.x + ((fwd.x - right.x * 0.6F) * r), s.y + ((fwd.y - right.y * 0.6F) * r)},
                {s.x + ((-fwd.x - right.x * 0.6F) * r), s.y + ((-fwd.y - right.y * 0.6F) * r)},
                {s.x + ((-fwd.x + right.x * 0.6F) * r), s.y + ((-fwd.y + right.y * 0.6F) * r)},
                col);
            dl->AddLine(s, {s.x + (fwd.x * r * 1.6F), s.y + (fwd.y * r * 1.6F)},
                        IM_COL32(255, 255, 255, 230), 2.0F);
            break;
        default:
            dl->AddCircleFilled(s, r * 0.7F, col);
            dl->AddLine(s, {s.x + (fwd.x * r * 1.6F), s.y + (fwd.y * r * 1.6F)},
                        IM_COL32(255, 255, 255, 230), 2.0F);
            break;
        }
        if (id == app.selected) {
            dl->AddCircle(s, r + 4.0F, IM_COL32(255, 235, 60, 255), 24, 2.0F);
        }
        if (app.draw.labels && (scale > 0.05F || id == app.selected || id < 100)) {
            // Try the right of the icon, then above, below and left; take the first free spot
            // so neighbouring entities (e.g. 3 and 4) never print on top of each other.
            const std::string text = std::to_string(id);
            const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
            const std::array<ImVec2, 6> candidates{{{s.x + r + 3.0F, s.y - (ts.y * 0.5F)},
                                                    {s.x + r + 3.0F, s.y - r - ts.y},
                                                    {s.x + r + 3.0F, s.y + r},
                                                    {s.x - r - 3.0F - ts.x, s.y - (ts.y * 0.5F)},
                                                    {s.x - (ts.x * 0.5F), s.y - r - ts.y - 6.0F},
                                                    {s.x - (ts.x * 0.5F), s.y + r + 6.0F}}};
            for (const ImVec2& c : candidates) {
                const ImVec4 rect{c.x - 1.0F, c.y, c.x + ts.x + 1.0F, c.y + ts.y};
                const bool free = std::ranges::none_of(label_rects, [&](const ImVec4& q) {
                    return rect.x < q.z && q.x < rect.z && rect.y < q.w && q.y < rect.w;
                });
                if (free) {
                    label_rects.push_back(rect);
                    dl->AddRectFilled({rect.x, rect.y}, {rect.z, rect.w}, IM_COL32(0, 0, 0, 120));
                    dl->AddText(c, IM_COL32(255, 255, 255, 235), text.c_str());
                    break;
                }
            }
        }
        const float d = std::hypot(io.MousePos.x - s.x, io.MousePos.y - s.y);
        if (d < nearest_dist) {
            nearest_dist = d;
            nearest = &e;
        }
    }
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0F) && nearest != nullptr) {
        select_entity(app, nearest->id);
    }

    // Scale bar and north arrow.
    float bar_m = 100.0F;
    while (bar_m * scale < 60.0F) {
        bar_m *= 2.0F;
    }
    const ImVec2 bar0{origin.x + 12.0F, origin.y + avail.y - 14.0F};
    dl->AddLine(bar0, {bar0.x + (bar_m * scale), bar0.y}, IM_COL32(255, 255, 255, 255), 2.0F);
    dl->AddText({bar0.x, bar0.y - 16.0F}, IM_COL32(255, 255, 255, 255),
                std::format("{:.0f} m", static_cast<double>(bar_m)).c_str());
    dl->AddText({origin.x + avail.x - 20.0F, origin.y + 4.0F}, IM_COL32(255, 255, 255, 255), "N");
    dl->AddLine({origin.x + avail.x - 14.0F, origin.y + 36.0F},
                {origin.x + avail.x - 14.0F, origin.y + 20.0F}, IM_COL32(255, 255, 255, 255), 2.0F);
    dl->PopClipRect();
    ImGui::End();
}

} // namespace

void draw_ui(AppState& app, const CameraResult& cam, const SkyState& sky, float fps) {
    if (app.ui.mode == UiMode::None) {
        return;
    }
    if (app.ui.show_map) {
        draw_map(app);
    }
    if (app.ui.mode == UiMode::Map) {
        return;
    }
    if (app.ui.show_link) {
        draw_link(app, cam, sky, fps);
    }
    if (app.ui.show_view) {
        draw_view_options(app);
    }
    if (app.ui.show_entities) {
        draw_entities(app);
    }
    if (app.ui.show_selected) {
        draw_selected(app);
    }
    if (app.ui.show_inspector) {
        draw_inspector(app);
    }
    if (app.ui.show_hat_hot) {
        draw_hat_hot(app);
    }
    draw_log(app);
}

} // namespace cigi::viewer
