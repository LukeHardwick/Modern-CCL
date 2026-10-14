# T22e — High-fidelity Cessna (geometry + PBR textures)

Tools track · Follows [T22d](T22d-cessna-172.md) (reviewed, PR open) · Branch `task/t22e-cessna-hifi`,
based on `task/t22d-cessna` @ `2c4e0f5` · Sonnet 5.5 agents, one phase each, Opus 5.5 planner reviews
every checkpoint · Started 2026-10-14 at the user's request.

## Why
The user (2026-10-14): the T22d Cessna "looks quite cartoonish (like Unturned or Roblox)"; they want
it **high fidelity**. The planner's diagnosis of the T22d renders (`docs/guide/img/models/cessna_*.png`):
1. **Flat materials.** One vertex colour per surface: no textures, no panel lines, rivets, wear or
   gloss variation.
2. **Toy proportions and soft forms.** Bulbous round nose bowl and spinner, blobby cowling, very soft
   edges everywhere, no crisp creases where the real aircraft has them.
3. **Missing detail** (T22d's own deferred list): panel lines and rivets, window frames and seals,
   door gaps, wing-root fairings, steps, stall-warning vent, elevator trim tab, painted prop tips,
   the second cheat-line colour, cowl flap/oil-door outlines, fuel-tank vents, landing-light lens.
4. **Flat preview lighting** (EEVEE, uniform background, flat green ground): no sky model, no
   reflections, so even good geometry reads as plastic.
The T22c audit cannot see any of this: it measures the mesh against its own `--detail 4` reference
(tessellation quality), not against the real aircraft. New **fidelity gates** are needed.

## User decisions (2026-10-14)
- **Scope: geometry + PBR textures.** Modelled detail where it changes the silhouette or catches
  light; baked maps (base colour, normal, occlusion, roughness/metallic) for panel lines, rivets,
  paint and gloss. Vertex colours stay as the fallback for the current raylib viewer, which shows
  textures only after T22b.
- **Reference three-view:** the agent proposes freely licensed candidates (URL, licence, author,
  resolution, which views); **agents never download**. The user approves one; the planner downloads
  it into `tools/models/reference/` (with its licence in CREDITS.md).
- **Branch:** new branch on top of T22d; PR #1 (T22d) stays as the reviewed baseline.
- **Budget unchanged:** exterior ≤ 100 k triangles, cockpit ≤ 150 k, total ≤ 250 k (a ceiling, not
  a target). `.glb` ≤ 10 MB was set without textures: Phase 0 proposes a texture budget.
- Quality gates report back to the planner at every checkpoint (the agent stops and reports;
  the planner reviews, then the user approves).

## Tooling rules
- **Headless Blender only** (`/Applications/Blender.app/Contents/MacOS/Blender -b --python …`, or
  whatever `scripts/make_models.sh` / `CREDITS.md` document). **Do not use the Blender MCP**: the
  one live Blender instance is shared with the T22a jet session.
- `tools/models/*.py` is the single source of truth. Committed `.glb`, textures and PNGs come only
  from the headless generator. No `.blend` committed.
- Do not change `hawk.py`. `mm_common.py`: additions only, no behaviour change for existing helpers;
  the jet's `jet.glb` must stay byte-identical. Cessna code lives in `cessna.py` (split into modules
  if it gets large, e.g. `cessna_textures.py`).
- Determinism: two headless builds give byte-identical `.glb`, textures and preview PNGs. Baking:
  Cycles on CPU, fixed seed, fixed samples, fixed tile/thread settings. If byte-identical bakes
  prove impossible, stop and report with a proposed pixel tolerance rather than silently relaxing.
- No downloads or installs. Web search/reading for research is fine. No C++ changes.
- Images you commit: ≤ 300 KB each (previews); read your own renders to check them.
- Commit at every checkpoint and **push the task branch to `origin`** (never `main` or tags).
  Commit messages end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

## Regression gates (T22c audit, must not get worse)
`scripts/audit_models.sh --name cessna` with `tools/models/audit/cessna.toml` (gates approved at T22d
STOP 0). Baseline: LOD0 92 / 107, no hard failure (`docs/guide/img/models/audit/cessna/cp3_cessna_audit.md`).
No gate that passes there may fail; no hard failure; detail_check PASS. Gates that are failing may
change, and must be reported. The reference geometry changes as the shape changes — that is
expected; the audit compares each build with its own `--detail 4` reference.

