# 05 — Cessna fidelity: why it looks cartoonish, how real sim aircraft are built, and the gates

Task T22e, Phase 0 (research and proposal only, nothing built). Written 2026-10-14 on branch
`task/t22e-cessna-hifi` (based on T22d `4f12671`). Every number marked **measured** comes from the
committed `cessna.glb` (read with `tools/models/glb_io.py`, model frame: metres, nose at -y);
numbers marked **estimate** are reasoned guesses that Phase 1 or CP1 must confirm.
Everything in section (d) is a **proposal**: the user decides.

## (a) Gap analysis: every cartoonish cue

Looked at: `docs/guide/img/models/cessna_34front.png`, `_side`, `_front`, `_closeup`.

| # | Cue | What we see / measure | Why it reads as a toy |
|---|---|---|---|
| 1 | **No textures at all** | 6 materials (`paint`, `dark`, `metal`, `light`, `glass`, `hud`), no UV coordinates in the file (**measured**: only POSITION, NORMAL, COLOR_0). The airframe is one flat off-white; the cheat line is the only paint feature. | Real aluminium skin shows panel lines, rivets, oil and exhaust stains, gloss variation. Flat colour is the strongest "game asset" signal. |
| 2 | **No panel lines or rivets** | 0 triangles of either (T22d deferred list). Skin is one 26.4 m² paint primitive on the fuselage (**measured**) with no seams. | Even a hint of lines every ~0.5 m changes the read from "plastic" to "metal". |
| 3 | **Blobby nose** | Cowl front section at y = -2.24: 0.66 m wide by 0.57 m high, superellipse exponent 2.4 (almost an ellipse); the section walls then round straight into the spinner (**measured** from `FUS_KEYS`). The nose reads as a dome. The real cowling has a distinct front face (a ring around the spinner) with a crisp lip and two air inlets set into it. | Round bowl plus round spinner is the classic toy-aeroplane face. |
| 4 | **Soft edges everywhere** | Hard creases (> 30 degrees between neighbouring faces) are 59 m of 1 914 m of fuselage edge (3.1 %), 19 m of 477 m on a wing (3.9 %), 4.8 m of 113 m on the propeller (**measured**). Doors and control surfaces have creases, but they are modelled as zero-width cuts. | Real aircraft are a mix of smooth skin and sharp, small-radius edges (control surface edges, door frames, cowl lip). |
| 5 | **Razor-sharp, zero-thickness trailing edges** | Aileron and flap trailing edges measure 0 mm thick (**measured**). Real ones are about 1-3 mm (estimate) and catch a thin highlight. | Light cannot catch a zero-width edge; control surfaces look like paper. |
| 6 | **Window and door detail** | Windows are flat glass patches with no frame, seal or visible thickness; door gaps are 4 mm (`DOOR_GAP`) but invisible at distance because gap interiors are not dark-shaded. Door handles are tiny dots. | No frames means no "window"; reads as a decal. |
| 7 | **Missing small parts** (T22d list) | Wing-root fairings, steps, stall-warning vent, elevator trim tab, fuel-tank vents, cowl flap and oil door outlines, landing-light lens, second cheat-line colour, painted propeller tips. The propeller has two 3 000-triangle blades but plain grey, no tip paint or decals. | Their absence leaves big smooth empty areas that look unfinished. |
| 8 | **Wheel fairings too clean and bulbous** | Closed, teardrop-like shells with no bolts, no split line, no brake line. | Reads as "pod", not as a fibreglass part. |
| 9 | **Everything is the same gloss** | Material roughness 0.55 on all `paint`, metallic 0. No variation between glossy paint, matt de-ice boots, dull rubber, bare metal strut, polished spinner. | Uniform specular = plastic. |
| 10 | **Preview lighting** (EEVEE, one flat sky colour, flat green plane, soft contact shadow only) | No sky model, no reflections; reflective glass looks grey. | Even good geometry looks plastic without reflections; also makes it hard to judge fidelity. (Fix: Cycles with a procedural sky; see (d) visual review.) |
| 11 | **Detail not matched to distance** | Exterior shape error is already about 1 mm (audit gate) but nothing exists at 1-5 mm scale; a viewer at 3-8 m sees large smooth faces. | The T22c audit rewards smoothness against its own reference and cannot ask for detail. |

