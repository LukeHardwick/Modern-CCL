// Reads the pcap writer's output back with tshark: an independent check of the IPv4/UDP headers.
#include "cigi/message/builder.hpp"
#include "cigi/packets/ig_control.hpp"
#include "cigi/tools/pcap_writer.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

namespace cigi::tools {
namespace {

constexpr Endpoint host{.address = Ipv4Address{{10, 0, 0, 1}}, .port = 8005};
constexpr Endpoint ig{.address = Ipv4Address{{10, 0, 0, 2}}, .port = 8004};

/// The command line as std::system should get it. PLATFORM: cmd.exe strips one pair of outer
/// quotes, so a command that starts with a quoted path must be wrapped in another pair.
std::string quote_for_shell(const std::string& command) {
#ifdef _WIN32
    return "\"" + command + "\"";
#else
    return command;
#endif
}

class TsharkRoundTrip : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() / "cigi_pcap_tshark_test";
        std::filesystem::create_directories(dir_);
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    [[nodiscard]] const std::filesystem::path& dir() const { return dir_; }

private:
    std::filesystem::path dir_;
};

// An independent reader: tshark must accept the file, verify both checksums and decode the CIGI
// payload.
TEST_F(TsharkRoundTrip, ReadsTheFileAndVerifiesChecksums) {
    StaticMessageBuilder<64> msg(Direction::HostToIg);
    const IgControl control{
        .database_number = 5,
        .ig_mode = IgMode::Operate,
        .timestamp_valid = true,
        .host_frame_number = 77,
        .timestamp = Timestamp{1234},
        .last_ig_frame_number = 76,
    };
    ASSERT_TRUE(msg.add(control));

    const auto pcap_path = dir() / "tshark.pcap";
    {
        // Wireshark shows the first datagram of a capture as Unknown (docs/interop/wireshark.md),
        // so the message is sent twice and the second one is checked.
        PcapWriter pcap{pcap_path};
        constexpr std::chrono::microseconds start{1'700'000'000LL * 1'000'000};
        ASSERT_TRUE(pcap.write_udp(start, host, ig, msg.bytes()));
        ASSERT_TRUE(pcap.write_udp(start + std::chrono::milliseconds{1}, host, ig, msg.bytes()));
    }
    const auto out_path = dir() / "tshark.txt";
    const std::string command = quote_for_shell(
        std::string{"\""} + CIGI_TSHARK_PATH + "\" -r \"" + pcap_path.string() +
        "\" -o ip.check_checksum:TRUE -o udp.check_checksum:TRUE"
        " -o cigi.host:10.0.0.1 -o cigi.ig:10.0.0.2 -T fields -E separator=/s"
        " -e ip.src -e ip.dst -e ip.checksum.status -e udp.srcport -e udp.dstport"
        " -e udp.length -e udp.checksum.status -e cigi.ig_control.host_frame_number > \"" +
        out_path.string() + "\"");
    // A test running the tshark found by CMake through the shell is the point of this test.
    // NOLINTNEXTLINE(bugprone-command-processor,cert-env33-c)
    ASSERT_EQ(std::system(command.c_str()), 0) << command;
    std::ifstream in{out_path};
    std::string first;
    std::string second;
    std::getline(in, first);
    std::getline(in, second);
    // Checksum status 1 is "Good" (0 would be "Bad"); the Unknown datagram has no CIGI fields.
    EXPECT_EQ(first, "10.0.0.1 10.0.0.2 1 8005 8004 32 1 ");
    EXPECT_EQ(second, "10.0.0.1 10.0.0.2 1 8005 8004 32 1 77");
}

} // namespace
} // namespace cigi::tools
