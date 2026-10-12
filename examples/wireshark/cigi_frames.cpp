// Simulates a CIGI start-up exchange in memory and writes every datagram into a pcap file.
// Usage: cigi_frames <output-dir>   (writes cigi_frames.pcap, via cigi::tools::PcapWriter)
// See README.md in this directory; open the pcap in Wireshark or tshark.
#include "cigi/message/builder.hpp"
#include "cigi/message/view.hpp"
#include "cigi/tools/pcap_writer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <print>
#include <span>
#include <string_view>

namespace {

constexpr int frame_count = 6;             // IG frames 1-3 in Reset/Standby, 4-6 in Operate.
constexpr int operate_request = 3;         // The Host asks for Operate in its reply to IG frame 3.
constexpr int foreign_frame = 5;           // This Host reply is sent in the other byte order.
constexpr std::uint32_t frame_us = 16'667; // 60 Hz.

// Host 10.0.0.1:8005 and IG 10.0.0.2:8004; set the same addresses in Wireshark's CIGI preferences.
constexpr cigi::tools::Endpoint host_endpoint{.address = {{10, 0, 0, 1}}, .port = 8005};
constexpr cigi::tools::Endpoint ig_endpoint{.address = {{10, 0, 0, 2}}, .port = 8004};
constexpr std::chrono::seconds capture_start{1'767'225'600}; // 2026-01-01 00:00:00 UTC.

/// Timestamp in 10 us ticks for a time in microseconds (ICD 4.1.1).
cigi::Timestamp ticks(std::uint32_t us) { return cigi::Timestamp{us / 10}; }

/// The IG reports its database as unavailable (-128), then loading (-1), then loaded (1).
std::int8_t database_for(int frame) {
    constexpr std::array<std::int8_t, 3> by_frame{-128, -1, 1};
    return by_frame.at(static_cast<std::size_t>(std::min(frame, 3) - 1));
}

/// Append one datagram to the capture `us` microseconds after the start.
bool record(cigi::tools::PcapWriter& pcap, std::uint32_t us, const cigi::tools::Endpoint& from,
            const cigi::tools::Endpoint& to, std::span<const std::byte> bytes) {
    const auto time = capture_start + std::chrono::microseconds{us};
    return pcap.write_udp(std::chrono::duration_cast<std::chrono::microseconds>(time), from, to,
                          bytes)
        .has_value();
}

/// Rewrites a native IG Control as a peer of the opposite endianness would have sent it: every
/// multi-byte field reversed, the magic number included (ICD 2.5). Bytes 0-5 are single bytes.
void make_foreign(std::span<std::byte> msg) {
    using cigi::wire::ByteOrder;
    constexpr std::array<std::size_t, 3> u32_offsets{8, 12, 16};
    const auto magic = cigi::wire::load<std::uint16_t>(msg, 6, ByteOrder::Native);
    cigi::wire::store<std::uint16_t>(msg, 6, magic, ByteOrder::Swapped);
    for (const std::size_t off : u32_offsets) {
        const auto value = cigi::wire::load<std::uint32_t>(msg, off, ByteOrder::Native);
        cigi::wire::store<std::uint32_t>(msg, off, value, ByteOrder::Swapped);
    }
}

/// Parse the message back with the public API and print one line per packet.
void summarise(std::string_view label, std::span<const std::byte> bytes) {
    const auto view = cigi::MessageView::parse(bytes);
    if (!view) {
        std::println("{}: parse failed: {}", label, cigi::to_string(view.error().code));
        return;
    }
    const auto* order = view->byte_order() == cigi::wire::ByteOrder::Native ? "native" : "FOREIGN";
    for (const auto& item : *view) {
        if (const auto sof = item.and_then([](const cigi::RawPacket& r) {
                return r.decode_as<cigi::StartOfFrame>();
            })) {
            std::println("{}: SOF ig_frame={} last_host={} mode={} db={} ts={}x10us ({} order)", label,
                         sof->ig_frame_number, sof->last_host_frame_number,
                         static_cast<int>(sof->ig_mode), sof->database_number,
                         sof->timestamp.count(), order);
        } else if (const auto igc = item.and_then([](const cigi::RawPacket& r) {
                       return r.decode_as<cigi::IgControl>();
                   })) {
            std::println("{}: IGC host_frame={} last_ig={} mode={} db={} ts={}x10us ({} order)",
                         label, igc->host_frame_number, igc->last_ig_frame_number,
                         static_cast<int>(igc->ig_mode), igc->database_number,
                         igc->timestamp.count(), order);
        }
    }
}

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape): std::print may throw; an example may terminate.
int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() != 2) {
        std::println(stderr, "usage: {} <output-dir>", args.front());
        return EXIT_FAILURE;
    }
    const std::filesystem::path dir{args.back()};
    std::filesystem::create_directories(dir);
    cigi::tools::PcapWriter pcap{dir / "cigi_frames.pcap"};
    if (!pcap.ok()) {
        std::println(stderr, "cannot write into {}", dir.string());
        return EXIT_FAILURE;
    }

    for (int frame = 1; frame <= frame_count; ++frame) {
        const auto ig_us = static_cast<std::uint32_t>(frame - 1) * frame_us;
        const auto number = static_cast<std::uint32_t>(frame);

        // IG -> Host: Start of Frame. Acknowledges the previous Host frame (ICD 2.3, Figure 6).
        cigi::StaticMessageBuilder<32> sof(cigi::Direction::IgToHost);
        const cigi::StartOfFrame start{
            .database_number = database_for(frame),
            .ig_mode = frame > operate_request ? cigi::IgMode::Operate : cigi::IgMode::ResetStandby,
            .timestamp_valid = true,
            .ig_frame_number = number,
            .timestamp = ticks(ig_us),
            .last_host_frame_number = number - 1,
        };
        if (!sof.add(start)) {
            return EXIT_FAILURE;
        }
        if (!record(pcap, ig_us, ig_endpoint, host_endpoint, sof.bytes())) {
            return EXIT_FAILURE;
        }
        summarise("IG   -> Host", sof.bytes());

        // Host -> IG: IG Control 2 ms later. Acknowledges this IG frame (ICD 2.3, Figure 6).
        const auto host_us = ig_us + 2'000;
        cigi::StaticMessageBuilder<32> igc(cigi::Direction::HostToIg);
        const cigi::IgControl control{
            .database_number = 1,
            .ig_mode = frame >= operate_request ? cigi::IgMode::Operate : cigi::IgMode::ResetStandby,
            .timestamp_valid = true,
            .host_frame_number = number,
            .timestamp = ticks(host_us),
            .last_ig_frame_number = number,
        };
        if (!igc.add(control)) {
            return EXIT_FAILURE;
        }
        std::array<std::byte, 32> datagram{};
        std::ranges::copy(igc.bytes(), datagram.begin());
        const auto bytes = std::span{datagram}.first(igc.size());
        if (frame == foreign_frame) {
            make_foreign(bytes);
        }
        if (!record(pcap, host_us, host_endpoint, ig_endpoint, bytes)) {
            return EXIT_FAILURE;
        }
        summarise(frame == foreign_frame ? "Host -> IG (foreign)" : "Host -> IG", bytes);
    }
    pcap.flush();
    std::println("wrote {} datagrams to {}", pcap.frames(), (dir / "cigi_frames.pcap").string());
    return pcap.ok() ? EXIT_SUCCESS : EXIT_FAILURE;
}
