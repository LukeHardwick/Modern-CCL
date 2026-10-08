# T08 — UDP transport (Boost.Asio) and `platform::sleep_until`

Phase A (MVP) · Depends on T02b · Parallel with T03–T05 · Branch `task/t08-udp-transport`
Follow the shared rules in [README.md](README.md).

## Goal
Move CIGI messages over UDP on macOS, Linux and Windows without platform code leaking into the
library, and provide the single sleep abstraction approved in the plan (P3).

## Spec
ICD 3.3 §2 (UDP over Ethernet is the assumed transport; multicast to several IGs is allowed — out of
scope here), §2.4 message frequency.

## Design (planner decision)
- New optional target **`cigi_transport`** (`cigi::transport`), option `CIGI_BUILD_TRANSPORT`
  (ON when top level). Depends on **Boost.Asio** fetched with FetchContent from the official Boost
  CMake release tarball (`boost-<ver>-cmake.tar.xz`, latest release at least two weeks old; pin URL
  + SHA256; `BOOST_INCLUDE_LIBRARIES=asio`; check what Asio pulls in and keep it minimal). The core
  `cigi` target must not depend on Boost.
- Exceptions: Asio needs exceptions unless `BOOST_NO_EXCEPTIONS` + a `boost::throw_exception`
  definition is provided. Prefer compiling `cigi_transport` **without** exceptions using that route
  and the `error_code` overloads; if that proves impractical, compile only this target with
  exceptions but expose a no-throw API — report which and why.
- API (synchronous, poll-friendly; no coroutines required):
  `class UdpEndpoint { static Result<UdpEndpoint> open(const UdpConfig&); Result<void>
  send(std::span<const std::byte>); Result<std::size_t> receive(std::span<std::byte> buffer,
  std::chrono::milliseconds timeout); local_port(); }` with `UdpConfig { local address/port, remote
  address/port, receive buffer size }`. New `ErrorCode`s as needed (e.g. `TransportError`,
  `Timeout`) with the OS error kept in a side field or logged — don't leak Asio types in headers
  (pimpl).
- Defaults: IG listens on 8004, Host on 8005 (common convention; configurable — note the ICD does not
  mandate ports).
- **`cigi::platform::sleep_until(std::chrono::steady_clock::time_point)`** in a tiny target
  `cigi_platform` (`src/platform/sleep.cpp`). It is the **only** place with timing `#ifdef _WIN32`:
  on Windows raise timer resolution once (`timeBeginPeriod(1)` in a function-local static RAII
  guard, `timeEndPeriod` at exit) then sleep; elsewhere `std::this_thread::sleep_until`. Mark with
  `// PLATFORM:`. Also `FramePacer` helper (fixed-rate loop: `wait_next()` returns overrun info).

## Tests
Loopback send/receive between two endpoints (ephemeral ports), timeout path, buffer-too-small path,
max datagram size, bad address. `FramePacer` at 60 Hz for ~0.5 s: mean period within tolerance (keep
it lenient; CI machines are noisy). Mark network tests with a ctest label `network`.

## Acceptance
README rules 6–9; `linux-check` must still pass (reviewer runs it). Windows code compiles only on
MSVC; report it as unverified.
