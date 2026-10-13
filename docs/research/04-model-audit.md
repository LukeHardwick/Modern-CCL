# 04 - Model audit: shape fidelity and triangle efficiency (T22c)

Date: 2026-10-13, revised the same day after an independent review (section 10 lists the
changes). Tool: `tools/models/audit_model.py` (with `glb_io.py`, `audit_render.py`),
configuration `tools/models/audit/jet.toml`, wrapper `scripts/audit_models.sh`, tests
`tools/models/test_audit.py`. Written for the user's review of the method and the gates; the
gate values are **proposals** until the user sets them when the new jet (T22a CP2a) is finished.

## 1. What the audit is for

The hero jet is built by a script (T22a), and its quality used to be judged by eye. The audit
judges it by numbers that any run reproduces exactly: is the shape accurate where it shows, is
the shading right, and does every triangle earn its place? It reads the `.glb` file the viewer
loads, compares it with a much denser build of the same model, and checks the result against
pass marks ("gates") in `jet.toml`. Each gate is reported as one of:

* **PASS**: measured and within its limit.
* **FAIL**: measured and outside its limit.
* **FAIL: not measured**: the figure could not be measured (for example a probe line that misses
  the model, or a group whose triangles are all hidden). Missing data never passes.
* **ABSENT**: a group of nodes that is not in the model at all (the cockpit before T22a CP2b);
  neither a pass nor a fail, unless the config marks the group `required`, which makes it a FAIL.

A few gates are **hard** (a definite fault, such as a mesh raylib cannot load correctly): when
one fails the tool exits with status 1. Exit status 2 means the audit could not run: bad input (a
missing or broken file, a configuration error such as a misspelt key; one line says what) or an
error in the tool itself (the Python traceback is printed). A crash can never end with status 0,
so a script that checks the status cannot mistake it for a pass.

How to run it (Blender 5.2, headless; nothing is installed):

```sh
# Usual way: build the reference into a temporary folder, audit the committed jet.glb,
# write build/audit/jet/jet_audit.md and jet_audit.json (add --images for heat maps;
# --help lists the other options, such as --model, --reference and --generator).
scripts/audit_models.sh [--images] [--out DIR] [--fast]

# The tool itself, when you already have a reference build:
blender --background --factory-startup --python-exit-code 2 \
    --python tools/models/audit_model.py -- \
    --model M.glb --reference R.glb --config tools/models/audit/jet.toml --out DIR \
    [--images] [--fast] [--strict] [--jobs N]

# The known-answer tests (exit status 0 when all pass):
blender --background --factory-startup --python-exit-code 2 \
    --python tools/models/test_audit.py
```

