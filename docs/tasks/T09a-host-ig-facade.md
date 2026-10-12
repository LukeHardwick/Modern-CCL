# T09a — High-level `cigi::Host` / `cigi::Ig` facade

Phase A (MVP) · Depends on T07, T08 · Before T09 · Branch `task/t09a-host-ig-facade`
Follow the shared rules in [README.md](README.md).

## Goal
Make "connect a Host to an IG and send entity updates every frame" a few lines of code. T07
(sessions) and T08 (UDP) are the low-level building blocks; every Host would otherwise rewrite the
same receive → session → ready? → build → send loop with its timeouts and error handling. The
facade owns that loop. The real target is a user-written Host that bridges DDS to Diamond Visionics
GenesisIG (CIGI 3.3), so the API must fit next to someone else's event loop or threads.

Target usage (the example in the docs must compile essentially as written):

```cpp
auto ig = cigi::Host::connect({.ig_address = "10.0.0.2"});   // defaults: ports 8004/8005, sync
if (!ig) return fail(ig.error());
ig->on<cigi::HatHotResponse>(handle_terrain);
ig->on_state_change([](cigi::LinkState from, cigi::LinkState to) { log(from, to); });
ig->on_frame([&](cigi::HostFrame& f) {                        // once per frame, only in Operate
    for (const auto& e : latest_samples()) f.add(to_entity_control(e));
});
return ig->run(stop_token) ? 0 : 1;                         // or ig->poll(timeout) in your loop
```

## Spec
ICD 3.3 §2.2 (synchronous vs asynchronous), §2.3 frame numbering, §2.4 message frequency, §2.8
start-up and shutdown, §4.1.1 / §4.2.1 (IG Mode, Database Number). The facade adds no protocol
rules of its own: all protocol behaviour stays in T07's sessions; the facade only drives them.

