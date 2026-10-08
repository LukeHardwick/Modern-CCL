# T15 — Remaining IG→Host packets (opcodes 104–117)

Phase B · Depends on M1 · Parallel with T13, T14, T16, T17 · Branch `task/t15-ig-responses`
Follow the shared rules in [README.md](README.md).

## Packets (IG→Host)
| Op | Packet | Size | Section (≈ line) | Notes |
|---|---|---|---|---|
| 104 | Line of Sight Response | 16 | §4.2.4 (12178) | Host Frame Number LSN (3.2+). |
| 105 | Line of Sight Extended Response | 56 | §4.2.5 (12322) | 3.2 **removed** the Intersection Point Coordinate System parameter — check how a 3.0/3.1 peer's bit decodes and document. |
| 106 | Sensor Response | 24 | §4.2.6 (12671) | "Mandatory: see packet description". Host Frame Number semantics changed in 3.2. |
| 107 | Sensor Extended Response | 48 | §4.2.7 (12830) | As above. |
| 108 | Position Response | 48 | §4.2.8 (13004) | |
| 109 | Weather Conditions Response | 32 | §4.2.9 (13263) | |
| 110 | Aerosol Concentration Response | 8 | §4.2.10 (13416) | |
| 111 | Maritime Surface Conditions Response | 16 | §4.2.11 (13522) | |
| 112 | Terrestrial Surface Conditions Response | 8 | §4.2.12 (13627) | |
| 113 | Collision Detection Segment Notification | 16 | §4.2.13 (13723) | Errata: grammar only. |
| 114 | Collision Detection Volume Notification | 16 | §4.2.14 (13825) | |
| 115 | Animation Stop Notification | 8 | §4.2.15 (13926) | |
| 116 | Event Notification | 16 | §4.2.16 (13977) | Three 32-bit event data words — reuse the word helpers from component data. |
| 117 | Image Generator Message | 4 + text, ≤ 104 | §4.2.17 (14069) | **Variable length**, UTF-8, max string incl. NUL 100 bytes. |

## Scope
1. Fixed-size packets as in T13 (layout comment, table-driven, enums, ranges).
2. **Strings.** Add a reusable UTF-8 string facility for variable-length packets (shared with T16):
   either a field descriptor (e.g. `wire::utf8(&P::text, offset, max_bytes)`) or helper functions
   used by custom codecs — choose and document. Requirements: storage without heap allocation
   (fixed-capacity string type, e.g. `cigi::FixedString<N>` with `std::string_view` access);
   encode truncates with `wire::utf8_truncate` (never splitting a code point), writes a NUL
   terminator and zero padding to an 8-byte boundary; decode accepts missing NUL / any bytes
   (tolerance) and never reads past the packet; `validate()` reports invalid UTF-8 and over-length.
3. Image Generator Message: custom `encoded_size/encode/decode` built on (2); packet size =
   4 + message bytes rounded up to a multiple of 8, ≤ 104.
4. Register in `IgToHostPackets`; extend the Wireshark and CCL suites; tests per README rule 6
   (for 117 add: empty message, exactly-max, over-max truncation at a multi-byte code point,
   malformed UTF-8 tolerance, missing NUL).

## Acceptance
README rules 6–9, plus Wireshark and CCL suites green or discrepancies documented.