`--python-exit-code 2` matters: without it Blender ends with status 0 even when the Python script
fails before it can set its own status.

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
and centre, plus a regular grid fine enough that neighbouring samples are at most 5 cm apart (on
triangles up to 20 m long; a longer one would get sparser samples and a warning). Each sample is
moved 1 % towards the triangle's centre so that no sample sits exactly on an edge two triangles
share. For each sample the tool finds the nearest point on the same-named reference node; the
distance is the error there, and a triangle's error is the largest of its samples. In the
reverse direction the centre of every reference triangle is measured against the model,
weighted by that reference triangle's area. The reverse direction catches shape the model has
lost, such as a flattened bulge or a missing part: the forward direction cannot, because every
model point may still lie close to *some* reference surface (test 1d: a model plate that lacks
the reference's 10 mm stud has forward error 0 and reverse max 10 mm). Statistics per node and
group: max, p99, p95 and mean, weighted by area, over the triangles that can be seen (hidden and
flipped triangles have their own checks) and are measured (next paragraph); a reverse point
counts when its nearest model triangle counts. The JSON also has the figures over all triangles.

*Not measured.* Where the generator does not refine a curved part with `--detail`, the model
and the reference contain the very same triangles there, so the distance is 0 although the flat
triangles still cut inside the intended curve: this reference cannot show their error. Such
triangles are **not measured**: they coincide with the reference (within 1 µm and 0.05°) and lie
on a curve, either smooth-shaded (their vertex normals differ by more than 1°) or flat-shaded
(an edge neighbour turns by 0.5° to 45°; a sharper turn is a real crease, such as the edge of a
box, and stays measured). They are left out of A, B, the error map and the headroom; counting
them as error 0 would make a model look better the less of it the reference refines. Their share
of each node's and group's visible area is reported and has its own gate.

*Missing data.* A group whose nodes are in the model but have nothing to measure (all hidden,
flipped or not measured) fails its A and B gates as "not measured". A model node without a
reference node of the same name fails ("not measured", the reason named), and so does a
reference node the model lacks.

*Pass mark.* Exterior: p99 ≤ 1.0 mm and max ≤ 4 mm. Cockpit: p99 ≤ 0.3 mm and max ≤ 1.2 mm.
In pixels: the exterior's 1 mm is an eighth of a pixel at its 8 m design distance and still
under half a pixel at 2 m, which leaves room for close outside views; the cockpit's 0.3 mm is
half a pixel at 0.6 m. The max is four times the p99, so a single worst spot on the exterior
stays under half a pixel at 8 m. Exterior parts that the pilot sees from close by (the canopy
frame, the top of the nose) are judged by the exterior gate too. Because chordal error
falls with the square of the number of points along a curve, placing points where the surface
curves most (curvature-adaptive tessellation) meets such a gate with far fewer triangles than a
uniform grid.

Not measured: at most 2 % of a group's visible area (not hard). A few small parts that the
generator keeps fixed are fine; a whole skin is not, because its accuracy is then unknown.

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
"reprojected samples"; test 1c). Statistics as in A, over the same triangles, except where the
reference itself faces the wrong way (see D).

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

Every step is measured exactly like the A gate: area-weighted p99 and max in both directions,
over the part of the surface the A gate measures (seen, not flipped, measured). For the piece
itself that is the same triangles, so its own figures equal its share of the A gate (test 1e
checks this to the micrometre), and "5 % worse" is relative to those figures. (On a piece with
more than 50 000 reverse points, every n-th one is used, for speed, so its reverse figures can
differ slightly.) A decimated piece has new, larger triangles that cross the old ones; there a
sample counts when the nearest triangle of the original piece counts, and a triangle's weight
is its area times the share of its samples that count. A piece none of whose triangles counts
(all hidden, flipped or not measured) is skipped and listed as such. Because it needs to know
what can be seen, this check runs after the visibility check.

*Pass mark.* ≤ 15 %. The steps are 10 % apart, so this reads: "keeping 90 % may pass, keeping
80 % must fail". Blender's decimator is greedy, not optimal, so the headroom is a lower bound on
what a careful hand could save.

#### C2. Error map

*What.* Each triangle's shape error divided by its group's p99 gate, sorted into four bands: under
0.1 (could be coarser), 0.1–0.5, 0.5–1 (about right), over 1 (too coarse). The report gives the
share of each node's measured, visible area in each band; the heat maps colour the triangles
dark blue, light blue, grey and red, with hidden triangles magenta and not-measured ones dark
grey.

*Why.* Headroom says how much could go; the map shows where. A well-made model is mostly grey,
with blue only on flat areas that need few triangles anyway.

*Pass mark.* None (a diagnostic).

#### C3. Hidden triangles

*What.* Triangles that can never be seen: not from any direction outside the aircraft, not from
the pilots' eyepoints, in any configured pose (rest; controls deflected one way; controls the
other way; canopy open; gear up).

*Why.* They cost memory and vertex work and add nothing. Large hidden areas usually mean parts
overlap more than needed or there is structure nobody can see.

*How.* Sample points on each triangle (a 25 cm grid on triangles up to 16 m long, at least 1 mm
inside every edge); from each sample, rays in 162 fixed directions spread evenly over the
half-sphere in front of the triangle (a Fibonacci spiral). Every non-glass triangle blocks rays;
glass does not (a material drawn see-through, alphaMode BLEND, that the config does not list as
glass gets a warning, because it is treated as opaque). A side of a triangle is visible when
some ray leaves the model's bounding sphere without hitting anything, or (once the empties
`eyepoint_front` and `eyepoint_rear` exist) when the straight line from an eyepoint to that side
is clear. When the front is not visible, the back side is tested too, from far away and from the
eyepoints, which is how flipped faces (D) are found: a cockpit panel whose back is all the pilot
can see is flipped, not hidden (test 3f).

