# T17 — User-defined packets (opcodes 201–255)

Phase B · Depends on T06 · Branch `task/t17-user-defined-packets`
Follow the shared rules in [README.md](README.md).

## Spec
ICD 3.3 §2.10 Interface Extensions, §4.3 User-Defined Packets (size 4 + 4n, multiple of 8, ≥ 8;
identifying data in the first two 32-bit words; devices should be configurable to ignore all
user-defined packets).

## Design (planner decision)
- `concept UserPacket<P>` = `Packet<P>` + opcode in 201–255 (compile-time checked) + direction
  `HostToIg`, `IgToHost` or `Both`. Users write user packets exactly like standard ones: a field
  table for fixed layouts (which also gives correct byte swapping for free — the reason CCL needed
  user-supplied swap code), or custom codecs for variable layouts.
- Registration = extending a packet set: `using MyHostPackets = cigi::extend_t<cigi::HostToIgPackets,
  MyPacket>`; dispatch and sessions accept any set (sessions become templates on the set or take it
  as a parameter — choose the least intrusive change to T07's API and document it).
- `SessionConfig::ignore_user_packets` (default false) per §4.3.
- Validation: builder rejects user packets whose size violates §4.3.
- `examples/user_packet.cpp` showing a Host and IG exchanging a custom packet.

## Tests
Opcode-range compile-time rejection (negative static_assert via a constexpr predicate), duplicate
opcode in an extended set rejected at compile time, round-trip in both byte orders, dispatch to a
user handler, ignore-user-packets mode, unknown user opcode still reaches `on_unknown`.

## Acceptance
README rules 6–9. Update the design doc with a "User-defined packets" section.
