# T02 — Vertical slice: IG Control, Start of Frame, builder, view ✅

Merged: `36e8e1f` (branch `task/t02-vertical-slice`). Ended at the **design checkpoint**.
Spec: ICD 3.3 §2.3, §2.5, §2.6, §2.8, §2.11, §4.1.1, §4.2.1; ICD 3.0/3.2 for legacy layouts.

## Planner-verified wire facts
- IG Control (1, 24 B): b2 major=3, b3 database (i8), b4 [7–4 minor, 3 extrap/interp, 2 ts valid,
  1–0 IG mode], b5 reserved, b6–7 magic 0x8000, 8 host frame u32, 12 timestamp u32 (10 µs),
  16 last IG frame u32, 20–23 reserved.
- Start of Frame (101, 24 B): b2 major, b3 database (i8), b4 IG status u8, b5 [7–4 minor, 3 ERM,
  2 ts valid, 1–0 IG mode], b6–7 magic, 8 IG frame, 12 timestamp, 16 last host frame, 20–23 reserved.
- CIGI 3.0/3.1: both packets 16 B, minor nibble reserved (0), bytes 8–11 "Frame Counter". 3.2: 24 B,
  no extrapolation flag.
- **Spec defect:** 3.3 tables give Minor Version "2" (copied from 3.2, not in errata). We send 3.

## Brief (as issued)
- `packet.hpp`: `Direction`, `ProtocolVersion`, `protocol_version{3,3}`, `Timestamp`
  (`duration<uint32_t, ratio<1,100000>>`), `Packet` concept (ADL `encoded_size`/`encode`/`decode`
  with `type_identity` tag), `PacketHeader`/`read_header`, codec conventions a–e (zero reserved,
  validate header/size, tolerant enums, ignore reserved, errors carry opcode/offset).
- `IgControl`, `StartOfFrame`, shared `IgMode`; legacy 16-byte decode; magic validation.
- `MessageBuilder` (caller storage, first-packet rule, duplicate frame packet, direction check,
  unchanged on `BufferTooSmall`), `StaticMessageBuilder<N>` (non-copyable).
- `MessageView::parse` + forward range of `Result<RawPacket>`; single error then end; unknown
  opcodes yielded; `RawPacket::decode_as<P>()`.
- `docs/design/03-packet-and-message-api.md`, `examples/minimal_roundtrip.cpp`.

## Checkpoint decisions (user, 2026-10-08)
Field-table codec (→ T02b); per-add `Result` in builder; tolerant decode + `validate()`; lazy parse +
`validate_all()`. Planner decisions: wrong-opcode errors report the found opcode (`OpcodeMismatch`);
`IgMode` shared, with Offline Maintenance invalid for IG Control.
