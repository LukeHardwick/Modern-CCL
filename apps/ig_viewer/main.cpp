// ig_viewer: a mock IG that shows what a CIGI Host is telling it. Part 1 runs a built-in scripted
// Host (no network). The scene logic lives in the cigi_mock_ig library; this file is window,
// input and the frame loop.

#include "app.hpp"
#include "args.hpp"
#include "renderer.hpp"
#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <imgui.h>
#include <raylib.h>
#include <rlImGui.h>
#include <rlgl.h>
#include <span>
#include <string>

namespace {

using namespace cigi;
using namespace cigi::viewer;

/// Switch to the next camera mode (key C). Entering Free continues from where the camera was.
void cycle_camera(AppState& app, const CameraResult& current) {
    switch (app.cam.mode) {
    case CameraKind::Host:
        app.cam.mode = CameraKind::Chase;
        break;
    case CameraKind::Chase:
        app.cam.mode = CameraKind::Free;
        app.cam.free_position = current.position;
        app.cam.free_yaw_deg = static_cast<float>(current.yaw_deg);
        break;
    case CameraKind::Free:
        app.cam.mode = CameraKind::Host;
        break;
    }
}

void update_camera_input(AppState& app, float frame_time) {
    const ImGuiIO& io = ImGui::GetIO();
    CameraState& cam = app.cam;
    if (cam.mode == CameraKind::Chase && !io.WantCaptureMouse) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const Vector2 d = GetMouseDelta();
            cam.chase_yaw_deg -= d.x * 0.3F;
            cam.chase_pitch_deg = std::clamp(cam.chase_pitch_deg + (d.y * 0.3F), -10.0F, 85.0F);
        }
        cam.chase_distance_m =
            std::clamp(cam.chase_distance_m * std::pow(0.9F, GetMouseWheelMove()), 8.0F, 3000.0F);
    } else if (cam.mode == CameraKind::Free) {
        if (!io.WantCaptureMouse && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            const Vector2 d = GetMouseDelta();
            cam.free_yaw_deg += d.x * 0.15F;
            cam.free_pitch_deg = std::clamp(cam.free_pitch_deg - (d.y * 0.15F), -89.0F, 89.0F);
        }
        if (!io.WantCaptureKeyboard) {
            const double yaw = static_cast<double>(cam.free_yaw_deg) * mock::deg_to_rad;
            const double pitch = static_cast<double>(cam.free_pitch_deg) * mock::deg_to_rad;
            const mock::Vec3d forward{std::sin(yaw) * std::cos(pitch),
                                      std::cos(yaw) * std::cos(pitch), std::sin(pitch)};
            const mock::Vec3d right{std::cos(yaw), -std::sin(yaw), 0.0};
            double speed = 150.0 * static_cast<double>(frame_time);
            if (IsKeyDown(KEY_LEFT_SHIFT)) {
                speed *= 5.0;
            }
            if (IsKeyDown(KEY_LEFT_CONTROL)) {
                speed *= 0.2;
            }
            mock::Vec3d move{};
            move = move +
                   (forward * ((IsKeyDown(KEY_W) ? 1.0 : 0.0) - (IsKeyDown(KEY_S) ? 1.0 : 0.0)));
            move =
                move + (right * ((IsKeyDown(KEY_D) ? 1.0 : 0.0) - (IsKeyDown(KEY_A) ? 1.0 : 0.0)));
            move = move + (mock::Vec3d{0.0, 0.0, 1.0} *
                           ((IsKeyDown(KEY_E) ? 1.0 : 0.0) - (IsKeyDown(KEY_Q) ? 1.0 : 0.0)));
            cam.free_position = cam.free_position + (move * speed);
        }
    }
}

/// Advance the simulation for this rendered frame.
void run_simulation(AppState& app, float frame_time, bool fixed_step) {
    int steps = 0;
    if (fixed_step) {
        steps = 1; // deterministic output for screenshots and benchmarks
    } else {
        if (!app.paused) {
            app.accumulator += static_cast<double>(frame_time) * static_cast<double>(app.speed);
            steps = std::min(static_cast<int>(app.accumulator / AppState::dt), 4);
            app.accumulator -= steps * AppState::dt;
            app.accumulator = std::min(app.accumulator, AppState::dt);
        }
        steps += app.step_requests;
        app.step_requests = 0;
    }
    for (int i = 0; i < steps; ++i) {
        step_simulation(app);
    }
    app.rate_timer += static_cast<double>(frame_time);
    if (app.rate_timer >= 1.0) {
        app.packets_per_s =
            static_cast<double>(app.total_packets - app.rate_packets_mark) / app.rate_timer;
        app.bytes_per_s =
            static_cast<double>(app.total_bytes - app.rate_bytes_mark) / app.rate_timer;
        app.rate_packets_mark = app.total_packets;
        app.rate_bytes_mark = app.total_bytes;
        app.rate_timer = 0.0;
    }
}

