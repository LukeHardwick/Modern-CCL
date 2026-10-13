# 04 - Model audit: shape fidelity and triangle efficiency (T22c)

Date: 2026-10-13. Tool: `tools/models/audit_model.py` (with `glb_io.py`, `audit_render.py`),
configuration `tools/models/audit/jet.toml`, wrapper `scripts/audit_models.sh`, tests
`tools/models/test_audit.py`. Written for the user's review of the method and the gates; the
gate values are **proposals** until the user sets them when the new jet (T22a CP2a) is finished.

## 1. What the audit is for

The hero jet is built by a script (T22a), and its quality used to be judged by eye. The audit
judges it by numbers that any run reproduces exactly: is the shape accurate where it shows, is
the shading right, and does every triangle earn its place? It reads the `.glb` file the viewer
loads, compares it with a much denser build of the same model, and checks the result against
pass marks ("gates") in `jet.toml`. Each gate is reported as PASS or FAIL; a few are **hard**
(a definite fault, such as a mesh raylib cannot load correctly) and make the tool exit with
status 1.

How to run it (Blender 5.2, headless; nothing is installed):

```sh
# Usual way: build the reference into a temporary folder, audit the committed jet.glb,
# write build/audit/jet/jet_audit.md and jet_audit.json (add --images for heat maps;
# --help lists the other options, such as --model, --reference and --generator).
scripts/audit_models.sh [--images] [--out DIR] [--fast]

# The tool itself, when you already have a reference build:
blender --background --factory-startup --python tools/models/audit_model.py -- \
    --model M.glb --reference R.glb --config tools/models/audit/jet.toml --out DIR \
    [--images] [--fast] [--strict] [--jobs N]

# The known-answer tests (exit status 0 when all pass):
blender --background --factory-startup --python tools/models/test_audit.py
```

Outputs: `<name>_audit.json` (every number; byte-identical between runs), `<name>_audit.md` (the
gate table, per-node figures, the worst places with coordinates) and, with `--images`, heat-map
PNGs. `--fast` uses fewer samples and ray directions for quick iteration; only the full mode
counts at checkpoints. `--strict` makes any failed gate, not only a hard one, exit with 1.

Coordinates in the reports are in the modelling frame of `tools/models`: metres, x = the
aircraft's left, y = towards the tail (the nose is at -y), z = up.

## 2. Four ideas used throughout

**The reference.** There is no exact mathematical description of the whole jet to compare
with, so the audit compares the model with the same generator run at `--detail 4`, which uses
four times as many points along every curve. A curved surface made of flat triangles cuts
inside the true curve; that "chordal" error shrinks with the square of the number of points,
so the reference's own error is about 1/16 of the model's. The audit therefore measures
**tessellation** (how well the triangles follow the intended shape), not whether the intended
shape is a good Hawk: that is what checks E and F and the user's eye are for. The report warns
when a reference node has fewer than four times the model's triangles (its errors are then
underestimated).

**Pixel size at the design distance.** Whether an error matters depends on how big it looks.
The config gives the viewing conditions (1920 × 1080, vertical field of view 60°) and, per group
of nodes, the closest distance it is normally seen from. One pixel at distance D covers
p = 2 · D · tan(30°) / 1080:

| Distance | 8 m (exterior) | 4 m | 2 m | 0.6 m (cockpit) |
|---|---|---|---|---|
| Pixel size p | 8.55 mm | 4.28 mm | 2.14 mm | 0.64 mm |

8 m is about the closest a camera can be while the whole 12.4 m aircraft still fits across a
16:9 screen (the frame is 16.4 m wide there); 0.6 m is roughly the pilot's eye to the
instrument panel.

**Percentiles weighted by area.** "p99 ≤ 1 mm" means: 99 % of the surface area has an error of
at most 1 mm. Weighting by area stops a crowd of tiny triangles from hiding a large bad one (or
the reverse). "max" is the single worst place.

