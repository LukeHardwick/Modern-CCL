# T05 — MVP query packets: HAT/HOT Request, HAT/HOT Response, HAT/HOT Extended Response

Phase A (MVP) · Depends on T02b · Parallel with T03, T04, T08 · Branch `task/t05-mvp-hat-hot`
Follow the shared rules in [README.md](README.md).

## Goal
The first request/response pair: the Host asks for Height Above Terrain / Height Of Terrain and the IG
answers. This gives the MVP an IG→Host data packet besides Start of Frame.

## Spec
ICD 3.3 §4.1.24 HAT/HOT Request (txt ≈ 8472–8654), §4.2.2 HAT/HOT Response (≈ 11914–12026),
§4.2.3 HAT/HOT Extended Response (≈ 12026–12178), §2.3 Frame Numbering (Host Frame Number LSN).
Appendix B: 3.2 added Update Period (request) and Host Frame Number LSN (responses) — reserved in
3.0/3.1, so they decode as 0 from legacy peers.

## Planner-verified layouts (bit 0 = LSB; verify enum values)
**HAT/HOT Request** (24, 32 B, Host→IG): 2–3 HAT/HOT ID u16 · byte 4: bits 7–3 reserved, bit 2
Coordinate System (geodetic / entity), bits 1–0 Request Type (2-bit: HAT, HOT, Extended) · 5 Update
Period u8 (0 = one-shot; n = every n frames) · 6–7 Entity ID u16 · 8 Latitude/X Offset f64 ·
16 Longitude/Y Offset f64 · 24 Altitude/Z Offset f64.

**HAT/HOT Response** (102, 16 B, IG→Host): 2–3 HAT/HOT ID u16 · byte 4: bits 7–4 Host Frame Number
LSN (4-bit), bits 3–2 reserved, bit 1 Response Type (HAT / HOT), bit 0 Valid · 5–7 reserved ·
8 Height f64.

**HAT/HOT Extended Response** (103, 40 B, IG→Host): 2–3 HAT/HOT ID u16 · byte 4: bits 7–4 Host Frame
Number LSN, bits 3–1 reserved, bit 0 Valid · 5–7 reserved · 8 HAT f64 · 16 HOT f64 · 24 Material Code
u32 · 28 Normal Vector Azimuth f32 · 32 Normal Vector Elevation f32 · 36–39 reserved.

## Scope
1. `include/cigi/packets/{hat_hot_request,hat_hot_response,hat_hot_extended_response}.hpp`,
   table-driven; enums `HatHotRequestType`, `HatHotCoordinateSystem`, `HatHotResponseType` with
   `EnumTraits`; ranges from the ICD (lat/lon when geodetic, azimuth/elevation ranges).
2. A tiny constexpr helper `host_frame_lsn(std::uint32_t host_frame) -> std::uint8_t` (low 4 bits) in
   `packets/common.hpp` or a new `packets/frame_lsn.hpp`, with tests; the sessions task will use it.
3. Append to `include/cigi/packets.hpp` (create if absent; alphabetical).
4. Tests per README rule 6, including request in a Host message and responses in an IG message
   built with `MessageBuilder` and read back with `MessageView`.

## Out of scope
Line-of-sight packets (→ T14/T15). Periodic-response logic (→ T09 mock IG).

## Acceptance
README rules 6–9.