Poses: a pose turns parts (a control surface, the canopy) or hides them (the gear when it is
up); everything attached below a part in the node tree moves or disappears with it (test 3h).
In the other poses only the triangles the pose can change are tested again: those of the parts
that moved or were hidden, and those whose rays were blocked by such a part. A ray blocked by a
part that did not move stays blocked, so this saving is exact. When an eyepoint itself moves in
a pose (it hangs under a part that turns), every triangle not yet seen is tested again in that
pose (test 3g).

A second saving: when a blocked ray hits a **closed** piece (a watertight surface, so it has a
proper inside) and every sample of the triangle lies inside it (an odd number of surface
crossings along two fixed test rays), then every ray from the triangle must cross that piece,
and the triangle is hidden without casting the rest; a test compares this with casting every
ray. It is never used for a piece with an eyepoint inside. The report gives the hidden share per
node (degenerate triangles, which have no area to see, are left out of it) and the largest
connected clusters of hidden triangles with their centre and size, so the modeller knows where
to look.

This is the slowest check, so on a model of 20 000 triangles or more its rays are cast by worker
processes (Blender started again in the background, 4 by default, `--jobs N` to change) while
the main process casts a share itself. A worker that fails, or is still running after
`worker_timeout` (20 minutes), is stopped and its share is cast in the main process instead:
slower, same result. Each triangle's answer depends only on the geometry, never on other
triangles, so the split cannot change the result; a test checks that it does not, also with
failing and hanging workers.

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
from normals or from colours. Every primitive gets its own index gate; when one node has two
primitives with the same material, their gates are told apart by the primitive's number.

*Pass mark.* At most 0.65 vertices per triangle: 30 % above a fully shared mesh, room for real
hard edges and material and colour boundaries. Every index ≤ 65 535: **hard**.

### D. Hygiene

| Fault | Rule | Why it matters | Gate |
|---|---|---|---|
| Degenerate | area < 1e-10 m² | invisible; breaks normal and lighting calculations | 0, hard |
| Sliver | smallest angle < 2° (not degenerate) | long needles blend normals and colours badly and flicker | ≤ 1 % |
| Duplicate face | the same three corners twice, in the same order (after welding) | two surfaces in one place facing the same way flicker against each other (z-fighting) | 0, hard |
| Flipped | only the back side can ever be seen (from outside or from an eyepoint); or the front is seen but the exported normals point against the winding; or both sides are seen and the normal is more than 90° off the reference's | with back-face culling the face vanishes or shows the inside; lighting is inverted | 0, hard |
| Inconsistent winding | an edge shared by exactly two triangles that both run along it the same way | the two disagree about which side is out (usually one is flipped) | 0 |
| Floating island | a connected piece more than 5 cm from the main cluster of pieces (bounding boxes, the same rule as `make_models.check_islands`) | a part visibly detached from the aircraft | 0, hard |

Non-manifold edges (shared by more than two triangles) and open edges are counted for
information. The same three corners in the opposite order are a back-to-back pair: a valid way
to make a thin part (a fin, a flap of skin) visible from both sides while the back faces are
culled, so such pairs are counted for information, not as duplicates. Inconsistent winding is not
hard because the flipped check, which looks at what can actually be seen, is the hard one; the
winding count points to where to look. Where only the model's front side can be seen but the
reference faces the other way, the reference is the flipped one: that is a warning, not a fault
of the model, and those triangles are left out of B.

Mirrored parts: glTF 2.0 says that a node whose transform mirrors the mesh (a negative
determinant, such as a scale of −1) has its front faces wound the other way round, and the audit
follows that rule. raylib 6.0 does not (it bakes the transform into the vertices and keeps the
winding), so the viewer would show such a part inside out; the report warns about every mirrored
node. The generator should mirror the mesh data instead.

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
change of slope between two 5 cm chords meeting at a sample. The step is the largest change of
height between neighbouring samples that the outline's slope does not explain: the change minus
what the slope of the 5 cm chord just before predicts, and the same with the chord just after;
the smaller of the two (in size) counts. Heights and slopes keep their signs, so a 3 mm drop on a
rising outline counts as 3 mm (test 7f); comparing sizes only, as the first version did, the rise
hid the drop. A probe can also run in another pose (for example with the canopy open); it then
traces the posed parts.

