# 03 - Packet and message API (T2 / T2b)

Status: T2 design approved at its checkpoint; T2b added field tables. Scope: `IgControl`,
`StartOfFrame`, `MessageBuilder`, `MessageView`, `wire/fields.hpp`. About 56 more packets follow.

## Decisions recorded at the T2 checkpoint

1. **Field tables.** Each packet declares every field once (member, offset, wire encoding); generic
   code encodes, decodes, validates and layout-checks it. Hand-written codecs stay possible.
2. **Builder: a `Result` per `add()`.** The first error is not latched; a failed `add` leaves the
   builder unchanged, so the caller can react (flush, retry) per packet.
3. **Tolerant decode + `validate()`.** Decode never rejects a value the ICD does not define (undefined
   enums, out-of-range numbers survive); `validate(const P&)` is the opt-in strict check.
4. **Lazy parse + `validate_all()`.** `MessageView::parse` checks only the first packet;
   `validate_all()` walks every header (sizes, bounds) and returns the first error, no bodies.

## Packets are values, not objects

A packet is a plain struct with defaults and a few free functions. No base class, no virtuals, no
heap (the original CCL uses a class hierarchy, which forces allocation and defeats `constexpr`).
`concept Packet<P>` needs static `opcode` and `direction` plus: `encoded_size(p)`, `encode(p, span)`,
`decode(std::type_identity<P>, span, ByteOrder) -> Result<P>` and `validate(p) -> Result<void>`.
User packets (opcodes 201-255, direction `Both`) satisfy it identically, so builder and view treat
built-in and user packets alike. Everything is `constexpr`.

## Codec conventions (every packet follows these)

a. encode writes reserved bytes/bits as 0, masks values to their width, always writes host byte
   order and version 3.3.
b. decode checks, in order: span >= 2 bytes (`Truncated`); opcode matches (`OpcodeMismatch`); size
   byte equals span size and is >= the minimum (`InvalidPacketSize`). Bytes past the known layout
   are ignored (ICD 2.11 rule 4).
c. decode is tolerant (decision 3); d. decode ignores reserved bits.
e. Every error carries the opcode actually *found* in the data (not the one expected). Packet-level
   offsets are 0; `RawPacket::decode_as` rewrites them to the position in the message.

Byte order: senders write native order (ICD 2.5). The receiver detects it once from the magic number
at bytes 6-7 and passes `ByteOrder` into every decode.

## Field tables (`cigi/wire/fields.hpp`)

| Descriptor | Meaning |
|---|---|
| `scalar(&P::m, off)` | whole-byte field; M = WireScalar, enum over one, or `std::chrono::duration` over one |
| `scalar<WireT>(&P::m, off)` | explicit wire type (e.g. a `bool` stored in a full byte) |
| `bits(&P::m, off, lsb, width)` | sub-byte field (ICD 2.7.3); M = enum, unsigned int or bool |
| `flag(&P::m, off, bit)` | one-bit `bool` |
| `.range(lo, hi)` | validate() requires `lo <= v <= hi` (NaN fails) |

Enums are validated through an opt-in `cigi::EnumTraits<E>::is_defined(E)`. Generic operations:
`encode_fields`, `decode_fields` (reads a field only if it lies wholly inside the received bytes,
so 16-byte 3.0/3.1 frame packets leave the newer fields at their defaults) and `validate_fields`
(first failing field in table order: `InvalidFieldValue`, `.offset` = field offset, `.opcode`).
`wire::layout_check<P>()` returns a `LayoutError` (`None`, `BadWireSize`, `OutOfBounds`,
`BadBitRange`, `Misaligned`, `HeaderOverlap`, `Overlap`); `layout_ok<P>()` is the boolean. Rules: inside
`wire_size` (a multiple of 8, at most 255), natural alignment (ICD 2.6), no byte/bit shared by two
fields, nothing in bytes 0-1. Every table-driven header ends with `static_assert(wire::layout_ok<P>());`
and the generic codecs assert it again.

**Selection.** The generic `encoded_size/encode/decode/validate` are templates constrained on
`TableDrivenPacket<P>` (opcode, direction, `wire_size`, `fields`, default-constructible), declared in
namespace `cigi` before the `Packet` concept. Callers inside `cigi` (concept, builder, view) find
them by ordinary lookup for a packet in any namespace; ADL adds a packet's own overloads. A packet
with a custom codec declares **non-template** overloads, which beat the templates in overload
resolution, so no opt-out trait is needed and customisation is per function (IgControl customises
`encode`, `decode`, `validate`). Code outside `cigi` should call `cigi::encode(...)` or use the
builder / view.