**Measure what the viewer loads.** The tool reads the `.glb` with its own reader (`glb_io.py`,
plain `struct` and `numpy`): node tree and transforms, every primitive's positions, normals and
indices. It does not go through Blender's glTF importer, which may merge or split data. Nodes
are compared by name (model `fuselage` against reference `fuselage`), in the rest pose unless a
check says otherwise. Blender's `mathutils.bvhtree` (a tree of boxes around the triangles that
makes "nearest triangle" and "what does this ray hit" questions fast) does the searching.

## 3. The checks

Each check: what it measures, why it matters, how it is computed, and how its proposed pass mark
was chosen.

### A. Shape accuracy (both directions)

*What.* How far the model's surface is from the reference's surface, in millimetres, measured
from the model to the reference and from the reference to the model.

*Why.* The outline against the sky and the way light runs over curved panels are what make the
jet look smooth. If the flat triangles cut too far inside the curve, the outline shows corners
and the panels show facets.

*How.* Every model triangle gets a fixed pattern of sample points: its corners, edge midpoints
and centre, plus a regular grid fine enough that neighbouring samples are at most 5 cm apart.
Each sample is moved 1 % towards the triangle's centre so that no sample sits exactly on an edge
two triangles share. For each sample the tool finds the nearest point on the same-named
reference node; the distance is the error there, and a triangle's error is the largest of its
samples. In the reverse direction the centre of every reference triangle is measured against
the model. The reverse direction catches shape the model has lost, such as a flattened bulge or
a missing part: the forward direction cannot, because every model point may still lie close to
*some* reference surface. Statistics per node and group: max, p99, p95 and mean, weighted by
area, over the triangles that can be seen (hidden and flipped triangles have their own checks;
the JSON also has the figures over all triangles). Triangles that coincide exactly with the
reference on a curved surface are listed as **not measured**: the generator did not refine them
with `--detail`, so this reference cannot show their error.

*Pass mark.* Exterior: p99 ≤ 1.0 mm and max ≤ 4 mm. Cockpit: p99 ≤ 0.3 mm and max ≤ 1.2 mm.
In pixels: the exterior's 1 mm is an eighth of a pixel at its 8 m design distance and still
under half a pixel at 2 m, which leaves room for close outside views; the cockpit's 0.3 mm is
half a pixel at 0.6 m. The max is four times the p99, so a single worst spot on the exterior
stays under half a pixel at 8 m. Exterior parts that the pilot sees from close by (the canopy
frame, the top of the nose) are judged by the exterior gate too. Because chordal error
falls with the square of the number of points along a curve, placing points where the surface
curves most (curvature-adaptive tessellation) meets such a gate with far fewer triangles than a
uniform grid.

### B. Shading accuracy

*What.* The angle between the direction the model's surface faces (its normal, which the
renderer uses for lighting) and the reference's normal at the same place.

*Why.* Lighting is computed from normals, not from the triangle shape. Wrong normals give bands,
dark streaks or highlights in the wrong place even where the shape is accurate; flat shading on a
curved panel shows every facet.