Missing data: at least 95 % of a probe's rays must hit (its coverage gate), so a probe line that
misses the model, or a node or material misspelt in the config, fails instead of passing on an
empty outline. A figure that needs more outline points than were hit (3 for the humps and dips,
two 5 cm chords for the kink and the step) is FAIL "not measured". The counts can be gated as
an exact number (`maxima = 1`: exactly one hump, so a missing canopy fails too) or as an upper
limit (`max_maxima`).

*Pass marks* (per probe, in `jet.toml`):

| Probe | Line (x; y from → to) | Gate |
|---|---|---|
| `canopy_crown_left` / `_right` | x = ±0.10 m; y −3.55 → −0.40 m | exactly 1 maximum, 0 minima (one smooth bubble) |
| `canopy_spine_left` / `_right` | x = ±0.10 m; y −0.60 → 1.20 m | 0 minima, kink ≤ 2°, step ≤ 2 mm |
| `nose_top` | x = 0; y −5.70 → −3.70 m | 0 maxima, 0 minima (rises steadily to the windscreen), step ≤ 2 mm |
| every probe | | at least 95 % of the rays hit |

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
| A. Groups in the model | exterior required (FAIL if absent); cockpit not required until T22a CP2b (ABSENT if missing) | |
| A. Shape, exterior group (`fuselage`, `canopy`, control surfaces, `airbrake`, `gear`) | p99 ≤ 1.0 mm, max ≤ 4.0 mm, in both directions | |
| A. Shape, cockpit group (`cockpit`, `hud_glass`) | p99 ≤ 0.3 mm, max ≤ 1.2 mm, in both directions | |
| A. Not measured (coincides with the reference on a curve) **new** | ≤ 2 % of each group's visible area | |
| A. Nodes in only one of the two files **new** | none (each such node is a FAIL) | |
| B. Shading, both groups | p95 ≤ 2°, max ≤ 10° | |
| C1. Simplification headroom, pieces of ≥ 2 000 triangles | ≤ 15 % | |
| C3. Hidden triangles | ≤ 2 % per node | |
| C4. Sub-pixel triangles at the design distance | ≤ 10 % per group | |
| C5. Vertices per triangle | ≤ 0.65 | |
| C5. Index limit | every index ≤ 65 535 | yes |
| D. Degenerate / duplicate (same winding) / flipped / floating | 0 each | yes |
| D. Inconsistent winding | 0 edges | |
| D. Slivers | ≤ 1 % of triangles | |
| E. Length / span / height against published | ±2 % | |
| F. Profile coverage **new** | ≥ 95 % of each probe's rays hit | |
| F. Profile fairness | per probe (section 3 F; the canopy crowns now need exactly 1 maximum) | |
| G. Triangles, total | ≤ 250 000 | yes |
| G. Triangles, exterior / cockpit | ≤ 100 000 / ≤ 150 000 | |
| G. File size | ≤ 10 MB | |
| G. Nodes / materials / primitives | ≤ 24 / ≤ 10 / ≤ 40 | |

Every gate whose figure cannot be measured is a FAIL ("not measured"), with the reason in the
report. Every number is in `jet.toml` (`[gates]`, `[budget]`, `[[groups]]`, `[[probes]]`), so
changing a gate never needs a code change. The other settings there (sample spacing and its
limits, ray directions, poses, probe lines, the worker timeout) are explained in its comments.
The file is checked when it is read: an unknown section or key (a misspelt gate would otherwise
silently not be checked), a group, pose or probe without its required keys, two entries with one
name, or a probe whose start and end coincide stops the audit with exit status 2.

## 5. How determinism is guaranteed

Two runs on the same files write byte-identical JSON:

* **No randomness.** Sample points are a fixed pattern computed with exact fractions; ray
  directions are a fixed Fibonacci spiral; the two inside-test rays are fixed vectors.
