# T00 — Project scaffold & tooling ✅

Merged: `051531a` (committed directly on `main`; it created the first commit worktrees need).

## Goal
Build/tooling skeleton only, no protocol code.

## Brief (as issued)
- CMake ≥ 3.28, C++23, no extensions, `compile_commands.json`. Options `CIGI_BUILD_TESTS`,
  `CIGI_ENABLE_SANITIZERS`, `CIGI_ENABLE_COVERAGE`, `CIGI_ENABLE_CLANG_TIDY`, `CIGI_WARNINGS_AS_ERRORS`.
- Static library `cigi` (`cigi::cigi`), `GenerateExportHeader` → `cigi/export.hpp`; compiled with
  exceptions disabled (`-fno-exceptions` / `/EHs-c- _HAS_EXCEPTIONS=0`).
- `cmake/`: CompilerWarnings (strict warnings, `-Werror`/`/WX`), Sanitizers (ASan+UBSan; ASan only on
  MSVC), Coverage (Clang source-based + HTML report target), StaticAnalysis (clang-tidy on our targets
  only; `format`/`format-check`), Dependencies (FetchContent only).
- GoogleTest v1.17.0 via URL + SHA256; third-party code gets no project warnings/tidy.
- `CMakePresets.json`: `clang-{debug,release,asan,coverage,tidy}` (Homebrew LLVM, Darwin),
  `linux-gcc-{debug,asan}`, `msvc-{debug,release,asan}` (unverified).
- `.clang-format`, `.clang-tidy` (bugprone, cppcoreguidelines, modernize, performance, readability,
  misc, portability; warnings as errors; naming rules), `.gitignore`, `.editorconfig`.
- `scripts/check.sh` (`--coverage`, `--linux`), `scripts/linux-check.sh` + `docker/gcc14.Dockerfile`.
- README; `third_party/spec/README.md` (ICD download location; PDFs git-ignored).

## Outcome
All checks green on macOS; Linux GCC 14 green in Docker. Deviations accepted: a `CoverageMerge.cmake`
helper (profraw globbing), Homebrew libc++ headers as well as libs in presets, Linux build trees in a
Docker volume. MSVC presets remain unverified (verify at milestone M1).
