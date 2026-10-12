// A user-defined packet (ICD 4.3, opcodes 201-255) through the Host/Ig facade: declare it like any
// packet, send it with HostFrame::add, receive it as a typed struct with Ig::on_user<P>, then
// switch user packets off. Host and Ig talk over an in-memory transport (no network).
#include "cigi/user_packet.hpp"

#include "cigi/host.hpp"
#include "cigi/ig.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <print>
#include <tuple>
#include <utility>

namespace myapp {
namespace {

/// Opcode 201, 16 bytes (4 + 4n, a multiple of 8), either direction. The identifying fields sit in
/// the first two 32-bit words (ICD 4.3). The field table also gives byte swapping for free.
struct Telemetry {
    static constexpr std::uint8_t opcode = 201;
    static constexpr cigi::Direction direction = cigi::Direction::Both;
    static constexpr std::size_t wire_size = 16;
    std::uint16_t sensor_id = 0;
    bool alarm = false;
    float temperature = 0.0F;
    std::uint32_t samples = 0;
    static constexpr auto fields = std::tuple{
        cigi::wire::scalar(&Telemetry::sensor_id, 2),
        cigi::wire::flag(&Telemetry::alarm, 4, 0),
        cigi::wire::scalar(&Telemetry::temperature, 8),
        cigi::wire::scalar(&Telemetry::samples, 12),
    };
};
static_assert(cigi::wire::layout_ok<Telemetry>());
static_assert(cigi::UserDefinedPacket<Telemetry>);

} // namespace
} // namespace myapp

// NOLINTNEXTLINE(bugprone-exception-escape): std::print may throw; an example may terminate.
int main() {
    using namespace std::chrono_literals;
    auto [host_wire, ig_wire] = cigi::make_memory_transport_pair();
    auto host = cigi::Host::over(cigi::HostConfig(), std::move(host_wire));
    auto ig = cigi::Ig::over({.rate_hz = 200.0}, std::move(ig_wire));
    if (!host || !ig) {
        return EXIT_FAILURE;
    }

    // Host: user packets go into each frame like any other packet (Operate/Debug only).
    host->on_frame([](cigi::HostFrame& frame) {
        // NOLINTNEXTLINE(bugprone-unused-return-value): a full frame just drops this update
        static_cast<void>(frame.add(myapp::Telemetry{
            .sensor_id = 7,
            .alarm = true,
            .temperature = 36.6F,
            .samples = frame.host_frame_number(),
        }));
    });
    // IG: a typed handler for opcode 201 (without one it would reach on_unknown, ICD 2.10).
    int received = 0;
    ig->on_user<myapp::Telemetry>([&](const myapp::Telemetry& t) {
        std::println("sensor {} at {} C, alarm {}, sample {}", t.sensor_id, t.temperature, t.alarm,
                     t.samples);
        ++received;
    });

    // Drive both sides until three packets arrived, then switch user packets off (ICD 4.3: a
    // device should be configurable to ignore them): nothing is delivered after that.
    const auto pump = [&](int rounds, int until) {
        for (int i = 0; i < rounds && received < until; ++i) {
            if (!ig->poll(5ms) || !host->poll(5ms)) {
                return false;
            }
        }
        return true;
    };
    if (!pump(2000, 3)) {
        return EXIT_FAILURE;
    }
    ig->session().handlers().ignore_user_packets(true);
    const int before = received;
    if (!pump(20, 1000)) {
        return EXIT_FAILURE;
    }
    std::println("after switching off: {} more packets delivered", received - before);
    return received >= 3 && received == before ? EXIT_SUCCESS : EXIT_FAILURE;
}
