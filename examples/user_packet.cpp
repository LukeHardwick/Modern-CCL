// A user-defined packet (ICD 4.3, opcodes 201-255): declare it like any packet, add it to a packet
// set with ExtendUser, send it from a Host and handle it on an IG, then switch user packets off.
#include "cigi/dispatch.hpp"
#include "cigi/message/builder.hpp"
#include "cigi/user_packet.hpp"

#include <cstdint>
#include <cstdlib>
#include <print>
#include <tuple>

namespace myapp {

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

} // namespace myapp

// Registration: the standard Host -> IG packets plus ours (checked at compile time: the opcode must
// be in 201-255 and unique).
using HostPackets = cigi::ExtendUser<cigi::HostToIgPackets, myapp::Telemetry>;

// NOLINTNEXTLINE(bugprone-exception-escape): std::print may throw; an example may terminate.
int main() {
    // Host side: a user packet goes into a message like any other.
    cigi::StaticMessageBuilder<128> builder(cigi::Direction::HostToIg);
    const bool built =
        builder.add(cigi::IgControl{}).has_value() &&
        builder
            .add(myapp::Telemetry{
                .sensor_id = 7, .alarm = true, .temperature = 36.6F, .samples = 1200})
            .has_value();
    if (!built) {
        return EXIT_FAILURE;
    }
    const auto view = cigi::MessageView::parse(builder.bytes());
    if (!view) {
        return EXIT_FAILURE;
    }

    // IG side: handlers for the extended set.
    cigi::Dispatcher<HostPackets> ig;
    ig.on<myapp::Telemetry>([](const myapp::Telemetry& t) {
        std::println("sensor {} at {} C, alarm {}, {} samples", t.sensor_id, t.temperature,
                     t.alarm, t.samples);
    });
    ig.on_unknown([](const cigi::UnknownPacket& u) { std::println("unknown opcode {}", u.opcode); });
    ig.dispatch(*view);

    // ICD 4.3: a device should be configurable to ignore user-defined packets.
    ig.ignore_user_packets(true);
    const auto stats = ig.dispatch(*view);
    std::println("ignoring user packets: handled {} ignored {}", stats.handled, stats.ignored);

    // A peer that does not know the packet sees it as an unknown opcode and carries on (ICD 2.10).
    cigi::Dispatcher<cigi::HostToIgPackets> plain_ig;
    plain_ig.on_unknown(
        [](const cigi::UnknownPacket& u) { std::println("plain IG: unknown opcode {}", u.opcode); });
    plain_ig.dispatch(*view);
    return EXIT_SUCCESS;
}
