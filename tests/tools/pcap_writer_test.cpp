#include "../support/bytes.hpp"
#include "cigi/message/builder.hpp"
#include "cigi/tools/pcap_writer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace cigi::tools {
namespace {

using test::bytes_of;
using ::testing::ElementsAreArray;

constexpr Endpoint host{.address = Ipv4Address{{10, 0, 0, 1}}, .port = 8005};
constexpr Endpoint ig{.address = Ipv4Address{{10, 0, 0, 2}}, .port = 8004};

/// Byte `i` of `v` as an int (bounds-checked).
int at(const std::vector<std::byte>& v, std::size_t i) {
    return std::to_integer<int>(v.at(i));
}

std::vector<std::byte> bytes_from(const std::string& s) {
    std::vector<std::byte> out;
    std::ranges::transform(s, std::back_inserter(out),
                           [](char c) { return static_cast<std::byte>(c); });
    return out;
}

// Checksums below were computed by hand (RFC 1071), not with the code under test.
TEST(InternetChecksum, KnownValues) {
    EXPECT_EQ(internet_checksum({}), 0xFFFF);
    // The IPv4 header of the packet in PacketLayout, checksum field zero.
    const auto header = bytes_of(0x45, 0x00, 0x00, 0x20, 0x00, 0x01, 0x40, 0x00, 0x40, 0x11, 0x00,
                                 0x00, 0x0A, 0x00, 0x00, 0x01, 0x0A, 0x00, 0x00, 0x02);
    EXPECT_EQ(internet_checksum(header), 0x26CA);
    // An odd byte count is padded with a zero byte.
    EXPECT_EQ(internet_checksum(bytes_of(0x01, 0x02, 0x03)), 0xFBFD);
}

TEST(InternetChecksum, ValidDataSumsToZero) {
    auto header = bytes_of(0x45, 0x00, 0x00, 0x20, 0x00, 0x01, 0x40, 0x00, 0x40, 0x11, 0x26, 0xCA,
                           0x0A, 0x00, 0x00, 0x01, 0x0A, 0x00, 0x00, 0x02);
    EXPECT_EQ(internet_checksum(header), 0);
}

TEST(BuildPacket, PacketLayout) {
    const auto payload = bytes_of(0xDE, 0xAD, 0xBE, 0xEF);
    const auto packet = build_ipv4_udp_packet(host, ig, payload, 1);
    ASSERT_TRUE(packet.has_value());
    const auto expected = bytes_of(
        // IPv4: version/IHL, TOS, total length 32, id 1, DF, TTL 64, UDP, checksum, 10.0.0.1,
        // 10.0.0.2
        0x45, 0x00, 0x00, 0x20, 0x00, 0x01, 0x40, 0x00, 0x40, 0x11, 0x26, 0xCA, 0x0A, 0x00, 0x00,
        0x01, 0x0A, 0x00, 0x00, 0x02,
        // UDP: 8005 -> 8004, length 12, checksum
        0x1F, 0x45, 0x1F, 0x44, 0x00, 0x0C, 0x0F, 0xAD,
        // payload
        0xDE, 0xAD, 0xBE, 0xEF);
    EXPECT_THAT(*packet, ElementsAreArray(expected));
}

TEST(BuildPacket, OddPayloadAndEmptyPayload) {
    const auto odd = build_ipv4_udp_packet(host, ig, bytes_of(1, 2, 3), 1);
    ASSERT_TRUE(odd.has_value());
    ASSERT_EQ(odd->size(), 31U);
    EXPECT_EQ(at(*odd, 3), 31); // IPv4 total length
    EXPECT_EQ(at(*odd, 10), 0x26);
    EXPECT_EQ(at(*odd, 11), 0xCB);
    EXPECT_EQ(at(*odd, 26), 0xA9); // UDP checksum
    EXPECT_EQ(at(*odd, 27), 0x4A);

    const auto empty = build_ipv4_udp_packet(host, ig, {}, 0);
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(empty->size(), ipv4_header_size + udp_header_size);
}

TEST(BuildPacket, ZeroUdpChecksumIsSentAsAllOnes) {
    // Search the payload value that makes the UDP checksum come out as 0 before the RFC 768
    // substitution: the checksum covers the payload word additively, so exactly one 16-bit value
    // does it. Found by trying all of them; the substitute must then be 0xFFFF, never 0.
    bool found = false;
    for (unsigned v = 0; v <= 0xFFFF && !found; ++v) {
        const auto payload = bytes_of(v >> 8U, v & 0xFFU);
        const auto packet = build_ipv4_udp_packet(host, ig, payload, 1);
        ASSERT_TRUE(packet.has_value());
        const auto sum =
            (static_cast<unsigned>(at(*packet, 26)) << 8U) | static_cast<unsigned>(at(*packet, 27));
        EXPECT_NE(sum, 0U);
        found = sum == 0xFFFFU;
    }
    EXPECT_TRUE(found);
}

TEST(BuildPacket, PayloadLimit) {
    const std::vector<std::byte> max(max_udp_payload, std::byte{0});
    EXPECT_TRUE(build_ipv4_udp_packet(host, ig, max, 1).has_value());
    const std::vector<std::byte> too_big(max_udp_payload + 1, std::byte{0});
    const auto r = build_ipv4_udp_packet(host, ig, too_big, 1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), PcapError::PayloadTooLarge);
}

