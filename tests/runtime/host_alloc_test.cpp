// The steady-state frame path of Host and Ig must not allocate (user callbacks excluded). This
// binary replaces the global operator new to count calls, so it is kept apart from the others.

#include "../support/counting_new.hpp"
#include "support.hpp"

#include <array>
#include <cstddef>
#include <gtest/gtest.h>
#include <new>

namespace {

using namespace cigi;
using namespace cigi::test;

/// A fixed-capacity queue of datagrams (no allocation after construction).
struct Ring {
    static constexpr std::size_t slots = 8;
    static constexpr std::size_t slot_bytes = 512;
    std::array<std::array<std::byte, slot_bytes>, slots> data{};
    std::array<std::size_t, slots> sizes{};
    std::size_t head = 0;
    std::size_t count = 0;
};

class RingTransport final : public DatagramTransport {
public:
    RingTransport(Ring& out, Ring& in) : out_(&out), in_(&in) {}
    Result<void> send(std::span<const std::byte> datagram) override {
        if (out_->count == Ring::slots || datagram.size() > Ring::slot_bytes) {
            return std::unexpected(Error{.code = ErrorCode::TransportError});
        }
        const std::size_t at = (out_->head + out_->count++) % Ring::slots;
        std::ranges::copy(datagram, out_->data[at].begin());
        out_->sizes[at] = datagram.size();
        return {};
    }
    Result<std::size_t> receive(std::span<std::byte> buffer, std::chrono::milliseconds) override {
        if (in_->count == 0) {
            return std::unexpected(Error{.code = ErrorCode::Timeout});
        }
        const std::size_t n = in_->sizes[in_->head];
        std::copy_n(in_->data[in_->head].begin(), n, buffer.begin());
        in_->head = (in_->head + 1) % Ring::slots;
        --in_->count;
        return n;
    }

private:
    Ring* out_;
    Ring* in_;
};

TEST(HostAllocations, TheSteadyStateFramePathDoesNotAllocate) {
    Ring to_ig;
    Ring to_host;
    FakeClock clock;
    auto host = Host::over(host_config_with(clock), std::make_unique<RingTransport>(to_ig, to_host))
                    .value();
    auto ig =
        Ig::over(ig_config_with(clock), std::make_unique<RingTransport>(to_host, to_ig)).value();

    int entities = 0;
    int responses = 0;
    ig.on<EntityControl>([&](const EntityControl&) { ++entities; });
    ig.on<HatHotRequest>([&](const HatHotRequest&) {});
    ig.on_frame([](IgFrame& f) { static_cast<void>(f.add(HatHotResponse{})); });
    host.on<HatHotResponse>([&](const HatHotResponse&) { ++responses; });
    host.on_frame([](HostFrame& f) {
        EntityControl entity;
        entity.entity_state = EntityState::Active;
        static_cast<void>(f.add(entity));
        static_cast<void>(f.add(HatHotRequest{}));
    });

    const auto step = [&] {
        clock.advance(frame_ticks);
        EXPECT_TRUE(ig.poll(std::chrono::milliseconds{0}));
        EXPECT_TRUE(host.poll(std::chrono::milliseconds{0}));
    };
    for (int i = 0; i < 20; ++i) { // warm-up: start-up sequence, first-use allocations
        step();
    }
    ASSERT_EQ(host.state(), LinkState::Operate);
    const int entities_before = entities;

    const std::size_t before = allocations.load();
    for (int i = 0; i < 200; ++i) {
        step();
    }
    const std::size_t after = allocations.load();

    EXPECT_EQ(after, before);
    EXPECT_GE(entities - entities_before, 190); // the frames really flowed
    EXPECT_GT(responses, 150);
}

TEST(HostAllocations, ReceivingTypedUserPacketsDoesNotAllocate) {
    Ring to_ig;
    Ring to_host;
    FakeClock clock;
    auto host = Host::over(host_config_with(clock), std::make_unique<RingTransport>(to_ig, to_host))
                    .value();
    auto ig =
        Ig::over(ig_config_with(clock), std::make_unique<RingTransport>(to_host, to_ig)).value();

    int at_ig = 0;
    int at_host = 0;
    ig.on_user<UserPacket>([&](const UserPacket&) { ++at_ig; });
    host.on_user<UserPacket>([&](const UserPacket&) { ++at_host; });
    ig.on_frame([](IgFrame& f) { static_cast<void>(f.add(UserPacket{.value = 2})); });
    host.on_frame([](HostFrame& f) { static_cast<void>(f.add(UserPacket{.value = 1})); });

    const auto step = [&] {
        clock.advance(frame_ticks);
        EXPECT_TRUE(ig.poll(std::chrono::milliseconds{0}));
        EXPECT_TRUE(host.poll(std::chrono::milliseconds{0}));
    };
    for (int i = 0; i < 20; ++i) {
        step();
    }
    ASSERT_EQ(host.state(), LinkState::Operate);
    const int ig_before = at_ig;
    const int host_before = at_host;

    const std::size_t before = allocations.load();
    for (int i = 0; i < 200; ++i) {
        step();
    }
    const std::size_t after = allocations.load();

    EXPECT_EQ(after, before);
    EXPECT_GE(at_ig - ig_before, 190);
    EXPECT_GE(at_host - host_before, 190);
}

} // namespace
