// A minimal Host: connect to an IG, wait for Operate and fly one entity in a circle.
//
//   host_minimal [--ig-address 127.0.0.1] [--seconds 30]
//
// Run it against the mock IG (demo_ig) or a real CIGI 3.3 IG; Ctrl-C stops it.

#include "cigi/host.hpp"

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stop_token>
#include <string>
#include <thread>

namespace {

std::stop_source g_stop; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

} // namespace

int main(int argc, char** argv) {
    cigi::HostConfig config{.ig_address = "127.0.0.1"};
    int seconds = 30;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--ig-address") == 0) {
            config.ig_address = argv[i + 1];
        } else if (std::strcmp(argv[i], "--seconds") == 0) {
            seconds = std::atoi(argv[i + 1]);
        }
    }
    static_cast<void>(std::signal(SIGINT, [](int) { g_stop.request_stop(); }));

    auto ig = cigi::Host::connect(config); // defaults: ports 8004/8005, synchronous
    if (!ig) {
        std::fprintf(stderr, "cannot connect: %s\n", cigi::to_string(ig.error().code).data());
        return 1;
    }
    ig->on<cigi::HatHotResponse>([](const cigi::HatHotResponse& r) {
        std::printf("terrain height %.1f m\n", r.height);
    });
    ig->on_state_change([](cigi::LinkState from, cigi::LinkState to) {
        std::printf("%s -> %s\n", cigi::to_string(from).data(), cigi::to_string(to).data());
    });
    double angle = 0.0;
    ig->on_frame([&](cigi::HostFrame& frame) { // once per frame, only in Operate
        angle += 0.02;
        cigi::EntityControl entity;
        entity.entity_id = 1;
        entity.entity_state = cigi::EntityState::Active;
        entity.latitude_or_x = 51.0 + 0.01 * std::sin(angle); // a circle of about 1 km
        entity.longitude_or_y = -1.0 + 0.01 * std::cos(angle);
        entity.altitude_or_z = 300.0;
        entity.yaw = static_cast<float>(std::fmod(angle * 57.29578 + 90.0, 360.0));
        frame.add(entity);
    });

    std::jthread timer([&](const std::stop_token& st) { // stops the run after --seconds
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (!st.stop_requested() && std::chrono::steady_clock::now() < end) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        g_stop.request_stop();
    });
    const auto result = ig->run(g_stop.get_token());
    timer.request_stop();
    return result ? 0 : 1;
}
