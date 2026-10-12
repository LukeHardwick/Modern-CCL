# T12 — libFuzzer + hardening

Phase A (MVP) · Depends on T06 (T07 for session fuzzing) · Branch `task/t12-fuzzing`
Follow the shared rules in [README.md](README.md).

## Goal
Prove the receive path survives arbitrary datagrams: no crash, no UB, no out-of-bounds read, no
infinite loop, no allocation in the hot path.

## Design (planner decision)
- Presets: `clang-fuzz` (macOS Homebrew LLVM — check that its compiler-rt ships libFuzzer; if not,
  say so) and `linux-clang-fuzz` (Docker image with Clang + libFuzzer; add a `docker/clang.Dockerfile`
  and a `scripts/fuzz.sh [--linux] <target> <seconds>`). Flags `-fsanitize=fuzzer,address,undefined`
  (PLATFORM: not available on MSVC — no MSVC preset).
- Targets in `fuzz/`:
  1. `fuzz_message_view`: `MessageView::parse` → `validate_all` → iterate → `decode_any` for both
     packet sets → `validate()` on each decoded packet.
  2. `fuzz_roundtrip`: for every packet that decodes successfully, encode it again and decode the
     result; the two decoded values must be equal (reserved bits are not preserved, values are).
  3. `fuzz_sessions`: feed arbitrary datagrams to `HostSession::receive` and `IgSession::receive`
     with every IG mode; sessions must never assert or crash.
- Seed corpora in `fuzz/corpus/<target>/` generated from the golden-byte test vectors and the T10
  Wireshark corpus (add a small generator). Dictionaries (`fuzz/cigi.dict`) with opcodes, sizes, magic
  numbers.
- Run each target at least 10 minutes locally (report execs/s, coverage, findings). Every finding:
  minimise, add as a regression unit test, fix, re-run.
- Also add a ctest that replays the seed corpus through each target's entry point in normal builds
  (so regressions are caught by `check.sh` without libFuzzer).

## Phasing (2026-10-12)
T07 (sessions) is not merged yet. Do targets 1 and 2, presets, Docker, corpora, dictionary and the
replay ctests first. Do **not** start `fuzz_sessions` until the planner tells you T07 is merged
(you will be asked to merge `main` into your branch then).

## Acceptance
README rules 7–9. Report fuzzing durations, exec counts, corpus sizes, and every finding + fix.

## Results (2026-10-12)
No crashes, leaks, timeouts or UB reports in any run; no library findings, so no new regression inputs.

| Target | macOS (Homebrew LLVM, ~661 s) | Linux (Docker, Clang 21 + libstdc++ 15, 60 s) |
|---|---|---|
| `fuzz_message_view` | 17.3 M runs, 26 k exec/s, cov 950 | 4.49 M runs, 73.6 k exec/s, cov 941 |
| `fuzz_roundtrip` | 8.2 M runs, 12 k exec/s, cov 1304 | 2.75 M runs, 45.1 k exec/s, cov 1291 |
| `fuzz_sessions` | 4.9 M runs, 7.4 k exec/s, cov 1618 | 1.46 M runs, 24.0 k exec/s, cov 1472 |

Linux image: Ubuntu 24.04's Clang 18 hides `std::expected` with both libstdc++ 13 and 14, and its
libFuzzer is libstdc++-ABI (so libc++ fails to link). `docker/clang.Dockerfile` therefore uses
Ubuntu 26.04 (Clang 21, libstdc++ 15). The Linux runs are 60 s smoke runs; the 10-minute runs were macOS.

### Corpora and toolchains
- Seed corpus: 81 seeds per target (`fuzz/corpus/<target>`, from `cigi_gen_corpus` + the 34 T10 Wireshark datagrams).
- Grown local corpora after the runs above (`build/fuzz-corpus/<target>`, not committed; cumulative over
  the 661 s and later 120 s runs): message_view 1383 files, roundtrip 2988, sessions 1382.
  The libFuzzer-minimised sets at the end of the latest 120 s run were 934, 1100 and 934 units.
- After review, the round-trip target checks `decode(encode(p)) == p` for every packet (not only the
  frame packets), `fuzz_sessions` checks monotonic statistics, the state after `reset()`/`reset_peer()`
  and that `finish_frame()` output parses and validates. Re-run on macOS (120 s each, new invariants,
  new seeds): message_view 4.82 M runs / 39.8 k exec/s / cov 950; roundtrip 1.87 M / 15.4 k / cov 1371;
  sessions 1.27 M / 10.5 k / cov 1642; no violations, so no library bugs found.
- macOS: Homebrew LLVM 23.1.0; its compiler-rt ships libFuzzer (`libclang_rt.fuzzer_osx.a`).
- Linux image: Ubuntu 26.04, `clang-21 --version`: Ubuntu clang version 21.1.8 (6ubuntu1), aarch64-unknown-linux-gnu.
