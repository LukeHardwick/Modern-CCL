# Model audit: cessna

Model `cessna.glb` (1.20 MB, sha256 `fc5a6a5617ea`) against reference `cessna.glb` (sha256 `ec0a187cb44e`), config `cessna.toml`; mode **full**.
Coordinates: modelling frame of tools/models: metres, x = vehicle left, y = aft (nose at -y), z = up.

**92 of 107 gates pass; 15 fail (0 hard).** Overall: **FAIL**.

| Check | Gate | Value | Limit | Result |
|---|---|---|---|---|
| A | Shape accuracy cockpit: model to reference max | 2.43 mm | <= 1.20 mm | **FAIL** |
| A | Shape accuracy cockpit: model to reference p99 | 0.70 mm | <= 0.30 mm | **FAIL** |
| A | Group cockpit: nodes in the model | 1 | >= 1 | PASS |
| A | Shape accuracy cockpit: reference to model max | 2.27 mm | <= 1.20 mm | **FAIL** |
| A | Shape accuracy cockpit: reference to model p99 | 0.48 mm | <= 0.30 mm | **FAIL** |
| A | Not measured cockpit: share of the visible area | 0.00 % | <= 2.00 % | PASS |
| A | Shape accuracy exterior: model to reference max | 1.96 mm | <= 4.00 mm | PASS |
| A | Shape accuracy exterior: model to reference p99 | 0.99 mm | <= 1.00 mm | PASS |
| A | Group exterior: nodes in the model | 14 | >= 1 | PASS |
| A | Shape accuracy exterior: reference to model max | 2.45 mm | <= 4.00 mm | PASS |
| A | Shape accuracy exterior: reference to model p99 | 0.76 mm | <= 1.00 mm | PASS |
| A | Not measured exterior: share of the visible area | 0.00 % | <= 2.00 % | PASS |
| B | Shading cockpit: normal error max | 28.72° | <= 10.00° | **FAIL** |
| B | Shading cockpit: normal error p95 | 0.54° | <= 2.00° | PASS |
| B | Shading exterior: normal error max | 90.00° | <= 10.00° | **FAIL** |
| B | Shading exterior: normal error p95 | 1.65° | <= 2.00° | PASS |
| C | Simplification headroom cockpit component 2 (2354 tris) | 40.06 % | <= 15.00 % | **FAIL** |
| C | Simplification headroom fuselage component 0 (9170 tris) | 30.01 % | <= 15.00 % | **FAIL** |
| C | Hidden triangles aileron_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles aileron_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles cockpit | 5.37 % | <= 2.00 % | **FAIL** |
| C | Hidden triangles door_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles door_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles elevator | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles flap_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles flap_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles fuselage | 2.36 % | <= 2.00 % | **FAIL** |
| C | Hidden triangles gear | 2.50 % | <= 2.00 % | **FAIL** |
| C | Hidden triangles prop_disc | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles propeller | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles rudder | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles wing_l | 2.48 % | <= 2.00 % | **FAIL** |
| C | Hidden triangles wing_r | 2.22 % | <= 2.00 % | **FAIL** |
| C | Index limit aileron_l / paint (120 vertices) | 119 | <= 65 535 | PASS |
| C | Index limit aileron_r / paint (120 vertices) | 119 | <= 65 535 | PASS |
| C | Index limit cockpit / dark (4989 vertices) | 4 988 | <= 65 535 | PASS |
| C | Index limit cockpit / light (159 vertices) | 158 | <= 65 535 | PASS |
| C | Index limit cockpit / metal (1701 vertices) | 1 700 | <= 65 535 | PASS |
| C | Index limit door_l / dark (326 vertices) | 325 | <= 65 535 | PASS |
| C | Index limit door_l / glass (96 vertices) | 95 | <= 65 535 | PASS |
| C | Index limit door_l / paint (408 vertices) | 407 | <= 65 535 | PASS |
| C | Index limit door_r / dark (326 vertices) | 325 | <= 65 535 | PASS |
| C | Index limit door_r / glass (96 vertices) | 95 | <= 65 535 | PASS |
| C | Index limit door_r / paint (408 vertices) | 407 | <= 65 535 | PASS |
| C | Index limit elevator / paint (212 vertices) | 211 | <= 65 535 | PASS |
| C | Index limit flap_l / paint (102 vertices) | 101 | <= 65 535 | PASS |
| C | Index limit flap_r / paint (102 vertices) | 101 | <= 65 535 | PASS |
| C | Index limit fuselage / dark (492 vertices) | 491 | <= 65 535 | PASS |
| C | Index limit fuselage / glass (693 vertices) | 692 | <= 65 535 | PASS |
| C | Index limit fuselage / light (48 vertices) | 47 | <= 65 535 | PASS |
| C | Index limit fuselage / metal (80 vertices) | 79 | <= 65 535 | PASS |
| C | Index limit fuselage / paint (6875 vertices) | 6 874 | <= 65 535 | PASS |
| C | Index limit gear / dark (1575 vertices) | 1 574 | <= 65 535 | PASS |
| C | Index limit gear / metal (196 vertices) | 195 | <= 65 535 | PASS |
| C | Index limit gear / paint (2017 vertices) | 2 016 | <= 65 535 | PASS |
| C | Index limit prop_disc / hud (288 vertices) | 287 | <= 65 535 | PASS |
| C | Index limit propeller / metal (1258 vertices) | 1 257 | <= 65 535 | PASS |
| C | Index limit propeller / paint (577 vertices) | 576 | <= 65 535 | PASS |
| C | Index limit rudder / paint (204 vertices) | 203 | <= 65 535 | PASS |
| C | Index limit wing_l / light (80 vertices) | 79 | <= 65 535 | PASS |
| C | Index limit wing_l / metal (84 vertices) | 83 | <= 65 535 | PASS |
| C | Index limit wing_l / paint (1213 vertices) | 1 212 | <= 65 535 | PASS |
| C | Index limit wing_r / light (64 vertices) | 63 | <= 65 535 | PASS |
| C | Index limit wing_r / metal (12 vertices) | 11 | <= 65 535 | PASS |
| C | Index limit wing_r / paint (1213 vertices) | 1 212 | <= 65 535 | PASS |
| C | Sub-pixel triangles cockpit at 0.6 m | 0.00 % | <= 10.00 % | PASS |
| C | Sub-pixel triangles exterior at 8 m | 6.14 % | <= 10.00 % | PASS |
| C | Vertices per triangle | 0.669 | <= 0.650 | **FAIL** |
| D | Degenerate triangles | 0 | <= 0 | PASS |
| D | Duplicate faces | 0 | <= 0 | PASS |
| D | Flipped faces | 0 | <= 0 | PASS |
| D | Floating islands | 0 | <= 0 | PASS |
| D | Sliver triangles (< 2 deg) | 1.56 % | <= 1.00 % | **FAIL** |
| D | Inconsistent winding (edges) | 0 | <= 0 | PASS |
| E | Height 2.720 m vs published 2.72 m | 0.00 % | <= 2.00 % | PASS |
| E | Length 8.280 m vs published 8.28 m | 0.00 % | <= 2.00 % | PASS |
| E | Span 11.010 m vs published 11 m | 0.09 % | <= 2.00 % | PASS |
| F | Profile belly: share of rays that hit | 100.00 % | >= 95.00 % | PASS |
| F | Profile belly: largest kink | 1.80° | <= 2.00° | PASS |
| F | Profile belly: maxima | 1 | <= 1 | PASS |
| F | Profile belly: minima | 0 | <= 0 | PASS |
| F | Profile belly: largest step | 0.05 mm | <= 2.00 mm | PASS |
| F | Profile cowl_to_windscreen_left: share of rays that hit | 99.43 % | >= 95.00 % | PASS |
| F | Profile cowl_to_windscreen_left: maxima | 0 | <= 0 | PASS |
| F | Profile cowl_to_windscreen_left: minima | 0 | <= 0 | PASS |
| F | Profile cowl_to_windscreen_left: largest step | 0.28 mm | <= 2.00 mm | PASS |
| F | Profile cowl_to_windscreen_right: share of rays that hit | 99.43 % | >= 95.00 % | PASS |
| F | Profile cowl_to_windscreen_right: maxima | 0 | <= 0 | PASS |
| F | Profile cowl_to_windscreen_right: minima | 0 | <= 0 | PASS |
| F | Profile cowl_to_windscreen_right: largest step | 0.28 mm | <= 2.00 mm | PASS |
| F | Profile tailcone_top_left: share of rays that hit | 100.00 % | >= 95.00 % | PASS |
| F | Profile tailcone_top_left: largest kink | 1.45° | <= 2.00° | PASS |
| F | Profile tailcone_top_left: maxima | 0 | <= 0 | PASS |
| F | Profile tailcone_top_left: minima | 0 | <= 0 | PASS |
| F | Profile tailcone_top_left: largest step | 0.06 mm | <= 2.00 mm | PASS |
| F | Profile tailcone_top_right: share of rays that hit | 100.00 % | >= 95.00 % | PASS |
| F | Profile tailcone_top_right: largest kink | 1.45° | <= 2.00° | PASS |
| F | Profile tailcone_top_right: maxima | 0 | <= 0 | PASS |
| F | Profile tailcone_top_right: minima | 0 | <= 0 | PASS |
| F | Profile tailcone_top_right: largest step | 0.06 mm | <= 2.00 mm | PASS |
| G | File size | 1.20 MB | <= 10.00 MB | PASS |
| G | Triangles, group cockpit | 8 543 | <= 150 000 | PASS |
| G | Triangles, group exterior | 30 519 | <= 100 000 | PASS |
| G | Materials | 6 | <= 10 | PASS |
| G | Nodes | 17 | <= 24 | PASS |
| G | Primitives (draw calls) | 32 | <= 40 | PASS |
| G | Triangles, total | 39 062 | <= 250 000 | PASS |

