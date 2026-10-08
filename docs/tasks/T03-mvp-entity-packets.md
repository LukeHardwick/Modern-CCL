# T03 — MVP entity packets: Entity Control, Articulated Part, Rate Control, Component Control

Phase A (MVP) · Depends on T02b · Parallel with T04, T05, T08 · Branch `task/t03-mvp-entity-packets`
Follow the shared rules in [README.md](README.md).

## Goal
The packets a Host uses to place and animate models. All are fixed-size and Host→IG, so all are
table-driven (no custom codecs) except where noted.

## Spec
ICD 3.3: §3.1 Entities, §3.4 Coordinate Systems (context), §4.1.2 Entity Control (txt ≈ 2763–3355),
§4.1.4 Component Control (≈ 3454–3898), §4.1.6 Articulated Part Control (≈ 4046–4307),
§4.1.8 Rate Control (≈ 4477–4692). No errata affect these packets. Compare the 3.2 ICD for fields
that are reserved in older minors (Entity Control extrapolation flag; Rate Control coordinate system).

## Planner-verified layouts (bit 0 = LSB; verify every enum value list in the ICD tables)
**Entity Control** (opcode 2, 48 B): 2–3 Entity ID u16 · byte 4: bit 7 reserved, bits 6–5 Ground/Ocean
Clamp (2-bit), bit 4 Inherit Alpha, bit 3 Collision Detection Enable, bit 2 Attach State, bits 1–0
Entity State (2-bit) · byte 5: bits 7–5 reserved, bit 4 Linear Extrapolation/Interpolation Enable
(3.3 only), bits 3–2 Animation State (2-bit), bit 1 Animation Loop Mode, bit 0 Animation Direction ·
6 Alpha u8 · 7 reserved · 8–9 Entity Type u16 · 10–11 Parent ID u16 · 12 Roll f32 · 16 Pitch f32 ·
20 Yaw f32 · 24 Latitude/X Offset f64 · 32 Longitude/Y Offset f64 · 40 Altitude/Z Offset f64.
(Lat/Lon/Alt are geodetic for top-level entities, X/Y/Z offsets in the parent's frame when attached.)

**Component Control** (4, 32 B): 2–3 Component ID u16 · 4–5 Instance ID u16 · byte 6: bits 7–6
reserved, bits 5–0 Component Class (6-bit) · 7 Component State u8 · 8–31 Component Data 1–6 (`word`,
six u32, each byte-swapped as a u32 — ICD 2.7.1, §4.1.4 Figs. 44–46).

**Articulated Part Control** (6, 32 B): 2–3 Entity ID u16 · 4 Articulated Part ID u8 · byte 5: bit 7
reserved, bit 6 Yaw Enable, 5 Pitch Enable, 4 Roll Enable, 3 Z Offset Enable, 2 Y Offset Enable,
1 X Offset Enable, 0 Articulated Part Enable · 6–7 reserved · 8 X Offset f32 · 12 Y · 16 Z ·
20 Roll · 24 Pitch · 28 Yaw (all f32).

**Rate Control** (8, 32 B): 2–3 Entity ID u16 · 4 Articulated Part ID u8 · byte 5: bits 7–2 reserved,
bit 1 Coordinate System (3.2+), bit 0 Apply to Articulated Part · 6–7 reserved · 8 X Linear Rate f32 ·
12 Y · 16 Z · 20 Roll Angular Rate · 24 Pitch · 28 Yaw (f32).

## Scope
1. Headers `include/cigi/packets/{entity_control,component_control,articulated_part_control,rate_control}.hpp`
   with value structs, `fields` tables, `layout_ok` asserts, and `EnumTraits` for every enum
   (e.g. `EntityState`, `AttachState`, `GroundClamp`, `AnimationDirection`, `AnimationLoopMode`,
   `AnimationState`, `ComponentClass` (values per §4.1.4 table, including the 3.3 Symbol Surface and
   Symbol classes), `RateCoordinateSystem`). Use `.range()` where the ICD restricts values (e.g.
   angles: roll/yaw −180..180, pitch −90..90; latitude −90..90, longitude −180..180 — only when the
   field is geodetic; document how validate() handles the attached case).
2. **Component data helpers** (`include/cigi/packets/component_data.hpp`): a small API so users never
   pack the six words by hand, per §4.1.4 Figs. 44–46: set/get a u32, i32 or f32 in word *i*; a double
   or u64 across words *i, i+1*; two u16 or four u8 in one word — all arranged so the receiver's
   32-bit word swap preserves the values. Derive the word order for 64-bit values from the ICD figure
   and document it. Round-trip tests across both byte orders are mandatory (this is the classic
   corruption bug the ICD warns about).
3. Add every packet to a single header `include/cigi/packets.hpp` that includes all packets
   (create it; T04/T05 will append — keep it alphabetical to reduce merge conflicts).
4. Tests per README rule 6, plus a legacy-peer test: a 3.2-style Entity Control (extrapolation bit
   0, as §2.11 rule 2 requires senders to zero reserved bits) decodes with
   `extrapolation_enabled == false`. No version context is passed to packet decode; senders zeroing
   reserved bits is what makes older minors decode correctly. Do not change the Packet concept.

## Out of scope
Short Component Control (5), Short Articulated Part (7), Conformal Clamped (3) → T13. Dispatch → T06.

## Acceptance
README rules 6–9. Report must list each enum with its ICD value table and any ICD ambiguity.