*How.* At each shape sample, the model's vertex normals are blended the way the GPU blends them
(by the sample's position in the triangle), and the same is done at the nearest reference point;
the angle between the two is the error, and a triangle's error is the largest of its samples.
Next to a crease, the nearest reference point can be on the far side of the crease, which faces
another way; where the two faces differ by more than 60°, the tool looks along the model's
normal for a reference face that faces the same way and compares there (reported as
"reprojected samples"). Statistics as in A.

*Pass mark.* p95 ≤ 2° and max ≤ 10°. A 2° tilt changes the brightness of a matt surface by at
most sin 2° ≈ 3.5 % of full light (usually much less); a reflection moves by twice the angle.
Up to 10° (at most 17 %) is allowed on the last 5 % of the area, typically at creases and tips.

### C. Needless triangles

#### C1. Simplification headroom (the main efficiency number)

*What.* The share of a large piece's triangles that could be removed without breaking the shape
gate.

*Why.* A triangle that can go without changing the shape costs memory, vertex work and file size
for nothing; the triangle budget is better spent where the surface curves.

*How.* Each connected piece of a node with at least 2 000 triangles (in practice the skins) is
welded (corners at the same position become one vertex; normals and colours are ignored) and
shrunk with Blender's Decimate modifier in collapse mode, which repeatedly merges the two ends
of the edge whose removal changes the shape least. It is shrunk to 90 %, 80 % … 10 %, 5 %, 2 %
and 1 % of its triangles, and after each step shape accuracy (A, both directions, against the
reference) is measured again; the run stops at the first step that fails. Headroom = 1 −
(triangles kept at the smallest passing step ÷ triangles at the start). A step passes when it
meets the group's shape gate or, for a piece that already misses the gate, when it is no more
than 5 % worse than the piece itself, so the check still says something useful about a piece
that is too coarse in one place and wasteful elsewhere. Pieces under 2 000 triangles (panel-line
strips, fasteners, small fittings) are left out: a thin strip or a small part loses its shape
after a few collapses, which says little about wasted triangles, and the sub-pixel and hidden
checks judge such parts better.

*Pass mark.* ≤ 15 %. The steps are 10 % apart, so this reads: "keeping 90 % may pass, keeping
80 % must fail". Blender's decimator is greedy, not optimal, so the headroom is a lower bound on
what a careful hand could save.

#### C2. Error map

*What.* Each triangle's shape error divided by its group's p99 gate, sorted into four bands: under
0.1 (could be coarser), 0.1–0.5, 0.5–1 (about right), over 1 (too coarse). The report gives the
share of each node's area in each band; the heat maps colour the triangles dark blue, light
blue, grey and red, with hidden triangles magenta and not-measured ones dark grey.

*Why.* Headroom says how much could go; the map shows where. A well-made model is mostly grey,
with blue only on flat areas that need few triangles anyway.

*Pass mark.* None (a diagnostic).

#### C3. Hidden triangles

*What.* Triangles that can never be seen: not from any direction outside the aircraft, not from
the pilots' eyepoints, in any configured pose (rest; controls deflected one way; controls the
other way; canopy open; gear up).

*Why.* They cost memory and vertex work and add nothing. Large hidden areas usually mean parts
overlap more than needed or there is structure nobody can see.

*How.* Sample points on each triangle (a 25 cm grid, at least 1 mm inside every edge); from each
sample, rays in 162 fixed directions spread evenly over the half-sphere in front of the triangle
(a Fibonacci spiral). Every non-glass triangle blocks rays; glass does not. A side of a triangle
is visible when some ray leaves the model's bounding sphere without hitting anything, or (front
side only, once the empties `eyepoint_front` and `eyepoint_rear` exist) when the straight line
to an eyepoint is clear. When the front is not visible, the back side is tested too, which is
how flipped faces (D) are found. In the other poses only the triangles the pose can change are
tested again: those of the parts that moved, and those whose rays were blocked by a moved part.
A ray blocked by a part that did not move stays blocked, so this saving is exact. A second
saving: when a blocked ray hits a **closed** piece (a watertight surface, so it has a proper
inside) and every sample of the triangle lies inside it (an odd number of surface crossings
along two fixed test rays), then every ray from the triangle must cross that piece, and the
triangle is hidden without casting the rest; a test compares this with casting every ray. The
report gives the hidden share per node and the largest connected clusters of hidden triangles
with their centre and size, so the modeller knows where to look. This is the slowest check, so
on a model of 20 000 triangles or more its rays are cast by worker processes (Blender started
again in the background, 4 by default, `--jobs N` to change) while the main process runs the
headroom check. Each triangle's answer depends only on the geometry, never on other triangles,
so the split cannot change the result; a test checks that it does not.

*Pass mark.* ≤ 2 % of each node's triangles. Some hidden triangles cannot be avoided where parts
meet (the end of a hinged surface tucked into its cut-out, a root buried in the fuselage); 2 %
allows those but flags a whole hidden part or an inner skin.

#### C4. Sub-pixel and thin triangles

*What.* Triangles smaller than one pixel, or narrower than half a pixel, at their group's
closest design distance (8 m exterior, 0.6 m cockpit; table in section 2).

*Why.* A GPU shades pixels in 2 × 2 blocks, so a triangle that covers one pixel still costs a
whole block: tiny triangles cost several times more per visible pixel, and their detail cannot
be seen. Thin triangles slip between pixel centres as the view moves and flicker ("shimmer").

