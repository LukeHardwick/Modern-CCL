#pragma once

// Declarative field tables: a packet lists each body field once (member, byte offset, wire
// encoding) and the generic functions below encode, decode, validate and layout-check it.
//
//   static constexpr auto fields = std::tuple{
//       wire::scalar(&P::count, 8),                       // whole-byte field
//       wire::bits(&P::mode, 4, 0, 2),                    // sub-byte field (ICD 2.7.3)
//       wire::flag(&P::valid, 4, 2),                      // one bit
//       wire::scalar(&P::level, 12).range(0, 100),        // validated by validate_fields()
//   };
//   static_assert(wire::layout_ok<P>());
//
// The table covers body fields only. Opcode and size (bytes 0-1) are written by the generic
// layer; reserved bytes and bits are simply not listed (encoded as 0 once the caller has zeroed
// the packet, ignored on decode).

#include "cigi/detail/assert.hpp"
#include "cigi/error.hpp"
#include "cigi/wire/bits.hpp"
#include "cigi/wire/byte_order.hpp"
#include "cigi/wire/codec.hpp"

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cigi {

/// Opt-in description of an enum, used by validate_fields(). Specialise it for an enum with
///   static constexpr bool is_defined(E value) noexcept;
/// to make validate() reject values the ICD does not define. Decode never consults it.
template <class E> struct EnumTraits {};

/// `lo <= value <= hi`, a convenient is_defined() body for enums with contiguous values.
template <class E>
    requires std::is_enum_v<E>
[[nodiscard]] constexpr bool enum_between(E value, E lo, E hi) noexcept {
    return lo <= value && value <= hi;
}

} // namespace cigi

