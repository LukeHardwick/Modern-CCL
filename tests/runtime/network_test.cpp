// Host and Ig facades over real loopback UDP, each running on its own thread (label: network).
// Timing is lenient: every wait has a generous limit and nothing depends on exact rates.

#include "cigi/host.hpp"
#include "cigi/ig.hpp"
#include "cigi/transport/udp.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>

namespace {

using namespace cigi;
using namespace std::chrono_literals;

std::uint16_t free_port() {
    auto endpoint = transport::UdpEndpoint::open(
        {.local_address = "127.0.0.1", .local_port = 0, .remote_address = {}});
    EXPECT_TRUE(endpoint.has_value());
    return endpoint ? endpoint->local_port() : std::uint16_t{0};
}

template <class Condition>
bool wait_for(const Condition& condition, std::chrono::seconds limit = 10s) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

/// One Host <-> Ig exchange: entity updates flow Host -> IG, a HAT/HOT request is answered.
struct Exchange {
    std::atomic<bool> operate{false};
    std::atomic<int> entities{0};
    std::atomic<bool> answered{false};
    std::atomic<std::uint16_t> answered_id{0};
    std::atomic<LinkState> final_state{LinkState::WaitingForIg};

    void run(HostConfig host_config, IgConfig ig_config) {
        host_config.peer_timeout = 5s;
        auto host = Host::connect(host_config);
        auto ig = Ig::connect(std::move(ig_config));
        ASSERT_TRUE(host.has_value());
        ASSERT_TRUE(ig.has_value());

        std::optional<std::uint16_t> pending;
        ig->on<EntityControl>([&](const EntityControl&) { ++entities; });
        ig->on<HatHotRequest>([&](const HatHotRequest& r) { pending = r.hat_hot_id; });
        ig->on_frame([&](IgFrame& frame) {
            if (pending) {
                HatHotResponse response;
                response.hat_hot_id = *pending;
                response.valid = true;
                response.height = 42.0;
                response.host_frame_lsn = frame.host_frame_lsn();
                if (frame.add(response)) {
                    pending.reset();
                }
            }
        });
        host->on<HatHotResponse>([&](const HatHotResponse& r) {
            answered_id = r.hat_hot_id;
            answered = true;
        });
        host->on_state_change([&](LinkState, LinkState to) {
            if (to == LinkState::Operate) {
                operate = true;
            }
        });
        float yaw = 0.0F;
        host->on_frame([&](HostFrame& frame) {
            EntityControl entity;
            entity.entity_id = 1;
            entity.entity_state = EntityState::Active;
            entity.yaw = yaw;
            yaw = yaw >= 359.0F ? 0.0F : yaw + 1.0F;
            EXPECT_TRUE(frame.add(entity));
            if (!answered) {
                HatHotRequest request;
                request.hat_hot_id = 17;
                EXPECT_TRUE(frame.add(request));
            }
        });

        Result<void> host_result;
        Result<void> ig_result;
        {
            std::jthread const ig_thread(
                [&](const std::stop_token& st) { ig_result = ig->run(st, 5ms); });
            std::jthread const host_thread(
                [&](const std::stop_token& st) { host_result = host->run(st, 5ms); });
            EXPECT_TRUE(wait_for(
                [&] { return operate.load() && entities.load() >= 10 && answered.load(); }));
        } // joins (jthread requests stop first)
        EXPECT_TRUE(host_result);
        EXPECT_TRUE(ig_result);
        final_state = host->state();
        EXPECT_EQ(host->stats().link_losses, 0U);
        EXPECT_EQ(host->stats().receive_errors, 0U);
        EXPECT_EQ(ig->stats().receive_errors, 0U);
        host->close();
        EXPECT_EQ(host->state(), LinkState::Closed);
    }
};

TEST(Network, HostReachesOperateExchangesEntitiesAndGetsAHatHotResponse) {
    const std::uint16_t ig_port = free_port();
    const std::uint16_t host_port = free_port();
    HostConfig host;
    host.ig_address = "127.0.0.1";
    host.ig_port = ig_port;
    host.local_address = "127.0.0.1";
    host.local_port = host_port;
    IgConfig ig;
    ig.listen_address = "127.0.0.1";
    ig.listen_port = ig_port;
    ig.host_address = "127.0.0.1";
    ig.host_port = host_port;

    Exchange exchange;
    exchange.run(host, ig);
    EXPECT_TRUE(exchange.operate);
    EXPECT_GE(exchange.entities.load(), 10);
    EXPECT_TRUE(exchange.answered);
    EXPECT_EQ(exchange.answered_id.load(), 17);
    EXPECT_EQ(exchange.final_state.load(), LinkState::Operate);
}

TEST(Network, AnIgThatLearnsItsHostFromTheFirstIgControlAnswersIt) {
    const std::uint16_t ig_port = free_port();
    const std::uint16_t host_port = free_port();
    HostConfig host;
    host.ig_address = "127.0.0.1";
    host.ig_port = ig_port;
    host.local_address = "127.0.0.1";
    host.local_port = host_port;
    host.probe_interval = 20ms; // the Host's probes are what the IG answers
    IgConfig ig;
    ig.listen_address = "127.0.0.1";
    ig.listen_port = ig_port; // no host_address: reply to the sender

    Exchange exchange;
    exchange.run(host, ig);
    EXPECT_TRUE(exchange.operate);
    EXPECT_TRUE(exchange.answered);
}

TEST(Network, AnAsynchronousHostSendsAtItsOwnRate) {
    const std::uint16_t ig_port = free_port();
    const std::uint16_t host_port = free_port();
    HostConfig host;
    host.ig_address = "127.0.0.1";
    host.ig_port = ig_port;
    host.local_address = "127.0.0.1";
    host.local_port = host_port;
    host.synchronous = false;
    host.async_rate_hz = 120.0;
    IgConfig ig;
    ig.listen_address = "127.0.0.1";
    ig.listen_port = ig_port;
    ig.host_address = "127.0.0.1";
    ig.host_port = host_port;

    Exchange exchange;
    exchange.run(host, ig);
    EXPECT_TRUE(exchange.operate);
    EXPECT_GE(exchange.entities.load(), 10);
}

} // namespace
