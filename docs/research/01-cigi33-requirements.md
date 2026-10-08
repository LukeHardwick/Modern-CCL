# CIGI 3.3 / CCL Modernisation — Requirements Research

Date: 2026-10-08
Primary sources:
- CIGI ICD v3.3, Boeing doc TST08I016, 3 Nov 2008 — <https://sourceforge.net/projects/cigi/files/CIGI%20ICD/Version%203.3/>
- CIGI 3.3 Appendix C Errata, 4 May 2009 (same folder)
- CCL 3.3.x Users' Manual and Doxygen reference — <https://cigi.sourceforge.io/manual/ccl/>, <https://cigi.sourceforge.io/docs/ccl/html/>
- CCL product page — <https://cigi.sourceforge.io/product_ccl.php>

The ICD and errata PDFs are not committed: the ICD asks that it not be reproduced for the purpose of
modifying the interface. Download them locally into `third_party/spec/` (git-ignored).

## 1. What CIGI 3.3 requires of an implementation

### 1.1 Wire format (ICD §2.5–2.7, §2.11)
| Rule | Source |
|---|---|
| A message is a sequence of packets. Each packet starts with `opcode:u8`, `size:u8`. | §2.6 |
| Host→IG messages must start with **IG Control** (op 1, 24 B). IG→Host messages must start with **Start of Frame** (op 101, 24 B). Exactly one per message. | §2.6, §4.2.1 |
| Every packet size is a multiple of 8; packets start/end on 64-bit boundaries. Since `size` is a u8, the largest legal packet is **248 bytes**. | §2.6, §4.3 |
| 16/32/64-bit fields are naturally aligned within the packet. | §2.6 |
| Integers: two's complement; reals: IEEE-754 single/double. `word` fields are swapped as u32. | §2.7.1 |
| Bit fields never cross byte boundaries; numbered LSB = bit 0; reserved bits must be sent as 0. | §2.7.3, §2.11 |
| **Receiver-makes-right byte order.** Sender writes native order and sets Byte Swap Magic Number = `0x8000` natively. Receiver swaps the *whole message* if it reads `0x0080`. | §2.5 |
| Strings are UTF-8; truncation must never split a code point; receivers must tolerate any UTF-8. | §2.7.2 |
| Unknown opcodes must be skipped gracefully. A packet larger than expected must be accepted and trailing bytes ignored (forward compatibility). | §2.10, §2.11 |
| Opcodes 201–255 are user-defined; size = 4 + 4n, multiple of 8. | §4.3 |

### 1.2 Packet catalogue (58 standard packets + user-defined)
Host→IG (1–35): IG Control 24, Entity Control 48, Conformal Clamped Entity 24, Component Control 32,
Short Component Control 16, Articulated Part 32, Short Articulated Part 16, Rate Control 32,
Celestial Sphere 16, Atmosphere 32, Environmental Region 48, Weather Control 56, Maritime Surface
Conditions 24, Wave Control 32, Terrestrial Surface Conditions 8, View Control 32, Sensor Control 24,
Motion Tracker 8, Earth Reference Model Def 24, Trajectory Def 24, View Def 32, Collision Segment Def 40,
Collision Volume Def 48, HAT/HOT Request 32, LOS Segment Request 64, LOS Vector Request 56,
Position Request 8, Environmental Conditions Request 32, Symbol Surface Def 56,
Symbol Text Def 12+text (≤248), Symbol Circle Def 16+24n (≤232), Symbol Line Def 16+8n (≤248),
Symbol Clone 8, Symbol Control 40, Short Symbol Control 32.

IG→Host (101–117): Start of Frame 24, HAT/HOT Response 16, HAT/HOT Extended 40, LOS Response 16,
LOS Extended 56, Sensor Response 24, Sensor Extended 48, Position Response 48, Weather Conditions 32,
Aerosol Concentration 8, Maritime Surface Conditions 16, Terrestrial Surface Conditions 8,
Collision Segment Notification 16, Collision Volume Notification 16, Animation Stop 8,
Event Notification 16, IG Message 4+text (≤104).

Variable-length packets: Symbol Text, Symbol Circle, Symbol Line, IG Message, user-defined.

### 1.3 Protocol behaviour
- **Sessions** (§2.1): independent state per Host↔IG pair; frame numbers are per session.
- **Sync vs async** (§2.2): in sync mode the IG's SOF is the heartbeat and the Host replies once per SOF.
- **Frame numbering** (§2.3, changed in 3.2): Host increments Host Frame Number in IG Control and echoes
  the last IG Frame Number; IG does the converse in SOF. Gaps reveal lost messages.
- **Startup/shutdown** (§2.8): IG starts in Reset/Standby and sends SOF; Host sends IG Control with
  IG Mode = Operate; Host sends other packets only once SOF reports Operate. Returning to Standby
  resets all IG state.
- **Ordering** (§2.6): an object (entity, symbol, surface…) must be created before being referenced
  in the same or a later message; nothing else is ordered.
- **Versioning** (§1.3, §2.11): major version in byte 2 (no swap needed to read); minor version in a
  nibble of byte 4. Minor bumps are backward compatible.

