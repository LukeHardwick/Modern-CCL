# T16 — Symbology packets (opcodes 29–35)

Phase B · Depends on M1 (and T15's string facility if merged first; otherwise build it here and
coordinate at review) · Branch `task/t16-symbology`
Follow the shared rules in [README.md](README.md).

## Goal
CIGI 3.3's headline feature (Appendix B): 2D symbology. Without it the library cannot claim full
3.3 support.

## Spec
ICD 3.3 §3.3 Symbols (surfaces, primitives, templates), §3.4.4–3.4.5 symbol coordinate systems,
§4.1.29–§4.1.35, **Appendix C errata (several apply — they override the base text):**
- 4.1.30 Symbol Text Definition: corrected Font ID typeface list and **maximum string length**.
- 4.1.31 Symbol Circle Definition: corrected **maximum number of circles** per packet.
- 4.1.33 / 4.1.34: captions only.
- 4.1.35 Short Symbol Control: **packet size in Figure 108 corrected**, and **colour attributes in
  RGBA order** (consistent with Symbol Control).

## Packets (Host→IG)
| Op | Packet | Size | Section (≈ line) |
|---|---|---|---|
| 29 | Symbol Surface Definition | 56 | §4.1.29 (9679) |
| 30 | Symbol Text Definition | 12 + text, ≤ 248 | §4.1.30 (10061) |
| 31 | Symbol Circle Definition | 16 + 24·n, ≤ 232 | §4.1.31 (10278) |
| 32 | Symbol Line Definition | 16 + 8·n, ≤ 248 | §4.1.32 (10629) |
| 33 | Symbol Clone | 8 | §4.1.33 (10915) |
| 34 | Symbol Control | 40 | §4.1.34 (11011) |
| 35 | Short Symbol Control | 32 | §4.1.35 (11453) |

## Scope
1. Fixed-size packets (29, 33, 34, 35) table-driven; Short Symbol Control's two attribute-value
   fields are interpreted by attribute selector (colour RGBA / float / integer) — provide typed
   accessors and test the RGBA order from the errata.
2. Variable-length packets (30, 31, 32): custom codecs. Text uses the shared UTF-8 facility (T15).
   Circles and line vertices: fixed-capacity arrays (`std::inplace_vector` is unavailable in libc++ —
   use a small `FixedVector<T, N>` with N = the errata maximum) and a per-element field table so
   element layouts are declared once; encode rejects/flags too many elements via `validate()`.
3. Symbol-related `ComponentClass` values already exist (T03) — add helpers if the ICD defines
   component IDs/states for symbols.
4. Register in `HostToIgPackets`; extend Wireshark (check whether the dissector supports 3.3
   symbology at all — record the result) and CCL suites; tests per README rule 6, plus boundary tests
   at the errata maxima and UTF-8 truncation.

## Acceptance
README rules 6–9, plus Wireshark and CCL suites green or discrepancies documented.
