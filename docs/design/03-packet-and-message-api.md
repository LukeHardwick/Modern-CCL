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

## Handling incoming packets

`cigi/packet_set.hpp` and `cigi/dispatch.hpp` turn a `MessageView` into typed packets (ICD 2.6,
2.10, 2.11 rule 3).

- **Sets.** `PacketSet<Ps...>` is a type list; `static_assert`s require unique opcodes and
  consistent directions (HostToIg and IgToHost may not mix; Both fits either). `HostToIgPackets`
  and `IgToHostPackets` hold every packet implemented so far. Extend with your own packet by
  `cigi::Extend<HostToIgPackets, MyPacket>` (or write the full list).
- **`decode_any<Set>(raw, message_direction)`** -> `Result<AnyPacket<Set>>`, a `std::variant` of the
  set's packets plus `UnknownPacket {opcode, bytes, offset, byte_order}` (non-owning, valid while
  the datagram lives). An opcode table built at compile time makes it O(1) with no allocation.
  A packet whose direction contradicts the message gives `WrongDirection`. The one-argument form
  skips that check. `WrongDirection` therefore means the *set* and the message disagree (an
  `IgToHostPackets` set on a Host message); in a matching set an opposite-direction packet's opcode
  is not in the set and arrives as `UnknownPacket`. A second IG Control / Start of Frame in a
  message is reported by `dispatch` (and `MessageView::validate_all`) as `DuplicateFramePacket`
  and not delivered. `Extend<Set, MyPacket...>` adds packets to a set. `UnknownPacket::bytes` is
  valid while the datagram buffer lives; sessions only guarantee the buffer during the handler
  call. `decode_any` is tolerant like `decode`; it does not call `validate`.
- **`dispatch<Set>(view, visitor [, on_error])`** visits packets in order. Use
  `cigi::overloaded{[](const IgControl&){...}, [](const UnknownPacket&){...}}`; packets the visitor
  cannot accept are skipped. Unknown opcodes are never errors; they reach the visitor only if it
  accepts `UnknownPacket`. A header error (bad size, truncation) stops the walk, because the next
  packet cannot be found; a body error (including `WrongDirection`) goes to `on_error` and the walk
  continues. `DispatchStats` counts `handled` (known packet, visitor accepts), `ignored` (known,
  no handler), `unknown` and `errors`. Nothing allocates.
- **`Dispatcher<Set>`** is the runtime form: `on<P>(fn)`, `on_unknown(fn)`, `on_error(fn)`,
  `clear<P>()`, `dispatch(view)`. Handlers live in one `std::function` per packet type, so
  registering may allocate; dispatching never does (tested with a counting `operator new`).
  Registering again replaces the handler; do not register from inside a handler. Copies of a
  `Dispatcher` share whatever their handlers capture by reference.

Validation is separate: call plain `cigi::validate(p)` on a decoded packet if you want the ICD
range checks (never `validate<P>(p)`, which would skip packets' cross-field overloads).

## User-defined packets (T17)

ICD 2.10 / 4.3 reserve opcodes 201-255. A user packet is written exactly like a standard one: a
value struct with `opcode`, `direction` (HostToIg, IgToHost or `Both`), `wire_size` and a field
table (byte swapping comes with the table, which is why CCL users needed hand-written swap code),
or a custom codec for variable layouts. Put the identifying fields in the first two 32-bit words
(ICD 4.3; a convention, not checked). Declare custom codec overloads in the packet's own namespace
or in `cigi` (ADL), and call `encode`/`decode` qualified (`cigi::encode`) from other namespaces.

`cigi/user_packet.hpp`: `is_user_opcode(op)`, `is_valid_user_size(n)` (4 + 4n, a multiple of 8,
8..248), and `concept UserDefinedPacket<P>` = `Packet<P>` + opcode 201-255 + a legal `wire_size`
when it has one. (Named `UserDefinedPacket` because the shared test helper already defines a
`cigi::test::UserPacket` struct.) A packet failing it is rejected at compile time by
`ExtendUser`.

- **Register** with `using Set = cigi::ExtendUser<cigi::HostToIgPackets, MyPacket>` (an `Extend`
  whose added packets must be `UserDefinedPacket`; duplicate opcodes still fail the `PacketSet`
  `static_assert`). Use `Dispatcher<Set>` / `dispatch<Set>` / `decode_any<Set>` as usual.
- **Builder:** `MessageBuilder::add` returns `InvalidPacketSize` (offset where it would have been
  written, opcode set, builder unchanged) for a user packet whose `encoded_size` is not a legal
  user size. This is the only check for variable-size packets.
- **Ignoring them (ICD 4.3: devices should be configurable to ignore user packets).**
  `Dispatcher::ignore_user_packets(true)` skips opcodes 201-255 undecoded at run time: counted as
  `ignored`, delivered to neither a handler nor `on_unknown`. `WithoutUserPackets<Set>` removes
  them at compile time (they then arrive as `UnknownPacket`; this is also the form for the free
  `dispatch<Set>`). A user opcode not in the set always reaches `on_unknown` (ICD 2.10).
- Example: `examples/user_packet.cpp`; tests: `tests/packets/user_packet_test.cpp`.

**Sessions.** `SessionConfig::ignore_user_packets` (default false) is forwarded to the session's
`Dispatcher` in both constructors. Sessions are *not* templated on the packet set (the least
intrusive change to T07's API), so their sets hold no user packets: user opcodes arrive as
`UnknownPacket` (`on_unknown`, `stats.unknown_packets`) or, with the flag set, are skipped. Typed
user packets in sessions (set-templated sessions) are deferred to the facade / T09a follow-up.

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
