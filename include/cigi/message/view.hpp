#pragma once

// Reading one received CIGI message (datagram) without copying or allocating (ICD 2.6).

#include "cigi/packet.hpp"
#include "cigi/packets/ig_control.hpp"
#include "cigi/packets/start_of_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
#include <type_traits>

namespace cigi {

/// One packet located inside a message: header-validated, not yet decoded.
struct RawPacket {
    std::uint8_t opcode = 0;
    std::span<const std::byte> bytes; ///< The whole packet, header included.
    std::size_t offset = 0;           ///< Offset of the packet within the message.
    wire::ByteOrder byte_order = wire::ByteOrder::Native;

    /// Decode as `P`. InvalidFieldValue if the opcode is not P::opcode; otherwise P's decode
    /// errors, with offset rewritten to the position in the message.
    template <Packet P> [[nodiscard]] constexpr Result<P> decode_as() const noexcept {
        if (opcode != P::opcode) {
            return std::unexpected(
                Error{.code = ErrorCode::InvalidFieldValue, .offset = offset, .opcode = opcode});
        }
        auto r = decode(std::type_identity<P>{}, bytes, byte_order);
        if (!r) {
            r.error().offset += offset;
        }
        return r;
    }
};

/// A validated message: its first packet is a well-formed IG Control / Start of Frame, and the
/// byte order and CIGI version are known. Packets are visited lazily; a malformed packet is
/// yielded once as an error and iteration then ends. Unknown opcodes are yielded like any other.
/// The view does not own the bytes.
class MessageView {
public:
    /// Iterator over the packets; value_type is Result<RawPacket>. Its whole state is the offset
    /// of the current packet, so it is a plain multipass forward iterator.
    // NOLINTBEGIN(readability-identifier-naming): names required by the iterator protocol.
    class iterator {
    public:
        using value_type = Result<RawPacket>;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::forward_iterator_tag;

        constexpr iterator() noexcept = default;

        [[nodiscard]] constexpr value_type operator*() const noexcept {
            return packet_at(bytes_, pos_, order_);
        }
        constexpr iterator& operator++() noexcept {
            const auto current = packet_at(bytes_, pos_, order_);
            pos_ = current ? pos_ + current->bytes.size() : bytes_.size();
            return *this;
        }
        constexpr iterator operator++(int) noexcept {
            auto previous = *this;
            ++*this;
            return previous;
        }
        friend constexpr bool operator==(const iterator& a, const iterator& b) noexcept {
            return a.pos_ == b.pos_;
        }

    private:
        friend class MessageView;
        constexpr iterator(std::span<const std::byte> bytes, std::size_t pos,
                           wire::ByteOrder order) noexcept
            : bytes_(bytes), pos_(pos), order_(order) {}

        std::span<const std::byte> bytes_;
        std::size_t pos_ = 0;
        wire::ByteOrder order_ = wire::ByteOrder::Native;
    };
    using sentinel = iterator; ///< The range is common: end() is an iterator.
    // NOLINTEND(readability-identifier-naming)

    /// Validate the start of a datagram. Errors: Truncated (< 16 bytes or first packet overruns),
    /// UnexpectedFirstPacket (byte 0 is neither 1 nor 101), UnsupportedVersion,
    /// InvalidByteSwapMagic, and anything decoding the first packet reports.
    [[nodiscard]] static constexpr Result<MessageView>
    parse(std::span<const std::byte> datagram) noexcept {
        constexpr std::size_t min_size = detail::frame_legacy_size;
        if (datagram.size() < min_size) {
            return std::unexpected(Error{.code = ErrorCode::Truncated});
        }
        const auto first = wire::load<std::uint8_t>(datagram, 0, wire::ByteOrder::Native);
        if (first != ig_control_opcode && first != start_of_frame_opcode) {
            return std::unexpected(
                Error{.code = ErrorCode::UnexpectedFirstPacket, .opcode = first});
        }
        const auto direction =
            first == ig_control_opcode ? Direction::HostToIg : Direction::IgToHost;
        if (wire::load<std::uint8_t>(datagram, 2, wire::ByteOrder::Native) !=
            protocol_version.major) {
            return std::unexpected(Error{.code = ErrorCode::UnsupportedVersion, .opcode = first});
        }
        const auto order = wire::detect_byte_order(wire::load<std::uint16_t>(
            datagram, detail::frame_magic_offset, wire::ByteOrder::Native));
        if (!order) {
            return std::unexpected(Error{.code = order.error().code, .opcode = first});
        }
        const auto packet = packet_at(datagram, 0, *order);
        if (!packet) {
            return std::unexpected(packet.error());
        }
        const auto frame = direction == Direction::HostToIg
                               ? packet->decode_as<IgControl>().transform(
                                     [](const IgControl& p) { return p.version; })
                               : packet->decode_as<StartOfFrame>().transform(
                                     [](const StartOfFrame& p) { return p.version; });
        if (!frame) {
            return std::unexpected(frame.error());
        }
        return MessageView(datagram, direction, *frame, *order);
    }

    [[nodiscard]] constexpr Direction direction() const noexcept { return direction_; }
    [[nodiscard]] constexpr ProtocolVersion version() const noexcept { return version_; }
    [[nodiscard]] constexpr wire::ByteOrder byte_order() const noexcept { return order_; }
    [[nodiscard]] constexpr std::span<const std::byte> bytes() const noexcept { return bytes_; }

    [[nodiscard]] constexpr iterator begin() const noexcept { return {bytes_, 0, order_}; }
    [[nodiscard]] constexpr iterator end() const noexcept {
        return {bytes_, bytes_.size(), order_};
    }

private:
    constexpr MessageView(std::span<const std::byte> bytes, Direction direction,
                          ProtocolVersion version, wire::ByteOrder order) noexcept
        : bytes_(bytes), direction_(direction), version_(version), order_(order) {}

    /// The packet starting at `pos` (< bytes.size() for a dereferenceable iterator), or the
    /// structural error that stops iteration: Truncated or InvalidPacketSize (ICD 2.6).
    [[nodiscard]] static constexpr Result<RawPacket>
    packet_at(std::span<const std::byte> bytes, std::size_t pos, wire::ByteOrder order) noexcept {
        const auto header = read_header(bytes, pos);
        if (!header) {
            return std::unexpected(header.error());
        }
        if (header->size == 0 || header->size % 8 != 0) {
            return std::unexpected(Error{
                .code = ErrorCode::InvalidPacketSize,
                .offset = pos,
                .opcode = header->opcode,
            });
        }
        if (header->size > bytes.size() - pos) {
            return std::unexpected(
                Error{.code = ErrorCode::Truncated, .offset = pos, .opcode = header->opcode});
        }
        return RawPacket{
            .opcode = header->opcode,
            .bytes = bytes.subspan(pos, header->size),
            .offset = pos,
            .byte_order = order,
        };
    }

    std::span<const std::byte> bytes_;
    Direction direction_;
    ProtocolVersion version_;
    wire::ByteOrder order_;
};

static_assert(std::forward_iterator<MessageView::iterator>);
static_assert(std::ranges::forward_range<MessageView>);

} // namespace cigi