*How.* Sub-pixel: area < p². Thin: the smallest height of the triangle < 0.5 p. At the closest
distance every triangle looks as large as it ever will, so a triangle that is sub-pixel there is
sub-pixel everywhere.

*Pass mark.* At most 10 % of a group's triangles sub-pixel. Fasteners, lights and small fittings
are small by nature; 10 % leaves room for them. Thin triangles are reported, not gated.

#### C5. Vertex efficiency, the index limit and draw calls

*What.* Exported vertices per triangle; the largest index in every primitive (one node and one
material); the numbers of primitives (draw calls), materials and nodes.

*Why.* The GPU processes every exported vertex, and a vertex is shared between triangles only
when its position, normal and colour are all the same, so colours set per face or hard edges
everywhere multiply the vertex count. A closed smooth mesh has about 0.5 vertices per triangle
(each vertex is shared by about six triangles). raylib 6.0 stores indices as 16-bit numbers, so
one primitive can use at most 65 536 vertices, and its glTF loader truncates larger indices
with only a warning (`rmodels.c`, around line 6093), which scrambles the mesh. Each primitive is
a draw call, which costs CPU time every frame (research 03, A1).

*How.* Counted from the file. Per primitive the report also gives the number of distinct
positions and of distinct (position, normal) pairs, which shows whether the extra vertices come
from normals or from colours.

*Pass mark.* At most 0.65 vertices per triangle: 30 % above a fully shared mesh, room for real
hard edges and material and colour boundaries. Every index ≤ 65 535: **hard**.

### D. Hygiene

| Fault | Rule | Why it matters | Gate |
|---|---|---|---|
| Degenerate | area < 1e-10 m² | invisible; breaks normal and lighting calculations | 0, hard |
| Sliver | smallest angle < 2° (not degenerate) | long needles blend normals and colours badly and flicker | ≤ 1 % |
| Duplicate face | the same three corners twice (after welding) | two surfaces in one place flicker against each other (z-fighting) | 0, hard |
| Flipped | only the back side can ever be seen; or the front is seen but the exported normals point against the winding; or both sides are seen and the normal is more than 90° off the reference's | with back-face culling the face vanishes or shows the inside; lighting is inverted | 0, hard |
| Inconsistent winding | an edge shared by exactly two triangles that both run along it the same way | the two disagree about which side is out (usually one is flipped) | 0 |
| Floating island | a connected piece more than 5 cm from the main cluster of pieces (bounding boxes, the same rule as `make_models.check_islands`) | a part visibly detached from the aircraft | 0, hard |

Non-manifold edges (shared by more than two triangles) and open edges are counted for
information. Inconsistent winding is not hard because the flipped check, which looks at what can
actually be seen, is the hard one; the winding count points to where to look. Where only the
model's front side can be seen but the reference faces the other way, the reference is the
flipped one: that is a warning, not a fault of the model.

### E. Shape plausibility

The bounding box of the model in its rest pose (gear down) against the published BAE Systems
Hawk 128 figures in the config: length 12.43 m, span 9.94 m, height 3.98 m. Pass mark ±2 %
(25 cm, 20 cm and 8 cm): tight enough to catch a wrong proportion, loose enough for small parts
at the extremities such as tip lights. (`make_models.py` itself allows 4 %.)

### F. Profile fairness

*What.* Whether an outline is smooth: the number of humps and dips, the sharpest bend ("kink")
and the largest step. It catches faults that are easy to miss in a few preview images, such as
a canopy with two humps or a step where the canopy meets the spine.

*How.* A probe in the config is a straight line with a ray direction (straight down by default).
Rays every 5 mm along the line give the first hit on the listed nodes and materials (glass
counts as surface), which traces the outline height along the line. Narrow parts are ignored:
connected pieces shorter than 10 cm along the line, such as canopy frame hoops, panel lines
running across, fasteners and lights; the report lists which were ignored. Humps and dips are
local maxima and minima that stand out by at least 2 mm (a maximum counts only once the outline
has dropped 2 mm below it again), so tessellation ripples do not count. The kink is the largest
change of slope between two 5 cm chords meeting at a sample; the step is the largest jump
between neighbouring samples beyond what the slope next to it explains.

