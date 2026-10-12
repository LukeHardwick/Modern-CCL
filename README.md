# Modern-CCL

A clean-room C++23 implementation of the CIGI 3.3 (Common Image Generator Interface) protocol.
No exceptions, no heap allocation on the encode/decode path, sends 3.3 and tolerates 3.0–3.2 peers.

Status: **v0.2.0** — Phase A MVP packet set, sessions, UDP transport, the high-level
`cigi::Host` / `cigi::Ig` facade, user-defined packets, a mock IG with a 3-D viewer, Wireshark
verification and fuzzing. See [docs/releases/v0.2.0.md](docs/releases/v0.2.0.md) and
[docs/tasks/STATUS.md](docs/tasks/STATUS.md). Windows/MSVC is not yet verified.

## Quick start (Host)
```cpp
#include <cigi/host.hpp>

auto host = cigi::Host::connect({.ig_address = "192.168.1.20"}); // Result<Host>, no exceptions
if (!host) { /* host.error().code */ }
host->on_frame([&](cigi::HostFrame& frame) {   // once per IG frame, in Operate/Debug
    cigi::EntityControl entity;
    entity.entity_id = 1;
    entity.entity_state = cigi::EntityState::Active;
    entity.latitude_or_x = 51.0;                  // ... position and attitude from your sim
    frame.add(entity);
});
host->run(stop_token);                          // or call host->poll(timeout) from your loop
```
Full walk-through: [docs/guide/host-quickstart.md](docs/guide/host-quickstart.md); runnable:
[examples/host_minimal.cpp](examples/host_minimal.cpp).

## Libraries and apps
| Target | What |
|---|---|
| `cigi::cigi` | packets, `MessageBuilder` / `MessageView`, packet sets, dispatch (no dependencies) |
| `cigi::session` | `HostSession` / `IgSession` (spdlog) |
| `cigi::runtime` | `cigi::Host` / `cigi::Ig` facade, UDP (Boost.Asio), fault-injecting transport |
| `cigi_mock_ig` | headless mock-IG core (`-DCIGI_BUILD_MOCK_IG`, on by default at top level) |
| `ig_viewer` | 3-D mock-IG viewer, raylib + Dear ImGui (`-DCIGI_BUILD_VIEWER=ON`; on in `clang-debug`) — [guide](docs/guide/ig-viewer.md) |
| `examples/` | `minimal_roundtrip`, `dispatch_example`, `user_packet`, `host_minimal`, `wireshark/cigi_frames` |
| `fuzz/` | libFuzzer targets; corpora replayed as ctests — `scripts/fuzz.sh` |

Design notes: [packets and messages](docs/design/03-packet-and-message-api.md),
[sessions](docs/design/04-sessions.md); Wireshark verification:
[docs/interop/wireshark.md](docs/interop/wireshark.md).

## Prerequisites
- CMake >= 3.28, Ninja, a C++23 compiler.
- macOS: Homebrew LLVM (`brew install llvm`) at `/opt/homebrew/opt/llvm` (clang++, clang-tidy,
  clang-format, llvm-cov, llvm-profdata). Presets link against its libc++.
- Linux/GCC check: Docker (Colima on macOS) - uses `docker/gcc14.Dockerfile`.
- Dependencies (GoogleTest, Boost.Asio, spdlog; raylib, Dear ImGui and rlImGui for the viewer)
  are fetched with `FetchContent`; network needed on first configure.

## Presets
| Preset | Platform | Compiler | Notes |
|---|---|---|---|
| `clang-debug` / `clang-release` | macOS | Homebrew clang++ | |
| `clang-asan` | macOS | Homebrew clang++ | ASan + UBSan, Debug |
| `clang-coverage` | macOS | Homebrew clang++ | `coverage` target -> `build/clang-coverage/coverage/html` |
| `clang-tidy` | macOS | Homebrew clang++ | clang-tidy on our targets, warnings are errors |
| `linux-gcc-debug` / `linux-gcc-asan` | Linux | g++ (14) | Run locally via `scripts/linux-check.sh` |
| `clang-fuzz` / `linux-clang-fuzz` | macOS / Linux | clang++ | libFuzzer + ASan/UBSan; use `scripts/fuzz.sh [--linux] <target> <seconds>` |
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
