#pragma once

// Helpers for the facade tests: a Host and an Ig wired back to back over the in-memory transport
// with one fake clock, and a scripted "raw" peer for the cases a real Ig would not produce.

#include "../session/support.hpp"
#include "cigi/datagram_transport.hpp"
#include "cigi/fault_transport.hpp"
#include "cigi/host.hpp"
#include "cigi/ig.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace cigi::test {

using namespace std::chrono_literals;

/// 1 / 60 s in 10 microsecond ticks (rounded): one IG frame.
inline constexpr std::uint32_t frame_ticks = 1667;

inline HostConfig host_config_with(const FakeClock& clock) {
    HostConfig c;
    c.session.clock = clock.fn();
    return c;
}
inline IgConfig ig_config_with(const FakeClock& clock) {
    IgConfig c;
    c.session.clock = clock.fn();
    return c;
}

/// A Host and an Ig joined by two in-memory wires (optionally with fault injection in between).
class Rig {
public:
    explicit Rig(bool faults = false) : Rig(HostConfig{}, IgConfig{}, faults) {}
    Rig(HostConfig host_config, IgConfig ig_config, bool faults = false)
        : clock_(std::make_shared<FakeClock>()) {
        host_config.session.clock = clock_->fn();
        ig_config.session.clock = clock_->fn();
        auto [to_host, to_ig] = make_memory_transport_pair();
        if (faults) {
            auto h = std::make_unique<FaultInjectingTransport>(std::move(to_host), clock_->fn(), 1);
            auto i = std::make_unique<FaultInjectingTransport>(std::move(to_ig), clock_->fn(), 2);
            host_faults_ = h.get();
            ig_faults_ = i.get();
            to_host = std::move(h);
            to_ig = std::move(i);
        }
        host_ =
            std::make_unique<Host>(Host::over(std::move(host_config), std::move(to_host)).value());
        ig_ = std::make_unique<Ig>(Ig::over(std::move(ig_config), std::move(to_ig)).value());
    }

    Host& host() { return *host_; }
    Ig& ig() { return *ig_; }
    const FakeClock& clock() const { return *clock_; }
    FaultInjectingTransport* host_faults() { return host_faults_; }
    FaultInjectingTransport* ig_faults() { return ig_faults_; }

    /// Advance the clock, then let the IG and the Host each poll once (the IG first, as its
    /// Start of Frame is what the synchronous Host answers).
    void step(std::uint32_t ticks = frame_ticks) {
        clock_->advance(ticks);
        EXPECT_TRUE(ig_->poll(0ms).has_value());
        EXPECT_TRUE(host_->poll(0ms).has_value());
    }
    void steps(int n, std::uint32_t ticks = frame_ticks) {
        for (int i = 0; i < n; ++i) {
            step(ticks);
        }
    }
    /// Step until the Host reaches `state` (at most `limit` steps); true if it did.
    bool run_until(LinkState state, int limit = 200) {
        for (int i = 0; i < limit && host_->state() != state; ++i) {
            step();
        }
        return host_->state() == state;
    }

private:
    std::shared_ptr<FakeClock> clock_;
    FaultInjectingTransport* host_faults_ = nullptr;
    FaultInjectingTransport* ig_faults_ = nullptr;
    std::unique_ptr<Host> host_;
    std::unique_ptr<Ig> ig_;
};

/// The other end of a Host (or Ig) under test, scripted by hand.
class RawPeer {
public:
    RawPeer() {
        auto [a, b] = make_memory_transport_pair();
        mine_ = std::move(a);
        theirs_ = std::move(b);
    }
    /// The end to give to Host::over / Ig::over (once).
    std::unique_ptr<DatagramTransport> take_other() { return std::move(theirs_); }

    void send(const Bytes& datagram) { EXPECT_TRUE(mine_->send(datagram).has_value()); }
    /// Everything the facade has sent so far.
    std::vector<Bytes> received() {
        std::vector<Bytes> all;
        Bytes buffer(65507);
        for (;;) {
            const auto n = mine_->receive(buffer, 0ms);
            if (!n) {
                break;
            }
            all.emplace_back(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*n));
        }
        return all;
    }

private:
    std::unique_ptr<DatagramTransport> mine_;
    std::unique_ptr<DatagramTransport> theirs_;
};

/// A transport that fails on demand.
class BrokenTransport final : public DatagramTransport {
public:
    ErrorCode send_error = ErrorCode::TransportError;
    bool fail_send = false;
    bool fail_receive = false;

    Result<void> send(std::span<const std::byte>) override {
        if (fail_send) {
            return std::unexpected(Error{.code = send_error, .os_error = 5});
        }
        return {};
    }
    Result<std::size_t> receive(std::span<std::byte>, std::chrono::milliseconds) override {
        if (fail_receive) {
            return std::unexpected(Error{.code = ErrorCode::TransportError, .os_error = 7});
        }
        return std::unexpected(Error{.code = ErrorCode::Timeout});
    }
};

/// The IgControl of a datagram the Host sent.
inline IgControl control_of(const Bytes& datagram) {
    return first_as<IgControl>(datagram);
}

} // namespace cigi::test