## Model

39 062 triangles, 26 134 vertices (0.67 per triangle), 17 nodes, 32 primitives (draw calls), 6 materials; largest primitive 6 875 vertices. Size 8.280 × 11.010 × 2.720 m (length × span × height). Hidden: 1 031 triangles (2.64 % of the non-degenerate ones).

## Per node

Shape and shading over the triangles that are measured: seen (hidden and flipped ones are counted by their own checks) and not coinciding with the reference on a curved surface ("not measured", share of the visible area in its own column; the JSON also has the figures over all measured triangles).

| Node | Group | Tris | Verts/tri | Shape p99 / max (mm) | Reverse p99 / max (mm) | Normal p95 / max (°) | Not measured | Hidden | Flipped | Sub-pixel | Thin | Slivers |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| aileron_l | exterior | 150 | 0.80 | 0.29 / 0.29 | 0.18 / 0.26 | 0.37 / 0.68 | 0.0 % | 0.0 % | 0 | 6.7 % | 8.0 % | 12 |
| aileron_r | exterior | 150 | 0.80 | 0.29 / 0.29 | 0.18 / 0.26 | 0.37 / 87.24 | 0.0 % | 0.0 % | 0 | 4.0 % | 5.3 % | 8 |
| cockpit | cockpit | 8543 | 0.80 | 0.70 / 2.43 | 0.48 / 2.27 | 0.54 / 28.72 | 0.0 % | 5.4 % | 0 | 0.0 % | 0.0 % | 98 |
| door_l | exterior | 978 | 0.85 | 0.84 / 0.86 | 0.74 / 0.79 | 2.57 / 90.00 | 0.0 % | 0.0 % | 0 | 0.0 % | 0.0 % | 0 |
| door_r | exterior | 978 | 0.85 | 0.84 / 0.86 | 0.74 / 0.79 | 2.57 / 90.00 | 0.0 % | 0.0 % | 0 | 0.0 % | 0.0 % | 0 |
| elevator | exterior | 252 | 0.84 | 0.11 / 0.11 | 0.09 / 0.10 | 0.20 / 89.28 | 0.0 % | 0.0 % | 0 | 7.1 % | 8.7 % | 22 |
| flap_l | exterior | 122 | 0.84 | 0.24 / 0.25 | 0.22 / 0.23 | 0.50 / 0.68 | 0.0 % | 0.0 % | 0 | 1.6 % | 3.3 % | 4 |
| flap_r | exterior | 122 | 0.84 | 0.24 / 0.25 | 0.22 / 0.23 | 0.50 / 0.68 | 0.0 % | 0.0 % | 0 | 2.5 % | 6.6 % | 8 |
| fuselage | exterior | 13233 | 0.62 | 1.07 / 1.96 | 0.84 / 2.45 | 1.94 / 88.58 | 0.0 % | 2.4 % | 0 | 3.8 % | 4.4 % | 356 |
| gear | exterior | 6562 | 0.58 | 0.97 / 1.03 | 0.79 / 1.01 | 2.37 / 90.00 | 0.0 % | 2.5 % | 0 | 13.4 % | 5.1 % | 21 |
| prop_disc | exterior | 480 | 0.60 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.0 % | 0.0 % | 0 | 0.0 % | 0.0 % | 0 |
| propeller | exterior | 3132 | 0.59 | 1.09 / 1.65 | 0.74 / 1.67 | 6.47 / 56.41 | 0.0 % | 0.0 % | 0 | 10.9 % | 7.5 % | 0 |
| rudder | exterior | 282 | 0.72 | 0.69 / 0.94 | 0.41 / 0.82 | 0.36 / 89.77 | 0.0 % | 0.0 % | 0 | 1.1 % | 2.5 % | 8 |
| wing_l | exterior | 2100 | 0.66 | 0.81 / 1.61 | 0.76 / 1.61 | 1.73 / 88.27 | 0.0 % | 2.5 % | 0 | 2.9 % | 2.9 % | 37 |
| wing_r | exterior | 1978 | 0.65 | 0.81 / 1.61 | 0.76 / 1.61 | 1.73 / 87.69 | 0.0 % | 2.2 % | 0 | 2.4 % | 2.7 % | 37 |