## Fidelity gates (new; proposed in Phase 0, approved by the user, checker built in Phase 1)
Deterministic, reproducible numbers with pass/fail, from a script (`tools/models/fidelity_audit.py`,
proposed name) with known-answer tests. Starting ideas — Phase 0 refines them and proposes values:
- **F1 Outline vs reference:** orthographic side/top/front silhouettes of the model, scaled to the
  published length/span/height, against the approved three-view (traced/thresholded outline):
  IoU and outline distance (p95/max, mm) per view, plus named station checks (cowling front, wind-
  screen base, wing leading edge, fin top, tailplane span).
- **F2 Feature checklist:** a TOML list of features a real 172R/S shows (window frames, door gaps,
  panel lines, rivet rows, fairings, steps, trim tab, lights, vents, prop tip paint, cockpit gauge
  faces, …), each checked from the `.glb` (named nodes/primitives, `extras`, material slots or
  texture regions) with counts.
- **F3 Section shape:** cross-sections at named stations (spinner, cowl front, cowl at the firewall,
  cabin, tail cone) against reference proportions (width/height ratio, flatness of sides, corner
  radii) — catches the "bulbous" look numerically.
- **F4 Materials and textures:** every visible primitive has a PBR material with the planned maps;
  exterior texel density ≥ N px/m; no UV overlap except deliberate mirroring; island padding ≥ N px
  at every mip level used; power-of-two sizes; total texture and `.glb` size within budget.
- **F5 Edge crispness:** bevel radii on hard edges within a range (not zero, not blobby) at listed
  edges (wing trailing edge, control-surface edges, door edges, window frames).
- **Visual review (subjective, labelled as such):** a fixed set of Cycles renders with a procedural
  sky (Nishita/Hosek, no HDRI download) and a ground with soft contact shadows, from the standard
  cameras plus close-ups; the planner and the user judge these.

## glTF colour issue to resolve in Phase 0
glTF multiplies `COLOR_0` with the base-colour texture. A file carrying both the current vertex
colours and a base-colour texture would double-tint in compliant viewers. Propose a solution (e.g.
`cessna.glb` = textured PBR, white/absent vertex colours, and `cessna_vc.glb` = today's vertex-
colour version for raylib until T22b; or another scheme) with its effect on T22/T22b and the LODs.

## Phases (each = one fresh Sonnet agent; STOP and report to the planner at the end of each)
- **Phase 0 — gap analysis, research, gate proposal (no modelling).** Renders of the current model
  next to a written list of every cartoonish cue (with measurements where possible); short research
  on how high-fidelity sim aircraft are built (MSFS/X-Plane: texel density, map set, bevel practice,
  typical exterior budget) with sources; reference three-view candidates; proposed fidelity gates
  and values in `tools/models/audit/cessna_fidelity.toml` (proposal) and the method in
  `docs/research/` (plain language, so the planner can explain it to the user); texture budget; the
  glTF colour decision; a phase plan for CP1–CP3 with expected triangle/texture numbers.
  Commit `docs` + proposal only. **STOP** (the user reviews research and gates before anything is built).
- **Phase 1 — fidelity checker** (a different agent from the modellers): `fidelity_audit.py`,
  known-answer tests (synthetic shapes with known IoU/sections/texel density), byte-identical
  reruns, baseline report on the T22d model (expected to fail). **STOP.**
- **CP1 — shape:** proportions and sections to the reference (nose/cowling, cabin section, wing root,
  tail), crisp hard edges with small bevels, window frames, door gaps; F1/F3/F5 pass; T22c audit no
  regression. Images + both audit reports. **STOP.**
- **CP2 — surface detail + UVs:** modelled small parts (steps, fairings, vents, trim tab, antennas,
  lights with lenses), high-res detail meshes for baking (panel lines, rivets, hinges), UV unwrap
  with texel density and padding to F4. **STOP.**
- **CP3 — PBR bake and wrap-up:** baked normal/AO/roughness/metallic/base colour (paint scheme with
  the two-tone cheat line, subtle grime at hinges and exhaust, generic, no registration or logos);
  cockpit textures (legible gauge faces, placards without real brands); glTF export with textures
  per the Phase 0 decision; upgraded preview renders; LODs updated (textured, sharing the atlas);
  every audit and fidelity gate; determinism; CREDITS; STATUS/README. **STOP** for the independent
  Opus review, then PR.

## Report at every STOP (≤ 60 lines)
Commits (pushed), what changed, triangle/size/texture table, T22c audit summary with every failing
gate, fidelity gate table (value vs gate), image list (paths), determinism result, open questions
for the user. Append a dated entry to the "Checkpoint log" below.

## Checkpoint log

