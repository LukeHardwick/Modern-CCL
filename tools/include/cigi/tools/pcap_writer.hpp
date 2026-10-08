#pragma once

// Writes CIGI datagrams into a classic libpcap file that Wireshark and tshark open directly, with
// no capture privileges and no network. Every datagram becomes one raw-IPv4 frame (link type 101,
// LINKTYPE_RAW) holding an IPv4 header and a UDP header with correct lengths and checksums, so
// the dissector's "Decode As" and the Host/IG address preferences work exactly as on live traffic.
//
//   cigi::tools::PcapWriter pcap{"capture.pcap"};
//   pcap.write_udp(time, host, ig, message.bytes());
//
// Built for tools, tests and demo apps; it allocates and uses iostreams, so it is not part of the
// no-allocation core library.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <span>
#include <vector>

namespace cigi::tools {

/// An IPv4 address, most significant octet first ("10.0.0.1" is {10, 0, 0, 1}).
struct Ipv4Address {
    std::array<std::uint8_t, 4> octets{};

    friend constexpr bool operator==(const Ipv4Address&, const Ipv4Address&) noexcept = default;
};

/// An IPv4 address and UDP port.
struct Endpoint {
    Ipv4Address address;
    std::uint16_t port = 0;

    friend constexpr bool operator==(const Endpoint&, const Endpoint&) noexcept = default;
};

/// Why a frame could not be built or written.
enum class PcapError : std::uint8_t {
    PayloadTooLarge, ///< The datagram does not fit in one UDP/IPv4 packet (65507 bytes).
    WriteFailed,     ///< The stream is not open or reported an error.
};

inline constexpr std::size_t ipv4_header_size = 20;
inline constexpr std::size_t udp_header_size = 8;
inline constexpr std::size_t max_udp_payload = 65535 - ipv4_header_size - udp_header_size;
inline constexpr std::uint32_t linktype_raw = 101; ///< LINKTYPE_RAW: the packet starts with IPv4.

/// RFC 1071 Internet checksum: the one's complement of the one's complement sum of the data read
/// as big-endian 16-bit words (an odd final byte is padded with zero).
[[nodiscard]] std::uint16_t internet_checksum(std::span<const std::byte> data) noexcept;

/// The IPv4 + UDP packet for `payload`: a 20-byte header (version 4, IHL 5, DF set, TTL 64,
/// protocol UDP, header checksum), an 8-byte UDP header (checksum over the pseudo-header, sent as
/// 0xFFFF if it would be 0), then the payload. `identification` fills the IPv4 Identification.
[[nodiscard]] std::expected<std::vector<std::byte>, PcapError>
build_ipv4_udp_packet(const Endpoint& source, const Endpoint& destination,
                      std::span<const std::byte> payload, std::uint16_t identification = 0);

/// A classic pcap file (microsecond timestamps, little-endian file header, snap length 65535).
/// Not copyable or movable: a stream-backed writer refers to the caller's stream.
class PcapWriter {
public:
    /// Time since the Unix epoch, microsecond resolution.
    using Time = std::chrono::microseconds;

    /// Write to `out`, which must outlive the writer. Writes the file header immediately.
    explicit PcapWriter(std::ostream& out);
    /// Create `path` and write the file header. Check ok() afterwards.
    explicit PcapWriter(const std::filesystem::path& path);
    PcapWriter(const PcapWriter&) = delete;
    PcapWriter& operator=(const PcapWriter&) = delete;
    PcapWriter(PcapWriter&&) = delete;
    PcapWriter& operator=(PcapWriter&&) = delete;
    ~PcapWriter() = default;

    /// False if the file could not be created or a write has failed.
    [[nodiscard]] bool ok() const noexcept;

    /// Append one frame carrying `payload` as a UDP datagram from `source` to `destination`,
    /// captured at `time`. The IPv4 Identification counts up from 1 per frame.
    [[nodiscard]] std::expected<void, PcapError> write_udp(Time time, const Endpoint& source,
                                                           const Endpoint& destination,
                                                           std::span<const std::byte> payload);

    /// Number of frames written so far.
    [[nodiscard]] std::size_t frames() const noexcept { return frames_; }

    /// Flush the stream.
    void flush();

private:
    void write_file_header();

    std::ofstream file_;
    std::ostream* out_;
    std::size_t frames_ = 0;
};

} // namespace cigi::tools
