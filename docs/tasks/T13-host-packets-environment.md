# T13 — Remaining entity & environment packets (opcodes 3, 5, 7, 10–15)

Phase B · Depends on M1 · Parallel with T14–T17 · Branch `task/t13-host-packets-environment`
Follow the shared rules in [README.md](README.md).

## Packets (all Host→IG, fixed size; ICD 3.3 txt line ≈ start of section)
| Op | Packet | Size | Section (≈ line) | Notes |
|---|---|---|---|---|
| 3 | Conformal Clamped Entity Control | 24 | §4.1.3 (3355) | Errata: Table 1 section number only. |
| 5 | Short Component Control | 16 | §4.1.5 (3898) | **Errata 4.1.5: Component Class field width corrected** — use the errata. Reuse `ComponentClass` and the component-data helpers (2 words). |
| 7 | Short Articulated Part Control | 16 | §4.1.7 (4307) | DOF select enums. |
| 10 | Atmosphere Control | 32 | §4.1.10 (4869) | |
| 11 | Environmental Region Control | 48 | §4.1.11 (5023) | Region state/merge enums. |
| 12 | Weather Control | 56 | §4.1.12 (5600) | Entity ID / Region ID union depends on Scope — model as one `std::uint16_t id` + scope enum, document. |
| 13 | Maritime Surface Conditions Control | 24 | §4.1.13 (6013) | Same ID/scope pattern. |
| 14 | Wave Control | 32 | §4.1.14 (6166) | Same ID/scope pattern. |
| 15 | Terrestrial Surface Conditions Control | 8 | §4.1.15 (6400) | Same ID/scope pattern. |

## Scope
1. Derive each layout from the ICD figure **and** parameter table (widths/types); put a byte/bit
   layout comment at the top of each header like the MVP packets. The planner verifies layouts at
   review — list any figure/table conflicts in the report.
2. Table-driven structs, enums with `EnumTraits`, ranges from the ICD; custom `validate()` only for
   cross-field rules the ICD states.
3. Register every packet in `HostToIgPackets` (T06), add rows to the Wireshark mapping table (T10)
   and to the CCL differential suite (T11) — run both suites (`ctest -L wireshark`, and with
   `CIGI_BUILD_CCL_INTEROP=ON` `ctest -L ccl`) and record any new discrepancies.
4. Tests per README rule 6.

## Acceptance
README rules 6–9, plus Wireshark and CCL suites green (or discrepancies documented and adjudicated).
