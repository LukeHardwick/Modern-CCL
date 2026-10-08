# Task Briefs

Each task is planned by Opus and implemented by one Sonnet sub-agent in its own git worktree.
Opus reviews the diff, runs the checks (macOS + Linux GCC), and merges into `main`.
These briefs are the **source of truth** for what each task was asked to do; the plan and its rationale
live in [../research/02-decisions-frameworks-plan.md](../research/02-decisions-frameworks-plan.md).

Strategy (decided 2026-10-08): **reduced packet set first.** Phase A builds a minimal but complete
Host/IG stack (MVP) and verifies it against Wireshark and the original CCL. Phase B adds the
remaining CIGI 3.3 packets, benchmarks and documentation.

## Index

| ID | Task | Phase | Depends on | Status |
|---|---|---|---|---|
| [T00](T00-scaffold.md) | Project scaffold & tooling | Foundation | — | ✅ `051531a` |
| [T01](T01-wire-primitives.md) | Wire primitives | Foundation | T00 | ✅ `41158fb` |
| [T02](T02-vertical-slice.md) | IG Control, Start of Frame, builder, view | Foundation | T01 | ✅ `36e8e1f` |
| [T02b](T02b-field-table.md) | Declarative field-table codec | Foundation | T02 | ✅ `e819387` |
| [T02c](T02c-wireshark-frame-example.md) | Example: IG Control/SOF traffic in Wireshark | Foundation | T02b | 🔄 |
| [T03](T03-mvp-entity-packets.md) | Entity, Articulated Part, Rate, Component Control | A (MVP) | T02b | ⏳ |
| [T04](T04-mvp-view-celestial-packets.md) | View Control, View Definition, Celestial Sphere | A (MVP) | T02b | ⏳ |
| [T05](T05-mvp-hat-hot-packets.md) | HAT/HOT Request, Response, Extended Response | A (MVP) | T02b | ⏳ |
| [T06](T06-dispatch.md) | Packet registry & dispatch | A (MVP) | T03–T05 | ⏳ |
| [T07](T07-sessions.md) | Host / IG sessions | A (MVP) | T06 | ⏳ |
| [T08](T08-udp-transport.md) | UDP transport (Boost.Asio) + `platform::sleep_until` | A (MVP) | T02b | ⏳ |
| [T09](T09-demo-apps.md) | `demo_host` + mock `demo_ig` | A (MVP) | T07, T08, T10 | ⏳ |
| [T10](T10-wireshark-verification.md) | Wireshark/tshark verification | A (MVP) | T06 | ⏳ |
| [T11](T11-ccl-interop.md) | CCL interoperability & differential tests | A (MVP) | T09 | ⏳ |
| [T12](T12-fuzzing.md) | libFuzzer + hardening | A (MVP) | T06 | ⏳ |
| **M1** | **MVP milestone: user trial + Windows/MSVC build** | | T03–T12 | |
| [T13](T13-host-packets-environment.md) | Remaining entity/environment packets (3,5,7,10–15) | B | M1 | ⏳ |
| [T14](T14-host-packets-sensors-requests.md) | Sensors, motion, ERM, trajectory, collision, requests (17–20, 22, 23, 25–28) | B | M1 | ⏳ |
| [T15](T15-ig-responses.md) | Remaining IG→Host packets (104–117) | B | M1 | ⏳ |
| [T16](T16-symbology.md) | Symbology (29–35), variable-length packets | B | M1 | ⏳ |
| [T17](T17-user-defined-packets.md) | User-defined packets (201–255) | B | T06 | ⏳ |
| [T18](T18-benchmarks.md) | Benchmarks vs CCL | B | T11 | ⏳ |
| [T19](T19-docs.md) | Doxygen site, README, coming-from-CCL guide | B | all | ⏳ |
| **M2** | **Full CIGI 3.3 milestone** | | T13–T19 | |

Parallelism: T03, T04, T05 and T08 are independent and can run at the same time. T10 and T12 can
run beside T07–T09. In Phase B, T13–T17 are independent.

## Rules that apply to every task (sub-agents must follow these)

1. **Worktree & branch.** Work in the worktree you are given; first run
   `git checkout -b task/tNN-<slug>` (slug from the brief's file name).
2. **Clean-room.** Library code (`include/`, `src/`) is written from the CIGI ICD only. Never open,
   download or copy source from the original CCL, libCIGI or Wireshark's `packet-cigi.c`.
   *Exception:* T11 and T18 may build and drive the CCL as a **black-box test peer** in `interop/`
   and `bench/`; even there, no CCL code is copied into `include/` or `src/`.
3. **Spec.** The ICDs are git-ignored in the main checkout:
   `/Users/luke/Documents/Projects/Assignments/Final_Year_Project/Modern-CCL/third_party/spec/`
   (`CIGI_ICD_3_3.txt/.pdf` primary; `CIGI_3_3_Appendix_C.*` errata; 3.0–3.2 ICDs for legacy
   peers). Cite section numbers in comments; never paste ICD prose into the repo. When the base ICD
   and the errata disagree, the **errata win**. When the CCL or Wireshark disagree with the ICD, the
   **ICD wins** — record the discrepancy in `docs/interop/discrepancies.md`.
4. **Library constraints.** C++23; no exceptions (`cigi` builds with `-fno-exceptions`); no heap
   allocation in encode/decode/builder/view; core packet/wire layer has no dependencies; constexpr
   where feasible; no C bit-fields, `#pragma pack`, `reinterpret_cast` or union punning. spdlog is
   allowed from T07 (sessions) upward. Flag any platform-specific code in the report and in a
   comment (`// PLATFORM:`).
5. **Packets** follow [../design/03-packet-and-message-api.md](../design/03-packet-and-message-api.md):
   a value struct + `fields` table + `static_assert(wire::layout_ok<P>())`; custom codecs only for
   variable-length or version-dependent packets; tolerant decode; `EnumTraits` for every enum;
   `.range()` where the ICD gives a range.
6. **Tests** use GoogleTest via `cigi_add_test(...)`. Every packet needs: hand-derived golden bytes
   (never generated from the field table) for both byte orders; reserved bytes/bits zero after
   encode into a 0xFF-filled buffer; `validate()` positive/negative; a constexpr round-trip
   `static_assert`; and a seeded random round-trip.
7. **Checks.** Run `scripts/check.sh --coverage` (format, clang-debug, clang-asan, clang-tidy with zero
   warnings, coverage). Redirect long output to a log file and read the tail. Line coverage ≥ 95% on
   the code you add. Do **not** run `scripts/linux-check.sh` (the reviewer does).
8. **Commits.** Logical commits on your branch, each message ending with
   `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Don't merge, don't push.
9. **Report** (≤ 60 lines): branch, commits, public API added, test count/results, coverage,
   deviations with reasons, ICD ambiguities, and decisions you think the user should make.
