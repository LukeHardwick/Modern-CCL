# T01 — Wire primitives ✅

Merged: `41158fb` (branch `task/t01-wire-primitives`, commits `5b83c7a`, `840b108`).
Spec: ICD 3.3 §2.5 Byte Order, §2.6 Message Structure, §2.7 Data Types, §2.11.

## Brief (as issued)
- `cigi/error.hpp`: `ErrorCode` (append-only), `Error{code, offset, opcode}`, `to_string`,
  `Result<T> = std::expected<T, Error>`.
- `cigi/wire/byte_order.hpp`: `ByteOrder{Native, Swapped}` (receiver-makes-right), `byte_swap_magic`,
  `detect_byte_order()` (0x8000 → Native, 0x0080 → Swapped, else `InvalidByteSwapMagic`);
  IEEE-754 and endianness `static_assert`s.
- `cigi/wire/codec.hpp`: `WireScalar` concept; constexpr `load<T>`/`store<T>` (unaligned, optional
  swap, floats swapped as raw bytes), debug-asserted bounds, `fits()`.
- `cigi/wire/bits.hpp`: `get_bits`/`set_bits`, `BitField<Lsb, Width>` (cannot cross a byte), flags.
- `cigi/wire/utf8.hpp`: `utf8_truncate` (never splits a code point) and strict `is_valid_utf8`.
- `cigi/wire/padding.hpp`: `pad_to_8`, `max_packet_size = 248`, `is_valid_packet_size`.
- Exhaustive tests incl. golden bytes, NaN bit patterns, unaligned offsets, `static_assert`s.

## Review outcome
- Fixed `utf8_truncate` on malformed input (bounded 3-byte look-back; only back off when a lead
  byte's sequence crosses the cut). The planner's two examples contradicted the rule; the rule won.
- Added `cigi_add_test()` + automatic coverage registry (`CIGI_TEST_TARGETS`).
- `.claude/worktrees/` ignored. 38 tests; 100% line coverage.
