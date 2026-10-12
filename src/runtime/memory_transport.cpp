#include "cigi/datagram_transport.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

namespace cigi {

namespace {

/// One direction of the link: a queue of datagrams.
struct Channel {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::vector<std::byte>> queue;
};

class MemoryTransport final : public DatagramTransport {
public:
    MemoryTransport(std::shared_ptr<Channel> out, std::shared_ptr<Channel> in)
        : out_(std::move(out)), in_(std::move(in)) {}

    Result<void> send(std::span<const std::byte> datagram) override {
        {
            const std::scoped_lock lock{out_->mutex};
            out_->queue.emplace_back(datagram.begin(), datagram.end());
        }
        out_->ready.notify_one();
        return {};
    }

    Result<std::size_t> receive(std::span<std::byte> buffer,
                                std::chrono::milliseconds timeout) override {
        std::unique_lock lock{in_->mutex};
        if (in_->queue.empty() && timeout.count() > 0) {
            in_->ready.wait_for(lock, timeout, [&] { return !in_->queue.empty(); });
        }
        if (in_->queue.empty()) {
            return std::unexpected(Error{.code = ErrorCode::Timeout});
        }
        const std::vector<std::byte> datagram = std::move(in_->queue.front());
        in_->queue.pop_front();
        if (datagram.size() > buffer.size()) {
            return std::unexpected(Error{.code = ErrorCode::BufferTooSmall});
        }
        std::ranges::copy(datagram, buffer.begin());
        return datagram.size();
    }

private:
    std::shared_ptr<Channel> out_;
    std::shared_ptr<Channel> in_;
};

} // namespace

std::pair<std::unique_ptr<DatagramTransport>, std::unique_ptr<DatagramTransport>>
make_memory_transport_pair() {
    const auto a_to_b = std::make_shared<Channel>();
    const auto b_to_a = std::make_shared<Channel>();
    return {std::make_unique<MemoryTransport>(a_to_b, b_to_a),
            std::make_unique<MemoryTransport>(b_to_a, a_to_b)};
}

} // namespace cigi
