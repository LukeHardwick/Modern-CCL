# 03 - Packet and message API (T2 vertical slice)

Status: for review at the T2 checkpoint. Scope: `IgControl`, `StartOfFrame`, `MessageBuilder`,
`MessageView`. About 56 more packets will copy this pattern, so this is the moment to object.

## Packets are values, not objects

A packet is a plain struct of fields with defaults, plus three free functions found by ADL. There
is no base class, no virtual dispatch, no heap, and no hidden state (the original CCL models each
packet as a class in a hierarchy with a virtual interface, which forces allocation and makes
packets non-constexpr and hard to compare).

```cpp
struct IgControl {
    static constexpr std::uint8_t opcode = 1;
    static constexpr Direction direction = Direction::HostToIg;
    static constexpr std::size_t wire_size = 24;   // fixed-size packets only
    std::int8_t database_number = 0;  /* ...fields... */
    friend constexpr bool operator==(const IgControl&, const IgControl&) = default;
};
constexpr std::size_t encoded_size(const IgControl&) noexcept;
constexpr void encode(const IgControl&, std::span<std::byte>) noexcept;   // size == encoded_size
constexpr Result<IgControl> decode(std::type_identity<IgControl>, std::span<const std::byte>,
                                   wire::ByteOrder) noexcept;
```

`concept Packet<P>` checks exactly this. A user-defined packet (opcodes 201-255, direction `Both`)
satisfies it the same way, so builder and view treat built-in and user packets alike. Variable-size
packets (strings, arrays) return a dynamic `encoded_size`; `wire_size` is then absent. Everything
is `constexpr`, which is why the golden-byte tests are partly `static_assert`s.

## Codec conventions (every packet follows these)

a. encode writes reserved bytes/bits as 0, masks each value to its field width, and always writes
   host byte order and version 3.3.
b. decode checks, in order: span >= 2 bytes (Truncated); opcode matches (InvalidFieldValue); size
   byte equals span size and is >= the packet minimum (InvalidPacketSize). Bytes past the known
   layout are ignored (ICD 2.11 rule 4).
c. decode is tolerant of enum values the ICD does not define: enums are `enum class X :
   std::uint8_t` and the raw value is stored. Range policy belongs to the application.
d. decode ignores reserved bits.
e. Errors carry the opcode. Packet-level errors have offset 0; `RawPacket::decode_as` rewrites the
   offset to the position inside the message.

Byte order: a sender always writes native order (ICD 2.5). The receiver reads the magic number at
bytes 6-7 once per message (`wire::detect_byte_order`) and passes `ByteOrder` into every decode.
`decode` of the frame packets re-checks the magic under the order it was given.

## Building and reading messages

```cpp
cigi::StaticMessageBuilder<1024> msg(cigi::Direction::HostToIg);   // or MessageBuilder over a span
msg.add(cigi::IgControl{.ig_mode = cigi::IgMode::Operate, .host_frame_number = n});  // first
msg.add(entity_control);                                            // later packets
send(msg.bytes());                                                  // add() is [[nodiscard]]

auto view = cigi::MessageView::parse(datagram);          // checks first packet, byte order, version
if (!view) return view.error();
for (const auto& item : *view) {                         // Result<RawPacket>
    if (!item) { log(item.error()); break; }             // yielded once, then iteration ends
    if (item->opcode == cigi::StartOfFrame::opcode) { auto sof = item->decode_as<cigi::StartOfFrame>(); }
}
```

Builder rules: first packet must be the frame packet for its direction (UnexpectedFirstPacket), only
once (DuplicateFramePacket), packet direction must fit (WrongDirection; `Both` always fits), and a
failed `add` leaves the builder untouched (BufferTooSmall). The caller owns storage; no allocation.
`StaticMessageBuilder<N>` owns an array and is deliberately non-copyable/non-movable (its span
would dangle).

View rules: iteration walks header sizes. Size 0 or not a multiple of 8 gives InvalidPacketSize,
overrun or a lone trailing byte gives Truncated; each error is yielded once and iteration ends, so
there is no infinite loop and no out-of-bounds read. Unknown opcodes are yielded normally; the
dispatch layer (T10) decides to ignore them. The iterator's whole state is a byte offset, so it is
a true multipass `std::forward_iterator`, and `MessageView` is a `std::ranges::forward_range`.

## Versions and older peers

We send 3.3 and tolerate 3.0-3.2 on receive only. IG Control / Start of Frame from 3.0 and 3.1
peers are 16 bytes: no last-frame field, and "Frame Counter" in bytes 8-11 maps to the host / IG
frame number. 3.2 has the 24-byte layout; its reserved bit where 3.3 puts the IG Control
Extrapolation flag is ignored. A 16-byte packet claiming minor >= 2 is InvalidPacketSize. The peer's
version is exposed as `IgControl::version`, `StartOfFrame::version` and `MessageView::version()`.

**Spec defect.** The 3.3 ICD tables give Minor Version "Value: 2" for both IG Control and Start of
Frame. This was copied from 3.2 and is not corrected by Appendix C. We send minor 3 (the
document's own version) and accept any minor on receive; a peer that insists on 2 would be reading
the defective table literally.

## Decisions taken without asking (change if you disagree)

- `MessageView::end()` returns an iterator, not a distinct sentinel type (`sentinel` is an alias).
- Decode of a wrong opcode reports the *expected* opcode in `Error::opcode`.
- `IgMode` is one enum shared by both packets; `OfflineMaintenance` (3) is only defined for Start
  of Frame, but IG Control decodes it without complaint (convention c).
- `ErrorCode` gained `WrongDirection` and `DuplicateFramePacket` (appended).
- `.clang-tidy` `HeaderFilterRegex` was anchored on the directory name `Modern-CCL`, so headers
  were silently unchecked inside git worktrees. It is now `.*/include/cigi/.*`; this surfaced
  latent findings in the T1 headers, fixed in this task (no behaviour change).

## Open questions for the checkpoint

1. Is `Result<void> add()` (error per packet) right, or should the builder latch the first error?
2. Should decode reject enum values outside the ICD (strict mode), or stay tolerant (current)?
3. Fixed-size packets repeat ~25 lines each. A `FixedPacket` helper/CRTP could shrink that, at the
   cost of hiding the layout. Current preference: stay explicit, one layout comment per packet.
4. Should `MessageView::parse` verify nothing beyond the first packet (current), or validate the
   whole message eagerly?
5. Timestamp is a 32-bit 10 us `std::chrono::duration` (wraps at ~11.9 h); wrap handling is left to
   the application.