### 1.4 3.3 deltas vs 3.2 (Appendix B)
2D symbology (packets 29–35); ANSI → UTF-8 text; extrapolation/interpolation flags in IG Control and
Entity Control; Symbol Surface / Symbol component classes; Celestial Sphere time is UTC.

### 1.5 Errata that change bytes on the wire (Appendix C, 2009)
- Short Component Control: Component Class field width corrected (4.1.5).
- Symbol Text Definition: max string length corrected (4.1.30).
- Symbol Circle Definition: max circles per packet corrected (4.1.31).
- Short Symbol Control: packet size in figure corrected; colour attributes are **RGBA** order (4.1.35).
The implementation must follow the errata, not the base ICD, and tests should cite them.

## 2. What the original CCL provides (feature parity targets)
From the CCL manual §1.1 and class reference:
1. Host and IG sessions (`CigiHostSession`, `CigiIGSession`).
2. Automatic pack/unpack/byte-swap; one class per packet with accessors.
3. Outgoing buffer management (`CigiOutgoingMsg`: `BeginMsg`, `operator<<`, `LockMsg`, `GetMsg`,
   `UnlockMsg`, `PackageMsg`, `FreeMsg`, frame counter update, buffer pool).
4. Incoming processing: event processors, signal processor, callbacks, or iteration.
5. Translation between CIGI 1, 2, 3.0–3.3 (lossy in places).
6. User-defined packet registration (`RegisterUserPacket`).
7. Errors via exceptions or return codes (`CIGI_NO_EXCEPT`), optional bounds checking.
8. `CigiIO` abstract transport hook (no networking included).

Last release 3.3.1 (2009); distros ship 3.3.3a (svn r818). LGPL.

## 3. Why CCL is a poor fit for modern C++ (motivation)
- Deep virtual class hierarchy (`CigiBasePacket` → `CigiBaseEntityCtrl` → `CigiEntityCtrlV3_3`)
  with per-version subclasses; heap-allocated packets; raw owning pointers.
- `Cigi_uint8*` + `int& length` buffer APIs; no `std::span`, no bounds-carrying types.
- Type punning via `memcpy`/pointer casts; manual swap macros instead of `std::byteswap`.
- Plain enums / integer constants instead of `enum class`; no `constexpr`.
- Error handling is a compile-time switch between exceptions and `int` return codes.
- Not thread-aware; shared mutable buffer lists.
- Make/VS7.1 build system; no package config.

## 4. Prior art
- **libCIGI** (SourceForge, MIT, header-only): CIGI 3.0/3.2/3.3 + draft 4.0. Uses C bit-fields, which
  its own page warns is compiler-dependent — a portability flaw a C++23 design avoids.
- **Wireshark `packet-cigi.c`**: dissector updated for 3.3 in 2009; known historical bugs in 3.2/3.3
  IG→Host decoding. Useful as an independent cross-check, not as ground truth.
- **SISO-STD-013-2014 (CIGI 4.0)**: successor, not wire-compatible with 3.x. Out of scope unless chosen.

## 5. Issues and risks surfaced
1. **Licensing.** CCL is LGPL. Porting its code makes the new library a derivative (LGPL). A
   clean-room implementation from the ICD can use any licence. CCL may still be used as a
   *test oracle* without affecting the library's licence.
2. **Spec reproduction.** Don't commit ICD text; cite section numbers instead.
3. **Component Control data words** (§4.1.4, Fig. 44–46): six u32 words swapped individually. A
   double split across two words is corrupted unless packed as the ICD shows. The API needs typed
   helpers for this.
4. **User-defined packets** can't be byte-swapped generically; the user must supply a swap/decode
   routine (the API needs a concept for this).
5. **Version negotiation scope.** Supporting only 3.3 removes CCL's biggest complexity (multi-version
   translation). Peers running 3.0–3.2 differ (frame counter semantics, missing fields, ANSI text).
   This needs a policy decision.
6. **C bit-fields / `#pragma pack` / `reinterpret_cast` overlays** are not portable or not defined
   behaviour. Use explicit offset/mask encoding with `std::bit_cast` + `std::byteswap`; this also
   makes encoding `constexpr` and testable with `static_assert`.
7. **UTF-8 truncation** at code-point boundaries is a stated requirement and easy to get wrong.
8. **Errata vs base ICD** — tests must encode errata behaviour.
9. **Toolchain reality for C++23** (checked locally, Homebrew Clang 23 + libc++, CMake 4.3):
   `std::expected`, `std::byteswap`, `std::print`, `std::flat_map`, `std::mdspan`, deducing `this`,
   `std::ranges::to`, `std::to_underlying` all work. `std::generator` and `std::start_lifetime_as`
   are **not** available in libc++. `import std;` is available through `libc++.modules.json`.
   Apple Clang 21 lags Homebrew LLVM; MSVC and GCC differ again. Keep the core to the common subset.
10. **No reference test vectors** exist in the ICD. Golden bytes must be derived by hand from the
    figures or produced by CCL as an oracle.