*Pass marks* (per probe, in `jet.toml`):

| Probe | Line (x; y from → to) | Gate |
|---|---|---|
| `canopy_crown_left` / `_right` | x = ±0.10 m; y −3.55 → −0.40 m | ≤ 1 maximum, 0 minima (one smooth bubble) |
| `canopy_spine_left` / `_right` | x = ±0.10 m; y −0.60 → 1.20 m | 0 minima, kink ≤ 2°, step ≤ 2 mm |
| `nose_top` | x = 0; y −5.70 → −3.70 m | 0 maxima, 0 minima (rises steadily to the windscreen), step ≤ 2 mm |

A 2° change of slope between 5 cm chords is the bend of a 1.4 m radius: a gentle continuous
curve passes, a crease where two surfaces meet at an angle fails. A 2 mm step is a quarter of a
pixel at 8 m, below what the outline can show.

### G. Budgets

The user's decision of 2026-10-13 (T22a brief): at most **250 000 triangles in total** (hard),
exterior ≤ 100 000, cockpit ≤ 150 000, `.glb` ≤ 10 MB. Also at most 24 nodes and 40 primitives
(draw calls), the hero row of research 03 A2, and 10 materials (a proposal: each material adds a
draw call to every node that uses it). The cap is a ceiling, not a target: checks A to C decide
whether the triangles are earned.

## 4. The gates (`tools/models/audit/jet.toml`)

| Check | Gate | Hard |
|---|---|---|
| A. Shape, exterior group (`fuselage`, `canopy`, control surfaces, `airbrake`, `gear`) | p99 ≤ 1.0 mm, max ≤ 4.0 mm, in both directions | |
| A. Shape, cockpit group (`cockpit`, `hud_glass`) | p99 ≤ 0.3 mm, max ≤ 1.2 mm, in both directions | |
| B. Shading, both groups | p95 ≤ 2°, max ≤ 10° | |
| C1. Simplification headroom, pieces of ≥ 2 000 triangles | ≤ 15 % | |
| C3. Hidden triangles | ≤ 2 % per node | |
| C4. Sub-pixel triangles at the design distance | ≤ 10 % per group | |
| C5. Vertices per triangle | ≤ 0.65 | |
| C5. Index limit | every index ≤ 65 535 | yes |
| D. Degenerate / duplicate / flipped / floating | 0 each | yes |
| D. Inconsistent winding | 0 edges | |
| D. Slivers | ≤ 1 % of triangles | |
| E. Length / span / height against published | ±2 % | |
| F. Profile fairness | per probe (section 3 F) | |
| G. Triangles, total | ≤ 250 000 | yes |
| G. Triangles, exterior / cockpit | ≤ 100 000 / ≤ 150 000 | |
| G. File size | ≤ 10 MB | |
| G. Nodes / materials / primitives | ≤ 24 / ≤ 10 / ≤ 40 | |

Every number is in `jet.toml` (`[gates]`, `[budget]`, `[[groups]]`, `[[probes]]`), so changing a
gate never needs a code change. The other settings there (sample spacing, ray directions, poses,
probe lines) are explained in its comments.

## 5. How determinism is guaranteed

Two runs on the same files write byte-identical JSON:

* **No randomness.** Sample points are a fixed pattern computed with exact fractions; ray
  directions are a fixed Fibonacci spiral; the two inside-test rays are fixed vectors.
* **Stable order.** Nodes are processed in sorted name order, triangles in file order, ties
  broken by index (stable sorts throughout); lists in the report are sorted.
* **Deterministic tools.** Blender's BVH searches and its Decimate modifier give the same answer
  for the same input; both are tested (Decimate twice in one process, the whole audit in two
  separate processes).
* **Worker processes cannot change the answer.** Triangles are dealt to the workers in a fixed
  way, each triangle's result and ray count depend only on the geometry, and the parts are
  merged in order; the JSON is byte-identical with 1 or 3 processes (test 3d). The number of
  workers is not written to the JSON.