/// Aspect ratio of the 3-D viewport. BeginMode3D() builds its projection from the framebuffer
/// (render) size, so use the same pair here; the screen size is in logical points and only
/// agrees with it up to integer rounding on a Retina display.
float viewport_aspect() {
    return static_cast<float>(GetRenderWidth()) /
           static_cast<float>(std::max(GetRenderHeight(), 1));
}

bool is_fullscreen() {
    return IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
}

/// True when the window fills its monitor without our borderless mode: the native macOS
/// fullscreen Space entered with the green button (which arrives only as a resize).
bool in_native_fullscreen() {
    if (is_fullscreen()) {
        return false;
    }
    const int monitor = GetCurrentMonitor();
    return GetScreenWidth() >= GetMonitorWidth(monitor) &&
           GetScreenHeight() >= GetMonitorHeight(monitor);
}

void set_fullscreen(bool on) {
    // PLATFORM: borderless fullscreen (a window the size of the current monitor) rather than
    // raylib's ToggleFullscreen(). ToggleFullscreen() switches raylib to "physical pixel" mode,
    // where GetScreenWidth() jumps from logical points to device pixels and GetWindowScaleDPI()
    // reports 1; on a macOS Retina display that disagrees with the 2x framebuffer scale ImGui
    // was told about and leaves the UI mis-sized. ToggleBorderlessWindowed() keeps the logical
    // coordinate system and the HiDPI scale, so only the window size changes. The native macOS
    // green button (a Space) also arrives as a plain resize, so the layout needs no special
    // handling. GLFW cannot leave a native Space, so F/F11/Esc ignore it (in_native_fullscreen).
    // Whether this borderless window covers the menu bar and Dock is unverified: raylib asks GLFW
    // for a monitor-sized window on the monitor, which on macOS is normally a full-screen window
    // that hides both, but I have not checked it by eye.
    if (is_fullscreen() != on) {
        ToggleBorderlessWindowed();
    }
}