Error map (share of each node's area by per-triangle error ÷ the group's p99 gate; < 0.1 could be coarser, > 1 is too coarse):

| Node | < 0.1 | 0.1–0.5 | 0.5–1 | > 1 |
|---|---|---|---|---|
| aileron_l | 24.6 % | 75.3 % | 0.0 % | 0.0 % |
| aileron_r | 24.6 % | 75.3 % | 0.0 % | 0.0 % |
| cockpit | 65.5 % | 4.8 % | 17.4 % | 12.3 % |
| door_l | 7.9 % | 79.7 % | 12.4 % | 0.0 % |
| door_r | 7.9 % | 79.7 % | 12.4 % | 0.0 % |
| elevator | 93.3 % | 6.7 % | 0.0 % | 0.0 % |
| flap_l | 40.6 % | 59.4 % | 0.0 % | 0.0 % |
| flap_r | 40.6 % | 59.4 % | 0.0 % | 0.0 % |
| fuselage | 8.1 % | 49.6 % | 40.2 % | 2.1 % |
| gear | 15.3 % | 12.5 % | 71.8 % | 0.4 % |
| prop_disc | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| propeller | 13.8 % | 49.9 % | 35.1 % | 1.2 % |
| rudder | 36.8 % | 59.6 % | 3.5 % | 0.0 % |
| wing_l | 19.4 % | 53.8 % | 26.4 % | 0.4 % |
| wing_r | 19.6 % | 53.8 % | 26.2 % | 0.4 % |