TEST(Ipv4Address, ParsesDottedDecimal) {
    EXPECT_EQ(Ipv4Address::parse("10.0.0.1"), (Ipv4Address{{10, 0, 0, 1}}));
    EXPECT_EQ(Ipv4Address::parse("255.255.255.255"), (Ipv4Address{{255, 255, 255, 255}}));
    EXPECT_EQ(Ipv4Address::parse("0.0.0.0"), (Ipv4Address{{0, 0, 0, 0}}));
    EXPECT_EQ(Ipv4Address::parse("001.02.3.004"), (Ipv4Address{{1, 2, 3, 4}}));
}

TEST(Ipv4Address, RejectsMalformedText) {
    constexpr std::array<std::string_view, 17> bad_text{
        "",          "1.2.3",      "1.2.3.4.5", "1.2.3.",   ".1.2.3",   "1..2.3",
        "256.0.0.1", "1.2.3.1000", "1.2.3.-4",  "1.2.3.4 ", " 1.2.3.4", "a.b.c.d",
        "1.2.3.+4",  "0x1.2.3.4",  "1.2.3.4x",  "1,2,3,4",  "....",
    };
    for (const std::string_view bad : bad_text) {
        EXPECT_FALSE(Ipv4Address::parse(bad).has_value()) << bad;
    }
}

TEST(Ipv4Address, FromUint32IsHostOrder) {
    EXPECT_EQ(Ipv4Address::from_uint32(0x0A000001U), (Ipv4Address{{10, 0, 0, 1}}));
    EXPECT_EQ(Ipv4Address::from_uint32(0xC0A80164U), (Ipv4Address{{192, 168, 1, 100}}));
    EXPECT_EQ(Ipv4Address::from_uint32(0), (Ipv4Address{{0, 0, 0, 0}}));
}

