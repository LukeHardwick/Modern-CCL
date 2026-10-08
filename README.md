# Modern-CCL

A clean-room C++23 implementation of the CIGI 3.3 (Common Image Generator Interface) protocol.
Status: build/tooling scaffold only (no protocol code yet).

## Prerequisites
- CMake >= 3.28, Ninja, a C++23 compiler.
- macOS: Homebrew LLVM (`brew install llvm`) at `/opt/homebrew/opt/llvm` (clang++, clang-tidy,
  clang-format, llvm-cov, llvm-profdata). Presets link against its libc++.
- Linux/GCC check: Docker (Colima on macOS) - uses `docker/gcc14.Dockerfile`.
- Dependencies (GoogleTest v1.17.0) are fetched with `FetchContent`; network needed on first configure.

## Presets
| Preset | Platform | Compiler | Notes |
|---|---|---|---|
| `clang-debug` / `clang-release` | macOS | Homebrew clang++ | |
| `clang-asan` | macOS | Homebrew clang++ | ASan + UBSan, Debug |
| `clang-coverage` | macOS | Homebrew clang++ | `coverage` target -> `build/clang-coverage/coverage/html` |
| `clang-tidy` | macOS | Homebrew clang++ | clang-tidy on our targets, warnings are errors |
| `linux-gcc-debug` / `linux-gcc-asan` | Linux | g++ (14) | Run locally via `scripts/linux-check.sh` |
| `msvc-debug` / `msvc-release` / `msvc-asan` | Windows | cl (Ninja) | **UNVERIFIED** |

MSVC presets use Ninja (run from a "x64 Native Tools" / VS developer prompt) and have never been
built or run: no Windows machine has been available. MSVC ASan has no UBSan.

Use: `cmake --preset clang-debug && cmake --build --preset clang-debug && ctest --preset clang-debug`.
Extra targets: `format`, `format-check`, `coverage`.

## Checks
- `scripts/check.sh` - format-check, then configure/build/test for clang-debug, clang-asan, clang-tidy.
  Flags: `--coverage` (coverage preset + report), `--linux` (Docker GCC 14 check).
- `scripts/linux-check.sh` - builds the GCC 14 image and runs linux-gcc-debug and linux-gcc-asan in
  it (build trees live in a Docker volume, not in `build/`).

## CIGI specification
The CIGI ICD is not redistributed. It lives git-ignored in `third_party/spec/` (see the README
there for the download link). Never commit the PDFs.
