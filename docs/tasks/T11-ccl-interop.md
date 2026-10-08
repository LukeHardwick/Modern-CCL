# T11 — CCL interoperability & differential tests

Phase A (MVP) · Depends on T09 · Branch `task/t11-ccl-interop`
Follow the shared rules in [README.md](README.md). This task uses the **rule 2 exception**: you may
download, build, read and call the original CIGI Class Library (CCL) as a black-box test peer, but
nothing from it may be copied into `include/` or `src/`, and library code must not be changed to
imitate CCL behaviour unless the ICD supports the change.

## Goal
Show that Modern-CCL and the original CCL understand each other byte-for-byte and live over UDP —
the strongest interoperability evidence available without a commercial IG.

## Design (planner decision)
- **Fetch & build the CCL** (option `CIGI_BUILD_CCL_INTEROP`, default OFF; never built by
  `scripts/check.sh` unless `--ccl` is passed):
  - Source: CCL 3.3.3a — prefer the upstream SourceForge release; the Debian source package
    (`cigi-ccl`, version 3.3.3a+svn818) is an acceptable alternative and ships patches for newer
    compilers that you may reuse. Pin URL + SHA256 via FetchContent/ExternalProject.
  - Expect 2009-era C++ issues (removed dynamic exception specs, `register`, implicit conversions,
    64-bit assumptions). Keep fixes as patch files in `interop/ccl/patches/` applied at configure
    time, with a README explaining each. Build the CCL with its own relaxed flags (no project
    warnings, no clang-tidy, no `-Werror`); C++ standard as needed for it to compile.
  - Licensing: the CCL is LGPL. It is linked only into interop/benchmark tools, never into `cigi`.
    State this in `interop/README.md` and the main README.
- **`interop/ccl_peer/`**: `ccl_host` and `ccl_ig` command-line programs using the CCL's sessions
  and packet classes, mirroring `demo_host`/`demo_ig` behaviour for the MVP packets and printing
  decoded values as stable `key=value` lines.
- **Differential tests** (`tests/interop/ccl/`, label `ccl`, built only with the option):
  1. ours → CCL: encode each MVP packet (seeded random values, many iterations, every enum value)
     with Modern-CCL, decode with the CCL, compare every field.
  2. CCL → ours: the reverse.
  3. Byte-level: for identical field values, compare the two encodings byte-for-byte (reserved bits
     included) and report differences.
  4. Foreign byte order: feed the CCL a message fabricated in swapped order by our library, and vice
     versa.
  5. Record which Minor Version the CCL sends and how it treats our 3 (relevant to the ICD defect).
- **Live interop** (label `ccl;network`): run `ccl_host ↔ demo_ig` and `demo_host ↔ ccl_ig` on
  loopback for a few seconds; both must reach Operate, exchange HAT/HOT, and exit 0.
- **Discrepancies** → `docs/interop/discrepancies.md` (shared with T10): ICD section, CCL behaviour,
  our behaviour, verdict (ICD wins), and whether a test tolerance was added. Never "fix" ours to match
  a CCL bug.
- `docs/interop/ccl.md`: how to build and run everything, results table per packet.

## Acceptance
README rules 7–9 for anything under `tools/` or library code touched. Report: build patches needed,
differential results per packet, live interop results, discrepancies and verdicts.