## How to add a packet

A fixed-size packet is the struct and nothing else:

```cpp
// Example: a 24-byte Host -> IG packet (illustrative layout, not an ICD packet).
struct Widget {
    static constexpr std::uint8_t opcode = 42;
    static constexpr Direction direction = Direction::HostToIg;
    static constexpr std::size_t wire_size = 24;

    std::uint16_t entity_id = 0;
    WidgetState state = WidgetState::Off;   // enum class : std::uint8_t, + EnumTraits
    bool visible = false;
    float scale = 1.0F;
    double altitude = 0;

    static constexpr auto fields = std::tuple{
        wire::scalar(&Widget::entity_id, 2),
        wire::bits(&Widget::state, 4, 0, 2),
        wire::flag(&Widget::visible, 4, 2),
        wire::scalar(&Widget::scale, 8).range(0.0F, 100.0F),
        wire::scalar(&Widget::altitude, 16),
    };
    friend constexpr bool operator==(const Widget&, const Widget&) noexcept = default;
};
static_assert(wire::layout_ok<Widget>());
```

Steps: pick `opcode`/`direction`/`wire_size` from the ICD packet table; list fields at their
ICD byte offsets (leave reserved ones out); add `EnumTraits` for new enums; end with the
`static_assert`; test against golden bytes derived by hand from the ICD, never from the table.
Add extra rules (version, legacy sizes, cross-field checks) with non-template `decode`/`validate`
that call `wire::decode_fields` / `wire::validate_fields` inside.

## Building and reading messages

```cpp
cigi::StaticMessageBuilder<1024> msg(cigi::Direction::HostToIg);
if (!msg.add(cigi::IgControl{.ig_mode = cigi::IgMode::Operate})) { /* handle */ }   // first
auto view = cigi::MessageView::parse(datagram);   // first packet, byte order, version
if (view && view->validate_all()) {
    for (const auto& item : *view) { /* Result<RawPacket>; item->decode_as<P>() */ }
}
```

Builder: first packet must be the frame packet (`UnexpectedFirstPacket`), only once
(`DuplicateFramePacket`), direction must fit (`WrongDirection`), `BufferTooSmall` otherwise;
failure leaves it untouched. View: iteration walks header sizes; size 0 / not a multiple of 8 is
`InvalidPacketSize`, an overrun or lone trailing byte `Truncated`; each error is yielded once then
iteration ends. Unknown opcodes are yielded normally. The iterator is a true forward iterator.

## Versions and older peers

We send 3.3 and accept 3.0-3.2. 3.0/3.1 frame packets are 16 bytes (bytes 8-11 are a "frame
counter"); 3.2 has no extrapolation flag (ignored below minor 3). A 16-byte packet claiming minor
>= 2 is `InvalidPacketSize`. **Spec defect:** the 3.3 tables say Minor Version "2" (copied from
3.2); we send 3 and accept any minor.

## validate() rules implemented

IG Control: Database Number 0..127; IG Mode must be Reset/Standby, Operate or Debug (the Host cannot
command Offline Maintenance, ICD 4.1.1, 2.8). Start of Frame: IG Mode and Earth Reference Model
defined; every Database Number and IG Status is defined.

Principle: field-local ICD ranges (and defined enums) are always checked, whether or not an enable
flag is set; cross-field and frustum rules apply only to values whose enable flag is set.

Celestial Sphere Control: Hour 0..23, Minute 0..59, Star Field Intensity 0..100; when Date/Time
Valid is set, Date must be a valid MMDDYYYY calendar date. View Control: Roll -180..180, Pitch
-90..90, Yaw 0..360. View Definition: Mirror Mode, Pixel Replication Mode (0..7, 4..7 are
IG-defined) and Projection Type defined; View Type 0..7; for enabled values only: Near > 0, Far > 0
and Near < Far (both enabled), each angle strictly inside (-90, 90), Left < Right and Bottom < Top
(both of a pair enabled).

Entity Control: field ranges (roll -180..180, pitch -90..90, yaw 0..360 per ICD Tables 7 and 12; the
T03 brief said -180..180 for yaw, which was an error), defined enums, and latitude -90..90 /
longitude -180..180 only when Attach State is Detach (attached, these are X/Y offsets). Ground Clamp
is ignored by the IG when attached but still checked for being defined. Articulated Part Control:
roll, pitch, yaw ranges as above. Rate Control: Coordinate System defined; rates have no ICD range.
Component Control: Component Class must be 0..15 (16..63 reserved). Call plain `validate(p)`; an
explicit `validate<EntityControl>` would pick the generic template and skip the lat/lon checks.
