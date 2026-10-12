# Host quickstart: `cigi::Host`

`cigi::Host` connects a simulation (the Host) to an Image Generator over CIGI 3.3 and owns the loop
every Host would otherwise write itself: receive a Start of Frame, answer it, run the start-up
sequence, notice a lost or restarted IG. You supply the content of each frame.

Link against `cigi::runtime` (C++23, no exceptions inside the library, spdlog for logging).

## Connect, receive, send

```cpp
#include "cigi/host.hpp"

auto ig = cigi::Host::connect({.ig_address = "10.0.0.2"});    // ports 8004 / 8005, synchronous
if (!ig) return fail(ig.error());
ig->on<cigi::HatHotResponse>(handle_terrain);                  // IG -> Host packets
ig->on_state_change([](cigi::LinkState from, cigi::LinkState to) { log(from, to); });
ig->on_frame([&](cigi::HostFrame& f) {                         // once per frame, Operate/Debug only
    for (const auto& e : latest_samples()) f.add(to_entity_control(e));
});
return ig->run(stop_token) ? 0 : 1;                            // or ig->poll(timeout) in your loop
```

`examples/host_minimal.cpp` is the complete program (one entity flying a circle).

`connect()` only opens a UDP socket; it never blocks and UDP has no connection, so the link is
established by the start-up sequence that `poll()` / `run()` drive. Addresses are numeric (IPv4 or
IPv6 literals); resolve names yourself. `Host::over(config, transport)` takes your own
`cigi::DatagramTransport` instead of UDP (tests, multicast, a capture, `FaultInjectingTransport`).

## Link states

| State | Meaning |
|---|---|
| `WaitingForIg` | No Start of Frame yet, or the IG restarted and has not been heard since. |
| `Standby` | The IG reports Reset/Standby (Offline Maintenance is reported as Standby too). |
| `LoadingDatabase` | The IG operates but reports a database being loaded (ICD 4.2.1). |
| `Operate` / `Debug` | Mission data flows; `on_frame` runs. |
| `Lost` | The IG was heard, then nothing arrived for `peer_timeout` (default 1 s). |
| `Closed` | `close()` was called. |

Until the IG operates the facade sends IG Control only (carrying the requested mode and database,
ICD 2.8, 4.1.1). `on_frame` is never called outside Operate/Debug. After `Lost` or an IG restart
the facade calls `HostSession::reset_peer()` and the start-up sequence runs again; the requested
mode and database are kept. An IG restart is recognised by its frame number going far backwards
or by a run of more than `stale_window` consecutive stale Start of Frame messages. A restart that
only passes through stale frames for a few messages shows up as a normal mode change (Operate to
Standby) and is handled the same way. A Host that has never heard an IG stays `WaitingForIg` (it
sends an IG Control every `probe_interval`, 100 ms, so an IG that learns its Host from the first
message can start).

## Frames

- **Synchronous** (default, ICD 2.2.1): one message per received Start of Frame. The Host runs at
  the IG's frame rate and `on_frame` runs once per Start of Frame.
- **Asynchronous** (ICD 2.2.2): `synchronous = false, async_rate_hz = 60`. A message every
  `1 / async_rate_hz` seconds regardless of the IG. If `poll` runs late the missed ticks are
  dropped, not replayed (`stats().late_frames`).

`HostFrame::add(packet)` appends any Host -> IG packet and returns the session's `Result<void>`.
It also exposes the IG frame number and timestamp, the time since the last frame and the link
state. A frame holds `max_message_bytes` (default 16384); an `add` that does not fit returns
`BufferTooSmall`, goes to `on_error` and `stats().oversize_adds`, and the frame is still sent with
what fit. A message above 1472 bytes will be IP-fragmented on a standard Ethernet MTU: the facade
warns once. Keep frames below that unless your network carries larger datagrams.

## Receiving

`on<P>(handler)` for the IG -> Host packets (version masking is applied first), `on_unknown` for
other opcodes, `on_error` for undecodable datagrams and packets. The Start of Frame itself is
consumed by the facade: `session().last_start_of_frame()` returns it. A handler for HAT/HOT
responses typically matches the request by `hat_hot_id`; `host_frame_lsn` identifies the frame
that carried the request (ICD 2.3, 4.2.2).

## Driving it

- `poll(timeout)` drains the datagrams that arrive within the timeout, feeds the session, checks
  the peer timeout and sends at most one message. `timeout = 0` never waits. Use it inside your own
  event loop; call it at least as often as your frame period.
- `run(stop_token)` loops `poll` until stop is requested, `close()` is called or the transport
  fails (a socket error is the only fatal error; everything else is `on_error` plus counters).
- `close()` (also the destructor) sends one IG Control requesting Reset/Standby when
  `standby_on_close` is set (ICD 2.8), then closes the socket.
- `request_mode()`, `request_database(n)`, `stats()` (session counters, frames sent, late frames,
  restarts, time in each state) and `session()` (the underlying `HostSession`) are always
  available.

## Threading

A `Host` is **not thread-safe** and is move-only. All callbacks run on the thread that calls
`poll` / `run` / `close`. If other threads produce your data, hand it over through your own
synchronisation (below). The frame path itself does not allocate after warm-up (your callbacks
excluded).

## Bridging DDS (pattern, no DDS dependency here)

```cpp
// DDS listener thread(s): keep only the latest sample per instance.
void on_data(const Sample& s) {
    std::lock_guard lock(mutex);
    latest[s.instance] = to_entity_control(s);        // overwrite: the newest sample wins
}
void on_disposed(InstanceId id) {                     // instance disposal -> one Destroyed entity
    std::lock_guard lock(mutex);
    cigi::EntityControl e = latest[id];
    e.entity_state = cigi::EntityState::Destroyed;
    destroyed.push_back(e);
    latest.erase(id);
}

// CIGI thread: one frame, one snapshot.
host->on_frame([&](cigi::HostFrame& f) {
    std::lock_guard lock(mutex);
    for (const auto& [id, e] : latest) f.add(e);
    for (const auto& e : destroyed) f.add(e);
    destroyed.clear();
});
```

Hold the lock only long enough to copy if your listeners are time critical; `on_frame` must
finish within the frame period.

## When to drop down

- Your own socket or scheduling (an existing network layer, multicast): `Host::over` with a
  `DatagramTransport`, or use `HostSession` directly (`docs/design/04-sessions.md`).
- Raw datagrams without sessions (a capture tool, a protocol test): `cigi::transport::UdpEndpoint`
  and the message builder/view (`docs/design/03-packet-and-message-api.md`).
- Several IGs: one `Host` per IG, each on its own `poll` schedule.

## Testing your Host without an IG

`cigi::Ig` is the mirror facade used by the mock IGs. In a unit test, wire a `Host` and an `Ig`
with `make_memory_transport_pair()`, inject a fake clock through `HostConfig::session.clock`, and
step both `poll(0ms)` calls. Wrap either transport in `FaultInjectingTransport` to drop, duplicate
or delay datagrams (`set_drop_probability`, `set_duplicate_probability`, `set_added_latency`);
`Ig::set_paused(true)` and `Ig::restart()` simulate a silent or restarted IG.