## Simplification headroom

Large connected pieces, decimated (Blender Decimate, collapse) until the shape error exceeds the gate or the piece's own error + 5 %, whichever is larger; measured like the A gate (visible, not flipped, measured triangles, area-weighted both ways).

| Node | Piece (centre, m) | Tris | Own p99 / max (mm) | Smallest passing ratio | Tris kept | Headroom |
|---|---|---|---|---|---|---|
| cockpit | (0.00, 0.88, 0.32) | 2354 | 0.77 / 0.81 | 0.6 | 1411 | 40.1 % |
| fuselage | (0.00, 1.46, 0.19) | 9170 | 1.06 / 2.00 | 0.7 | 6418 | 30.0 % |

## Hidden triangles: largest clusters

| Node | Tris | Area (m²) | Centre (x, y, z) m | Size (m) |
|---|---|---|---|---|
| cockpit | 66 | 0.0049 | (-0.25, -0.94, 0.08) | 0.12 × 0.03 × 0.02 |
| cockpit | 66 | 0.0049 | (0.25, -0.94, 0.08) | 0.12 × 0.03 × 0.02 |
| fuselage | 52 | 0.1624 | (0.00, 4.42, 0.29) | 0.18 × 1.46 × 0.12 |
| fuselage | 35 | 0.0449 | (-0.16, 4.49, 0.16) | 0.10 × 0.68 × 0.11 |
| fuselage | 35 | 0.0447 | (0.16, 4.49, 0.16) | 0.10 × 0.68 × 0.11 |
| cockpit | 27 | 0.0362 | (-0.34, 0.28, 0.96) | 0.05 × 1.05 × 0.02 |
| cockpit | 26 | 0.0366 | (0.34, 0.28, 0.97) | 0.05 × 1.05 × 0.02 |
| fuselage | 25 | 0.1415 | (-0.06, 0.14, 0.99) | 0.69 × 1.09 × 0.03 |
| wing_l | 21 | 0.3235 | (0.15, 0.30, 0.96) | 0.31 × 1.10 × 0.03 |
| wing_r | 19 | 0.2908 | (-0.15, 0.25, 0.96) | 0.31 × 0.99 × 0.02 |
| fuselage | 18 | 0.0608 | (0.36, 0.28, 0.98) | 0.08 × 1.05 × 0.02 |
| cockpit | 17 | 0.0050 | (0.27, -1.22, 0.18) | 0.07 × 0.00 × 0.09 |
| cockpit | 16 | 0.0007 | (0.06, -1.15, -0.02) | 0.03 × 0.00 × 0.03 |
| cockpit | 16 | 0.0050 | (0.16, -1.22, 0.18) | 0.08 × 0.00 × 0.09 |
| cockpit | 16 | 0.0050 | (0.16, -1.22, 0.30) | 0.08 × 0.00 × 0.09 |

