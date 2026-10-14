# Task Briefs

Each task is planned by Opus and implemented by one Sonnet sub-agent in its own git worktree.
Opus reviews the diff, runs the checks (macOS + Linux GCC), and merges into `main`.
These briefs are the **source of truth** for what each task was asked to do; the plan and its rationale
live in [../research/02-decisions-frameworks-plan.md](../research/02-decisions-frameworks-plan.md).

Strategy (decided 2026-10-08): **reduced packet set first.** Phase A builds a minimal but complete
Host/IG stack (MVP) and verifies it against Wireshark and the original CCL. Phase B adds the
remaining CIGI 3.3 packets, benchmarks and documentation.

**Current status and session hand-off: [STATUS.md](STATUS.md).**

## Index

| ID | Task | Phase | Depends on | Status |
|---|---|---|---|---|
| [T00](T00-scaffold.md) | Project scaffold & tooling | Foundation | — | ✅ `051531a` |
| [T01](T01-wire-primitives.md) | Wire primitives | Foundation | T00 | ✅ `41158fb` |
| [T02](T02-vertical-slice.md) | IG Control, Start of Frame, builder, view | Foundation | T01 | ✅ `36e8e1f` |
| [T02b](T02b-field-table.md) | Declarative field-table codec | Foundation | T02 | ✅ `e819387` |
| [T02c](T02c-wireshark-frame-example.md) | Example: IG Control/SOF traffic in Wireshark | Foundation | T02b | ✅ `11cbfd7` |
| [T03](T03-mvp-entity-packets.md) | Entity, Articulated Part, Rate, Component Control | A (MVP) | T02b | ✅ `e040dd0` |
| [T04](T04-mvp-view-celestial-packets.md) | View Control, View Definition, Celestial Sphere | A (MVP) | T02b | ✅ `d7f3779` |
| [T05](T05-mvp-hat-hot-packets.md) | HAT/HOT Request, Response, Extended Response | A (MVP) | T02b | ✅ `315d0a7` |
| [T06](T06-dispatch.md) | Packet registry & dispatch | A (MVP) | T03–T05 | ✅ `6dede26` |
| [T07](T07-sessions.md) | Host / IG sessions | A (MVP) | T06 | ✅ `0546d7e` |
| [T08](T08-udp-transport.md) | UDP transport (Boost.Asio) + `platform::sleep_until` | A (MVP) | T02b | ✅ `fd5b167` |
| [T09a](T09a-host-ig-facade.md) | High-level `cigi::Host` / `cigi::Ig` facade | A (MVP) | T07, T08 | ✅ |
| [T09b](T09b-ig-viewer.md) | Mock-IG core + visual `ig_viewer` (raylib + Dear ImGui) | A (MVP) | P1: T06 · P2: T09a | P1 ✅ · P2 ⏳ |
| [T09c](T09c-facade-examples.md) | `on_user<P>` in the facade; `user_packet` + `cigi_frames` examples on the facade | A (after v0.2.0) | T09a, T17 | ✅ `d348d73` |
| [T09d](T09d-facade-peer-hardening.md) | Facade: peer learning hook, sender filtering, `on_ig_control`, `local_port()` | A (follow-up) | T09, T09c | ⏳ |
| [T09e](T09e-facade-followups.md) | T09a follow-ups: Standby false positive, fault-transport docs, example comment, tests | A (follow-up) | T09a | ⏳ |
| [T09](T09-demo-apps.md) | `demo_host` + mock `demo_ig` (+ fault flags) | A (MVP) | T09a, T09b P1, T10 | ✅ `47775ef` |
| [T10](T10-wireshark-verification.md) | Wireshark/tshark verification | A (MVP) | T06 | ✅ `2781a3a` |
| [T11](T11-ccl-interop.md) | CCL interoperability & differential tests | A (MVP) | T09 | ❌ discarded by the user 2026-10-14 |
| [T12](T12-fuzzing.md) | libFuzzer + hardening | A (MVP) | T06 | ✅ |
| **M1** | **MVP milestone: user trial + Windows/MSVC build** | | T03–T12 (incl. T09a, T09b) | |
| [T13](T13-host-packets-environment.md) | Remaining entity/environment packets (3,5,7,10–15) | B | M1 | ⏳ |
| [T14](T14-host-packets-sensors-requests.md) | Sensors, motion, ERM, trajectory, collision, requests (17–20, 22, 23, 25–28) | B | M1 | ⏳ |
| [T15](T15-ig-responses.md) | Remaining IG→Host packets (104–117) | B | M1 | ⏳ |
| [T16](T16-symbology.md) | Symbology (29–35), variable-length packets | B | M1 | ⏳ |
| [T17](T17-user-defined-packets.md) | User-defined packets (201–255) | B | T06 | ✅ `a57c7f4` |
| [T18](T18-benchmarks.md) | Benchmarks vs CCL + real-network performance test | B | T11 (discarded: re-plan before starting) | ⏳ |
| [T19](T19-docs.md) | Doxygen site, README, coming-from-CCL guide | B | all | ⏳ |
| **M2** | **Full CIGI 3.3 milestone** | | T13–T19 | |
| [T20](T20-ig-database.md) | Mock-IG database: TOML manifest, loader, Database Number protocol, `cigi_db` | Tools | T09b P1 | 📝 |
| [T21](T21-heightmap-terrain.md) | Heightmap terrain per database (rendering + HAT/HOT) | Tools | T20 | 📝 |
| [T22a](T22a-sample-models-blender.md) | Sample models (tank, helicopter, jet, truck) from a Blender script + format/fidelity research | Tools | T20 (database.toml only) | ⏳ |
| T22d | Hero Cessna 172-class aircraft (brief on branch `task/t22d-cessna`) | Tools | T22a, T22c | 🔍 PR open |
| T22e | High-fidelity Cessna: geometry + PBR textures, fidelity gates (brief on branch `task/t22e-cessna-hifi`) | Tools | T22d | ⏳ |
| [T22](T22-gltf-models.md) | glTF models, articulated part nodes, primitive fallback, Database panel | Tools | T20 | 📝 |
| [T22b](T22b-viewer-render-upgrades.md) | Viewer render upgrades: cockpit pass, instancing, textures/PBR (later) | Tools | T22 | 📝 |
| [T22c](T22c-model-audit.md) | Deterministic model audit: shape fidelity, needless triangles, hygiene, budgets (Opus) | Tools | T22a CP1 | ✅ into T22a branch `f7b47a8` |
| [T23](T23-ig-config-file.md) | `ig.toml` config for `ig_viewer` / `demo_ig` | Tools | T09b P2, T20 | 📝 |
| [T24](T24-scenario-files.md) | Scenario files replace the hardcoded scripted Host | Tools | T20 | 📝 |
| [T25](T25-record-replay.md) | Record / replay (pcap + pcapng reader, JSON index, replay panel) | Tools | T09b P2, T10 | 📝 |
| [T26](T26-tools-platforms-packaging.md) | Viewer on Linux/Windows, CPack bundle, tools guide | Tools | T20–T25 | 📝 |
| [T27](T27-interactive-host.md) | *Optional:* interactive Host (fly/drive with a gamepad) | Tools | T09b P2, T20 | 📝 |
| [T28](T28-unreal-ig.md) | *Optional:* Unreal Engine IG plugin — **🔒 locked: needs the user's express permission to start** | Future | M1, T20 | 🔒 |

Parallelism: T03, T04, T05 and T08 are independent and can run at the same time. T10 and T12 can
run beside T07–T09 (T09a sits between T07/T08 and T09). T09b Part 1 (headless mock-IG core +
viewer rendering) runs beside T07/T09a; its Part 2 (network mode) follows T09a. In Phase B, T13–T17 are independent.
Tools track (productionising `ig_viewer` / `cigi_mock_ig`, planned 2026-10-12; 📝 = brief written,
not launched): T09b P2 ‖ T20 → T22a → T21 ‖ T22 ‖ T24 → T23 ‖ T25 (‖ T27) → T26.

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