namespace cigi::wire {

// --- Wire representation --------------------------------------------------------------------

/// The WireScalar a member type travels as: itself, an enum's underlying type, or a duration's
/// rep. Undefined for other types (use `scalar<WireT>(...)` to name the wire type explicitly).
template <class M> struct WireRepr {};
template <WireScalar M> struct WireRepr<M> {
    using Type = M;
};
template <class E>
    requires(std::is_enum_v<E> && WireScalar<std::underlying_type_t<E>>)
struct WireRepr<E> {
    using Type = std::underlying_type_t<E>;
};
template <WireScalar Rep, class Period> struct WireRepr<std::chrono::duration<Rep, Period>> {
    using Type = Rep;
};

namespace detail {

template <class M> inline constexpr bool is_duration_v = false;
template <class Rep, class Period>
inline constexpr bool is_duration_v<std::chrono::duration<Rep, Period>> = true;

template <class W, class M> struct ResolveWire {
    using Type = W;
};
template <class M>
    requires requires { typename WireRepr<M>::Type; }
struct ResolveWire<void, M> {
    using Type = WireRepr<M>::Type;
};

template <class W, class M> [[nodiscard]] constexpr W to_wire(const M& value) noexcept {
    if constexpr (is_duration_v<M>) {
        return static_cast<W>(value.count());
    } else {
        return static_cast<W>(value);
    }
}

template <class M, class W> [[nodiscard]] constexpr M from_wire(W raw) noexcept {
    if constexpr (is_duration_v<M>) {
        return M{static_cast<M::rep>(raw)};
    } else if constexpr (std::same_as<M, bool>) {
        return raw != W{};
    } else {
        return static_cast<M>(raw);
    }
}

template <class M>
concept HasIsDefined = requires(M v) {
    { EnumTraits<M>::is_defined(v) } -> std::convertible_to<bool>;
};

/// Value-validity metadata shared by both descriptor kinds.
template <class M> struct Validity {
    bool has_range = false;
    // Unused unless has_range, hence the (otherwise suspicious) value-initialisation of enums.
    M lo{}; // NOLINT(bugprone-invalid-enum-default-initialization)
    M hi{}; // NOLINT(bugprone-invalid-enum-default-initialization)

    [[nodiscard]] constexpr bool ok(const M& v) const noexcept {
        const bool in_range = !has_range || (lo <= v && v <= hi);
        if constexpr (HasIsDefined<M>) {
            return in_range && EnumTraits<M>::is_defined(v);
        } else {
            return in_range;
        }
    }
};

} // namespace detail

// --- Descriptors ----------------------------------------------------------------------------

/// A whole-byte field of wire type W at a byte offset. Build with scalar().
template <class P, class M, class W> struct ScalarField {
    M P::* member = nullptr;
    std::size_t offset = 0;
    detail::Validity<M> validity{};

    /// Same field, but validate() requires lo <= value <= hi (also catches NaN for floats).
    [[nodiscard]] constexpr ScalarField range(M lo, M hi) const noexcept {
        auto copy = *this;
        copy.validity = {.has_range = true, .lo = lo, .hi = hi};
        return copy;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return sizeof(W); }
    [[nodiscard]] constexpr std::size_t alignment() const noexcept { return sizeof(W); }
    [[nodiscard]] constexpr bool shape_ok() const noexcept { return true; }
    /// Mask of the bits this field occupies in each of its bytes.
    [[nodiscard]] constexpr std::uint8_t byte_mask() const noexcept { return 0xFF; }

    constexpr void encode(const P& p, std::span<std::byte> out) const noexcept {
        store<W>(out, offset, detail::to_wire<W>(p.*member));
    }
    constexpr void decode(P& p, std::span<const std::byte> in, ByteOrder order) const noexcept {
        if (fits(in, offset, sizeof(W))) {
            p.*member = detail::from_wire<M>(load<W>(in, offset, order));
        }
    }
    [[nodiscard]] constexpr bool valid(const P& p) const noexcept { return validity.ok(p.*member); }
};

/// A sub-byte field: `width` bits at bit `lsb` (0 = least significant) of the byte at `offset`.
/// Build with bits() / flag().
template <class P, class M> struct BitsField {
    M P::* member = nullptr;
    std::size_t offset = 0;
    unsigned lsb = 0;
    unsigned width = 1;
    detail::Validity<M> validity{};

    [[nodiscard]] constexpr BitsField range(M lo, M hi) const noexcept {
        auto copy = *this;
        copy.validity = {.has_range = true, .lo = lo, .hi = hi};
        return copy;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return 1; }
    [[nodiscard]] constexpr std::size_t alignment() const noexcept { return 1; }
    /// A bit field must be non-empty and stay inside its byte (ICD 2.7.3).
    [[nodiscard]] constexpr bool shape_ok() const noexcept {
        return width >= 1 && lsb + width <= 8;
    }
    [[nodiscard]] constexpr std::uint8_t byte_mask() const noexcept {
        return static_cast<std::uint8_t>(detail::mask(width) << lsb);
    }

    constexpr void encode(const P& p, std::span<std::byte> out) const noexcept {
        const auto raw = detail::to_wire<std::uint8_t>(p.*member);
        const auto byte = load<std::uint8_t>(out, offset, ByteOrder::Native);
        store<std::uint8_t>(out, offset, set_bits(byte, lsb, width, raw));
    }
    constexpr void decode(P& p, std::span<const std::byte> in, ByteOrder /*order*/) const noexcept {
        if (fits(in, offset, 1)) {
            p.*member = detail::from_wire<M>(
                get_bits(load<std::uint8_t>(in, offset, ByteOrder::Native), lsb, width));
        }
    }
    [[nodiscard]] constexpr bool valid(const P& p) const noexcept { return validity.ok(p.*member); }
};

/// Whole-byte field. M is a WireScalar, an enum class over one, or a std::chrono::duration over
/// one; or any other arithmetic/enum type with an explicit wire type `scalar<WireT>(...)`
/// (e.g. a bool stored in a full byte).
template <class W = void, class P, class M>
[[nodiscard]] constexpr auto scalar(M P::* member, std::size_t offset) noexcept {
    using Wire = detail::ResolveWire<W, M>::Type;
    static_assert(WireScalar<Wire>, "the wire type of a scalar field must be a WireScalar");
    return ScalarField<P, M, Wire>{.member = member, .offset = offset};
}

/// Sub-byte field. M is an enum class, an unsigned integer, or bool.
template <class P, class M>
    requires(std::is_enum_v<M> || std::unsigned_integral<M> || std::same_as<M, bool>)
[[nodiscard]] constexpr auto bits(M P::* member, std::size_t offset, unsigned lsb,
                                  unsigned width) noexcept {
    return BitsField<P, M>{.member = member, .offset = offset, .lsb = lsb, .width = width};
}

/// One-bit boolean field.
template <class P>
[[nodiscard]] constexpr auto flag(bool P::* member, std::size_t offset, unsigned bit) noexcept {
    return bits(member, offset, bit, 1);
}

// --- Generic operations ---------------------------------------------------------------------

/// Write every table field into `out`. Precondition: `out` is the whole packet (>= the end of
/// the last field) and has been zeroed by the caller (bit fields are OR-ed into their byte).
template <class P> constexpr void encode_fields(const P& p, std::span<std::byte> out) noexcept {
    std::apply([&](const auto&... f) { (f.encode(p, out), ...); }, P::fields);
}

/// Read every table field that lies entirely inside `bytes`; others keep their current value.
template <class P>
constexpr void decode_fields(P& p, std::span<const std::byte> bytes, ByteOrder order) noexcept {
    std::apply([&](const auto&... f) { (f.decode(p, bytes, order), ...); }, P::fields);
}

/// First field (in table order) whose value is out of range / an undefined enum:
/// InvalidFieldValue with .offset = the field's byte offset (within the packet) and .opcode.
template <class P> [[nodiscard]] constexpr Result<void> validate_fields(const P& p) noexcept {
    std::size_t bad_offset = 0;
    const bool all_ok = std::apply(
        [&](const auto&... f) { return (... && (f.valid(p) || ((bad_offset = f.offset), false))); },
        P::fields);
    if (all_ok) {
        return {};
    }
    return std::unexpected(
        Error{.code = ErrorCode::InvalidFieldValue, .offset = bad_offset, .opcode = P::opcode});
}

// --- Compile-time layout verification -------------------------------------------------------

/// Why a field table is not a legal layout (ICD 2.6, 2.7.3). Checks run per field, in table
/// order, and the first failure wins.
enum class LayoutError : std::uint8_t {
    None,
    BadWireSize,   ///< Packet size is 0, not a multiple of 8, or above 255.
    OutOfBounds,   ///< Field extends past the packet size.
    BadBitRange,   ///< Bit field is empty or crosses a byte boundary.
    Misaligned,    ///< 2/4/8-byte field not at a multiple of its size.
    HeaderOverlap, ///< Field overlaps bytes 0-1 (opcode, size).
    Overlap,       ///< Field shares a byte (or bit) with an earlier field.
};

inline constexpr std::size_t max_packet_size = 255; ///< The size byte is 8 bits (ICD 2.6).

namespace detail {

/// The layout-relevant facts of one descriptor, so the checking logic is not a template.
struct FieldShape {
    std::size_t offset = 0;
    std::size_t size = 0;
    std::size_t alignment = 1;
    std::uint8_t mask = 0xFF; ///< Bits occupied in each byte of the field.
    bool shape_ok = true;
};

template <class F> [[nodiscard]] constexpr FieldShape shape_of(const F& f) noexcept {
    return {
        .offset = f.offset,
        .size = f.size(),
        .alignment = f.alignment(),
        .mask = f.shape_ok() ? f.byte_mask() : std::uint8_t{0},
        .shape_ok = f.shape_ok(),
    };
}

/// True if two (individually valid) fields share a byte and a bit within it.
[[nodiscard]] constexpr bool shares_bits(const FieldShape& a, const FieldShape& b) noexcept {
    return a.offset < b.offset + b.size && b.offset < a.offset + a.size && (a.mask & b.mask) != 0;
}

/// Layout problem of `f` on its own or against the earlier fields.
[[nodiscard]] constexpr LayoutError check_field(const FieldShape& f,
                                                std::span<const FieldShape> earlier,
                                                std::size_t wire_size) noexcept {
    if (!f.shape_ok) {
        return LayoutError::BadBitRange;
    }
    if (f.offset > wire_size || f.size > wire_size - f.offset) {
        return LayoutError::OutOfBounds;
    }
    if (f.offset % f.alignment != 0) {
        return LayoutError::Misaligned;
    }
    if (f.offset < 2) {
        return LayoutError::HeaderOverlap;
    }
    for (const auto& other : earlier) {
        if (shares_bits(other, f)) {
            return LayoutError::Overlap;
        }
    }
    return LayoutError::None;
}

/// Non-template core of layout_check: the first failing field, in table order, decides.
[[nodiscard]] constexpr LayoutError check_shapes(std::span<const FieldShape> shapes,
                                                 std::size_t wire_size) noexcept {
    if (wire_size == 0 || wire_size % 8 != 0 || wire_size > max_packet_size) {
        return LayoutError::BadWireSize;
    }
    std::size_t index = 0;
    for (const auto& shape : shapes) {
        const auto error = check_field(shape, shapes.first(index), wire_size);
        if (error != LayoutError::None) {
            return error;
        }
        ++index;
    }
    return LayoutError::None;
}

} // namespace detail

/// Check `table` against a packet of `wire_size` bytes.
template <class... Fs>
[[nodiscard]] constexpr LayoutError layout_check(const std::tuple<Fs...>& table,
                                                 std::size_t wire_size) noexcept {
    const auto shapes = std::apply(
        [](const auto&... f) {
            return std::array<detail::FieldShape, sizeof...(Fs)>{detail::shape_of(f)...};
        },
        table);
    return detail::check_shapes(shapes, wire_size);
}

/// Check P::fields against P::wire_size.
template <class P> [[nodiscard]] constexpr LayoutError layout_check() noexcept {
    return layout_check(P::fields, P::wire_size);
}

/// True if P's table is a legal layout. Every table-driven packet header asserts this.
template <class P> [[nodiscard]] constexpr bool layout_ok() noexcept {
    return layout_check<P>() == LayoutError::None;
}

} // namespace cigi::wire
