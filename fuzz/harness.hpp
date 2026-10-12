#pragma once

// Shared logic of the fuzz targets (fuzz_message_view, fuzz_roundtrip) and of the corpus-replay
// tests. Everything here is plain C++ over the public API; a violated property aborts, so
// libFuzzer (and the replay ctests) report it as a crash.
//
// Packets are decoded through decode_any<HostToIgPackets> / decode_any<IgToHostPackets>, so a
// packet added to those sets is fuzzed with no change here.

#include "cigi/dispatch.hpp"
#include "cigi/message/builder.hpp"
#include "cigi/message/view.hpp"
#include "cigi/packet_set.hpp"
#include "cigi/packets.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

namespace cigi::fuzz {

/// Abort with a message when a property does not hold.
inline void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "fuzz property violated: %s\n", what);
        std::abort();
    }
}

inline wire::ByteOrder opposite(wire::ByteOrder o) noexcept {
    return o == wire::ByteOrder::Native ? wire::ByteOrder::Swapped : wire::ByteOrder::Native;
}

/// What decode_any<Set> produced for one packet: checks the result against the packet and
/// message, runs validate() on the decoded packet, and calls `on_packet(index, raw, packet)` for a
/// known packet.
template <class Set, class F>
void decode_with_set(const RawPacket& raw, Direction message, std::size_t index, F& on_packet) {
    const auto decoded = decode_any<Set>(raw, message);
    if (!decoded) {
        check(decoded.error().offset >= raw.offset && decoded.error().opcode == raw.opcode,
              "decode error carries message offset and opcode");
        return;
    }
    std::visit(
        [&](const auto& p) {
            using P = std::remove_cvref_t<decltype(p)>;
            if constexpr (std::is_same_v<P, UnknownPacket>) {
                check(p.opcode == raw.opcode && p.bytes.data() == raw.bytes.data() &&
                          p.bytes.size() == raw.bytes.size() && p.offset == raw.offset,
                      "UnknownPacket describes the raw packet");
            } else {
                check(P::opcode == raw.opcode, "decoded packet has the raw packet's opcode");
                (void)validate(p);
                on_packet(index, raw, p);
            }
        },
        *decoded);
}

/// MessageView::parse, validate_all, iterate; each packet is decoded with decode_any for both
/// packet sets (the message's own direction, and also with the direction check off, so the
/// "wrong set for this message" paths run), and decode_as with the other byte order.
/// `on_packet` is called as on_packet(index, raw, decoded) for each successful known decode of the
/// set that matches the message direction.
template <class F> void walk_message(std::span<const std::byte> data, F&& on_packet) {
    const auto view = MessageView::parse(data);
    if (!view) {
        check(view.error().offset <= data.size(), "parse error offset in range");
        return;
    }
    const auto valid = view->validate_all();
    const bool host_message = view->direction() == Direction::HostToIg;
    const auto ignore = [](std::size_t, const RawPacket&, const auto&) {};
    std::size_t expected_offset = 0;
    bool saw_error = false;
    bool saw_duplicate_frame = false;
    std::size_t index = 0;
    for (const auto& item : *view) {
        check(!saw_error, "a structural error ends iteration");
        if (!item) {
            saw_error = true;
            check(item.error().offset <= data.size(), "iteration error offset in range");
            continue;
        }
        const RawPacket& raw = *item;
        check(raw.offset == expected_offset, "packets are contiguous");
        check(!raw.bytes.empty() && raw.bytes.size() % 8 == 0, "packet size is a multiple of 8");
        check(raw.offset + raw.bytes.size() <= data.size(), "packet lies inside the datagram");
        check(raw.bytes.data() == data.data() + raw.offset, "packet bytes alias the datagram");
        expected_offset += raw.bytes.size();
        saw_duplicate_frame =
            saw_duplicate_frame ||
            (raw.offset != 0 && raw.opcode == frame_packet_opcode(view->direction()));

        RawPacket flipped = raw;
        flipped.byte_order = opposite(raw.byte_order);
        decode_with_set<HostToIgPackets>(flipped, Direction::Both, index, ignore);
        decode_with_set<IgToHostPackets>(flipped, Direction::Both, index, ignore);
        if (host_message) {
            decode_with_set<HostToIgPackets>(raw, view->direction(), index, on_packet);
            decode_with_set<IgToHostPackets>(raw, Direction::Both, index, ignore);
        } else {
            decode_with_set<IgToHostPackets>(raw, view->direction(), index, on_packet);
            decode_with_set<HostToIgPackets>(raw, Direction::Both, index, ignore);
        }
        ++index;
    }
    check(valid.has_value() == !(saw_error || saw_duplicate_frame),
          "validate_all agrees with iteration");
    check(saw_error || expected_offset == data.size(), "a clean walk consumes the datagram");
}