## Design (planner decision)
- **Target** `cigi_runtime` (`cigi::runtime`), depends on `cigi_session`, `cigi_transport`,
  `cigi_platform`. Headers `include/cigi/host.hpp`, `include/cigi/ig.hpp`. No Boost types in
  headers (reuse T08's pimpl). No exceptions. `Host`/`Ig` are move-only.
- **Transport seam.** A small internal interface `cigi::DatagramTransport { send(span) ;
  receive(span, timeout) }`, implemented by T08's `UdpEndpoint` and by an in-memory pair for tests.
  `Host::connect(config)` uses UDP; `Host::over(config, std::unique_ptr<DatagramTransport>)` lets
  tests (and advanced users, e.g. multicast later) inject their own. Same for `Ig`.
- **Clock seam.** Timeouts and pacing use an injectable clock (same approach as T07) so state
  machine tests are deterministic and fast.

### `cigi::Host`
- `struct HostConfig`: `ig_address`, `ig_port = 8004`, `local_address = any`, `local_port = 8005`,
  `synchronous = true`, `async_rate_hz = 60` (async mode only), `requested_mode = Operate`,
  `database = std::nullopt` (load request on start-up), `peer_timeout = 1s` (no SOF → Lost),
  `max_message_bytes = 16384`, `standby_on_close = true`, `session = SessionConfig{}`,
  `logger` (spdlog, default null sink).
- `connect()` only opens the socket; it never blocks (UDP is connectionless). The link is
  established by the start-up sequence driven by `poll`/`run`.
- **Link state** `enum class LinkState { WaitingForIg, Standby, LoadingDatabase, Operate, Debug,
  Lost, Closed }` with `EnumTraits`; `state()` and `on_state_change(from, to)`. Derived from the
  IG mode and database fields of received SOFs plus the peer timeout. After Lost, resumed SOFs
  restart the start-up sequence (detect an IG restart by its frame number going backwards).
- **Frames.** `on_frame(callable(HostFrame&))` is called once per frame the session is ready to
  send (per SOF in sync mode; at `async_rate_hz` via `FramePacer` in async mode) and **only while
  in Operate/Debug** — before that the facade sends the IG Control start-up messages itself.
  `HostFrame` wraps the session's `MessageBuilder`: `Result<void> add(const P&)` for any
  Host→IG packet, plus read-only context: IG frame number, IG timestamp, time since last frame,
  link state. If no `on_frame` is set, the facade still sends IG Control each frame.
- **Receive.** `on<P>(handler)`, `on_unknown`, `on_error` forward to the session's dispatcher.
- **Driving.** `Result<PollStatus> poll(std::chrono::milliseconds timeout)`: drains all datagrams
  available within the timeout, feeds the session, checks timeouts, sends at most one frame.
  `Result<void> run(std::stop_token)`: loops `poll` until stop is requested or a fatal error.
  Non-fatal problems (decode errors, oversize add, late frame) go to `on_error` and stats; fatal
  ones (socket failure) end `run` with the error.
- **Message size.** An `add()` that would exceed `max_message_bytes` returns `BufferTooSmall` and
  the frame is still sent with what fit. Log a one-time warning when a message exceeds 1472 bytes
  (it will be IP-fragmented on a standard Ethernet MTU).
- **Pass-throughs.** `request_mode(IgMode)`, `request_database(n)`, `stats()` (session + link:
  frames sent, late frames, time in each state), and `session()` / escape hatch for advanced use.
- `close()` (also in the destructor): if `standby_on_close`, send one IG Control requesting
  Standby (§2.8), then close the socket. State → Closed.
- **Threading.** Not thread-safe; all callbacks run on the thread calling `poll`/`run`. Document
  the intended DDS pattern (no DDS dependency in this repo): DDS listeners write the latest sample
  per instance into a mutex-protected map; `on_frame` takes a snapshot and adds Entity Controls;
  instance disposal → one Entity Control with state Destroyed.

### `cigi::Ig` (mirror, smaller)
- `IgConfig`: `listen_port = 8004`, `host_address/port` (default: reply to the sender of the first
  IG Control), `rate_hz = 60`, `peer_timeout`, `logger`, `session`.
- `on_mode_request(callable(IgMode) -> bool)` (accept/refuse), `set_mode()` when a transition
  completes, `on<P>` for Host→IG packets, `on_frame(callable(IgFrame&))` to add responses after
  the SOF the facade writes, `poll`/`run`/`close`, `stats()`. Paced by `FramePacer`.
- Primarily for the mock IG (T09) and tests; keep it minimal.
- **Fault hooks (for the mock IGs, T09/T09b; added 2026-10-12).** `Ig::set_paused(bool)` stops
  sending Start of Frame while still receiving; `Ig::restart()` resets the IG frame counter and
  session to Reset/Standby as if the IG process restarted. Refusing Operate and slow database loads
  are already expressible via `on_mode_request` / `set_mode`.
- **`FaultInjectingTransport`** (`include/cigi/fault_transport.hpp`, in `cigi_runtime`): a
  `DatagramTransport` decorator with runtime-adjustable `drop_probability`, `duplicate_probability`
  and `added_latency` (delayed datagrams queued and released by the injected clock), seeded RNG for
  determinism. Usable on either side (`Host::over`/`Ig::over`). Unit-test each fault.

## Tests
- State machine with the in-memory transport and fake clock: WaitingForIg → Standby → Operate;
  database load (LoadingDatabase then Operate); IG refusing Operate; peer timeout → Lost and
  recovery; IG restart (frame number reset); `on_frame` never called before Operate; sync gating
  (one frame per SOF) and async pacing; `max_message_bytes` overflow path; `standby_on_close`.
- `Host` ↔ `Ig` facades over real loopback UDP for ~1 s (label `network`, lenient timing):
  reaches Operate, entity updates arrive, HAT/HOT request answered.
- No allocation in the steady-state frame path after warm-up (replaced `operator new` counter),
  excluding user callbacks.

## Docs and examples
- `examples/host_minimal.cpp`: the target usage above, against `demo_ig` or a real IG
  (`--ig-address`), with one entity flying a circle. ≤ 80 lines.
- `docs/guide/host-quickstart.md`: connect, states, frames, receiving responses, sync vs async,
  sizing, the DDS bridge pattern (pseudo-code only), and when to drop down to sessions/UDP.
- Update `docs/design/04-sessions.md` with a short "Facade" section (layering diagram).

## Follow-on changes (planner will apply to other briefs on approval)
- T09 `demo_host` / `demo_ig` are built on `cigi::Host` / `cigi::Ig` (T09 then depends on T09a).
- T09b Part 2 (`ig_viewer` network mode) uses `cigi::Ig`, the fault hooks and `FaultInjectingTransport`.
- T18 `net_perf` uses the low-level session + UDP API directly (to measure the stack without facade
  scheduling), and adds one facade run for comparison.

## Acceptance
README rules 6–9; `linux-check` passes (reviewer). `examples/host_minimal.cpp` builds under
`check.sh`, and the network test shows a `Host` reaching Operate against an `Ig` and exchanging
entity updates and a HAT/HOT request.
