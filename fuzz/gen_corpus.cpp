// Seed-corpus generator for the fuzz targets.
//
//   cigi_gen_corpus <out_dir> [--import <dir>]... [--import-pcap <file.pcap>]...
//
// Writes the same seed messages into <out_dir>/message_view and <out_dir>/roundtrip, and into
// <out_dir>/sessions with the two option bytes fuzz_sessions expects in front (cycled so that every
// IG mode and option combination is seeded). The seeds are
// (a) the hand-derived golden byte vectors of tests/packets/*_test.cpp, (b) messages assembled
// with MessageBuilder in both byte orders, and (c) hand-damaged messages (truncation, bad size,
// bad magic, wrong version, unknown opcodes). Every `--import <dir>` copies each regular file of
// <dir> into the corpora as `import_<name>`: the hook for any external raw-datagram corpus.
// `--import-pcap <file>` does the same for every UDP payload of a classic pcap file (raw-IPv4 or
// Ethernet link type): the T10 Wireshark corpus (make_wireshark_corpus -> corpus.pcap) goes in
// this way, see fuzz/regen_corpus.sh.

#include "harness.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

using cigi::fuzz::swap_frame_fields;
using Bytes = std::vector<std::byte>;
namespace fs = std::filesystem;

template <class... T> Bytes bytes_of(T... values) {
    return {static_cast<std::byte>(values)...};
}

struct Seeds {
    std::vector<std::pair<std::string, Bytes>> items;
    void add(const std::string& name, Bytes b) { items.emplace_back(name, std::move(b)); }
};

template <class P> Bytes message(const P& frame) {
    cigi::StaticMessageBuilder<64> builder(P::direction);
    (void)builder.add(frame);
    return {builder.bytes().begin(), builder.bytes().end()};
}

/// A packet of `size` bytes with the given opcode and a recognisable filler.
Bytes filler_packet(std::uint8_t opcode, std::uint8_t size, std::uint8_t fill) {
    Bytes b(size, static_cast<std::byte>(fill));
    b[0] = static_cast<std::byte>(opcode);
    b[1] = static_cast<std::byte>(size);
    return b;
}