**2026-10-14 — Phase 0 (research, gap analysis, gate proposal; no modelling).** Research doc
[docs/research/05-cessna-fidelity.md](../research/05-cessna-fidelity.md) and proposed gates
`tools/models/audit/cessna_fidelity.toml` (PROPOSED, not approved). Gap analysis (11 cues; measured:
no UVs and 6 flat materials, 0 mm trailing-edge thickness, hard creases 3-4 % of edge length,
dome-like nose section); sources (MSFS 2024 SDK, X-Plane developer pages); reference candidates
(Commons: Werneuchen PD SVG, Marek Cel CC0 SVG; a 1956 manual drawing is rejected); F1-F5 methods
and values; texture budget (3 sheets x 3 maps at 2048, about 200 MB GPU, textured `.glb` cap
proposed 24 MB hard / 16 MB aim); colour decision (`cessna.glb` textured without COLOR_0 plus
`cessna_vc*.glb` vertex-colour family); CP1-CP3 plan (about 40 k, 55 k, 60 k exterior triangles).
Determinism finding: Cycles AO bakes byte-identical (also 1 thread vs auto); `uv.smart_project` and
Displace-modifier meshes are NOT deterministic, so the generator must build bake meshes and UVs
itself. Waiting for the user: reference choice, file cap, texture plan, colour scheme, gate values.

**2026-10-14 — Phase 1 (fidelity checker; no modelling).** Built by a separate agent from the
modellers, as required. **User decisions of 2026-10-14 (recorded here):** (1) reference = drawing A
(`tools/models/reference/cessna_172_skyhawk_line_drawing.svg`, public domain; the planner checked it
is a modern 172: swept fin, dorsal fin, rear window); it is hand-drawn and its side view has no wing,
so F1 side leaves the wing band out and the wing outline is checked in the front and top views;
(2) textured `cessna.glb` <= 24 MB hard / 16 MB aim, `cessna_vc*.glb` keeps 10 MB, 3 sheets x 3 maps at
2048 px, airframe >= 400 px/m, the textured `.glb` is committed only at CP3; (3) colour fix = two
families: `cessna.glb` (+LODs) textured PBR without COLOR_0, `cessna_vc.glb` (+LODs) today's
vertex-colour file; (4) F1-F5 as proposed are the starting values, Phase 1 calibrates them and every
change goes to the user for approval.

*What exists.* `tools/models/fidelity_audit.py` (+ `fidelity_model/outline/section/tex/features.py`,
`raster_util.py`, `png_io.py`; `glb_io.py` gained UV, COLOR_0, image and texture support, additions
only), config `tools/models/audit/cessna_fidelity.toml`, wrapper `scripts/fidelity_audit.sh`,
`tools/models/fidelity_noise.py` (noise floor), `tools/models/bake_determinism_check.py`, reference
masks and `make_reference_masks.py` (command in `tools/models/reference/README.md`), known-answer
tests `tools/models/test_fidelity.py` (40 tests; also `blender -b --python`). Pure Python + numpy: the
report is the same bytes under python3 3.14 / numpy 2.4 and Blender's 3.13 / numpy 2.3. No scipy.
Run: `scripts/fidelity_audit.sh --images --out DIR` (exit 1 = a hard gate failed or could not be
measured; 2 = bad input).

*How each gate is measured.* **F1:** triangles of the exterior (not `cockpit`, `prop_disc`, propeller
blades) are rasterised as silhouettes on a 5 mm grid; the reference masks are scaled on each axis to
the published length / span / height (no best fit) and anchored at the model's spinner tip, lowest
point and x = 0; IoU, model-to-reference outline distance (p95, max) and 20 named stations (extreme
or scan operators applied identically to both masks). **F2:** a feature needs a probe box in the
config (empty until CP2 = absent); geometry features count triangles in the box and their relief along
the box's thin axis, texture features count groove marks in the normal map (or colour regions) through
the UVs. **F3:** plane cut, outline = farthest point per 0.125 degree sector from the centre (an open
roof or door gap becomes a chord and is ignored for the radius; a thin fin is ignored for the radius),
width / height, fill, side flatness, tightest radius (24 mm arc: radii under about 15 mm are not
resolved). **F4:** needs no textures for the UV gates (assumed 2048 sheet): texel density per group, spread,
UV range, overlap (exact x -> -x mirror pairs allowed), island padding (square window); with maps:
sizes, normal length, metallic binary, base-colour range, edge bleed; always: file size by family,
vertex limit, no COLOR_0 on textured primitives. A model with no UVs / textures (T22d) FAILs
`F4.maps` / `F4.uvs` and reports the dependent gates N.A. (counted as hard failures). **F5:** exact
section polylines; a bevel's radius from its own turning vertices (needs >= 3 segments unless
< 2 mm), thickness 1 mm behind the tip, hard-crease share (> 30 degrees).

