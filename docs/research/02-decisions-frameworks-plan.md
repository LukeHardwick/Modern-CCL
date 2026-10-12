# Decisions, Proposed Frameworks and Implementation Plan

Status: Frameworks approved 2026-10-08.
Builds on [01-cigi33-requirements.md](01-cigi33-requirements.md).

## 1. Decisions taken (2026-10-08)
| Topic | Decision |
|---|---|
| Versions | Send CIGI 3.3. Receive 3.0–3.3 (detect minor version, decode compatible fields). No CIGI 1/2. |
| Provenance | Clean-room from the ICD + errata. No CCL source is copied or ported. |
| API | New idiomatic C++23 API. No CCL compatibility layer. |
| Errors | `std::expected<T, cigi::Error>`; library must build with exceptions disabled. |
| Platforms | macOS/Clang (libc++), Linux/GCC 14+, Windows/MSVC. Platform-specific code must be flagged. Cross-platform libraries (e.g. Asio/Boost) are allowed. |
| Tests | GoogleTest. |
| Extras | UDP transport + demo host/IG apps; benchmarks against the original CCL. |
| Licence | Decide later (check your university's IP rules). |
| Process | Opus plans and reviews; each task is implemented by a Sonnet sub-agent. |

## 2. Frameworks and tools (APPROVED 2026-10-08)
| Need | Choice | Notes |
|---|---|---|
| Build | **CMake ≥ 3.28** + `CMakePresets.json` | One preset per compiler/config (debug, release, asan, coverage, fuzz). |
| Dependencies | **CMake `FetchContent`** with pinned tags/hashes | No package manager. All third-party deps are opt-in per target. |
| Unit tests | **GoogleTest + GMock** | |
| Benchmarks | **Google Benchmark** | |
| Networking | **Boost.Asio** | Fetched from the official Boost CMake release tarball with `BOOST_INCLUDE_LIBRARIES=asio` to limit what is configured. Transport target only. |
| Logging | **spdlog** | Built with `SPDLOG_NO_EXCEPTIONS` and `SPDLOG_USE_STD_FORMAT`. Used by session, transport and demos. The wire/packet layer stays pure (returns `std::expected`, never logs). |
| CLI parsing | **CLI11** | Demo apps only. |
| Mock-IG viewer | **raylib + Dear ImGui + rlImGui** | `ig_viewer` only (T09b), behind `CIGI_BUILD_VIEWER` (default OFF). Approved 2026-10-12. |
| Fuzzing | **libFuzzer** | Clang only (macOS Homebrew LLVM, Linux Clang). |
| Sanitizers | ASan + UBSan where available | ASan only on MSVC; UBSan/libFuzzer not on MSVC. |
| Static analysis | **clang-format**, **clang-tidy** (`modernize-*`, `cppcoreguidelines-*`, `bugprone-*`, `performance-*`, `readability-*` subset) | Run through a CMake target/preset. |
| Coverage | **llvm-cov / llvm-profdata** (source-based) | `coverage` preset; HTML report for the FYP write-up. |
| Docs | **Doxygen** + **Graphviz** (`HAVE_DOT = YES`) | Installed. Include-dependency and call graphs for the report. |
| CI | **None (no GitHub Actions).** Local verification instead | `scripts/check.sh` runs format, tidy, all presets and tests on macOS. A Docker (Colima) GCC 14 image tests Linux locally. |
| CCL baseline | CCL 3.3.3a via `ExternalProject`, benchmarks only | Likely needs build patches. |

Core library (`cigi`) dependencies: standard library + spdlog (sessions only). Packet/wire layer: none.

## 3. Platform-specific areas (flagged)
| # | Area | Platform issue | Mitigation |
|---|---|---|---|
| P1 | UDP sockets | BSD sockets vs Winsock | Boost.Asio; transport is an optional target. |
| P2 | Shared-library export | MSVC `__declspec` vs visibility | CMake `GenerateExportHeader`; static by default. |
| P3 | Sleep granularity (frame pacing) | Windows ≈15.6 ms unless `timeBeginPeriod(1)` | **One** small abstraction, `cigi::platform::sleep_until(steady_clock::time_point)` in `src/platform/sleep.cpp`. The only `#ifdef _WIN32` for timing lives there; callers (demo host, demo IG, async session tick) use it. |
| P4 | Endianness | — | `std::endian` + `std::byteswap`. |
| P5 | Float format | IEEE-754 assumed | `static_assert(std::numeric_limits<float/double>::is_iec559)`. |
| P6 | Fuzzing / UBSan | Unavailable on MSVC | Presets only defined for Clang/GCC. |
| P7 | C++23 library gaps | libc++ lacks `std::generator`, `std::start_lifetime_as` | Not used. |
| P8 | **Windows/MSVC verification** | No CI and no Windows machine in the loop | Code is written to the MSVC-supported subset and flagged; you must build the `msvc` preset on a Windows machine at milestones M1 and M2. |

## 4. Architecture sketch
```
cigi (core, header + static lib, no deps)
├─ wire/       ByteReader/ByteWriter over std::span<std::byte>; bit-field codec; byte swap; UTF-8 truncation
├─ packets/    one aggregate struct per packet (enum class fields) + constexpr encode/decode
├─ message/    MessageBuilder (fixed buffer, IG Control/SOF first, 8-byte rules)
│              MessageView (lazy range over packets; swap detection; unknown/oversize tolerance)
├─ dispatch/   std::variant<Packet...> + visit(overloaded{...}); opcode handler table
├─ session/    HostSession / IgSession: frame numbering, sync/async, IG mode state machine,
│              peer minor-version tracking (3.0–3.3)
└─ user/       UserPacket concept (opcode 201–255, user-supplied encode/decode/swap)
cigi_transport (optional; Boost.Asio UDP)       examples/ demo_host, demo_ig
tests/ (GoogleTest)   fuzz/ (libFuzzer)   bench/ (Google Benchmark, CCL baseline)
```
Key design points: no virtual packet hierarchy; no heap allocation on encode/decode; no C bit-fields,
`#pragma pack` or `reinterpret_cast` overlays; encode/decode are `constexpr`, so golden-byte tests can
be `static_assert`s.

## 5. Task plan
**Superseded on 2026-10-08 by the reduced-set-first plan.** The authoritative task list, status and
the full brief for every task now live in [../tasks/README.md](../tasks/README.md):
- Foundation (done): T00 scaffold, T01 wire primitives, T02 vertical slice, T02b field tables.
- Phase A (MVP): T03–T05 reduced packet set (Entity, Articulated Part, Rate, Component, View Control,
  View Definition, Celestial Sphere, HAT/HOT request/responses), T06 dispatch, T07 sessions, T08 UDP,
  T09 demo host + mock IG, T10 Wireshark verification, T11 CCL interop + differential tests, T12 fuzzing.
  Milestone M1 = user trial + Windows/MSVC build.
- Phase B (full 3.3): T13–T16 remaining packets incl. symbology, T17 user-defined packets,
  T18 benchmarks vs CCL, T19 documentation. Milestone M2 = full CIGI 3.3.

Note: the CCL is now used as a black-box test peer (T11) and benchmark baseline (T18), confined to
`interop/` and `bench/`; library code remains clean-room.

## 6. Open items
- Licence (deferred).
- Check the FYP regulations on declaring AI-assisted implementation.
- C++ modules: out of scope; can be revisited after T16.

## 7. Git workflow
- T0 is committed directly on `main` (it creates the first commit, which worktrees need as a base).
- From T1 on, each task's sub-agent works in its **own git worktree** on a branch `task/tNN-<slug>`, commits there, and Opus reviews the diff before merging into `main`.
- Commit at least once per task; larger tasks (packet batches) commit per packet group.

## 8. Design checkpoint outcome (T2, 2026-10-08)
- Packet codecs: declarative constexpr field tables (offsets written once), compile-time layout verification.
- MessageBuilder: each `add()` returns `Result<void>`; builder unchanged on failure.
- Decode stays tolerant of undefined enum values; separate `validate(packet)` for range checks.
- MessageView: lazy parse plus optional `validate_all()`.
- Spec defect recorded: 3.3 ICD gives Minor Version "2" in IG Control/SOF; we send 3.
