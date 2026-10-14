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
