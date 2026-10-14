#pragma once

// Command line of ig_viewer (Part 1: offline scripted mode only). Deliberately tiny; a real CLI
// library arrives with the network mode.

#include <optional>
#include <string>

namespace cigi::viewer {

enum class CameraKind { Host, Chase, Free };
enum class UiMode { None, Map, All };

struct Args {
    bool scripted = true; ///< The only mode in Part 1.
    double origin_lat = 51.5;
    double origin_lon = -0.12;
    std::optional<std::string> db_root; ///< --db-root DIR: serve the databases under DIR (T20).
    int database = 1;                   ///< --database N: the one in force at start.
    int width = 1280;
    int height = 720;
    bool fullscreen = false; ///< Start in borderless fullscreen (--fullscreen).
    bool hidpi = false;      ///< Hidden: request a HiDPI framebuffer in capture mode.
    int resize_w = 0;        ///< Hidden: --resize-to WxH, resize the window at frame 3.
    int resize_h = 0;
    int toggle_fs = 0; ///< Hidden: --toggle-fs N, toggle fullscreen at frames N, 2N, ...
    std::optional<std::string> screenshot; ///< Render --frames frames, save a PNG, exit.
    int frames = 120;
    int stress = 0;                       ///< Hidden: extra scripted entities.
    int bench = 0;                        ///< Hidden: render N frames unthrottled, print fps.
    CameraKind camera = CameraKind::Host; ///< Initial camera (--camera host|chase|free).
    UiMode ui = UiMode::All;              ///< Which panels to show (--ui none|map|all).
    int select = 0;                       ///< Entity selected at start (--select ID).
    double start_hour = 10.0;             ///< Scripted UTC start (--start-hour H).
    bool help = false;
    std::string error; ///< Non-empty: the arguments were invalid.
};

/// Parse argv (without the program name). Never throws; problems are reported in `error`.
[[nodiscard]] Args parse_args(int argc, const char* const* argv);

[[nodiscard]] const char* usage() noexcept;

} // namespace cigi::viewer