* **Stable order.** Nodes are processed in sorted name order, triangles in file order, ties
  broken by index (stable sorts throughout); lists in the report are sorted.
* **Deterministic tools.** Blender's BVH searches and its Decimate modifier give the same answer
  for the same input; both are tested (Decimate twice in one process, the whole audit in two
  separate processes). A nearest-point search first looks within 1 cm (about 2.5 times faster)
  and searches again without that limit where it found nothing; this is the same in every run,
  and it can change the answer only where two triangles are exactly equally near.
* **Worker processes cannot change the answer.** Triangles are dealt to the workers in a fixed
  way, each triangle's result and ray count depend only on the geometry, and the parts are
  merged in order; the JSON is byte-identical with 1 or 3 processes, and when workers fail or
  hang and the main process casts their share (test 3d). The number of workers is not written to
  the JSON.
* **Rounded output.** Lengths to 1e-6 m (a micrometre), angles to 0.01°, fractions to 0.01 %,
  written with sorted keys. Rounding hides the last-bit differences that harmless changes of
  arithmetic order could cause.
* **Nothing run-dependent in the JSON.** Input files are named and identified by their SHA-256,
  not by their folder; timings go only into the Markdown report.

The guarantee is for the same inputs on the same Blender version (the JSON records it); another
CPU or Blender release could change a last digit.

## 6. Known-answer tests

`tools/models/test_audit.py` builds small models whose answers are known in advance, writes them
as `.glb` files and audits them. All 36 pass (Blender 5.2.2, 10 s in total). Tests marked
**new** were added after the review (section 10).

