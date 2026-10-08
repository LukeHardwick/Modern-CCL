# T02b — Declarative field-table codec ✅

Merged: `e819387` (branch `task/t02b-field-table`).

## Brief (as issued)
- `cigi/wire/fields.hpp`: descriptors `scalar(&P::m, off)`, `scalar<WireT>(...)`, `bits(&P::m, off,
  lsb, width)`, `flag(&P::m, off, bit)`, `.range(lo, hi)`; `WireRepr<M>` trait (WireScalar, enums,
  `std::chrono::duration`); `EnumTraits<E>::is_defined` for `validate()`.
- `encode_fields`, `decode_fields` (reads a field only if it lies inside the received packet, so
  short legacy packets keep defaults), `validate_fields`.
- `layout_ok<P>()` compile-time checks: within `wire_size`, natural alignment (§2.6), no overlapping
  bytes/bits, nothing in bytes 0–1, valid `wire_size`.
- Generic constrained `encoded_size/encode/decode/validate` for `TableDrivenPacket`; packets with
  custom logic declare non-template overloads, which win overload resolution.
- `OpcodeMismatch` (found opcode reported); `validate()` for IgControl/StartOfFrame;
  `MessageView::validate_all()`.

## Outcome
134 tests; 100% line coverage on `fields.hpp`, `packet.hpp`, `packets/`, `view.hpp`. A pure table
packet is ~9 lines. Known gaps carried forward: no string/array descriptors (needed by T15/T16);
variable-length packets keep custom codecs.