Not cartoonish (keep): overall proportions are within the published size ±2 %; NACA 2412 wing
with washout and dihedral; struts, gear, fuselage lofting; interior (8.5 k cockpit triangles).

## (b) How high-fidelity sim aircraft are built (short research)

Sources: MSFS 2024 SDK documentation, "Aircraft Modeling General Principles" and "Airframe
Textures and Materials" (<https://docs.flightsimulator.com/msfs2024/retail/models-and-textures/modeling/aircraft/aircraft-modeling-general-principles/>
and <https://docs.flightsimulator.com/msfs2024/retail/models-and-textures/modeling/aircraft/airframe/airframe-textures-and-materials/index.md>),
read 2026-10-14; X-Plane developer pages (<https://developer.x-plane.com/article/normal-maps>,
<https://developer.x-plane.com/2011/07/making-specularity-work-for-you/>). The MSFS pages are long;
what follows is what I could read directly, and anything I could not read is flagged.

- **Map set.** MSFS uses three maps per material: albedo (`ALBD`), a packed `COMP` map for
  ambient occlusion, roughness and metallic, and a tangent-space normal map (`NORM`); the SDK
  page confirms COMP is "AO/Roughness/Metal". The channel order (occlusion in red, as quoted by
  community posts) I could not confirm from the SDK text. X-Plane 11 uses albedo, a normal map and
  a gloss map, plus a separate night (emissive) map, all on **one UV set**. glTF's own packed map
  (`occlusionTexture` R, `metallicRoughnessTexture` G = roughness, B = metallic) is the
  equivalent and is what raylib's glTF loader reads.
- **Texture size and texel density.** The SDK says a small-to-medium aircraft should fit its whole
  length on **one 2K (2048 px) sheet**, with decals for fine detail and livery; large airliners
  reach about 5 000 px along their length. That is about 250 px/m for our 8.28 m aircraft, about
  4 mm per pixel. It also says a 4096 texture should not be used except for flat albedo/normal
  optimisation. A typical total texture budget including cockpit is about 150 MB (small aircraft)
  to 600 MB (large); KTX2 on-disk size is roughly GPU size.
- **Geometry vs maps.** MSFS 2024 states the engine "prefers mesh over texture resolution": if a
  detail has a silhouette, model it; if it sits in a recess and does not change the outline,
  bake it to the normal map. Normal maps are recommended for rivets, micro relief and
  dense small detail (screws, cabling).
- **Exterior budgets.** MSFS 2024 upper limits (LOD0): 1.5 million triangles, 600 materials,
  250 000 vertices per the SDK table; the SDK says to stay "well below". A secondary source
  (forum relaying the 2020 SDK) says about 300 000 triangles for a medium exterior. Our budget
  (exterior 100 k, total 250 k) is already well inside; even 60 k exterior triangles is in
  line with high-quality general-aviation add-ons.
- **Bevels.** The MSFS docs read did not give a numeric bevel size. Common practice (hard-surface
  modelling, stated here as general knowledge, not a cited source): small bevels of 1-5 mm on
  panel and control-surface edges, either geometric or baked into the normal map from a high-poly
  mesh; the mesh bevel is kept only where it changes the silhouette, otherwise the bake carries it.
- **X-Plane.** I found no official numbers for triangle or texture budgets; X-Plane advice is about
  normal maps being uncompressed and costly (so keep them to the sizes that matter), one UV set
  for all maps, and gloss maps in place of the older specular level.

What we take from it: a **2K-class atlas, three maps, small bevels, geometry for silhouette,
normals for micro-detail, tiling/decal detail for what the atlas cannot resolve**. Our
panel-line width (about 1 mm) is below one texel at 2K density (about 2.5-4 mm), so the lines are
rendered as soft normal-map grooves a few pixels wide plus an AO darkening; that is what the sims do too.

## (c) Reference three-view candidates (PROPOSALS: nothing downloaded)

Checked on Wikimedia Commons file pages on 2026-10-14. A drawing is only a guide to the **shape**;
none states the model year, and the best-known 172R/S drawings from Cessna's own manuals are
copyrighted and excluded. All numbers about accuracy are the planner's to judge from the pictures.

| | File | Author | Licence | Resolution / format | Views | Notes |
|---|---|---|---|---|---|---|
| **A** (recommended to look at first) | [File:Cessna 172 Skyhawk line drawing.svg](https://commons.wikimedia.org/wiki/File:Cessna_172_Skyhawk_line_drawing.svg) | Werneuchen (own work, Feb 2008) | **Public domain** (PD-self, worldwide) | SVG, nominal 1 800 x 1 200 px (vector, so any scale) | "3-plan view" line drawing (view names not stated; check) | Clean vector, no copyright strings attached. Variant unknown. |
| **B** | [File:Cessna 172.svg](https://commons.wikimedia.org/wiki/File:Cessna_172.svg) | Marek Cel ("Cel 84"), own work, 28 Nov 2019 | **CC0 1.0** | SVG, nominal 496 x 567 px (vector) | Three-view | Smaller nominal canvas; vector so resolution is not the limit, but accuracy depends on its trace. |
| **C** | [File:Cessna 172 3-view line drawing.png](https://commons.wikimedia.org/wiki/File:Cessna_172_3-view_line_drawing.png) | Noha307 (upload), cropped from the 1956 Cessna 172 Owner's Manual | **Public domain in the US only** (defective copyright notice); explicitly **not** PD in Canada, Germany, Mexico | PNG 1 202 x 1 751 | Three-view | It shows a **1956** 172 (straight fin, no rear window), not the 172R/S. Licence is territorial: **not recommended**. |
| D (not recommended) | [File:Cessna P172D Powermatic 3-view line drawing.svg](https://commons.wikimedia.org/wiki/File:Cessna_P172D_Powermatic_3-view_line_drawing.svg) | listed on Commons search; details not read | not read | SVG | Three-view | A 1960s variant, again not the R/S. Listed only for completeness. |

Honest caveat: all candidates are probably older-variant or generic outlines. The 172R/S differs
from the 1960s-70s aircraft in the cabin (rear window, door and windscreen shape) and the engine
cowling. The gate should therefore weight the **length, span and height positions that all variants
share** (fin top, wing position, tailplane, spinner, gear) and be tolerant where variants differ
(cabin glazing). Please look at A and B and say whether either is close enough; the
alternative is a user-supplied photo-derived trace.

## (d) Fidelity gates F1-F5 (PROPOSED): how each is measured, values, reasons

All gates are computed by one script (`tools/models/fidelity_audit.py`, built in Phase 1) that
reads the `.glb` directly (like the T22c audit), so a result is a pure function of the file and
the committed reference data. The same input gives the same numbers on every run.
Values are **proposals calibrated on reasoning, not yet on data**: Phase 1 measures the T22d
baseline and the noise floor (reference compared with itself), then these numbers are fixed by the user
before CP1. The machine-readable list is `tools/models/audit/cessna_fidelity.toml`.

**F1 Outline vs reference.** Render the exterior triangles (all nodes except `cockpit`,
`prop_disc`) as orthographic silhouettes (side, top, front) by rasterising the triangles in
numpy on a fixed pixel grid (2 px per cm of aircraft). The reference drawing is converted once,
by a committed script, into three binary silhouette masks plus a TOML of landmark points
(spinner tip, wheel contact points, fin top, wing tips); the masks are committed so the gate
never depends on an SVG renderer. Scaling and placement use the **published length, span and
height and two named landmarks**, not best-fit, so errors cannot be fitted away.
Measured: intersection-over-union (IoU), and outline distance (the distance from each point of the
model outline to the nearest reference outline point, p95 and max in mm), plus ten named stations
(cowling front, windscreen base, wing leading edge at root and tip, fin top, rudder trailing edge,
tailplane tip, spinner tip, nose-wheel centre, tail-wheel-less belly line).
Proposed: IoU side >= 0.94, top >= 0.94, front >= 0.90; outline p95 <= 30 mm, max <= 80 mm;
stations +-40 mm. Why: a hand-traced 1 800 px drawing has about 5 mm/pixel and a trace error of
2-3 pixels, so about 15 mm noise; the cabin and cowl differ between variants; the front view
includes struts and wheel pods and is the least reliable. Likely baseline: the current model
fails the nose and cowl stations.

**F2 Feature checklist.** A TOML list of about 40 features of a real 172 (the draft is in the
proposal file: panel lines, rivet rows, window frames, door gaps, steps, wing-root fairings, trim
tab, vents, lights with lenses, prop-tip paint, gauge faces, ...). Each feature has a *kind*:
`geometry` (a named probe box in the model frame must contain at least N triangles of the right
material and a relief of at least h mm, so a feature is not "claimed" by a name), or `texture`
(through the model's UVs, the probe region on the normal map must contain at least N groove
pixels, i.e. normal tilt > 0.1, or a distinct colour). Proposed: **every required feature (about
28) present, at least 75 % of the optional ones (about 12)**. Why: names alone prove nothing;
the probe requires real data. The probe boxes can only be filled once each feature is modelled,
so CP2 completes them; Phase 1 builds the checker and tests it.

**F3 Section shape.** Cut the fuselage skin with planes at named stations (spinner, cowl front lip,
cowl at the firewall, cabin at the door, cabin rear, tail cone at the tailplane) and measure on the
outline polygon: width/height ratio, fill ratio (outline area over bounding-rectangle area),
side flatness (largest sideways deviation from a straight line over the door height), smallest
corner radius (the polygon's tightest turn, smoothed over 3 vertices). Proposed ranges
(**estimates**: Phase 1 refines them from the reference's front/top silhouettes and photographs):

| Station (y) | Width/height | Fill ratio | Side flatness | Min corner radius |
|---|---|---|---|---|
| cowl lip, y -2.0 | 1.00-1.20 | >= 0.76 | n/a | 15-90 mm at the lip edge |
| firewall, y -1.55 | 0.90-1.05 | >= 0.80 | <= 25 mm | 50-250 mm |
| cabin at door, y -0.2 | 0.62-0.72 | >= 0.84 | <= 12 mm over 0.7 m | 60-250 mm |
| tail cone, y 3.0 | 0.80-1.00 | >= 0.76 | n/a | 80-300 mm |

Why: it measures "bulbous" without a reference mesh: a round dome has a low fill ratio and a
radius equal to half the width; a crisp cowl has a higher ratio and a smaller corner radius.

**F4 Materials and textures.** Per visible primitive: it has a PBR material with the planned maps
(base colour, normal, ORM); **texel density** (sqrt of UV area over world area, times texture size,
area-weighted p5): airframe >= 400 px/m (about 2.5 mm per pixel), gear, propeller and small parts
>= 250 px/m, instrument panel faces >= 1 500 px/m; density spread (p95 / p5) <= 2.0 per sheet;
**UV overlap**: 0 overlapping pixels (rasterised in UV space) except triangles that are exact
mirrors of their partner under x -> -x (left/right parts); **island padding** >= 8 px (so four
mip levels stay clean) and edge bleed of 8 px; texture sizes powers of two, at most 2048; normal
map vectors unit length (mean 1 +- 0.02, z > 0); ORM metallic channel nearly binary (>= 95 % of
pixels < 0.1 or > 0.9); base colour (non-emissive) between 30 and 240 sRGB (PBR range, no pure white
paint); texture bytes and uncompressed GPU size within budget (section e).
Why 400 px/m: MSFS's 2K-sheet guidance gives about 250 px/m; we have two exterior sheets, so
about 400-450 px/m is reachable (calculation in (e)); close-up inspection is beyond that, which is what the normal map
and decals are for.

**F5 Edge crispness.** For a list of named edges (wing and control-surface trailing edges, door
frame, window frames, cowl lip, cowl-to-windscreen joint, tailplane tips, fin leading edge), cut a
2-D section perpendicular to the edge through the mesh and fit the tightest radius over the
turning of the section's tangent through 90 per cent of the corner angle. A hard edge must lie
between a minimum and maximum radius: not zero, not blobby. Proposed: trailing edge radius
1.0-3.0 mm and thickness 1.5-5 mm (current 0 mm); control-surface and door edges 2-6 mm; window
frame edges 3-8 mm; cowl lip 8-20 mm; no named edge above 40 mm where the real aircraft
has a crease. A radius under 2 mm only counts if the normal map carries it; otherwise it must
be geometric (checked: the section needs at least 3 mesh segments across the bevel).
Also: percentage of surface edge length that is a hard crease within 3-8 % (current 3-4 %).

**Visual review (subjective, labelled).** Cycles on the CPU, fixed seed and samples,
Nishita procedural sky (no HDRI download), shadow-catcher ground with soft contact shadows,
standard 9 cameras plus 4 close-ups (nose, wing root, door, tail) at 1 600 px, <= 300 KB PNG. The planner and
user judge these; they are not gates. (Cycles previews are slower than EEVEE: expect about 20-60 s per image.)

## (e) Texture budget and the 10 MB `.glb` cap

**Unique surface area to texture** (**measured**, mirrored left/right parts counted once): fuselage
paint + glass + dark 31 m², one wing with flap and aileron about 17.5 m² (upper and lower), one
tailplane half 1.5 m², rudder 1.6 m², one door about 2.6 m², gear 2.3 m², propeller 0.7 m². The
mirrored sum is about **38 m²** of exterior. At 400 px/m that is 38 x 160 000 = 6.1 Mpx, at 75 %
packing efficiency 8.1 Mpx: **two 2048 x 2048 sheets** (8.4 Mpx). Cockpit unique area is 17 m²
but only a few parts need density: one 2048 sheet with the instrument panel and radio stack
given about 1 500 px/m and the rest about 250 px/m.

Proposed set: `ext_fuselage` (fuselage, doors, cowling, gear), `ext_aero` (wing, flaps, ailerons,
tailplane, fin, rudder, propeller; one sheet shared left/right by mirroring), `cockpit`, each at
2048 x 2048 with three maps (base colour sRGB, normal, ORM). **9 maps**. GPU cost uncompressed
RGBA8 with mips: 9 x 16.8 MB x 1.33 = about **200 MB** (within the MSFS-style 150-600 MB class
and raylib has no GPU compression path). Optional later: a 512 px detail-normal tile for skin.

**File size (estimate: Phase CP3 reports the real numbers).** Geometry: today 1.15 MB; adding UVs
(8 bytes per vertex), extra detail and the mirrored parts brings about 2-3 MB. Textures as PNG:
base colour of mostly flat paint compresses well (about 1-2 MB each), normal maps of panel lines
about 2-4 MB each, ORM about 1-2 MB each: **about 14-25 MB for 9 maps**. That breaks the 10 MB
`.glb` cap set before textures existed.
Proposal: keep the **10 MB limit for the geometry-only `cessna_vc.glb`** family and set a separate
limit for the textured `cessna.glb`: **<= 24 MB hard, <= 16 MB aim**; if over, first drop the
cockpit sheet to 1024 for non-panel parts, then use JPEG q92 for base colour. KTX2/Basis (the
real compressed format) is **not** an option: raylib's glTF path does not read `KHR_texture_basisu`.
LODs: they share UV layout; LOD1/2/3 embed the same maps downsampled to 1024 / 512 / 256 (about 1/4,
1/16, 1/64 of the pixels), so they add roughly 33 % to the LOD0 texture bytes rather than 4x.

## (f) glTF COLOR_0 x baseColor decision

glTF multiplies `COLOR_0` with the base colour (factor x texture). Today `COLOR_0` carries the paint
colours **and baked vertex ambient occlusion**; with a base-colour texture it would double-tint
and double-shade in compliant viewers (raylib's cgltf path multiplies as well).

**Proposal: two families from the same generator, selected by a flag.**
- `cessna.glb`, `cessna_lod1..3.glb`: textured PBR. **No `COLOR_0` attribute**, no vertex AO (AO
  lives in the ORM map), base colour and relief in the textures.
- `cessna_vc.glb`, `cessna_vc_lod1..3.glb`: today's vertex-colour, texture-free version (no `TEXCOORD_0`),
  for the raylib viewer until T22b can show textures; same nodes, pivots and geometry as the
  textured files (the extra relief that only exists in normals does not exist here).

Effects: the T22 viewer and the database entry (T20) point at `cessna_vc*.glb` until T22b lands, then
switch to `cessna*.glb`, with no code change (it is a path). The T22c audit measures geometry; it uses
`COLOR_0` only as a flag (`has_color`), so audit gates are unaffected; `cessna_vc.glb` is audited as today.
Vertices per triangle rises a little (UV seams split vertices; about +5-10 %, estimate) so the
0.65 gate needs checking on the textured file (the gate is a regression gate: report). The LODs
double from 3 to 6 files (small: the `_vc` ones are about 0.4 MB in total). Unreal (T28) uses the textured files.
Alternatives rejected: one file with white `COLOR_0` (raylib would show unpainted white
aircraft, and the file would still carry useless data); `baseColorFactor` per material
(cannot carry the cheat line).

## (g) Phase plan CP1-CP3, numbers and risks

| | Exterior triangles | Cockpit | Textures | Main work | Gates |
|---|---|---|---|---|---|
| T22d (now) | 30 519 | 8 543 | none | | |
| CP1 shape | 38-45 k (bevels, frames, door gaps, cowl face, new section lofts) | 8.5 k | none | proportions from the reference, crisp edges with 2-5 mm bevels, trailing edge thickness, window frames, cowl lip and inlets | F1, F3, F5 pass; T22c no regression |
| CP2 detail + UVs | 50-60 k (steps, fairings, vents, trim tab, lights with lenses, hinge and panel-line details) | 10-12 k | UV layout only | small modelled parts; **high-poly detail meshes for baking (panel lines, rivets, hinges) built in memory, never shipped (millions of triangles, transient)**; **own deterministic UV layout** | F2 (geometry), F4 (UV part) |
| CP3 bake + wrap-up | 55-65 k | 12-15 k | 3 sheets x 3 maps at 2048, LOD sheets downsampled | Cycles bakes; paint scheme; gauge faces; two glTF families; Cycles previews; CREDITS, STATUS | all gates, determinism |

Total stays near 70-80 k of the 250 k cap (about 30 %); this is a ceiling, not a target.

**Risks.**
1. **Determinism of bakes (tested here, scratchpad only, Blender 5.2.2).** 5 Cycles AO bakes (CPU,
   fixed seed 7, 64 samples, no denoise, 1 and automatic thread counts) were byte-identical,
   including between 1 thread and automatic. A normal bake (selected-to-active) gave the same bytes
   in 2 of 3 runs; the odd run came with a different mesh hash from a **Displace modifier** (modifier
   evaluation is not bit-stable). Also **`bpy.ops.uv.smart_project` is not deterministic**
   (island placement changed between runs, mean pixel difference 18/255). Mitigations: build
   high/low bake meshes directly in Python/bmesh (as the generator already does, no modifiers),
   compute UVs ourselves, disable denoising and adaptive sampling, fixed seed/samples/margin, pin
   thread count if needed, and put a bake-determinism check (two runs, hash compare) into Phase 1
   before any modelling. If a few ulp-level pixel differences persist, report and propose a pixel
   tolerance rather than relaxing silently (brief's rule).
2. **Reference accuracy:** the drawing may be an older variant (see (c)); F1 tolerance and station
   weighting need calibrating in Phase 1.
3. **Bevel vs triangle cost:** a 3 mm bevel needs 3 segments on every hard edge. Hard edges are
   about 180 m of the model; at about 1 triangle per 4 mm per segment this is 20-40 k triangles if done
   everywhere; so geometric bevels only on named edges, the rest in the normal map.
4. **File size:** see (e); possible need to cut texture resolution.
5. **Vertex limit:** raylib's 65 536 vertices per primitive; UV seams add vertices, the fuselage
   paint primitive is already 6 875 vertices at most (OK), but the cockpit/wing splits must be rechecked.
6. **LOD audit:** LOD2/3 are experimental (T22d); textured LODs inherit that.
7. **Cycles render time** for previews: 13 images at 1 600 px at 64-128 samples, about 15-30 minutes headless.

## Decisions for the user
1. Which reference three-view (A, B, or your own supply)? (nothing is downloaded until you choose.)
2. File cap: accept 24 MB hard / 16 MB aim for the textured `cessna.glb` (10 MB kept for `_vc`)?
3. Texture plan: 3 sheets x 3 maps at 2048, 400 px/m airframe floor: OK, or smaller/larger?
4. Two file families (`cessna.glb` textured, `cessna_vc.glb` vertex-colour) as the colour fix?
5. Gate values in section (d)/`cessna_fidelity.toml`: accept as the starting point for Phase 1 calibration?