| # | Test | Expected | Result |
|---|---|---|---|
| 1 | Sphere, r = 1 m, 16 segments, against a 128-segment sphere | largest deviation within 5 % of the exact chordal error | 37.09 mm measured, 37.36 mm exact (0.7 % under) |
| 1b **new** | Shading: a flat-shaded and a smooth 16-segment sphere against a smooth 128-segment one | flat: the facet angle at the worst corner sample, 15.62° (computed exactly); smooth: about 0 | flat 15.61°, B gate fails; smooth 0.06°, B gates pass |
| 1c **new** | Shading next to a crease: a cube against a box 3 mm larger at the sides, 8 mm at top and bottom | samples near an edge compare with the reference face that faces the same way: shading error 0; shape max 8 mm | 244 samples reprojected, shading max 0.00°, shape max 8.000 mm |
| 1d **new** | Reverse direction: a plate against a reference plate carrying a 10 mm stud | forward error 0 (gate passes), reverse max 10 mm (gate fails) | 0.0 mm forward; reverse max 10.0 mm, p99 0.0 mm |
| 1e **new** | Not measured, smooth: a node holding a 128-segment sphere written identically in both files (99 % of the area) and a coarse small sphere | the big sphere not measured; the figures equal the small sphere audited alone; not-measured gate fails; headroom skips the big piece, and the small piece's own error equals the A gate's | 99.13 % not measured; p99 = max = 13.231 mm, as for the small sphere alone; as expected |
| 1f **new** | Not measured, flat-shaded: a 24-segment sphere and a box, each written identically in both files | sphere: all not measured, A and B gates FAIL "not measured"; box: its 90° edges are creases, so all measured, error 0, gates pass | 528 of 528 sphere triangles not measured (100 %); box 0, max 0 mm |
| 2a | 64 × 64 flat plate (8 192 triangles) against a one-square plate | headroom ≥ 95 % | 99.0 % (80 triangles kept); the gate fails as it should |
| 2b | Two-triangle plate against itself | headroom 0, nothing flagged | 0 %, no gate fails, shape and shading error 0 |
| 2c **new** | The 5 % rule: a 64 × 64 plate 5 mm off its reference; a fine sphere 5 mm inside its reference | plate: every step stays at 5 mm (within 5.25 mm), headroom ≥ 95 %; sphere: the first step (90 %) is more than 5 % worse, headroom 0 | plate 99.0 %; sphere 6.116 mm → 7.041 mm at 90 %, headroom 0 |
| 3 | Closed box inside a closed box (the inner node also holds a degenerate triangle) | inner 100 % hidden, outer 0 %; the degenerate triangle not counted | 100 % (12 of 12) and 0 % |
| 3b | Box closed by a hinged lid | top face hidden at rest, seen once a pose opens the lid | as expected |
| 3c | Sphere holding a box inside, a box poking out and an inside-out box | the closed-piece shortcut gives the same hidden and flipped triangles as casting every ray | identical; 23 005 rays instead of 109 402 |
| 3d | The scene of 3c plus glass and a lid that a pose opens: 1 and 3 processes; workers that fail; workers that hang (stopped after a 1 s timeout) | byte-identical JSON in all four runs | identical (26 hidden triangles, 59 618 rays both ways) |
| 3e | Cube inside a closed shell, with and without an eyepoint inside the shell | all 12 hidden without it; with it exactly the face towards the eyepoint is seen and the shortcut stays off | 12 hidden; triangles 4 and 5 (the -y face) seen, 0 shortcuts |
| 3f **new** | A panel inside a closed shell whose front faces away from the eyepoint | flipped (2 triangles), not hidden; wound the other way: seen | 2 flipped, 0 hidden; 0 and 0 |
| 3g **new** | An eyepoint hung under a pivot that a pose turns 180° | both faces it sees (one per pose) count as seen | triangles 4–7 seen, 8 hidden |
| 3h **new** | A pose hides the parent of a closed hood around a cube; a probe in that pose | the hood goes with its parent: cube 0 of 12 hidden; the probe traces the cube (0.2 m), at rest the hood (0.5 m); a pose naming a missing node is warned about | 0 hidden; 0.5 m and 0.2 m; warned |
| 4 | Cube with one face wound inwards | exactly that face's 2 triangles flipped | triangles 10 and 11; 4 inconsistent edges; hard FAIL |
| 4b **new** | A box under a node mirrored by a scale of −1 | wound outwards in its own frame: 0 flipped, 0 hidden, a raylib warning; wound inwards: all 12 flipped | 0, 0, warned; 12 |
| 5 | 5 good, 4 sliver (1.9°, 1°, 0.5°, 0.1°) and 3 degenerate triangles | counted exactly | 3 degenerate, 4 slivers |
| 5b | Box with a part 3 cm away, one 20 cm away, a face written twice and a fin made of a square and its back-to-back copy | 1 floating island, 1 duplicate (hard), 2 back-to-back pairs (not a fault) | 1 (the far part), 1, 2 |
| 6 | Primitive with 70 000 vertices; one with 65 536 (two touching triangles wound with their normals) | the first fails the index limit and no other hard gate (exit 1); the second passes every hard gate | max index 69 999, exit 1, only the index gate fails hard; passes |
| 7a | Outline with one bump and a 4 cm frame hoop on top | 1 maximum, the hoop ignored | 1 maximum at y = 1.00, 0 minima, 1 part ignored |
| 7b | Two bumps with a dip between | 2 maxima, 1 minimum | 2 and 1, minimum at y = 1.00 |
| 7c | A 10 mm step; a 3° kink | both found where they are | step 10.0 mm at y = 1.00; kink 3.00° at y = 1.00 |
| 7d **new** | A probe beside the model; one running 1 m past its end; an 8 cm probe | coverage 0 % (FAIL) and the other gates FAIL "not measured"; 50 % (FAIL); 100 %, but kink and step "not measured" (too short) | 0 %; 49.9 %; as expected |
| 7e **new** | Exact count `maxima = 1` on one bump, a flat outline, two bumps; `maxima` and `max_maxima` together | pass, fail (0), fail (2); a configuration error | as expected |
| 7f **new** | A 3 mm step down, and one up, in an outline rising at 30° | 3 mm both ways, where it is | 3.0 mm at y = 1.0025 both ways |
| 8 | Triangles of 0.999 p² and 1.001 p² at 8 m | 7 below, 5 above, classified correctly | 7 counted sub-pixel |
| 9a | Blender's Decimate run twice on the same mesh | identical output | identical |
| 9b | Two separate audit processes on the same files (every check exercised, 2 workers each) | byte-identical JSON | identical (29 456 bytes) |
| 10 **new** | Missing data: a group whose nodes are all hidden; a node without a reference node, and the reverse; a group of such a node; an absent group, required and not; no reference at all | FAIL "not measured"; FAIL; FAIL naming the node; ABSENT and FAIL; FAIL "not measured" | as expected |
| 11 **new** | An internal error (forced), also in a Blender process started without `--python-exit-code`; a missing model file; a bad pose | exit 2 with the traceback; exit 2; exit 2 with one line | 2 and 2; 2; 2 |
| 12 **new** | Six config files with a mistake (misspelt key or section, missing key, name used twice, zero-length probe); `jet.toml` | each refused with a message naming it; `jet.toml` loads | as expected |
| - | GLB reader against the writer | same positions, normals, indices, nodes, transforms, 16- and 32-bit indices | identical |
| - **new** | GLB variants: a node with a matrix, positions and normals interleaved in one strided buffer, two primitives of one node with one material, a BLEND material not listed as glass | read correctly; the two primitives get distinct index-gate names; a warning | as expected |

