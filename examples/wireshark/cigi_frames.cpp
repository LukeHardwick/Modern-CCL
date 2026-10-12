// Runs a real cigi::Host and cigi::Ig back to back over the in-memory transport and writes every
// datagram they send into a pcap file, so Wireshark shows the start-up exchange the library really
// produces (ICD 2.8, 4.1.1, 4.2.1).
// Usage: cigi_frames <output-dir>   (writes cigi_frames.pcap, via cigi::tools::PcapWriter)
// See README.md in this directory; open the pcap in Wireshark or tshark.
//
// A small DatagramTransport decorator (Recorder) sits between each facade and the in-memory wire.
// Time is a manual clock stepped at 60 Hz, so the capture is identical on every run.
#include "cigi/datagram_transport.hpp"
#include "cigi/host.hpp"
#include "cigi/ig.hpp"
#include "cigi/tools/pcap_writer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <utility>

namespace {

constexpr int step_count = 9;              // 60 Hz steps; the exchange settles within a few.
constexpr int foreign_datagram = 4;        // The Host's 4th datagram is sent in the other byte order.
constexpr int operate_step = 3;             // The IG reports Operate from this step on.
constexpr int loaded_step = 6;              // ... and database 1 as loaded from this one.
constexpr std::uint32_t frame_ticks = 1667; // 1 / 60 s in 10 us ticks.

// Host 10.0.0.1:8005 and IG 10.0.0.2:8004; set the same addresses in Wireshark's CIGI preferences.
constexpr cigi::tools::Endpoint host_endpoint{.address = {{10, 0, 0, 1}}, .port = 8005};
constexpr cigi::tools::Endpoint ig_endpoint{.address = {{10, 0, 0, 2}}, .port = 8004};
constexpr std::chrono::seconds capture_start{1'767'225'600}; // 2026-01-01 00:00:00 UTC.

/// The clock both facades share: a counter of 10 us ticks that main() advances by hand.
struct ManualClock {
    std::shared_ptr<std::uint32_t> ticks = std::make_shared<std::uint32_t>(0);
    [[nodiscard]] cigi::ClockFn fn() const {
        return [t = ticks] { return cigi::Timestamp{*t}; };
    }
};

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

/// A DatagramTransport that forwards to another one and records every datagram it sends.
class Recorder final : public cigi::DatagramTransport {
public:
    Recorder(std::unique_ptr<cigi::DatagramTransport> inner, cigi::tools::PcapWriter& pcap,
             std::shared_ptr<std::uint32_t> clock, cigi::tools::Endpoint from,
             cigi::tools::Endpoint to, int foreign_nth = 0)
        : inner_(std::move(inner)), pcap_(&pcap), clock_(std::move(clock)), from_(from), to_(to),
          foreign_nth_(foreign_nth) {}

    cigi::Result<void> send(std::span<const std::byte> datagram) override {
        std::array<std::byte, 64> copy{};
        const bool swap = ++sent_ == foreign_nth_ && datagram.size() <= copy.size();
        std::span<const std::byte> out = datagram;
        if (swap) {
            std::ranges::copy(datagram, copy.begin());
            make_foreign(std::span{copy}.first(datagram.size()));
            out = std::span<const std::byte>{copy}.first(datagram.size());
        }
        const auto us = capture_start + std::chrono::microseconds{std::int64_t{*clock_} * 10};
        if (!pcap_->write_udp(std::chrono::duration_cast<std::chrono::microseconds>(us), from_,
                              to_, out)) {
            return std::unexpected(cigi::Error{.code = cigi::ErrorCode::TransportError});
        }
        return inner_->send(out);
    }
    cigi::Result<std::size_t> receive(std::span<std::byte> buffer,
                                      std::chrono::milliseconds timeout) override {
        return inner_->receive(buffer, timeout);
    }

private:
    std::unique_ptr<cigi::DatagramTransport> inner_;
    cigi::tools::PcapWriter* pcap_;
    std::shared_ptr<std::uint32_t> clock_;
    cigi::tools::Endpoint from_;
    cigi::tools::Endpoint to_;
    int foreign_nth_;
    int sent_ = 0;
};

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

    const ManualClock clock;
    auto [host_wire, ig_wire] = cigi::make_memory_transport_pair();
    cigi::HostConfig host_config;
    host_config.database = 1; // ask for database 1 once the IG operates
    host_config.session.clock = clock.fn();
    cigi::IgConfig ig_config;
    ig_config.session.clock = clock.fn();
    auto host = cigi::Host::over(
        host_config, std::make_unique<Recorder>(std::move(host_wire), pcap, clock.ticks,
                                                host_endpoint, ig_endpoint, foreign_datagram));
    auto ig = cigi::Ig::over(ig_config, std::make_unique<Recorder>(std::move(ig_wire), pcap,
                                                                   clock.ticks, ig_endpoint,
                                                                   host_endpoint));
    if (!host || !ig) {
        return EXIT_FAILURE;
    }

    // The IG takes a few frames to leave Reset/Standby (ICD 2.8): it accepts the Host's request
    // for Operate at once but reports the new mode only from step `operate_step` on.
    bool operate_pending = false;
    ig->on_mode_request([&](cigi::IgMode mode) {
        operate_pending = mode == cigi::IgMode::Operate;
        return true;
    });
    // The IG answers a database request the way ICD 4.1.1 describes: -n while loading, then n.
    std::optional<std::int8_t> loading;
    ig->on_database_request([&](std::int8_t n) {
        ig->set_database(static_cast<std::int8_t>(-n));
        loading = n;
    });
    host->on_state_change([](cigi::LinkState from, cigi::LinkState to) {
        std::println("Host link: {} -> {}", cigi::to_string(from), cigi::to_string(to));
    });

    for (int step = 0; step < step_count; ++step) {
        *clock.ticks += frame_ticks;
        if (step == operate_step && operate_pending) {
            ig->set_mode(cigi::IgMode::Operate);
        }
        if (step == loaded_step && loading) {
            ig->set_database(*loading); // loaded
        }
        if (!ig->poll(std::chrono::milliseconds{0}) || !host->poll(std::chrono::milliseconds{0})) {
            return EXIT_FAILURE;
        }
    }
    host->close(); // sends the closing IG Control (Reset/Standby, ICD 2.8)
    pcap.flush();
    std::println("wrote {} datagrams to {}", pcap.frames(), (dir / "cigi_frames.pcap").string());
    return pcap.ok() ? EXIT_SUCCESS : EXIT_FAILURE;
}