TEST(PcapWriter, ToTimeConvertsWallClock) {
    using namespace std::chrono;
    const auto tp = system_clock::time_point{duration_cast<system_clock::duration>(
        microseconds{(1'700'000'000LL * 1'000'000) + 123'456})};
    std::ostringstream a;
    std::ostringstream b;
    PcapWriter by_time{a};
    PcapWriter by_micros{b};
    ASSERT_TRUE(by_time.write_udp(PcapWriter::to_time(tp), host, ig, bytes_of(1)));
    ASSERT_TRUE(by_micros.write_udp(PcapWriter::to_time(tp), host, ig, bytes_of(1)));
    EXPECT_EQ(a.str(), b.str());
    EXPECT_EQ(PcapWriter::to_time(tp).count(), (1'700'000'000LL * 1'000'000) + 123'456);
    // Pre-1970 clamps to 0; seconds beyond 2^32 wrap.
    std::ostringstream c;
    PcapWriter early{c};
    ASSERT_TRUE(early.write_udp(PcapWriter::to_time(system_clock::time_point{} - hours{24}), host,
                                ig, bytes_of(1)));
    EXPECT_EQ(at(bytes_from(c.str()), 24), 0);
    std::ostringstream d;
    PcapWriter late{d};
    ASSERT_TRUE(
        late.write_udp(microseconds{(4'294'967'296LL + 7) * 1'000'000}, host, ig, bytes_of(1)));
    EXPECT_EQ(at(bytes_from(d.str()), 24), 7);
}

// Counts flush() calls so auto_flush can be observed.
class FlushCountingBuf : public std::stringbuf {
public:
    int flushes = 0;

protected:
    int sync() override {
        ++flushes;
        return 0;
    }
};

TEST(PcapWriter, AutoFlushFlushesEveryRecord) {
    FlushCountingBuf buf;
    std::ostream out{&buf};
    PcapWriter pcap{out, PcapOptions{.auto_flush = true}};
    EXPECT_EQ(buf.flushes, 0);
    ASSERT_TRUE(pcap.write_udp({}, host, ig, bytes_of(1)));
    EXPECT_EQ(buf.flushes, 1);
    ASSERT_TRUE(pcap.write_udp({}, host, ig, bytes_of(1)));
    EXPECT_EQ(buf.flushes, 2);
    pcap.flush();
    EXPECT_EQ(buf.flushes, 3);
}

TEST(PcapWriter, NoAutoFlushByDefault) {
    FlushCountingBuf buf;
    std::ostream out{&buf};
    PcapWriter pcap{out};
    ASSERT_TRUE(pcap.write_udp({}, host, ig, bytes_of(1)));
    EXPECT_EQ(buf.flushes, 0);
}

TEST(PcapWriter, ByteExactFile) {
    std::ostringstream out;
    PcapWriter pcap{out};
    EXPECT_TRUE(pcap.ok());
    const auto payload = bytes_of(0xDE, 0xAD, 0xBE, 0xEF);
    // 1700000000 s + 123456 us.
    ASSERT_TRUE(pcap.write_udp(std::chrono::microseconds{(1'700'000'000LL * 1'000'000) + 123'456},
                               host, ig, payload));
    EXPECT_EQ(pcap.frames(), 1U);
    pcap.flush();

    const auto expected = bytes_of(
        // File header: magic (little-endian), version 2.4, zone, accuracy, snap length 65535,
        // link type 101 (raw IP).
        0xD4, 0xC3, 0xB2, 0xA1, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xFF, 0xFF, 0x00, 0x00, 0x65, 0x00, 0x00, 0x00,
        // Record header: seconds 0x6553F100, microseconds 0x1E240, captured and original length 32.
        0x00, 0xF1, 0x53, 0x65, 0x40, 0xE2, 0x01, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
        0x00,
        // The IPv4/UDP packet of BuildPacket.PacketLayout.
        0x45, 0x00, 0x00, 0x20, 0x00, 0x01, 0x40, 0x00, 0x40, 0x11, 0x26, 0xCA, 0x0A, 0x00, 0x00,
        0x01, 0x0A, 0x00, 0x00, 0x02, 0x1F, 0x45, 0x1F, 0x44, 0x00, 0x0C, 0x0F, 0xAD, 0xDE, 0xAD,
        0xBE, 0xEF);
    EXPECT_THAT(bytes_from(out.str()), ElementsAreArray(expected));
}

TEST(PcapWriter, IdentificationCountsAndNegativeTimeClamps) {
    std::ostringstream out;
    PcapWriter pcap{out};
    const auto payload = bytes_of(1);
    ASSERT_TRUE(pcap.write_udp(std::chrono::microseconds{-5}, host, ig, payload));
    ASSERT_TRUE(pcap.write_udp(std::chrono::microseconds{0}, ig, host, payload));
    EXPECT_EQ(pcap.frames(), 2U);
    const auto file = bytes_from(out.str());
    constexpr std::size_t record = 16 + ipv4_header_size + udp_header_size + 1;
    ASSERT_EQ(file.size(), 24 + (2 * record));
    EXPECT_EQ(at(file, 24), 0);                   // First record: seconds clamped to 0.
    EXPECT_EQ(at(file, 24 + 16 + 5), 1);          // IPv4 id of frame 1.
    EXPECT_EQ(at(file, 24 + record + 16 + 5), 2); // IPv4 id of frame 2.
}

TEST(PcapWriter, RejectsOversizedPayloadWithoutWriting) {
    std::ostringstream out;
    PcapWriter pcap{out};
    const std::vector<std::byte> too_big(max_udp_payload + 1, std::byte{0});
    const auto r = pcap.write_udp({}, host, ig, too_big);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), PcapError::PayloadTooLarge);
    EXPECT_EQ(pcap.frames(), 0U);
    EXPECT_EQ(out.str().size(), 24U);
}

TEST(PcapWriter, ReportsAFailedStream) {
    std::ostringstream out;
    PcapWriter pcap{out};
    out.setstate(std::ios::badbit);
    EXPECT_FALSE(pcap.ok());
    const auto r = pcap.write_udp({}, host, ig, bytes_of(1));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), PcapError::WriteFailed);
    EXPECT_EQ(pcap.frames(), 0U);
}

class PcapFile : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() / "cigi_pcap_writer_test";
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

TEST_F(PcapFile, WritesToAPath) {
    const auto path = dir() / "out.pcap";
    {
        PcapWriter pcap{path};
        ASSERT_TRUE(pcap.ok());
        ASSERT_TRUE(pcap.write_udp({}, host, ig, bytes_of(1, 2)));
    }
    EXPECT_EQ(std::filesystem::file_size(path), 24U + 16U + 28U + 2U);
}

TEST_F(PcapFile, UncreatableFileIsNotOk) {
    PcapWriter pcap{dir() / "missing-directory" / "out.pcap"};
    EXPECT_FALSE(pcap.ok());
    EXPECT_FALSE(pcap.write_udp({}, host, ig, bytes_of(1)).has_value());
    EXPECT_TRUE(pcap.open_error());
    EXPECT_EQ(pcap.open_error(), std::make_error_code(std::errc::no_such_file_or_directory));
    EXPECT_FALSE(pcap.open_error().message().empty());
}

TEST_F(PcapFile, OpenedFileHasNoOpenError) {
    PcapWriter pcap{dir() / "fine.pcap", PcapOptions{.auto_flush = true}};
    EXPECT_TRUE(pcap.ok());
    EXPECT_FALSE(pcap.open_error());
    ASSERT_TRUE(pcap.write_udp({}, host, ig, bytes_of(1)));
    // Flushed already: readable before the writer is destroyed.
    EXPECT_EQ(std::filesystem::file_size(dir() / "fine.pcap"), 24U + 16U + 28U + 1U);
}

TEST(PcapWriter, StreamConstructorHasNoOpenError) {
    std::ostringstream out;
    EXPECT_FALSE(PcapWriter{out}.open_error());
}

} // namespace
} // namespace cigi::tools