## 7. Runtime

Target: the full audit of a 250 000-triangle model in about 3 minutes on this Mac (Apple M4,
10 cores, 16 GB; Blender 5.2.2). Measured with the first version of the tool, before the review
fixes (the jet was not run again afterwards; see below), full mode unless stated:

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
shading 32 s, headroom 96 s, visibility 134 s and the rest 13 s. In that version the visibility
rays ran in worker processes alongside the headroom check, which was then the longest stage
(121 s while the workers ran); that brought the total under the target. A model that meets the
headroom and hidden gates does less work than this test.

*After the review fixes.* The headroom check now measures exactly what the A gate measures, so it
has to wait for the visibility result (the main process casts a share of the rays meanwhile
instead), and each decimation step needs one more nearest-point search per sample. That made the
audit slower; making every nearest-point search look within 1 cm first (section 5) more than
made up for it. As the jet was not to be run again, both versions were timed on two synthetic
models instead (smooth ellipsoids crossing each other like a fuselage, wing and fin, plus a
hidden inner part, each against a reference with 16 times the triangles; 4 worker processes):

| Synthetic model | Triangles: model / reference | First version | After the review fixes |
|---|---|---|---|
| small | 47 200 / 764 800 | 12.7 s | 10.3 s (17.1 s before the faster searches) |
| large | 241 200 / 3 880 800 | 47.9 s | 35.0 s |

The scale test above should therefore not be slower now; by stage, the estimate for it is about
2.2 min (shape and shading about 20 s, visibility about 35 s, headroom about 60 s, the rest about
15 s). This is an estimate: the audit of the new jet (CP2a) gives the real figure.

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
* **Sample spacing has a ceiling.** The 5 cm shape grid holds on triangles up to 20 m long and
  the 25 cm visibility grid up to 16 m (`kmax`, `vis_kmax`); a longer triangle gets sparser
  samples and the report warns. Nothing on the jet comes close.
* **Visibility is sampled too.** 162 directions from points on a 25 cm grid: a triangle that can
  be seen only through a narrow gap, or only at a grazing angle, can fall between the rays and be
  reported hidden. Observers are assumed far away in any direction, plus the two eyepoints;
  glass is fully transparent; only the configured poses are tried. The inside test behind the
  shortcut is exact for a watertight piece apart from rays that graze an edge exactly, which the
  two independent test rays guard against.
* **Not measured is a judgement.** A triangle counts as not measured when it coincides with the
  reference and a neighbour turns by 0.5° to 45° (or its vertex normals spread by more than 1°);
  a coincident curve made of fewer than 8 facets per turn (45° or more between neighbours) would
  count as creases and be measured with error 0.
* **The decimated region is approximate.** A decimated piece counts the samples whose nearest
  original triangle counts, so its boundary with a hidden or not-measured area is blurred by
  about one original triangle.
* **Headroom depends on Blender's decimator**, which is greedy and ignores normals and colours,
  so it underestimates what could be saved; it is measured on pieces of at least 2 000 triangles
  and in 10 % steps.
* **Sub-pixel uses area at the closest design distance**, ignoring the viewing angle (a triangle
  seen edge-on covers fewer pixels).
