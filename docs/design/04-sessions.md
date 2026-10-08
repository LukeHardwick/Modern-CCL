# 04 - Host and IG sessions (T07)

Scope: `cigi::HostSession`, `cigi::IgSession` in the `cigi_session` target (`cigi::session`;
spdlog, no Boost, no sockets). A session owns the protocol state of one peer relationship; the
transport (T08) and the facade (T09a) only move bytes and decide when to call it. Not thread-safe:
one session per thread.

## What a session does

```mermaid
sequenceDiagram
    participant H as HostSession
    participant I as IgSession
    I->>H: Start of Frame (IG frame n, mode Standby)
    Note over H: record peer version and byte order; track n; ready_to_send()
    H->>I: IG Control (host frame m, last IG n, request Operate)
    Note over I: on_mode_request(Operate) accepted
    I->>H: Start of Frame (n+1, last host m, mode Standby)
    Note over I: transition done: set_mode(Operate)
    I->>H: Start of Frame (n+2, Operate)
    Note over H: operational(): add() now allowed
    H->>I: IG Control + Entity Control + HAT/HOT Request
    Note over I: Operate: all packets reach handlers()
    H->>I: IG Control (database +7)
    I->>H: Start of Frame (database -7, loading)
    Note over H: acknowledged: IG Control database back to 0
    I->>H: Start of Frame (database +7, loaded)
```

- **Frame numbers (ICD 2.3).** Each side numbers its own messages (`initial_frame_number`, +1,
  wrapping) and acknowledges the peer's last number. Gaps are counted as lost messages; a repeated or
  backward number is a *regression* (reordering, duplicate, or peer restart) and is reported in
  `ReceiveReport` and `SessionStats` for the facade to interpret. Peers older than 3.2 have one
  "Frame Counter" originated by the IG: the Host copies the IG's counter into IG Control.
- **Start-up (ICD 2.8).** `begin_frame()` writes the frame packet. While `enforce_startup_sequence`
  is on, `HostSession::add()` fails with `ErrorCode::NotOperational` until a Start of Frame reports
  Operate or Debug; `IgSession::add()` fails in Reset/Standby and Offline Maintenance. Synchronous
  hosts send once per received Start of Frame (`ready_to_send()`).
- **Mode and database.** The Host asks with `request_mode()` / `request_database(n)`. The IG
  application is asked through `on_mode_request` (once per change of the Host's request; the app
  calls `set_mode()` when the transition is complete) and `on_database_request` (Operate/Debug only,
  once per request); it reports with `set_mode`, `set_database(-n / n / -128)`. The Host keeps
  sending `n` until the IG reports `-n` (or `-128`), then sends 0 (ICD 4.1.1, Figure 38).
- **Receiving.** `receive()` parses, checks direction, updates state, then delivers through the
  session-owned `Dispatcher` (`handlers()`), frame packet included, after the session has processed
  it. The session walks the message with `dispatch<Set>` and a masking visitor that calls the new
  `Dispatcher::deliver()` (additive to T06), so no change to the dispatch walk was needed.
  IG, Reset/Standby: only IG Control is delivered, the rest counted in `ignored_packets`.
  IG, Offline Maintenance: nothing is delivered (the ICD says the IG ignores IG Control too;
  the brief said only non-control packets, the ICD wins).
- **Sending.** `begin_frame()`, `add()`, `finish_frame()` instead of a bare `MessageBuilder&`
  so `add()` can enforce the rules above. The buffer is session-owned
  (`max_message_bytes`); no allocation per frame. `validate()` runs on send only if
  `validate_outgoing` (default off, because ICD ranges such as yaw 0..360 reject common conventions).
- Sessions always **send** 3.3 (24-byte frame packets, native byte order). Only content is masked
  for the peer's version.

## Version masking (peer minor version, learned from its last message)

| Peer | On send (`add()`) | On receive (before the handler) |
|---|---|---|
| minor < 3 | Entity Control extrapolation bit and IG Control extrapolation flag cleared; Component Control classes 14, 15 refused (`UnsupportedVersion`) | Entity Control extrapolation bit cleared |
| minor < 2 | HAT/HOT Request update period = 0; Rate Control coordinate system = World/Parent | HAT/HOT Request update period = 0; HAT/HOT (Extended) Response host frame LSN = 0; Rate Control coordinate system = World/Parent; 16-byte frame packets (decoded by T02) |

Before the first message from the peer nothing is masked (assumed 3.3).

## For the facade (T09a)

The facade supplies the clock (`SessionConfig::clock`), the logger, and the transport; it reads
`ready_to_send()`, `operational()`, `ig_mode()`, `ig_database()`, `database_loading()`,
`last_start_of_frame()`, `peer()`, `stats()` and the `ReceiveReport` of each `receive()`
(`frame_regressed` is the IG-restart signal). Peer timeouts and `LinkState` are the facade's: they
need wall time, which the session deliberately does not track.

## Statistics

`SessionStats`: messages sent/received, lost frames, frame regressions, decode errors, ignored
packets (mode rules), unknown packets, rejected `add()` calls, refused mode requests.