* **Rounded output.** Lengths to 1e-6 m (a micrometre), angles to 0.01°, fractions to 0.01 %,
  written with sorted keys. Rounding hides the last-bit differences that harmless changes of
  arithmetic order could cause.
* **Nothing run-dependent in the JSON.** Input files are named and identified by their SHA-256,
  not by their folder; timings go only into the Markdown report.

The guarantee is for the same inputs on the same Blender version (the JSON records it); another
CPU or Blender release could change a last digit.

## 6. Known-answer tests

`tools/models/test_audit.py` builds small models whose answers are known in advance, writes them
as `.glb` files and audits them. All 19 pass (Blender 5.2.2, 10 s in total):

| # | Test | Expected | Result |
|---|---|---|---|
| 1 | Sphere, r = 1 m, 16 segments, against a 128-segment sphere | largest deviation within 5 % of the exact chordal error | 37.09 mm measured, 37.36 mm exact (0.7 % under) |
| 2a | 64 × 64 flat plate (8 192 triangles) against a one-square plate | headroom ≥ 95 % | 99.0 % (80 triangles kept); the gate fails as it should |
| 2b | Two-triangle plate against itself | headroom 0, nothing flagged | 0 %, no gate fails, shape and shading error 0 |
| 3 | Closed box inside a closed box | inner 100 % hidden, outer 0 % | 100 % and 0 % |
| 3b | Box closed by a hinged lid | top face hidden at rest, seen once a pose opens the lid | as expected |
| 3c | Sphere holding a box inside, a box poking out and an inside-out box | the closed-piece shortcut gives the same hidden and flipped triangles as casting every ray | identical; 23 005 rays instead of 109 402 |
| 3d | The scene of 3c plus glass and a lid that a pose opens, in 1 and in 3 processes | byte-identical JSON | identical (26 hidden triangles, 44 066 rays both ways) |
| 3e | Cube inside a closed shell, with and without an eyepoint inside the shell | all 12 hidden without it; with it exactly the face towards the eyepoint is seen and the shortcut stays off | 12 hidden; triangles 4 and 5 (the -y face) seen, 0 shortcuts |
| 4 | Cube with one face wound inwards | exactly that face's 2 triangles flipped | triangles 10 and 11; 4 inconsistent edges; hard FAIL |
| 5 | 5 good, 4 sliver (1.9°, 1°, 0.5°, 0.1°) and 3 degenerate triangles | counted exactly | 3 degenerate, 4 slivers |
| 5b | Box with a part 3 cm away, one 20 cm away and a face written twice | 1 floating island, 1 duplicate | 1 (the far part), 1 |
| 6 | Primitive with 70 000 vertices; one with 65 536 | the first fails the index limit (hard, exit 1), the second passes | max index 69 999, exit 1; passes |
| 7a | Outline with one bump and a 4 cm frame hoop on top | 1 maximum, the hoop ignored | 1 maximum at y = 1.00, 0 minima, 1 part ignored |
| 7b | Two bumps with a dip between | 2 maxima, 1 minimum | 2 and 1, minimum at y = 1.00 |
| 7c | A 10 mm step; a 3° kink | both found where they are | step 10.0 mm at y = 1.00; kink 3.00° at y = 1.00 |
| 8 | Triangles of 0.999 p² and 1.001 p² at 8 m | 7 below, 5 above, classified correctly | 7 counted sub-pixel |
| 9a | Blender's Decimate run twice on the same mesh | identical output | identical |
| 9b | Two separate audit processes on the same files (every check exercised) | byte-identical JSON | identical (27 783 bytes) |
| - | GLB reader against the writer | same positions, normals, indices, nodes, transforms, 16- and 32-bit indices | identical |

## 7. Runtime

Target: the full audit of a 250 000-triangle model in about 3 minutes on this Mac (Apple M4,
10 cores, 16 GB; Blender 5.2.2). Measured, full mode unless stated:

