#include "cigi/tools/pcap_writer.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>

namespace cigi::tools {

namespace {

using Bytes = std::vector<std::byte>;

constexpr unsigned ip_version_ihl = 0x45; // IPv4, five 32-bit header words.
constexpr unsigned ip_ttl = 64;
constexpr unsigned ip_protocol_udp = 17;
constexpr unsigned ip_flag_dont_fragment = 0x4000;
constexpr std::uint32_t pcap_snap_length = 65535;

void push8(Bytes& out, unsigned v) {
    out.push_back(static_cast<std::byte>(v & 0xFFU));
}

/// Big-endian (network order).
void push16(Bytes& out, unsigned v) {
    push8(out, v >> 8U);
    push8(out, v);
}

void push_address(Bytes& out, const Ipv4Address& a) {
    std::ranges::transform(a.octets, std::back_inserter(out),
                           [](std::uint8_t b) { return static_cast<std::byte>(b); });
}

Bytes ip_header(const Endpoint& source, const Endpoint& destination, std::size_t total_length,
                std::uint16_t identification, std::uint16_t checksum) {
    Bytes h;
    h.reserve(ipv4_header_size);
    push8(h, ip_version_ihl);
    push8(h, 0); // Type of service.
    push16(h, static_cast<unsigned>(total_length));
    push16(h, identification);
    push16(h, ip_flag_dont_fragment);
    push8(h, ip_ttl);
    push8(h, ip_protocol_udp);
    push16(h, checksum);
    push_address(h, source.address);
    push_address(h, destination.address);
    return h;
}

Bytes udp_header(const Endpoint& source, const Endpoint& destination, std::size_t length,
                 std::uint16_t checksum) {
    Bytes h;
    h.reserve(udp_header_size);
    push16(h, source.port);
    push16(h, destination.port);
    push16(h, static_cast<unsigned>(length));
    push16(h, checksum);
    return h;
}

/// Append `v` to a char buffer little-endian (the pcap file and record headers).
template <class T> void put_le(std::string& out, T v) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(static_cast<char>((static_cast<std::uint64_t>(v) >> (8U * i)) & 0xFFU));
    }
}

} // namespace

std::uint16_t internet_checksum(std::span<const std::byte> data) noexcept {
    std::uint64_t sum = 0;
    bool high_byte = true;
    for (const std::byte b : data) {
        const auto v = std::to_integer<std::uint64_t>(b);
        sum += high_byte ? (v << 8U) : v;
        high_byte = !high_byte;
    }
    while ((sum >> 16U) != 0U) {
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    }
    return static_cast<std::uint16_t>(~sum & 0xFFFFU);
}

std::expected<std::vector<std::byte>, PcapError>
build_ipv4_udp_packet(const Endpoint& source, const Endpoint& destination,
                      std::span<const std::byte> payload, std::uint16_t identification) {
    if (payload.size() > max_udp_payload) {
        return std::unexpected(PcapError::PayloadTooLarge);
    }
    const std::size_t udp_length = udp_header_size + payload.size();

    // The UDP checksum covers a pseudo-header (addresses, protocol, UDP length) plus the datagram
    // with its checksum field zero.
    Bytes pseudo;
    push_address(pseudo, source.address);
    push_address(pseudo, destination.address);
    push8(pseudo, 0);
    push8(pseudo, ip_protocol_udp);
    push16(pseudo, static_cast<unsigned>(udp_length));
    const Bytes udp_zero = udp_header(source, destination, udp_length, 0);
    pseudo.insert(pseudo.end(), udp_zero.begin(), udp_zero.end());
    pseudo.insert(pseudo.end(), payload.begin(), payload.end());
    std::uint16_t udp_sum = internet_checksum(pseudo);
    if (udp_sum == 0) {
        udp_sum = 0xFFFF; // RFC 768: zero means "no checksum".
    }

    const std::size_t total = ipv4_header_size + udp_length;
    const Bytes ip_zero = ip_header(source, destination, total, identification, 0);
    const Bytes ip =
        ip_header(source, destination, total, identification, internet_checksum(ip_zero));
    const Bytes udp = udp_header(source, destination, udp_length, udp_sum);

    Bytes packet;
    packet.reserve(total);
    packet.insert(packet.end(), ip.begin(), ip.end());
    packet.insert(packet.end(), udp.begin(), udp.end());
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

PcapWriter::PcapWriter(std::ostream& out) : out_(&out) {
    write_file_header();
}

PcapWriter::PcapWriter(const std::filesystem::path& path)
    : file_(path, std::ios::binary | std::ios::trunc), out_(&file_) {
    write_file_header();
}

void PcapWriter::write_file_header() {
    std::string h;
    put_le<std::uint32_t>(h, 0xA1B2C3D4U); // Magic: microsecond timestamps, written little-endian.
    put_le<std::uint16_t>(h, 2);           // Version 2.4.
    put_le<std::uint16_t>(h, 4);
    put_le<std::int32_t>(h, 0);  // Time zone correction.
    put_le<std::uint32_t>(h, 0); // Timestamp accuracy.
    put_le<std::uint32_t>(h, pcap_snap_length);
    put_le<std::uint32_t>(h, linktype_raw);
    out_->write(h.data(), static_cast<std::streamsize>(h.size()));
}

bool PcapWriter::ok() const noexcept {
    return out_->good();
}

std::expected<void, PcapError> PcapWriter::write_udp(Time time, const Endpoint& source,
                                                     const Endpoint& destination,
                                                     std::span<const std::byte> payload) {
    const auto id = static_cast<std::uint16_t>(frames_ + 1);
    const auto packet = build_ipv4_udp_packet(source, destination, payload, id);
    if (!packet) {
        return std::unexpected(packet.error());
    }
    const auto count = std::max(time.count(), Time::rep{0});
    const auto seconds = static_cast<std::uint32_t>(count / 1'000'000);
    const auto micros = static_cast<std::uint32_t>(count % 1'000'000);
    const auto length = static_cast<std::uint32_t>(packet->size());

    std::string record;
    record.reserve(16 + packet->size());
    put_le(record, seconds);
    put_le(record, micros);
    put_le(record, length); // Captured length.
    put_le(record, length); // Original length.
    std::ranges::transform(*packet, std::back_inserter(record),
                           [](std::byte b) { return static_cast<char>(std::to_integer<int>(b)); });
    out_->write(record.data(), static_cast<std::streamsize>(record.size()));
    if (!out_->good()) {
        return std::unexpected(PcapError::WriteFailed);
    }
    ++frames_;
    return {};
}

void PcapWriter::flush() {
    out_->flush();
}

} // namespace cigi::tools