void append(Bytes& dst, const Bytes& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

Bytes foreign(Bytes b) {
    swap_frame_fields(b);
    return b;
}

/// Turn a modern frame packet into what a CIGI 3.0 / 3.1 peer sends: 16 bytes, given minor.
Bytes legacy(Bytes b, std::uint8_t minor) {
    const std::size_t flags =
        std::to_integer<std::uint8_t>(b[0]) == cigi::ig_control_opcode ? 4 : 5;
    b.resize(16);
    b[1] = std::byte{16};
    b[flags] = static_cast<std::byte>((std::to_integer<std::uint8_t>(b[flags]) & 0x0FU) |
                                      static_cast<unsigned>(minor << 4));
    return b;
}

Bytes with_byte(Bytes b, std::size_t i, std::uint8_t v) {
    b[i] = static_cast<std::byte>(v);
    return b;
}

void make_seeds(Seeds& s) {
    using namespace cigi;

    // (a) Golden vectors (tests/packets/ig_control_test.cpp), little- and big-endian.
    const auto ig_le = bytes_of(0x01, 0x18, 0x03, 0x05, 0x3D, 0x00, 0x00, 0x80, //
                                0x04, 0x03, 0x02, 0x01, 0x08, 0x07, 0x06, 0x05, //
                                0x0C, 0x0B, 0x0A, 0x09, 0x00, 0x00, 0x00, 0x00);
    const auto ig_be = bytes_of(0x01, 0x18, 0x03, 0x05, 0x3D, 0x00, 0x80, 0x00, //
                                0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, //
                                0x09, 0x0A, 0x0B, 0x0C, 0x00, 0x00, 0x00, 0x00);
    s.add("golden_ig_control_le", ig_le);
    s.add("golden_ig_control_be", ig_be);

    // (b) Builder-made messages: each mode and flag combination, both packets, both orders.
    const IgControl ig_default{};
    const IgControl ig_operate{.database_number = 5,
                               .ig_mode = IgMode::Operate,
                               .timestamp_valid = true,
                               .extrapolation_enabled = true,
                               .host_frame_number = 0x01020304,
                               .timestamp = Timestamp{0x05060708},
                               .last_ig_frame_number = 0x090A0B0C};
    const IgControl ig_debug{.database_number = 127,
                             .ig_mode = IgMode::Debug,
                             .host_frame_number = 0xFFFFFFFF,
                             .timestamp = Timestamp{0xFFFFFFFF}};
    const IgControl ig_offline{.database_number = -1, .ig_mode = IgMode::OfflineMaintenance};
    const StartOfFrame sof_default{};
    const StartOfFrame sof_operate{.database_number = -128,
                                   .ig_status = 0xA5,
                                   .ig_mode = IgMode::Operate,
                                   .timestamp_valid = true,
                                   .earth_reference_model = EarthReferenceModel::HostDefined,
                                   .ig_frame_number = 0x11223344,
                                   .timestamp = Timestamp{0x55667788},
                                   .last_host_frame_number = 0x99AABBCC};
    const StartOfFrame sof_offline{
        .database_number = 127, .ig_mode = IgMode::OfflineMaintenance, .ig_frame_number = 1};

    const auto ig_default_m = message(ig_default);
    const auto ig_operate_m = message(ig_operate);
    const auto sof_default_m = message(sof_default);
    const auto sof_operate_m = message(sof_operate);

    s.add("ig_control_default", ig_default_m);
    s.add("ig_control_operate", ig_operate_m);
    s.add("ig_control_debug", message(ig_debug));
    s.add("ig_control_offline", message(ig_offline));
    s.add("sof_default", sof_default_m);
    s.add("sof_operate", sof_operate_m);
    s.add("sof_offline", message(sof_offline));
    s.add("ig_control_operate_foreign", foreign(ig_operate_m));
    s.add("ig_control_default_foreign", foreign(ig_default_m));
    s.add("sof_operate_foreign", foreign(sof_operate_m));
    s.add("sof_default_foreign", foreign(sof_default_m));

    // Older peers (ICD 3.0 / 3.1 send 16 bytes; 3.2 uses 24 but has no extrapolation flag).
    s.add("ig_control_v30", legacy(ig_operate_m, 0));
    s.add("ig_control_v31", legacy(ig_operate_m, 1));
    s.add("ig_control_v31_foreign", legacy(foreign(ig_operate_m), 1));
    s.add("sof_v30", legacy(sof_operate_m, 0));
    s.add("sof_v31", legacy(sof_operate_m, 1));
    s.add("sof_v31_foreign", legacy(foreign(sof_operate_m), 1));
    s.add("ig_control_v32", with_byte(ig_operate_m, 4, 0x2D));
    s.add("sof_v32", with_byte(sof_operate_m, 5, 0x25));
    s.add("ig_control_v34", with_byte(ig_operate_m, 4, 0x4D)); // unknown minor 4
    s.add("sof_v3f", with_byte(sof_operate_m, 5, 0xF5));

    // Multi-packet messages: frame packet followed by other packets, known or not.
    auto multi = ig_operate_m;
    append(multi, filler_packet(2, 16, 0xAB));
    append(multi, filler_packet(3, 48, 0xCD));
    append(multi, filler_packet(201, 8, 0x5A));
    s.add("ig_control_then_packets", multi);
    s.add("ig_control_then_packets_foreign", foreign(multi));
    auto multi_ig = sof_operate_m;
    append(multi_ig, filler_packet(102, 24, 0x11));
    append(multi_ig, filler_packet(105, 16, 0x22));
    s.add("sof_then_packets", multi_ig);
    auto dup = ig_default_m; // a second frame packet in one message (ICD 2.6)
    append(dup, ig_operate_m);
    s.add("ig_control_duplicate", dup);
    auto wrong_dir = ig_default_m; // a Start of Frame inside a Host message
    append(wrong_dir, sof_default_m);
    s.add("ig_control_then_sof", wrong_dir);
    auto many = ig_default_m;
    for (std::uint8_t i = 0; i < 8; ++i) {
        append(many, filler_packet(static_cast<std::uint8_t>(4 + i), 8, i));
    }
    s.add("ig_control_many_small", many);

    // (c) Damaged messages.
    s.add("empty", {});
    s.add("one_byte", bytes_of(0x01));
    s.add("truncated_15", Bytes(ig_operate_m.begin(), ig_operate_m.begin() + 15));
    s.add("truncated_23", Bytes(ig_operate_m.begin(), ig_operate_m.begin() + 23));
    s.add("size_zero", with_byte(ig_operate_m, 1, 0));
    s.add("size_not_multiple_of_8", with_byte(ig_operate_m, 1, 20));
    s.add("size_overruns", with_byte(ig_operate_m, 1, 40));
    s.add("size_255", with_byte(ig_operate_m, 1, 255));
    s.add("bad_magic", with_byte(ig_operate_m, 6, 0x12));
    s.add("bad_major_version", with_byte(ig_operate_m, 2, 4));
    s.add("first_packet_not_frame", filler_packet(2, 24, 0));
    auto bad_second = ig_operate_m;
    append(bad_second, with_byte(filler_packet(2, 16, 0xAB), 1, 12));
    s.add("second_packet_bad_size", bad_second);
    auto zero_second = ig_operate_m;
    append(zero_second, with_byte(filler_packet(2, 16, 0), 1, 0));
    s.add("second_packet_size_zero", zero_second);
    auto overrun_second = ig_operate_m;
    append(overrun_second, with_byte(filler_packet(2, 16, 0xAB), 1, 64));
    s.add("second_packet_overruns", overrun_second);
    auto trailing = ig_operate_m;
    append(trailing, bytes_of(0x02));
    s.add("trailing_single_byte", trailing);
    s.add("all_ff", Bytes(32, std::byte{0xFF}));
    s.add("all_zero", Bytes(32, std::byte{0}));
    s.add("ig_control_larger_than_24", [&] {
        auto b = ig_operate_m;
        append(b, bytes_of(1, 2, 3, 4, 5, 6, 7, 8));
        b[1] = std::byte{32}; // a newer peer's longer packet: extra bytes are ignored
        return b;
    }());
}

bool write_file(const fs::path& path, const Bytes& b) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(
                  b.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
              static_cast<std::streamsize>(b.size()));
    return out.good();
}