| Run | Triangles: model / reference | Audit time |
|---|---|---|
| Smoke run on the current jet (a functional check only) | 48 088 / 572 444 | 21.7 s and 21.6 s, JSON byte-identical (41.5 s in one process) |
| Scale test, one process (`--jobs 1`) | 253 558 / 3 704 954 | 275 s (4.6 min) |
| Scale test, 4 worker processes (the default) | 253 558 / 3 704 954 | 168 s (2.8 min) |
| Scale test, `--fast` | 253 558 / 3 704 954 | 86 s |

How the 250 k figure was obtained: scaling the smoke run's time by the triangle count would give
about 1.9 min (3.6 min in one process), but the work does not grow in proportion to the
triangles alone. Shape and shading also grow with the size of the reference, and the headroom
and visibility checks depend on the model (every decimation step that passes and every hidden
triangle adds work). So the same generator was built at a higher detail (`--detail 2.6`, 253 558
triangles) with a reference at four times that detail (`--detail 10.4`), and that pair was
timed; its results were not used for anything else. In one process the time went to shape and
shading 32 s, headroom 96 s, visibility 134 s and the rest 13 s. The visibility rays now run in
worker processes alongside the headroom check, which is then the longest stage (121 s while the
workers run); that brought the total under the target. A model that meets the headroom and
hidden gates does less work than this test.

Building the reference comes on top of the audit: 17 s for the current jet's 572 444-triangle
reference, 151 s for the 3.7 M-triangle reference of the scale test. Memory: the main process
peaked at about 5.5 GB with the 3.7 M-triangle reference, each worker at about 0.6 GB.

## 8. Known limits and approximations

* **Tessellation, not design.** The reference is the same generator, so a shape that is wrong by
  design is wrong in both and passes A to C. Checks E and F and review by eye cover design.
* **The reference has its own error** (about 1/16 of the model's at `--detail 4`), so the shape
  error is slightly underestimated; the report warns when a node is less than 4× denser in the
  reference. Parts the generator does not refine with `--detail` are reported as not measured.
* **Sampled, not exact.** Shape error is the largest of a triangle's samples, not the exact
  largest distance (test 1: within 1 % on a sphere). Nearest-point distance does not know which
  surface "should" correspond, so near thin parts and creases a point can be close to the wrong
  surface; measuring in both directions limits this.
* **Visibility is sampled too.** 162 directions from points on a 25 cm grid: a triangle that can
  be seen only through a narrow gap, or only at a grazing angle, can fall between the rays and be
  reported hidden. Observers are assumed far away in any direction, plus the two eyepoints;
  glass is fully transparent; only the configured poses are tried. The inside test behind the
  shortcut is exact for a watertight piece apart from rays that graze an edge exactly, which the
  two independent test rays guard against.
* **Headroom depends on Blender's decimator**, which is greedy and ignores normals and colours,
  so it underestimates what could be saved; it is measured on pieces of at least 2 000 triangles
  and in 10 % steps.
* **Sub-pixel uses area at the closest design distance**, ignoring the viewing angle (a triangle
  seen edge-on covers fewer pixels).
* **Floating islands use bounding boxes**: a detached part that sits inside another part's box
  counts as touching (the same rule as the generator's own check).
* **Profiles see only where probes are placed**, ignore parts narrower than 10 cm along the line
  and skip gaps where the ray hits nothing (gaps are counted in the JSON).
* **GLB reader scope**: one binary chunk, triangle primitives, no sparse accessors or external
  buffers (anything else is reported as an error or a warning).

## 9. Decisions for the user

* The gate values in section 4, once CP2a exists to measure against (the user's decision of
  2026-10-13: there is no CP1 baseline).
* Which gates are hard. Proposed: the index limit, degenerate, duplicate, flipped and floating
  faults and the 250 k total. Should the 10 MB file cap be hard too?
* The probe lines in `jet.toml` (positions of the canopy and spine probes) should be checked
  against CP2a's geometry before the profile gates are approved.
* Reference build time: for a 250 k model it is about 2.5 min on top of the audit. A generator
  option to skip the baked ambient occlusion in reference builds (it only colours vertices, it
  does not change the shape) would shorten it; how much was not measured.
