#pragma once

// Packet sets and "decode any": turn a RawPacket into one alternative of a std::variant by opcode,
// through a constexpr 256-entry table (O(1), no allocation). ICD 2.6, 2.10, 2.11 rule 3, 4.3.

#include "cigi/message/view.hpp"
#include "cigi/packet.hpp"
#include "cigi/packets.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <type_traits>
#include <variant>

namespace cigi {

/// A packet whose opcode is not in the set being decoded. ICD 2.11 rule 3 says a receiver ignores
/// such packets, so this is a value, not an error. Non-owning: `bytes` points into the datagram
/// buffer and is valid for as long as that buffer lives. (A session hands it to a handler and only
/// guarantees the buffer for the duration of that call; copy the bytes to keep them longer.)
struct UnknownPacket {
    std::uint8_t opcode = 0;
    std::span<const std::byte> bytes; ///< The whole packet, header included.
    std::size_t offset = 0;           ///< Offset of the packet within the message.
    wire::ByteOrder byte_order = wire::ByteOrder::Native;
};

namespace detail {

/// True if no two packets share an opcode.
template <class... Ps> [[nodiscard]] consteval bool opcodes_unique() noexcept {
    std::array<std::uint8_t, sizeof...(Ps)> ops{Ps::opcode...};
    std::ranges::sort(ops);
    return std::ranges::adjacent_find(ops) == ops.end();
}

/// True if the packets do not mix HostToIg with IgToHost (Both fits with either).
template <class... Ps> [[nodiscard]] consteval bool directions_consistent() noexcept {
    constexpr std::array<Direction, sizeof...(Ps)> dirs{Ps::direction...};
    bool host = false;
    bool ig = false;
    for (const Direction d : dirs) {
        host = host || d == Direction::HostToIg;
        ig = ig || d == Direction::IgToHost;
    }
    return !(host && ig);
}

} // namespace detail

/// A list of packet types that can be decoded together. Compile-time checks: opcodes are unique
/// and the directions are consistent (all HostToIg, all IgToHost, with Both allowed anywhere).
/// Extend one with your own packets via `PacketSet<..., MyPacket>` or `Concat`.
template <Packet... Ps> struct PacketSet {
    static_assert(detail::opcodes_unique<Ps...>(), "PacketSet: two packets share an opcode");
    static_assert(detail::directions_consistent<Ps...>(),
                  "PacketSet: mixes HostToIg and IgToHost packets");

    static constexpr std::size_t size = sizeof...(Ps);

    /// The decoded form: one alternative per packet, plus UnknownPacket last.
    using Variant = std::variant<Ps..., UnknownPacket>;

    template <class P> static constexpr bool contains = (std::is_same_v<P, Ps> || ...);
};

namespace detail {

template <class... Sets> struct ConcatImpl;
template <> struct ConcatImpl<> {
    using Type = PacketSet<>;
};
template <Packet... As> struct ConcatImpl<PacketSet<As...>> {
    using Type = PacketSet<As...>;
};
template <Packet... As, Packet... Bs, class... Rest>
struct ConcatImpl<PacketSet<As...>, PacketSet<Bs...>, Rest...>
    : ConcatImpl<PacketSet<As..., Bs...>, Rest...> {};

} // namespace detail

/// The packets of all the given sets, in order. The result is checked like any PacketSet.
template <class... Sets> using Concat = detail::ConcatImpl<Sets...>::Type;

/// Every Host -> IG packet implemented so far.
using HostToIgPackets =
    PacketSet<IgControl, EntityControl, ComponentControl, ArticulatedPartControl, RateControl,
              CelestialSphereControl, ViewControl, ViewDefinition, HatHotRequest>;

/// Every IG -> Host packet implemented so far.
using IgToHostPackets = PacketSet<StartOfFrame, HatHotResponse, HatHotExtendedResponse>;

/// `Set` followed by more packets (your own, T17): `Extend<HostToIgPackets, MyPacket>`.
template <class Set, Packet... Ps> using Extend = Concat<Set, PacketSet<Ps...>>;

/// std::variant of every packet in `Set`, plus UnknownPacket.
template <class Set> using AnyPacket = Set::Variant;

namespace detail {

template <class Set> using DecodeFn = Result<AnyPacket<Set>> (*)(const RawPacket&, Direction);

template <class Set, class P>
[[nodiscard]] constexpr Result<AnyPacket<Set>> decode_entry(const RawPacket& raw,
                                                            Direction message) {
    // ICD 2.3: a packet that cannot travel in this message's direction is not decoded.
    if (message != Direction::Both && P::direction != Direction::Both && P::direction != message) {
        return std::unexpected(
            Error{.code = ErrorCode::WrongDirection, .offset = raw.offset, .opcode = raw.opcode});
    }
    auto decoded = raw.template decode_as<P>();
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    return AnyPacket<Set>{std::in_place_type<P>, *decoded};
}

template <class Set> struct Table;
template <Packet... Ps> struct Table<PacketSet<Ps...>> {
    using Fn = DecodeFn<PacketSet<Ps...>>;
    /// Opcode -> 1 + position in the set, 0 for opcodes outside it. (Function pointers are only
    /// called, never compared: GCC's sanitizers make such comparisons non-constant.)
    static constexpr std::array<std::uint16_t, 256> index = [] {
        std::array<std::uint16_t, 256> t{};
        std::uint16_t next = 1;
        ((std::get<Ps::opcode>(t) = next++), ...);
        return t;
    }();
    /// Decoders in set order.
    static constexpr std::array<Fn, sizeof...(Ps)> decoders{
        &decode_entry<PacketSet<Ps...>, Ps>...,
    };
};

} // namespace detail

/// Decode `raw` as whichever packet of `Set` its opcode names; an opcode outside the set gives
/// an UnknownPacket. Errors are those of the packet's own decode (offsets relative to the
/// message), or WrongDirection if `message` is HostToIg/IgToHost and the packet's direction
/// contradicts it (pass Direction::Both to skip the check). That happens only when the set and
/// the message disagree (e.g. an IgToHostPackets set on a Host message). In a matching set an
/// opposite-direction packet's opcode is simply not in the set, so it arrives as UnknownPacket.
template <class Set>
[[nodiscard]] constexpr Result<AnyPacket<Set>> decode_any(const RawPacket& raw, Direction message) {
    const std::size_t slot = detail::Table<Set>::index.at(raw.opcode);
    if (slot == 0) {
        const UnknownPacket unknown{
            .opcode = raw.opcode,
            .bytes = raw.bytes,
            .offset = raw.offset,
            .byte_order = raw.byte_order,
        };
        return AnyPacket<Set>{std::in_place_type<UnknownPacket>, unknown};
    }
    return detail::Table<Set>::decoders.at(slot - 1)(raw, message);
}

/// decode_any without the direction check.
template <class Set>
[[nodiscard]] constexpr Result<AnyPacket<Set>> decode_any(const RawPacket& raw) {
    return decode_any<Set>(raw, Direction::Both);
}

} // namespace cigi
