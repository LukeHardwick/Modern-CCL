#include "args.hpp"

#include <charconv>
#include <span>
#include <string_view>

namespace cigi::viewer {

namespace {

template <class T> bool parse_number(std::string_view text, T& out) {
    const auto* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, out);
    return ec == std::errc{} && ptr == end;
}

} // namespace

const char* usage() noexcept {
    return "usage: ig_viewer [options]\n"
           "  --scripted                 built-in scripted Host (the default; Part 1)\n"
           "  --origin LAT,LON           centre of the scenario in degrees (default 51.5,-0.12)\n"
           "  --width N --height N       window size (default 1280x720)\n"
           "  --fullscreen               start in borderless fullscreen (F or F11 toggles)\n"
           "  --camera host|chase|free   initial camera\n"
           "  --ui none|map|all          panels to show\n"
           "  --select ID                entity selected at start\n"
           "  --start-hour H             scripted UTC time of day at start (default 10)\n"
           "  --screenshot FILE.png      render --frames N frames, save a PNG and exit\n"
           "  --frames N                 frames for --screenshot (default 120)\n"
           "  --stress N                 add N extra scripted entities (load testing)\n"
           "  --bench N                  render N frames unthrottled, print the frame rate\n"
           "  --help\n";
}

Args parse_args(int argc, const char* const* argv) {
    Args a;
    const std::span<const char* const> list(argv, static_cast<std::size_t>(argc));
    for (std::size_t i = 0; i < list.size() && a.error.empty(); ++i) {
        const std::string_view arg = list[i];
        const auto next = [&]() -> std::string_view {
            if (i + 1 >= list.size()) {
                a.error = "missing value for " + std::string(arg);
                return {};
            }
            return list[++i];
        };
        const auto number = [&](auto& out) {
            const std::string_view v = next();
            if (!a.error.empty()) {
                return;
            }
            if (!parse_number(v, out)) {
                a.error = "bad value for " + std::string(arg) + ": " + std::string(v);
            }
        };
        if (arg == "--scripted") {
            a.scripted = true;
        } else if (arg == "--help" || arg == "-h") {
            a.help = true;
        } else if (arg == "--fullscreen") {
            a.fullscreen = true;
        } else if (arg == "--toggle-fs") {
            number(a.toggle_fs);
        } else if (arg == "--hidpi") {
            a.hidpi = true;
        } else if (arg == "--resize-to") {
            const std::string_view v = next();
            const auto x = v.find('x');
            if (!a.error.empty()) {
                continue;
            }
            if (x == std::string_view::npos || !parse_number(v.substr(0, x), a.resize_w) ||
                !parse_number(v.substr(x + 1), a.resize_h) || a.resize_w < 320 ||
                a.resize_h < 240) {
                a.error = "--resize-to expects WxH (at least 320x240)";
            }
        } else if (arg == "--width") {
            number(a.width);
        } else if (arg == "--height") {
            number(a.height);
        } else if (arg == "--frames") {
            number(a.frames);
        } else if (arg == "--stress") {
            number(a.stress);
        } else if (arg == "--bench") {
            number(a.bench);
        } else if (arg == "--select") {
            number(a.select);
        } else if (arg == "--start-hour") {
            number(a.start_hour);
        } else if (arg == "--screenshot") {
            const std::string_view v = next();
            if (a.error.empty()) {
                a.screenshot = std::string(v);
            }
        } else if (arg == "--origin") {
            const std::string_view v = next();
            const auto comma = v.find(',');
            if (!a.error.empty()) {
                continue;
            }
            if (comma == std::string_view::npos ||
                !parse_number(v.substr(0, comma), a.origin_lat) ||
                !parse_number(v.substr(comma + 1), a.origin_lon)) {
                a.error = "--origin expects LAT,LON";
            }
        } else if (arg == "--camera") {
            const std::string_view v = next();
            if (v == "host") {
                a.camera = CameraKind::Host;
            } else if (v == "chase") {
                a.camera = CameraKind::Chase;
            } else if (v == "free") {
                a.camera = CameraKind::Free;
            } else if (a.error.empty()) {
                a.error = "--camera expects host, chase or free";
            }
        } else if (arg == "--ui") {
            const std::string_view v = next();
            if (v == "none") {
                a.ui = UiMode::None;
            } else if (v == "map") {
                a.ui = UiMode::Map;
            } else if (v == "all") {
                a.ui = UiMode::All;
            } else if (a.error.empty()) {
                a.error = "--ui expects none, map or all";
            }
        } else {
            a.error = "unknown option " + std::string(arg);
        }
    }
    if (a.error.empty() && (a.width < 320 || a.height < 240)) {
        a.error = "window is too small";
    }
    if (a.error.empty() && (a.origin_lat < -89.0 || a.origin_lat > 89.0)) {
        a.error = "origin latitude must be within +-89 degrees";
    }
    if (a.error.empty() && (a.stress < 0 || a.stress > 1000)) {
        a.error = "--stress must be 0..1000 (the Host message must fit in a UDP datagram)";
    }
    return a;
}

} // namespace cigi::viewer
