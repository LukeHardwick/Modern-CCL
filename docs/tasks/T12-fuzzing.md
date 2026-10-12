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
