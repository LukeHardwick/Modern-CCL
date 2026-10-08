# T07 — Host / IG sessions

Phase A (MVP) · Depends on T06 · Branch `task/t07-sessions`
Follow the shared rules in [README.md](README.md).

## Goal
Encapsulate per-session protocol state so applications don't hand-manage frame numbers, the IG mode
start-up sequence, byte order or peer version. Transport-agnostic: sessions consume and produce byte
spans; T08 moves them over UDP.

## Spec
ICD 3.3 §2.1 Sessions, §2.2 synchronous/asynchronous operation, §2.3 Frame Numbering (and the 3.0/3.1
"Frame Counter" note), §2.8 Startup and Shutdown, §2.11 versions, §4.1.1 and §4.2.1 parameter
semantics (Database Number handshake, Timestamp Valid rules), §4.2.2/4.2.3 Host Frame Number LSN.

## Design (planner decision)
- New library target **`cigi_session`** (`cigi::session`), depends on `cigi` and **spdlog** (FetchContent,
  latest release at least two weeks old; verify the tag with `git ls-remote`, pin URL + SHA256; build
  with `SPDLOG_NO_EXCEPTIONS` and `SPDLOG_USE_STD_FORMAT`; the core `cigi` target stays
  dependency-free). Logger is injectable (`std::shared_ptr<spdlog::logger>`; default = null sink).
- `struct SessionConfig { bool synchronous = true; bool enforce_startup_sequence = true; ... }`.
- Time source injectable (`std::function<Timestamp()>` or a clock template parameter) so tests are
  deterministic; default derives 10 µs ticks from `std::chrono::steady_clock` (wrapping).
- **HostSession**
  - `begin_frame() -> Result<MessageBuilder&>`: writes IG Control with host frame number + 1, last IG
    frame number, timestamp/valid, requested IG mode, database number, extrapolation flag; the app then
    `add()`s packets; `finish_frame() -> std::span<const std::byte>`.
  - Enforced start-up (§2.8, when enabled): until a Start of Frame reports Operate (or Debug), adding
    any packet other than IG Control returns a new `ErrorCode::NotOperational`.
  - Synchronous mode: `ready_to_send()` is true once per received SOF; async mode: always true.
  - `receive(std::span<const std::byte>) -> Result<void>`: parse, require IG→Host, record peer
    version/byte order/IG mode/database/status, update last IG frame, detect gaps in IG frame numbers
    (count lost frames), then dispatch the remaining packets to the application's handlers
    (`Dispatcher<IgToHostPackets>` owned by the session, exposed via `handlers()`).
  - Database handshake helper: `request_database(n)`; when SOF reports −n (loading) the session sets
    the IG Control database number back to 0 (§4.1.1).
  - Legacy peer (minor < 2): Frame Counter semantics — Host copies the IG's counter into its IG Control.
- **IgSession** (mirror)
  - `begin_frame()` writes Start of Frame (IG frame + 1, last host frame, mode, status, database,
    ERM flag, timestamp); `finish_frame()`.
  - `receive()`: require Host→IG; update last host frame (gap detection), apply mode requests through
    an app callback (`on_mode_request(IgMode) -> bool accept`); the app calls `set_mode()` when the
    transition completes. In Reset/Standby and Offline Maintenance, packets other than IG Control are
    ignored and counted (§2.8, §4.2.1); in Operate/Debug they're dispatched to handlers.
  - `host_frame_lsn()` for responses (helper from T05).
- `SessionStats`: messages sent/received, lost frames, decode errors, ignored packets, unknown packets.
- Not thread-safe by design (one session per thread); document it.

- **Version masking (review notes from T03–T05).** Packet decode is version-agnostic, so the
  session masks by the peer's negotiated minor version:
  - Receiving from a peer with minor < 2: treat HAT/HOT `update_period` and `host_frame_lsn` as 0
    (reserved in 3.0/3.1), like IG Control's `last_ig_frame_number`.
  - Sending to a peer with minor < 3: clear 3.3-only content (Entity Control extrapolation bit;
    Component Classes 14/15 are not sent — return/log an error). Minor < 2: clear Rate Control's
    coordinate-system bit and HAT/HOT update period. List exactly what is masked in the design doc.
- **Dispatch (T06 review note):** the session handles the frame packet itself and must hand only the
  remaining packets to the application's `Dispatcher` (and, for `IgSession` in Reset/Standby, only
  IG Control). Filter in a session-side wrapper or add an offset/iterator-start `dispatch` overload
  in `dispatch.hpp` — choose and document. `UnknownPacket` data is only guaranteed for the handler
  call because the receive buffer is reused.
- **No automatic `validate()` on send by default** (`SessionConfig::validate_outgoing = false`).
  Strict ICD ranges (e.g. yaw 0..360) would otherwise reject common host conventions (yaw
  −180..180). When enabled, failures are returned from `add()`.

## Tests
Deterministic scripted exchanges between a HostSession and an IgSession wired back-to-back in memory:
full start-up (Standby → Operate), mode refusal, database load handshake, frame numbering per §2.3
Figure 6, lost-message detection, sync vs async gating, NotOperational enforcement on/off, foreign
byte order peer, legacy 3.0/3.1 peer (16-byte frame packets), wrap-around of u32 frame numbers and
timestamps, IG ignoring non-control packets in Standby, logging via a test sink.

## Acceptance
README rules 6–9. Add `docs/design/04-sessions.md` (≤ 2 pages, with a sequence diagram in Mermaid).
