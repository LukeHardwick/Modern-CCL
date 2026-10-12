#pragma once

// Assembling one CIGI message (datagram) into caller-owned storage (ICD 2.6). No allocation.

#include "cigi/detail/assert.hpp"
#include "cigi/packet.hpp"
#include "cigi/user_packet.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace cigi {

/// Appends packets to a message. The first packet must be IgControl (HostToIg) or StartOfFrame
/// (IgToHost), and that packet may appear only once. A failed add() leaves the builder unchanged.
class MessageBuilder {
public:
    /// `dir` must be HostToIg or IgToHost. The builder writes into `storage`, which the caller
    /// keeps alive for as long as the builder (or its bytes()) is used.
    constexpr MessageBuilder(Direction dir, std::span<std::byte> storage) noexcept
        : direction_(dir), storage_(storage) {
        CIGI_ASSERT(dir != Direction::Both);
    }

    /// Errors: WrongDirection, UnexpectedFirstPacket, DuplicateFramePacket, InvalidPacketSize (a
    /// user-defined packet whose size violates ICD 4.3), BufferTooSmall (each with the packet's
    /// opcode and the offset at which it would have been written).
    template <Packet P> [[nodiscard]] constexpr Result<void> add(const P& p) noexcept {
        const auto fail = [&](ErrorCode code) {
            return std::unexpected(Error{.code = code, .offset = size_, .opcode = P::opcode});
        };
        if (P::direction != Direction::Both && P::direction != direction_) {
            return fail(ErrorCode::WrongDirection);
        }
        const bool is_frame_packet = P::opcode == frame_packet_opcode(direction_);
        if (size_ == 0 && !is_frame_packet) {
            return fail(ErrorCode::UnexpectedFirstPacket);
        }
        if (size_ != 0 && is_frame_packet) {
            return fail(ErrorCode::DuplicateFramePacket);
        }
        const std::size_t n = encoded_size(p);
        // ICD 4.3: a user-defined packet is 4 + 4n bytes and a multiple of 8.
        if constexpr (is_user_opcode(P::opcode)) {
            if (!is_valid_user_size(n)) {
                return fail(ErrorCode::InvalidPacketSize);
            }
        }
        if (n > storage_.size() - size_) {
            return fail(ErrorCode::BufferTooSmall);
        }
        encode(p, storage_.subspan(size_, n));
        size_ += n;
        return {};
    }

    /// The message built so far.
    [[nodiscard]] constexpr std::span<const std::byte> bytes() const noexcept {
        return storage_.first(size_);
    }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return storage_.size(); }
    [[nodiscard]] constexpr Direction direction() const noexcept { return direction_; }

    /// Start a new message in the same storage.
    constexpr void clear() noexcept { size_ = 0; }

private:
    Direction direction_;
    std::span<std::byte> storage_;
    std::size_t size_ = 0;
};

/// MessageBuilder that owns its N bytes of storage. Not copyable or movable (the inner span
/// refers to the member array).
template <std::size_t N> class StaticMessageBuilder {
public:
    explicit constexpr StaticMessageBuilder(Direction dir) noexcept : builder_(dir, storage_) {}
    StaticMessageBuilder(const StaticMessageBuilder&) = delete;
    StaticMessageBuilder& operator=(const StaticMessageBuilder&) = delete;
    StaticMessageBuilder(StaticMessageBuilder&&) = delete;
    StaticMessageBuilder& operator=(StaticMessageBuilder&&) = delete;
    ~StaticMessageBuilder() = default;

    template <Packet P> [[nodiscard]] constexpr Result<void> add(const P& p) noexcept {
        return builder_.add(p);
    }
    [[nodiscard]] constexpr std::span<const std::byte> bytes() const noexcept {
        return builder_.bytes();
    }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return builder_.size(); }
    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return N; }
    [[nodiscard]] constexpr Direction direction() const noexcept { return builder_.direction(); }
    constexpr void clear() noexcept { builder_.clear(); }

private:
    std::array<std::byte, N> storage_{};
    MessageBuilder builder_;
};

} // namespace cigi
