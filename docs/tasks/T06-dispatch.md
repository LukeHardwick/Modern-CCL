# T06 — Packet registry & dispatch

Phase A (MVP) · Depends on T03, T04, T05 · Branch `task/t06-dispatch`
Follow the shared rules in [README.md](README.md).

## Goal
Turn a `MessageView` into typed packets the application can handle, without the application writing
opcode switches. Replaces CCL's event/signal/callback/iteration processors with one C++23 mechanism
plus a runtime convenience layer.

## Spec
ICD 3.3 §2.6 (message structure; ordering note), §2.10 and §2.11 rule 3 (unknown packets are ignored),
§4.3 (user-defined opcodes 201–255 — registration itself is T17, but the design must allow it).

## Design (planner decision)
1. **Packet sets.** `template <Packet... Ps> struct PacketSet` (type list) with compile-time checks:
   unique opcodes, consistent directions. Provide `cigi::HostToIgPackets` and `cigi::IgToHostPackets`
   aliases containing every packet implemented so far (MVP: IgControl, EntityControl,
   ComponentControl, ArticulatedPartControl, RateControl, CelestialSphereControl, ViewControl,
   ViewDefinition, HatHotRequest / StartOfFrame, HatHotResponse, HatHotExtendedResponse). Users extend
   a set with their own packets (T17) via `PacketSet<..., MyPacket>` or a `concat` helper.
2. **Unknown packets.** `struct UnknownPacket { std::uint8_t opcode; std::span<const std::byte> bytes;
   std::size_t offset; }` (non-owning; valid while the datagram lives).
3. **Decode any.** `template <class Set> using AnyPacket = std::variant<Ps..., UnknownPacket>`;
   `decode_any<Set>(const RawPacket&) -> Result<AnyPacket<Set>>` using a constexpr 256-entry opcode →
   decoder table built from the set (O(1), no allocation). Direction rule: a packet in the set whose
   direction contradicts the message direction → `WrongDirection`.
4. **Compile-time dispatch (primary API).**
   `dispatch<Set>(const MessageView&, Visitor&&) -> DispatchStats` visits each packet in order with
   the visitor (an `overloaded{...}` of lambdas is the canonical usage; provide `cigi::overloaded`).
   Packets with no matching overload are skipped silently (use `requires` to detect callability).
   Errors (malformed header, decode failure) go to an optional error callback; behaviour after a
   header error = stop (iteration can't continue), after a body decode error = continue with the next
   packet. `DispatchStats` counts handled / ignored / unknown / errors.
5. **Runtime handler table (convenience).** `Dispatcher<Set>` with `on<P>(callable)`,
   `on_unknown(callable)`, `on_error(callable)`, `dispatch(view)`. Storage: one slot per packet type
   in the set (a `std::tuple` of `std::function<void(const P&)>` is acceptable — allocation happens at
   registration, never during dispatch; state this in the docs). Rationale: replaces CCL's
   per-packet event processor registration.
6. `MessageView` stays independent of dispatch (no new includes in view.hpp).

## Tests
Opcode table correctness for every packet in both sets; mixed message with all MVP packets plus an
unknown opcode and a user-range opcode → correct order/types; visitor with partial overloads;
wrong-direction packet; body decode error continues, header error stops; Dispatcher registration
and replacement; `DispatchStats`; constexpr `decode_any` test where feasible; zero allocations during
`dispatch` (count with a replaced global `operator new` in the test binary).

## Acceptance
README rules 6–9. Add a section "Handling incoming packets" to
`docs/design/03-packet-and-message-api.md` and update `examples/` with a `dispatch_example.cpp`.
