# T22c — Deterministic model audit: shape fidelity and triangle efficiency

Tools track · Depends on T22a CP1 · Branch `task/t22c-model-audit`, based on the
`task/t22a-sample-models` commit given in the launch prompt · **Opus 5.5** (user's choice, 2026-10-12)
Shared rules in [README.md](README.md): 1 (worktree and branch), 8 (commits; trailer
`Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`) and 9 (report) apply. Rules 2–7 do not
(Python tooling only, no C++).

## Why
The user wants the hero jet's quality checked by numbers that any run reproduces, not only by eye
or by the agent that builds it: is the shape accurate where it shows, and does every triangle earn
its place? This tool is written by a different agent from the T22a modeller, and the user approves
its method and gates **before the full model is built** (user, 2026-10-12: "explain it before
making the full model").

Planner measurements on CP1 (`databases/sample/models/jet.glb` at `cbf595a`: 48 088 triangles,
47 113 vertices, 2.0 MB):
- The fuselage skin loft is a uniform 72 × 180 grid: 25 920 triangles (54 % of the model). Its
  chordal error against the analytic surface (`hawk.fus_point`) is about 0.9 mm around and 1.2 mm
  along. A curvature-adaptive grid reaches ≤ 1 mm with about 4 760 triangles (≤ 0.5 mm with 9 600).
- 0.98 vertices per triangle: colours are set per face, so the exporter splits most vertices.
- The `fuselage` × `paint` primitive already has 38 108 vertices (the raylib limit is 65 536).
- Panel lines, fasteners and patches (thin geometry floating 4–6 mm above the skin): about 4 600
  triangles.

The tool must make findings like these routine.

## Context
Blender 5.2.2 at `/opt/homebrew/bin/blender`. Generator (owned by T22a): `tools/models/make_models.py`
(entry point, export, checks, previews; `--detail` scales ring and segment counts),
`tools/models/mm_common.py` (geometry helpers), `tools/models/hawk.py` (the jet). Build:
`blender --background --python tools/models/make_models.py -- --out <dir> [--only jet] [--detail 4]`.
Research: [docs/research/03-model-format-and-fidelity.md](../research/03-model-format-and-fidelity.md)
(Addendum A: budgets, tiers, cockpit). The T22a plan update (end of the
[T22a brief](T22a-sample-models-blender.md)) says how the modeller will use this tool.

## Deliverables
1. `tools/models/audit_model.py`, run headless:
   `blender --background --factory-startup --python-exit-code 2 --python tools/models/audit_model.py -- --model M.glb --reference R.glb --config tools/models/audit/jet.toml --out DIR [--images] [--fast]`
   writes `DIR/<name>_audit.json`, `DIR/<name>_audit.md` (PASS/FAIL per gate) and, with `--images`,
   heat-map PNGs. Exit status 1 when a hard gate fails.
2. `tools/models/audit/jet.toml`: design viewing conditions, node groups, poses, profile probes and
   gates (read with `tomllib`). The numbers are proposals until the user approves them.
3. `scripts/audit_models.sh`: builds the reference with the generator at `--detail 4` into a temp
   directory (never committed) and audits the committed `.glb`.
4. `tools/models/test_audit.py`: the known-answer tests below, run headless; non-zero exit on
   failure.
5. `docs/research/04-model-audit.md`: the method in plain language (the planner uses it to explain
   the tool to the user), test results, the CP1 baseline with heat maps, the proposed gates with
   reasons, and the runtime.

## Method (everything deterministic)
- **Measure what the viewer loads.** Parse the GLB yourself (`struct` + `numpy`): node hierarchy
  and TRS, primitives, indices, POSITION / NORMAL / COLOR_0. Do not measure through Blender's glTF
  importer (it can merge or split data). Use `mathutils.bvhtree.BVHTree` for nearest-point and ray
  queries. Compare node by node (same node names in model and reference), in the rest pose unless a
  check says otherwise.
- **Reference.** The same generator at `--detail 4` has about 16× the triangles, so its own
  chordal error is about 1/16 of the model's (error ∝ 1/n²). This measures tessellation, not
  design accuracy; the report says so. Warn when a reference node is not at least 4× denser.
- **A. Shape accuracy (both directions).** Sample every model triangle with a fixed barycentric
  pattern (centroid, edge midpoints, plus a regular grid so no two samples are more than `s` apart;
  `s` from the config) and measure the distance to the reference node's surface. Also measure
  reference triangle centroids against the model (catches flattened or missing shape). Per node and
  group: max, p99, p95, area-weighted mean. Per-triangle error = max over its samples.
- **B. Shading accuracy.** At each sample, the angle between the exported interpolated normal and
  the reference's interpolated normal at the nearest point: p95 and max.
- **C. Needless triangles.**
  - *Simplification headroom* (the main number): weld coincident vertices (geometry only),
    decimate with Blender's Decimate (COLLAPSE) at ratios 0.9, 0.8 … 0.1 and re-measure A.
    Headroom = 1 − the smallest ratio that still passes the A gate. Large headroom means
    triangles that change nothing. Thin decal strips and small parts can distort this; measure it
    on the large connected components (the skins) and document how components are chosen.
  - *Error map*: per-triangle error ÷ gate, bucketed (< 0.1, 0.1–0.5, 0.5–1, > 1). In the heat
    map, blue = could be coarser, red = too coarse, magenta = hidden.
  - *Hidden*: occluders = all non-glass geometry. From each triangle's samples, cast rays along a
    fixed set of at least 162 directions in its front hemisphere. A triangle is visible if any ray
    leaves the model's bounding sphere, or (when the empties exist) if `eyepoint_front` or
    `eyepoint_rear` sees it, in any configured pose (rest, control surfaces deflected, canopy open;
    see `DEFLECT` in `make_models.py`). Report % hidden per node and the largest hidden clusters
    with their location.
  - *Sub-pixel and thin*: config design view (default 1920 × 1080, vertical FOV 60°, closest
    distance per group: exterior 8 m, cockpit 0.6 m) gives a pixel size p = 2·D·tan(FOV/2)/H.
    Area < p² = sub-pixel; smallest altitude < 0.5 p = thin (shimmer risk).
  - *Vertex efficiency*: exported vertices per triangle (per primitive and total). **Every index
    ≤ 65 535** (hard gate): raylib 6.0's `Mesh.indices` is `unsigned short` and its glTF loader
    truncates u32 indices with only a warning (`rmodels.c` around line 6093). Also report
    primitives (draw calls), materials and nodes.
- **D. Hygiene.** Degenerate triangles (area < 1e-10 m²), slivers (smallest angle < 2°),
  duplicate faces, flipped faces (back side visible from outside while the front is not, or normal
  error > 90°), inconsistent winding on manifold edges, floating islands (same rule as
  `make_models.check_islands`).
- **E. Shape plausibility.** Bounding box length / span / height against published figures in the
  config (±2 %).
- **F. Profile fairness** (catches lumpy outlines and steps, like the CP1 review's two-hump canopy
  and the canopy-to-spine step). Probe lines from the config: rays every 5 mm along a line (for
  example straight down at x = ±0.10 m over the canopy and spine) give an outline z(y) of the
  selected nodes and materials (glass counts as surface). Narrow features such as frame hoops,
  antennas and lights are excluded by a minimum feature width; document what is excluded. Count
  local extrema above a prominence threshold, the largest kink (slope change between 5 cm
  segments) and the largest step. Gates per probe, for example canopy crown: one maximum; canopy to
  spine: kink ≤ 2°, step ≤ 2 mm. CP1 must fail this: its canopy-height keys dip from 0.93 to
  0.90 m between the seats, and the spine starts about 0.2 m above the canopy's rear end.
- **G. Budgets.** Triangles per node and group against the config; totals; file size.
- **Determinism.** No randomness (fixed direction sets such as a Fibonacci sphere, fixed sample
  patterns), stable order (sorted names, index order), rounded output (1e-6 m, 0.01°, 0.01 %),
  `json.dumps(..., sort_keys=True)`. Two runs give byte-identical JSON. Test Decimate's
  determinism; if anything is not deterministic, replace it or document it.
- **Speed.** The full audit takes at most about 3 min for 250 k triangles on this Mac (report the
  time). A `--fast` mode (fewer directions and samples) is fine for iteration, but only the full
  mode counts at checkpoints.

## Known-answer tests (`test_audit.py`)
1. Sphere (r = 1 m, n segments) against a dense sphere: max deviation within 5 % of the analytic
   chordal error.
2. 64 × 64 flat plate against a one-quad plate: headroom ≥ 95 %. A two-triangle plate: headroom 0,
   nothing flagged.
3. Closed box inside a closed box: inner box 100 % hidden, outer box 0 %.
4. Cube with one face flipped: exactly that face reported.
5. Degenerate and sliver triangles counted exactly.
6. A primitive with 70 000 vertices fails the index limit.
7. Profiles: one bump gives 1 maximum; two bumps with a dip give 2 maxima and 1 minimum; a step
   and a 3° kink are detected.
8. Sub-pixel boundary: triangles just above and below p² are classified correctly.
9. Determinism: two audits of the same files give byte-identical JSON.

## Proposed gates (defaults in `jet.toml`; the user sets the final values after the CP1 baseline)
| Check | Gate |
|---|---|
| Shape accuracy, exterior groups | p99 ≤ 1.0 mm, max ≤ 4 mm |
| Shape accuracy, cockpit group | p99 ≤ 0.3 mm |
| Shading | p95 ≤ 2°, max ≤ 10° |
| Simplification headroom (components ≥ 2 000 triangles) | ≤ 15 % |
| Hidden triangles | ≤ 2 % per node |
| Sub-pixel triangles at the design distance | ≤ 10 % |
| Vertices per triangle | ≤ 0.65 |
| Index limit | every index ≤ 65 535 (hard) |
| Degenerate / duplicate / flipped / floating | 0 (hard) |
| Slivers | ≤ 1 % |
| Length / span / height vs published | ±2 % |
| Profile fairness | per probe (see F) |
| Budgets | total ≤ 250 k triangles (exterior ≤ 100 k, cockpit ≤ 150 k), `.glb` ≤ 10 MB (user, 2026-10-13) |

## Boundaries
- Do not edit `tools/models/hawk.py`, `mm_common.py` or `make_models.py` (the T22a session owns
  them). If the generator needs a flag (for example to skip baked AO so reference builds are
  faster), propose it in the report.
- Do not regenerate or commit `databases/sample/models/*.glb` or the preview PNGs. Baseline heat
  maps go to `docs/guide/img/models/audit/` (PNG ≤ 300 KB each); read every image yourself before
  reporting.
- Blender's bundled Python only (numpy, mathutils, bmesh, tomllib). No installs, no downloads, no
  Blender MCP.
- Stop after the report. Do not merge.

## Acceptance
All tests pass; two runs give byte-identical JSON; the CP1 baseline (report + heat maps) is
committed; `docs/research/04-model-audit.md` explains the method in plain language; runtime
reported.

## Scope change — 2026-10-13 (user)
Skip the CP1 baseline: no baseline report, no CP1 heat maps, no baseline section in
`docs/research/04-model-audit.md`. One smoke run on CP1 is allowed only to prove the tool runs at
full size and to measure the runtime. The model is evaluated when the new version (CP2a) is
finished; the user sets the final gate values then.
Tooling only (user, 2026-10-13): the CP1 jet will be replaced, so do not review, interpret or tune
anything to its numbers. The smoke run reports only: completed, runtime, two runs byte-identical.
Correctness is shown with the synthetic known-answer tests.
