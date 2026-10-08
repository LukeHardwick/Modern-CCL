#pragma once

// Helpers for the session tests: a controllable clock, message fabrication and a way to read back
// what a session produced.

#include "../support/bytes.hpp"
#include "cigi/message/view.hpp"
#include "cigi/packets.hpp"
#include "cigi/session/host_session.hpp"
#include "cigi/session/ig_session.hpp"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace cigi::test {

/// A clock the test advances by hand. Copies of the ClockFn share the same counter.
class FakeClock {
public:
    explicit FakeClock(std::uint32_t start = 0) : ticks_(std::make_shared<std::uint32_t>(start)) {}
    [[nodiscard]] ClockFn fn() const {
        return [ticks = ticks_] { return Timestamp{*ticks}; };
    }
    void set(std::uint32_t t) const { *ticks_ = t; }
    void advance(std::uint32_t dt) const { *ticks_ += dt; } // wraps like the real timestamp

private:
    std::shared_ptr<std::uint32_t> ticks_;
};

template <class P> Bytes encoded(const P& p) {
    Bytes b(encoded_size(p));
    encode(p, b);
    return b;
}

/// Replace the Minor Version nibble of a frame packet at the start of `b`.
inline void set_minor(Bytes& b, std::uint8_t minor) {
    const std::size_t flags = std::to_integer<std::uint8_t>(b.at(0)) == ig_control_opcode ? 4 : 5;
    const auto old = std::to_integer<std::uint8_t>(b.at(flags));
    b.at(flags) = static_cast<std::byte>((old & 0x0FU) | (static_cast<unsigned>(minor) << 4U));
}

inline Bytes sof_message(std::uint32_t ig_frame, IgMode mode = IgMode::ResetStandby,
                         std::int8_t database = 0) {
    StartOfFrame sof;
    sof.ig_frame_number = ig_frame;
    sof.ig_mode = mode;
    sof.database_number = database;
    return encoded(sof);
}

inline Bytes control_message(std::uint32_t host_frame, IgMode mode = IgMode::ResetStandby,
                             std::int8_t database = 0) {
    IgControl control;
    control.host_frame_number = host_frame;
    control.ig_mode = mode;
    control.database_number = database;
    return encoded(control);
}

/// Parse a message a session produced and return its first packet as `P`.
template <class P> P first_as(std::span<const std::byte> message) {
    const auto view = MessageView::parse(message);
    EXPECT_TRUE(view.has_value());
    const auto packet =
        (*view->begin()).and_then([](const RawPacket& r) { return r.decode_as<P>(); });
    EXPECT_TRUE(packet.has_value());
    return packet.value_or(P{});
}

/// Find the first packet of type P after the frame packet; nullopt if absent.
template <class P> std::optional<P> find_packet(std::span<const std::byte> message) {
    const auto view = MessageView::parse(message);
    if (!view) {
        return std::nullopt;
    }
    for (const auto& item : *view) {
        if (item && item->opcode == P::opcode && item->offset != 0) {
            return item->template decode_as<P>().value_or(P{});
        }
    }
    return std::nullopt;
}

/// The contents of an optional that must be present (a failure is recorded, then a default is
/// used).
template <class T> T unwrap(const std::optional<T>& value) {
    EXPECT_TRUE(value.has_value());
    return value.value_or(T{});
}

/// A session config with a fake clock.
inline SessionConfig config_with(const FakeClock& clock, bool synchronous = true) {
    SessionConfig c;
    c.synchronous = synchronous;
    c.clock = clock.fn();
    return c;
}

/// Bring a Host to Operate against a 3.3 IG.
inline void make_operational(HostSession& host, IgMode mode = IgMode::Operate) {
    EXPECT_TRUE(host.receive(sof_message(1, mode)).has_value());
}

} // namespace cigi::test