## Flipped triangles: largest clusters

None.

## Profiles

| Probe | Rays that hit | Maxima | Minima | Largest kink | Largest step | Excluded narrow parts |
|---|---|---|---|---|---|---|
| cowl_to_windscreen_left | 349 of 351 (99.4 %) | 0 | 0 | 5.6° at y = -1.40 | 0.3 mm at y = -1.42 | 0 |
| cowl_to_windscreen_right | 349 of 351 (99.4 %) | 0 | 0 | 5.6° at y = -1.40 | 0.3 mm at y = -1.42 | 0 |
| tailcone_top_left | 551 of 551 (100.0 %) | 0 | 0 | 1.4° at y = 2.56 | 0.1 mm at y = 1.79 | 0 |
| tailcone_top_right | 551 of 551 (100.0 %) | 0 | 0 | 1.4° at y = 2.56 | 0.1 mm at y = 1.79 | 0 |
| belly | 1221 of 1221 (100.0 %) | 1 | 0 | 1.8° at y = 0.69 | 0.0 mm at y = -1.13 | 0 |

## Hygiene

Degenerate 0, slivers 611, duplicate faces (same winding) 0, back-to-back face pairs (not a fault) 0, flipped 0 (back side visible 0, normals against winding 0, normal error > 90° 0), floating islands 0, inconsistent winding edges 0, non-manifold edges 72, open (boundary) edges 2760.

## Warnings

- 154 visible triangles face the opposite way in the reference (nodes aileron_l, aileron_r, door_l, door_r, elevator, flap_r, fuselage, propeller, rudder, wing_r): the reference build is flipped there
- group cockpit: listed nodes not in the model: cockpit_panel
- node fuselage: 2 triangles on curved surfaces (0.0 % of its visible area) are tessellated exactly as in the reference, so their shape error is not measured and they are left out of checks A and B (the generator does not scale them with --detail)
- node gear: 7 triangles on curved surfaces (0.0 % of its visible area) are tessellated exactly as in the reference, so their shape error is not measured and they are left out of checks A and B (the generator does not scale them with --detail)

## Runtime

load 0.5 s, topology 0.1 s, shape+shading 2.5 s, visibility 5.4 s, hygiene 0.2 s, headroom 2.0 s, profiles 0.1 s, total 11.1 s, images 6.5 s; the visibility check ran in 4 worker processes and this one (the only part of this report that changes between runs).