*Determinism.* Two runs: byte-identical JSON, Markdown and overlay PNGs, also python3 vs Blender;
`bake_determinism_check.py -- --twice` (AO and tangent-space normal bake of a bmesh mesh, no
modifiers, own UVs, Cycles CPU, seed 7, 48 samples, no denoise, 1 thread): the hashes of both
float images and PNGs were identical in 4 of 4 repeats (8 bakes, separate processes).

*Noise floor* (`docs/guide/img/models/audit/cessna/fidelity_noise.md`): raster pitch 2.5 / 5 / 10 mm
moves IoU by <= 0.003 and p95 by <= 6 mm (one 10 mm outlier of 84 mm on the discrete p95 of the side
view); re-extracting the drawing (render scale 2-4, bridge radius, stroke half-width) moves IoU by
<= 0.009 (top) and p95 by <= 5 mm, stations by <= 19 mm (tailplane_te 27 mm). **The drawing itself is
the larger uncertainty:** scaled to the published size on each axis, its views disagree by 5.4 %
(length, side vs top), 1.3 % (span) and 4.4 % (height), and its inboard wing chord measures about
1.9 m against 1.63 m for the T22d model, so the top-view wing edges follow the drawing, not
necessarily the aircraft.

*Calibration (every change from the proposal, for the user's approval).* (a) `iou_min` 0.94 / 0.94 /
0.90 -> **0.92 / 0.95 / 0.72** (side / top / front): an IoU gate must not be stricter than the
outline gate; each value is the IoU of a uniform 30 mm error (the p95 gate), measured by offsetting
the reference (noise study 3: 0.9226 / 0.9523 / 0.7240, rounded down); the thin wing makes the front
view 3x as sensitive. (b) F3 `cowl_lip` ratio 1.00-1.20 -> **1.17-1.43** (reference: 0.857 m wide /
0.660 m high = 1.30, +-10 %). (c) F3 `tail_cone` ratio 0.80-1.00 -> **0.79-0.97** (reference 0.88
+-10 %; T22d measures 0.862). Kept with data behind them: `outline_p95_mm` 30 and `outline_max_mm` 80
(processing noise <= 10 mm), `station_tol_mm` 40 (noise <= 27 mm, usually <= 12). Kept, **not calibratable here**:
F2 counts, F3 firewall / cabin ranges and all radii, every F4 value (no textured sample until
CP2 / CP3: only the known-answer tests back them), F5 radii, `hard_crease_fraction` (T22d is
already inside the band at 4.66 %, so it cannot discriminate). **Flagged:** F3 `cowl_lip` radius
[15, 90] mm looks like the lip-edge bevel, not a section corner (T22d: 189 mm; a 90 mm limit asks for
a nearly square cowl); `cabin_door` ratio [0.62, 0.72] vs 0.72-0.78 read from neighbouring reference
stations; F3 `tail_cone` fill 0.76 is met by T22d with 0.759 (borderline).

*Baseline* (T22d `cessna.glb`, `docs/guide/img/models/audit/cessna/p1_baseline_cessna_fidelity.md`,
overlays `..._f1_{side,top,front}.png`): 28 of 118 gates pass, 78 fail, 12 N.A. (48 hard failures); exit 1. F1 side IoU
0.702 / p95 534 mm, top 0.739 / 480 mm, front 0.255 / 535 mm; passes only the anchored stations
(spinner tip, rudder TE, fin top, wing tip, main-wheel track) and wing TE root. Largest station
errors: windscreen base 563 mm, wing LE tip 641 mm, wing TE tip 512 mm, wing top at mid-span 523 mm
(model wing about 0.5 m higher than the drawing relative to the ground), cabin roof 203 mm, cowl front
117 mm. F2 0 of 28 required. F3 15 of 18 (fails: cowl ratio 1.06, cowl radius 189 mm, tail fill 0.759). F4: maps
and UVs FAIL (none), 12 gates N.A. F5: trailing edge radius 0 mm / thickness 0.3 mm, control surfaces,
door, cowl lip and tips all 0 mm; only the fin leading edge (4.2 mm) passes.

*For the user:* approve or change the calibration list above; decide whether drawing A's wing chord
(about 1.9 m scaled) is acceptable as the top-view target or whether F1 top should use the published
wing planform (or reference B) for the wing edges; confirm the F3 flags. Waiting for approval before CP1.
