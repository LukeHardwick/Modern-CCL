# T09c — Typed user packets in the facade; examples on the facade

Post-release follow-up · Depends on T09a, T17 · Branch `task/t09c-facade-examples`
Follow the shared rules in [README.md](README.md).

## Why
An audit of the examples (2026-10-12) found that only `examples/host_minimal.cpp` uses the T09a
facade (`cigi::Host` / `cigi::Ig`). Two examples simulate a Host/IG by hand where a user would
really use the facade, and the facade cannot yet receive a user-defined packet as a typed struct
(T17 deferred this: user opcodes reach only `on_unknown`).

## Spec
ICD 3.3 §2.10, §4.3 (user-defined packets: 201–255, size 4 + 4n, multiple of 8; configurable to
ignore). Start-up exchange: §2.8, §4.1.1 IG Control, §4.2.1 Start of Frame.

## Work
1. **`on_user<P>` on `Host` and `Ig`.** Constrained to `UserDefinedPacket<P>` with the direction the
   side receives (`IgToHost`/`Both` for Host, `HostToIg`/`Both` for Ig). No session templating
   (keep T07's API). Suggested design (from the T09a review): a fixed `std::array<handler, 55>`
   indexed by `opcode - 201`; an internal `on_unknown` decodes `P` from the unknown packet's bytes
   in the peer's byte order and calls the handler, otherwise falls back to the user's `on_unknown`.
   Registering twice for one opcode replaces the handler (document it). Decode errors go to
   `on_error`. `config.session.ignore_user_packets` still wins (nothing is delivered). No
   allocation on the receive path once handlers are registered.
   Tests: typed receive in both directions and both byte orders, fallback to `on_unknown` for an
   unregistered user opcode, ignore switch, malformed user packet → `on_error`, wrong-direction
   packet rejected at compile time (constexpr predicate), steady-state no-allocation.
2. **Rewrite `examples/user_packet.cpp` on the facade:** a `Host` and an `Ig` over
   `make_memory_transport_pair()` exchange a custom packet (`HostFrame::add` to send, `on_user<P>`
   to receive), then switch user packets off. Keep it short (≤ 90 lines).
3. **Port `examples/wireshark/cigi_frames.cpp` to the facade:** a real `Host` and `Ig` over the
   in-memory transport, with a small recording `DatagramTransport` decorator that writes every
   datagram through `cigi::tools::PcapWriter`, so the pcap shows the start-up exchange the library
   actually produces. Keep the output deterministic (fixed timestamps/seed). Update
   `examples/wireshark/README.md` and the T10 tshark expectations (`check_pcap.sh` / golden files)
   to match; tshark must still decode every packet without malformed-packet warnings.
4. **Low-level examples stay low-level:** add a one-line comment to `examples/minimal_roundtrip.cpp`
   and `examples/dispatch_example.cpp` pointing to `host_minimal.cpp` as the usual way to write a
   Host. Document `on_user<P>` in `docs/guide/host-quickstart.md` and in the "User-defined packets"
   section of `docs/design/03-packet-and-message-api.md` (replace the "deferred" note).

## Acceptance
README rules 6–9; ≥ 95 % line coverage on new code; `check_pcap.sh` passes.
