# T04 — MVP view & time packets: View Control, View Definition, Celestial Sphere Control

Phase A (MVP) · Depends on T02b · Parallel with T03, T05, T08 · Branch `task/t04-mvp-view-celestial`
Follow the shared rules in [README.md](README.md).

## Goal
Let a Host position cameras, set their frustum, and set time of day.

## Spec
ICD 3.3: §3.2 Views (viewing volumes, view groups), §4.1.9 Celestial Sphere Control (txt ≈ 4692–4869),
§4.1.16 View Control (≈ 6539–6811), §4.1.21 View Definition (≈ 7660–7947). Appendix B: in 3.3 the
Celestial Sphere Hour/Minute are **UTC** (were local time before 3.3) — document on the struct.

## Planner-verified layouts (bit 0 = LSB; verify enum values in the ICD tables)
**Celestial Sphere Control** (9, 16 B): 2 Hour u8 · 3 Minute u8 · byte 4: bits 7–5 reserved, bit 4
Date/Time Valid, 3 Star Field Enable, 2 Moon Enable, 1 Sun Enable, 0 Ephemeris Model Enable ·
5–7 reserved · 8–11 Date u32 (ICD defines the MMDDYYYY decimal encoding — provide a typed helper
to/from `std::chrono::year_month_day`) · 12 Star Field Intensity f32 (percent, range per ICD).

**View Control** (16, 32 B): 2–3 View ID u16 · 4 Group ID u8 · byte 5: bits 7–6 reserved, bit 5 Yaw
Enable, 4 Pitch Enable, 3 Roll Enable, 2 Z Offset Enable, 1 Y Offset Enable, 0 X Offset Enable ·
6–7 Entity ID u16 · 8 X Offset f32 · 12 Y · 16 Z · 20 Roll · 24 Pitch · 28 Yaw.

**View Definition** (21, 32 B): 2–3 View ID u16 · 4 Group ID u8 · byte 5: bits 7–6 Mirror Mode (2-bit),
5 Bottom Enable, 4 Top Enable, 3 Right Enable, 2 Left Enable, 1 Far Enable, 0 Near Enable · byte 6:
bits 7–5 View Type (3-bit, IG-defined), bit 4 Reorder, bit 3 Projection Type, bits 2–0 Pixel
Replication Mode (3-bit) · 7 reserved · 8 Near f32 · 12 Far · 16 Left · 20 Right · 24 Top · 28 Bottom.

## Scope
1. `include/cigi/packets/{celestial_sphere_control,view_control,view_definition}.hpp`, table-driven,
   `EnumTraits` for `MirrorMode`, `PixelReplicationMode`, `ProjectionType`, `Reorder` (or bool, per
   ICD semantics) etc.; `.range()` from the ICD (hour 0–23, minute 0–59, star intensity, angles).
   Note in `validate()` docs any cross-field rules the ICD states (e.g. near < far) — implement
   cross-field rules in a custom `validate()` that calls `validate_fields` first.
2. Date helper for Celestial Sphere Control with tests (round-trip, invalid dates rejected by
   `validate()` when Date/Time Valid is set).
3. Append to `include/cigi/packets.hpp` (create it if T03 hasn't merged yet; keep alphabetical).
4. Tests per README rule 6.

## Out of scope
Sensor Control, Motion Tracker (→ T14). Dispatch (→ T06).

## Acceptance
README rules 6–9.