bool save_screenshot(const std::string& path) {
    Image image = LoadImageFromScreen();
    const bool ok = ExportImage(image, path.c_str());
    UnloadImage(image);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    const Args args = parse_args(argc - 1, argv + 1); // NOLINT: argv is the C entry point
    if (args.help || !args.error.empty()) {
        if (!args.error.empty()) {
            std::fprintf(stderr, "ig_viewer: %s\n", args.error.c_str());
        }
        std::fputs(usage(), args.error.empty() ? stdout : stderr);
        return args.error.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    mock::ScriptedHostConfig host_config;
    host_config.origin = {args.origin_lat, args.origin_lon, 0.0};
    host_config.extra_entities = static_cast<std::uint16_t>(args.stress);
    host_config.start_utc_hours = args.start_hour;
    mock::SceneConfig scene_config;
    scene_config.origin = host_config.origin;
    scene_config.bounds = mock::DatabaseBounds::around(host_config.origin, 0.25);
    AppState app(args, host_config, scene_config);

    const bool offline_capture = args.screenshot.has_value() || args.bench > 0;
    unsigned flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE;
    if (!offline_capture) {
        // PLATFORM: HiDPI scaling and vsync are requested through raylib's window flags; macOS
        // Retina displays render at the logical size times the scale factor.
        flags |= FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI;
    } else if (args.hidpi) {
        flags |=
            FLAG_WINDOW_HIGHDPI; // test hook: captures are then framebuffer-sized (2x on Retina)
    }
    SetConfigFlags(flags);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(args.width, args.height, "CIGI mock IG viewer");
    if (!IsWindowReady()) {
        std::fputs("ig_viewer: could not create a window\n", stderr);
        return EXIT_FAILURE;
    }
    if (!offline_capture) {
        SetTargetFPS(60);
    }
    rlImGuiSetup(true);
    // Panel layout comes from the window size (mock::compute_panel_layout), not from a saved file.
    ImGui::GetIO().IniFilename = nullptr;
    // Esc leaves fullscreen first and quits only from a normal window (see the loop).
    SetExitKey(KEY_NULL);
    SetWindowMinSize(320, 240); // the smallest size the panel layout is designed for
    if (args.fullscreen) {
        set_fullscreen(true);
    }

    int exit_code = EXIT_SUCCESS;
    {
        Renderer renderer;
        // The first scripted frame defines the world (entities, view); run it before drawing.
        step_simulation(app);
        renderer.init(app.ig.scene());

        const int total_frames = args.bench > 0 ? args.bench : args.frames;
        const double start_time = GetTime();
        int frame = 0;
        while (!WindowShouldClose()) {
            const float frame_time = std::max(GetFrameTime(), 0.0001F);
            if (args.resize_w > 0 && frame == 3) {
                SetWindowSize(args.resize_w, args.resize_h); // test hook for the resize path
            }
            if (args.toggle_fs > 0 && frame > 0 && frame % args.toggle_fs == 0) {
                set_fullscreen(!is_fullscreen()); // test hook: same path as the F key
            }
            const mock::SceneModel& scene = app.ig.scene();

            // Input first, against the camera as drawn last frame.
            const ImGuiIO& io = ImGui::GetIO();
            if (!io.WantTextInput) {
                // In native fullscreen (green button) F/F11/Esc must not stack our borderless mode
                // on top of it or quit by accident; the user leaves it with the green button.
                const bool native_fs = in_native_fullscreen();
                if ((IsKeyPressed(KEY_F) || IsKeyPressed(KEY_F11)) && !native_fs) {
                    set_fullscreen(!is_fullscreen());
                }
                if (IsKeyPressed(KEY_ESCAPE) && !native_fs) {
                    if (is_fullscreen()) {
                        set_fullscreen(false);
                    } else {
                        break;
                    }
                }
                if (IsKeyPressed(KEY_L)) {
                    app.ui.relayout_requested = true;
                }
                if (IsKeyPressed(KEY_C)) {
                    cycle_camera(app, make_camera(scene, app.cam, viewport_aspect()));
                }
                if (IsKeyPressed(KEY_P)) {
                    app.paused = !app.paused;
                }
                if (IsKeyPressed(KEY_N)) {
                    app.step_requests += 1;
                }
                if (IsKeyPressed(KEY_TAB)) {
                    app.ui.mode = app.ui.mode == UiMode::None ? UiMode::All : UiMode::None;
                }
            }
            update_camera_input(app, frame_time);
            run_simulation(app, frame_time, offline_capture);

            // Sizes are read after the input above, which may have toggled fullscreen. 2-D drawing
            // (background, labels, HUD, ImGui) uses logical screen points; raylib scales them to
            // the framebuffer on HiDPI displays. The 3-D viewport uses the render size.
            const int screen_w = GetScreenWidth();
            const int screen_h = GetScreenHeight();
            const CameraResult cam = make_camera(scene, app.cam, viewport_aspect());
            const SkyState sky = compute_sky(scene);

            BeginDrawing();
            renderer.draw_background(sky, cam, screen_w, screen_h);
            rlSetClipPlanes(cam.near_m, cam.far_m);
            BeginMode3D(cam.camera);
            renderer.draw_world(scene, sky, cam, app.draw, app.selected,
                                app.cam.mode == CameraKind::Host && cam.host_view);
            EndMode3D();
            renderer.draw_overlay(cam, sky, scene, app.cam, app.draw, app.selected);

            rlImGuiBegin();
            draw_ui(app, cam, sky, static_cast<float>(GetFPS()));
            rlImGuiEnd();

            ++frame;
            const bool last = offline_capture && frame >= total_frames;
            if (last && args.screenshot) {
                // Read the finished frame before the buffers are swapped.
                if (!save_screenshot(*args.screenshot)) {
                    std::fprintf(stderr, "ig_viewer: could not write %s\n",
                                 args.screenshot->c_str());
                    exit_code = EXIT_FAILURE;
                }
            }
            EndDrawing();
            if (last) {
                if (args.bench > 0) {
                    const double seconds = GetTime() - start_time;
                    std::printf("bench: %d frames, %zu entities, %.1f fps (%.2f ms/frame)\n", frame,
                                scene.entities().size(), frame / seconds, 1000.0 * seconds / frame);
                }
                break;
            }
        }
    }
    rlImGuiShutdown();
    CloseWindow();
    return exit_code;
}
