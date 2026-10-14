# T09e — T09a facade follow-ups (small fixes)

Phase A follow-up · Depends on T09a ✅ · Runs beside T09d · Branch `task/t09e-facade-followups`
Follow the shared rules in [README.md](README.md).

## Why (planner, 2026-10-14)
The T09a review listed small follow-ups (STATUS.md, "T09a — merged"). The two that touch the IG
latch path (IgCore-level latch-retry test, the "Host connected" log on every failed latch attempt
in `src/runtime/ig.cpp`) moved to **T09d**, which is rewriting that code. This task does the rest.
Keep the diff small: T09d runs at the same time and merges first; this branch rebases onto it.

## Work
1. **Standby false positive** (`src/runtime/host.cpp`, the stale/duplicate restart rule with
   `saw_stale_`): a Host-requested Standby plus one reordered stale frame before the IG's first
   Standby SOF counts as an IG restart and replays the database. Skip the restart rule while the
   requested IG mode is Standby (ICD 4.1.1 IG Mode, 4.2.1 Start of Frame, §2.8 start-up). Test
   the exact sequence (request Standby → stale frame → Standby SOF: no restart, no replay), plus
   the existing restart cases still detected in Operate.
2. **`FaultInjectingTransport`** (`include/cigi/fault_transport.hpp`): document that a failing
   `send()` returns its own error and drops (while still counting) the deferred datagram. Add a
   test that pins this behaviour.
3. **`examples/host_minimal.cpp:19`**: the comment says "a plain flag is async-signal-safe". Fix
   it (only `volatile std::sig_atomic_t` or a lock-free `std::atomic` is; check what the code
   actually uses and make the comment and code agree).
4. **Missing tests:** `-n` (frame-count limit) after the session went Lost; `request_database`
   while Lost (what the facade does, as documented, with a test).

## Acceptance
README rules 7–9 (push the task branch to `origin` when done; do not merge). No public API
changes beyond doc comments unless a test shows one is needed (then report it). Coverage of the
changed code ≥ 95 %.