* **Floating islands use bounding boxes**: a detached part that sits inside another part's box
  counts as touching (the same rule as the generator's own check).
* **Profiles see only where probes are placed**, ignore parts narrower than 10 cm along the line
  and skip gaps where the ray hits nothing (gaps are counted in the JSON; a probe with more than
  5 % gaps fails its coverage gate).
* **GLB reader scope**: one binary chunk, triangle primitives, no sparse accessors or external
  buffers (anything else is reported as an error or a warning).

## 9. Decisions for the user

* The gate values in section 4, once CP2a exists to measure against (the user's decision of
  2026-10-13: there is no CP1 baseline).
* The gates added after the review (all proposals, none hard): not measured ≤ 2 % of each
  group's visible area; every probe's coverage ≥ 95 %; exactly one maximum on the canopy crown
  probes; the exterior group required and the cockpit group not required until CP2b; a node
  that only one of the two files has counts as a FAIL. Also the time after which a stuck
  visibility worker is replaced (20 minutes).
* Which gates are hard. Proposed: the index limit, degenerate, duplicate, flipped and floating
  faults and the 250 k total. Should the 10 MB file cap be hard too?
* The probe lines in `jet.toml` (positions of the canopy and spine probes) should be checked
  against CP2a's geometry before the profile gates are approved.
* Reference build time: for a 250 k model it is about 2.5 min on top of the audit. A generator
  option to skip the baked ambient occlusion in reference builds (it only colours vertices, it
  does not change the shape) would shorten it; how much was not measured.

## 10. Changes after the review (2026-10-13)

An independent review of the first version found that some data could pass without being
measured, and some smaller faults. What changed, in the order of the review's findings:

1. **Not-measured triangles no longer count as perfect.** Triangles that coincide with the
   reference on a curve used to enter checks A and B, the error map and the headroom with error
   0. They are now left out, their share of the visible area is reported per node and group,
   and a new gate (≤ 2 %) catches a skin the reference cannot measure (tests 1e, 1f).
2. **Missing data fails.** A probe that misses the model fails its new coverage gate; a figure
   that cannot be measured is FAIL "not measured" instead of being dropped from the table; a
   node that only one of the two files has fails; a group that is not in the model is ABSENT,
   or FAIL when it is `required`; the canopy crown probes ask for exactly one maximum (the old
   "at most one" let a missing canopy pass); a misspelt key in the config stops the audit
   instead of silently switching its gate off; a pose naming a missing node is warned about
   (tests 7d, 7e, 10, 12, 3h).
3. **Steps are measured with their sign**, so a drop on a rising outline is no longer hidden by
   the rise (test 7f).
4. **A crash exits with status 2**, never 0 (the traceback is printed), and every documented
   command has `--python-exit-code 2` (test 11).
5. **A reversed face that only the pilot could see** is reported flipped, not hidden: the back
   side is tested from the eyepoints too (test 3f).
6. **Hiding a part in a pose hides its children** (test 3h).
7. **The headroom is measured like the A gate**: area-weighted in both directions, over the
   triangles the A gate measures, so its "5 % worse" rule compares like with like (tests 1e,
   2c). To keep the audit fast, nearest-point searches now look within 1 cm first (section 7).
8. **Test 6 now fails only the index gate**, and its 65 536-vertex model passes every hard
   gate.
9. **A probe in a pose that hides parts works** (it used to look up the wrong triangles), and
   its image shows the model in that pose.
10. **New tests** for all of the above (17 more, 36 in all; section 6).

Smaller points: the sample grids state their true limits (20 m and 16 m triangles, with a
warning beyond); a visibility worker that hangs is stopped after `worker_timeout` and its share
cast in the main process; two primitives of one node with one material get distinct gate
names; a see-through (BLEND) material not listed as glass is warned about; mirrored nodes follow
glTF 2.0 (and are warned about, since raylib does not); duplicates count only faces with the
same winding (back-to-back pairs are reported, not failed); the hidden share leaves out
degenerate triangles; an eyepoint that moves in a pose is handled; and `scripts/audit_models.sh`
no longer leaves `__pycache__` folders or, if something fails early, its temporary folder
behind.
