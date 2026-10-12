// A minimal Host: connect to an IG, wait for Operate and fly one entity in a circle.
//   host_minimal [--ig-address 127.0.0.1] [--seconds 30]   (Ctrl-C stops it)
#include "cigi/host.hpp"

#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <memory>
#include <print>
#include <span>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>

namespace { // Set by SIGINT (a plain flag is async-signal-safe); the timer thread polls it.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_interrupted = 0;
} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape): std::println may throw; an example may terminate.
int main(int argc, char** argv) {
    cigi::HostConfig config{.ig_address = "127.0.0.1"};
    int seconds = 30;
    const std::span<const char* const> args(argv, static_cast<std::size_t>(argc));
    for (auto arg = args.begin() + 1; arg != args.end() && arg + 1 != args.end(); arg += 2) {
        const std::string_view key = *arg;
        const std::string_view value = *(arg + 1);
        if (key == "--ig-address") {
            config.ig_address = value;
        } else if (key == "--seconds") {
            static_cast<void>(std::from_chars(std::to_address(value.begin()),
                                              std::to_address(value.end()), seconds));
        }
    }
    static_cast<void>(std::signal(SIGINT, [](int) { g_interrupted = 1; }));

    auto ig = cigi::Host::connect(config); // defaults: ports 8004/8005, synchronous
    if (!ig) {
        return std::println(stderr, "cannot connect: {}", cigi::to_string(ig.error().code)), 1;
    }
    ig->on<cigi::HatHotResponse>(
        [](const cigi::HatHotResponse& r) { std::println("terrain height {:.1f} m", r.height); });
    ig->on_state_change([](cigi::LinkState from, cigi::LinkState to) {
        std::println("{} -> {}", cigi::to_string(from), cigi::to_string(to));
    });
    double angle = 0.0;
    bool asked_for_terrain = false;
    ig->on_frame([&](cigi::HostFrame& frame) { // once per frame, only in Operate
        angle += 0.02;
        if (!std::exchange(asked_for_terrain, true)) { // one HAT/HOT request, answered above
            // NOLINTNEXTLINE(bugprone-unused-return-value)
            static_cast<void>(frame.add(cigi::HatHotRequest{.latitude_or_x = 51.0}));
        }
        cigi::EntityControl entity;
        entity.entity_id = 1;
        entity.entity_state = cigi::EntityState::Active;
        entity.latitude_or_x = 51.0 + (0.01 * std::sin(angle)); // a circle of about 1 km
        entity.longitude_or_y = -1.0 + (0.01 * std::cos(angle));
        entity.altitude_or_z = 300.0;
        entity.yaw = static_cast<float>(std::fmod((angle * 57.29578) + 90.0, 360.0));
        // NOLINTNEXTLINE(bugprone-unused-return-value)
        static_cast<void>(frame.add(entity)); // a full frame just drops this update
    });

    std::stop_source stop;
    std::jthread timer([&](const std::stop_token& st) { // stops the run on Ctrl-C or --seconds
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (!st.stop_requested() && g_interrupted == 0 &&
               std::chrono::steady_clock::now() < end) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        stop.request_stop();
    });
    const auto result = ig->run(stop.get_token());
    timer.request_stop();
    return result ? 0 : 1;
}