bool read_file(const fs::path& path, Bytes& b) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    const std::vector<char> raw{std::istreambuf_iterator<char>(in), {}};
    b.clear();
    for (const char c : raw) {
        b.push_back(static_cast<std::byte>(c));
    }
    return true;
}

std::uint32_t le32(const Bytes& b, std::size_t at) {
    std::uint32_t v = 0;
    for (std::size_t i = 4; i-- > 0;) {
        v = (v << 8U) | std::to_integer<std::uint32_t>(b[at + i]);
    }
    return v;
}

/// UDP payloads of a classic little-endian pcap with microsecond timestamps (LINKTYPE_RAW or
/// LINKTYPE_ETHERNET carrying IPv4/UDP). Frames that are anything else are skipped.
std::vector<Bytes> pcap_payloads(const Bytes& file) {
    std::vector<Bytes> out;
    constexpr std::size_t global_header = 24;
    constexpr std::size_t record_header = 16;
    if (file.size() < global_header || le32(file, 0) != 0xA1B2C3D4U) {
        return out;
    }
    const std::uint32_t link = le32(file, 20);
    const std::size_t link_header = link == 1 ? 14 : 0; // Ethernet, or raw IP
    std::size_t pos = global_header;
    while (pos + record_header <= file.size()) {
        const std::size_t len = le32(file, pos + 8);
        pos += record_header;
        if (len > file.size() - pos) {
            break;
        }
        const std::size_t ip = pos + link_header;
        const std::size_t end = pos + len;
        if (ip + 20 <= end && (std::to_integer<unsigned>(file[ip]) >> 4U) == 4U &&
            std::to_integer<unsigned>(file[ip + 9]) == 17U) {
            const std::size_t ihl = (std::to_integer<std::size_t>(file[ip]) & 0x0FU) * 4U;
            const std::size_t payload = ip + ihl + 8;
            if (ihl >= 20 && payload <= end) {
                out.emplace_back(file.begin() + static_cast<std::ptrdiff_t>(payload),
                                 file.begin() + static_cast<std::ptrdiff_t>(end));
            }
        }
        pos = end;
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fputs("usage: cigi_gen_corpus <out_dir> [--import <dir>]... [--import-pcap <file>]...\n", stderr);
        return 2;
    }
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    const fs::path out = args[1];
    Seeds seeds;
    make_seeds(seeds);

    std::vector<fs::path> imports;
    std::vector<fs::path> pcaps;
    for (std::size_t i = 2; i < args.size(); ++i) {
        const std::string_view option = args[i];
        if ((option != "--import" && option != "--import-pcap") || i + 1 >= args.size()) {
            std::fputs("usage: cigi_gen_corpus <out_dir> [--import <dir>]... "
                       "[--import-pcap <file>]...\n",
                       stderr);
            return 2;
        }
        (option == "--import" ? imports : pcaps).emplace_back(args[++i]);
    }
    for (const auto& pcap : pcaps) {
        Bytes file;
        if (!read_file(pcap, file)) {
            std::fprintf(stderr, "cannot read %s\n", pcap.string().c_str());
            return 1;
        }
        std::size_t n = 0;
        for (auto& payload : pcap_payloads(file)) {
            seeds.add("pcap_" + pcap.stem().string() + "_" + std::to_string(n++),
                      std::move(payload));
        }
    }
    std::error_code ec;
    for (const auto& dir : imports) {
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            Bytes b;
            if (entry.is_regular_file(ec) && read_file(entry.path(), b)) {
                seeds.add("import_" + entry.path().filename().string(), std::move(b));
            }
        }
    }

    for (const char* target : {"message_view", "roundtrip", "sessions"}) {
        const fs::path dir = out / target;
        fs::create_directories(dir, ec);
        unsigned index = 0;
        for (const auto& [name, message] : seeds.items) {
            Bytes b = message;
            if (std::string_view(target) == "sessions") {
                // options byte: cycle the five option bits; mode byte: cycle the four IG modes.
                b.insert(b.begin(), {static_cast<std::byte>(index % 32U),
                                     static_cast<std::byte>((index / 2U) % 4U)});
            }
            ++index;
            if (!write_file(dir / (name + ".bin"), b)) {
                std::fprintf(stderr, "cannot write %s\n", (dir / name).string().c_str());
                return 1;
            }
        }
    }
    std::printf("wrote %zu seeds per target to %s\n", seeds.items.size(), out.string().c_str());
    return 0;
}
