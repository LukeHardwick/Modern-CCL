# T09d — Facade: peer learning, sender filtering, small API gaps

Phase A follow-up · Depends on T09c, T09 (both merged first) · Branch `task/t09d-facade-peer`
Follow the shared rules in [README.md](README.md).

## Why (planner, 2026-10-12)
The T09 and T09c Opus reviews found gaps in the T09a facade. T09 worked around them in
`apps/demo_common/udp_link.cpp`; this task fixes them in the library so the apps can drop the
workarounds.

## Spec
ICD 3.3 §2.8 (start-up), §4.1.1 IG Control, §4.2.1 Start of Frame. Cite sections in comments.

## Work
1. **Peer learning hook.** `Ig::connect()` learns the Host from the first valid message, but
   `Ig::over(transport)` assumes the peer is always known, so a custom transport (pcap recorder,
   fault injection) must re-implement learning. Add an optional peer-learning interface on
   `DatagramTransport` (e.g. `latch_peer()` / `forget_peer()`, default no-op) that `IgCore`
   calls after the session **accepted** a message and on Host timeout / IG restart.
2. **Sender filtering.** After latching, datagrams from any other sender must be dropped and
   counted (stats), not dispatched, in `UdpDatagramTransport` and via the hook above. A
   restarted Host on a new port must be re-learned after the timeout. Same question for the
   Host side: a stray datagram on the Host's port must not count as a protocol error.
3. **Dispatch after acceptance** (`src/runtime/ig.cpp` ~147-161): `session_.receive()` delivers
   to handlers (incl. `on_user`) before `latch_peer()`; a refused sender's message has already
   reached the handlers. Accept the sender first, then dispatch.
4. **`Ig::on_ig_control`** (once per received IG Control), so apps don't poll
   `session().last_ig_control()` every frame.
5. **`Ig::local_port()` / `Host::local_port()`** (bound port; useful with ephemeral port 0).
6. **Test seam for the IgCore latch retry** (e.g. an injectable peer learner) and the missing
   "IgCore-level latch retry" test from T09a.
7. **Optional:** the T09c review's Standby tightening — skip the stale-restart rule in Standby
   only if no duplicate was seen in the stale run (closes a narrow missed-restart gap).
8. Port `apps/demo_common` (`UdpLink`, `DemoIg`) onto the new hooks and delete the duplicated
   learning logic; keep T09's tests passing.

## Acceptance
README rules 6–9; ≥ 95 % line coverage on new library code; T09's network tests unchanged and green.