/// Fuzz target 1: nothing may crash, whatever the bytes.
inline void exercise_message_view(std::span<const std::byte> data) {
    walk_message(data, [](std::size_t, const RawPacket&, const auto&) {});
}

// ---- Round trip -------------------------------------------------------------------------

/// Reverse the multi-byte fields of a 24-byte IG Control / Start of Frame packet (bytes 6-7 and
/// the u32 at 8, 12 and 16): what a peer of the other byte order would have sent.
inline void swap_frame_fields(std::span<std::byte> b) {
    const auto rev = [&](std::size_t off, std::size_t n) {
        for (std::size_t i = 0; i < n / 2; ++i) {
            std::swap(b[off + i], b[off + n - 1 - i]);
        }
    };
    rev(6, 2);
    rev(8, 4);
    rev(12, 4);
    rev(16, 4);
}

/// Frame packets carry the peer's version; encode always writes the library's own.
template <class P> [[nodiscard]] P normalised(P p) {
    if constexpr (requires { p.version; }) {
        p.version = protocol_version;
    }
    return p;
}

template <class P> void round_trip_one(const P& decoded) {
    // Encode into a dirty buffer: reserved bytes must be written, not left alone.
    std::array<std::byte, 1024> storage{};
    storage.fill(std::byte{0xFF});
    const std::size_t n = encoded_size(decoded);
    check(n >= 2 && n % 8 == 0 && n <= storage.size(), "encoded_size is a sane multiple of 8");
    const std::span<std::byte> buf = std::span(storage).first(n);
    encode(decoded, buf);
    check(buf[0] == static_cast<std::byte>(P::opcode) && buf[1] == static_cast<std::byte>(n),
          "encode writes opcode and size");

    const auto again = decode(std::type_identity<P>{}, buf, wire::ByteOrder::Native);
    check(again.has_value(), "encoded bytes decode");
    // Compare by value (a NaN field never compares equal to itself, so skip those) and, always,
    // by bytes: re-encoding the decoded value must reproduce the first encoding exactly.
    const P expected = normalised(decoded);
    if (expected == expected) {
        check(*again == expected, "decode(encode(p)) == p");
    }
    check(validate(*again).has_value() == validate(decoded).has_value(),
          "validate() is preserved by a round trip");
    std::array<std::byte, 1024> storage2{};
    storage2.fill(std::byte{0xEE}); // a different dirty value: reserved bytes must still be zero
    const std::span<std::byte> buf2 = std::span(storage2).first(n);
    encode(*again, buf2);
    check(std::ranges::equal(buf, buf2), "encode is idempotent");

    if constexpr (requires { decoded.version; }) {
        // Reserved bytes 20-23 are zero in both frame packets (ICD 4.1.1, 4.2.1).
        for (std::size_t i = 20; i < 24; ++i) {
            check(buf[i] == std::byte{0}, "reserved bytes are zero after encode");
        }
        // The same packet as the other byte order would send it.
        std::array<std::byte, P::wire_size> foreign{};
        std::ranges::copy(buf, foreign.begin());
        swap_frame_fields(foreign);
        const auto swapped = decode(std::type_identity<P>{}, foreign, wire::ByteOrder::Swapped);
        check(swapped.has_value() && *swapped == normalised(decoded),
              "foreign byte order decodes equal");
    }
}

/// Fuzz target 2: every packet that decodes survives encode -> decode unchanged, and a message
/// rebuilt with MessageBuilder from the decoded packets parses again.
inline void exercise_roundtrip(std::span<const std::byte> data) {
    // Rebuilding needs the frame packet first; later packets are re-added when they fit.
    std::array<std::byte, 4096> storage{};
    std::optional<MessageBuilder> builder;
    walk_message(data, [&](std::size_t index, const RawPacket& /*raw*/, const auto& packet) {
        round_trip_one(packet);
        if (index == 0) {
            builder.emplace(packet.direction, storage);
        }
        if (builder) {
            const auto added = builder->add(packet);
            // Only the frame packet (index 0) is certain to fit; anything else may be a duplicate
            // frame packet or overflow the buffer, which the builder reports without side effects.
            check(index != 0 || added.has_value(), "builder accepts the first frame packet");
        }
    });
    if (builder) {
        const auto view = MessageView::parse(builder->bytes());
        check(view.has_value() && view->validate_all().has_value(), "rebuilt message parses");
    }
}

} // namespace cigi::fuzz
