# Model audit: cessna

Model `cessna.glb` (0.66 MB, sha256 `f30b229c58f1`) against reference `cessna.glb` (sha256 `a9b942b9a981`), config `cessna.toml`; mode **fast**.
Coordinates: modelling frame of tools/models: metres, x = vehicle left, y = aft (nose at -y), z = up.

**76 of 89 gates pass; 12 fail (1 hard); 1 absent (group not in the model, not required).** Overall: **FAIL (hard)**.

| Check | Gate | Value | Limit | Result |
|---|---|---|---|---|
| A | Group cockpit: nodes in the model | 0 | >= 1 | ABSENT (none of its nodes is in the model (the group is not required)) |
| A | Shape accuracy exterior: model to reference max | 15.28 mm | <= 4.00 mm | **FAIL** |
| A | Shape accuracy exterior: model to reference p99 | 2.42 mm | <= 1.00 mm | **FAIL** |
| A | Group exterior: nodes in the model | 14 | >= 1 | PASS |
| A | Shape accuracy exterior: reference to model max | 14.24 mm | <= 4.00 mm | **FAIL** |
| A | Shape accuracy exterior: reference to model p99 | 1.62 mm | <= 1.00 mm | **FAIL** |
| A | Not measured exterior: share of the visible area | 0.00 % | <= 2.00 % | PASS |
| B | Shading exterior: normal error max | 90.00° | <= 10.00° | **FAIL** |
| B | Shading exterior: normal error p95 | 2.55° | <= 2.00° | **FAIL** |
| C | Simplification headroom fuselage component 0 (10511 tris) | 20.01 % | <= 15.00 % | **FAIL** |
| C | Hidden triangles aileron_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles aileron_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles door_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles door_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles elevator | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles flap_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles flap_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles fuselage | 2.14 % | <= 2.00 % | **FAIL** |
| C | Hidden triangles gear | 3.67 % | <= 2.00 % | **FAIL** |
| C | Hidden triangles prop_disc | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles propeller | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles rudder | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles wing_l | 0.38 % | <= 2.00 % | PASS |
| C | Hidden triangles wing_r | 0.38 % | <= 2.00 % | PASS |
| C | Index limit aileron_l / paint (162 vertices) | 161 | <= 65 535 | PASS |
| C | Index limit aileron_r / paint (162 vertices) | 161 | <= 65 535 | PASS |
| C | Index limit door_l / dark (203 vertices) | 202 | <= 65 535 | PASS |
| C | Index limit door_l / glass (54 vertices) | 53 | <= 65 535 | PASS |
| C | Index limit door_l / paint (269 vertices) | 268 | <= 65 535 | PASS |
| C | Index limit door_r / dark (200 vertices) | 199 | <= 65 535 | PASS |
| C | Index limit door_r / glass (63 vertices) | 62 | <= 65 535 | PASS |
| C | Index limit door_r / paint (255 vertices) | 254 | <= 65 535 | PASS |
| C | Index limit elevator / paint (380 vertices) | 379 | <= 65 535 | PASS |
| C | Index limit flap_l / paint (162 vertices) | 161 | <= 65 535 | PASS |
| C | Index limit flap_r / paint (162 vertices) | 161 | <= 65 535 | PASS |
| C | Index limit fuselage / dark (87 vertices) | 86 | <= 65 535 | PASS |
| C | Index limit fuselage / glass (698 vertices) | 697 | <= 65 535 | PASS |
| C | Index limit fuselage / paint (6318 vertices) | 6 317 | <= 65 535 | PASS |
| C | Index limit gear / dark (468 vertices) | 467 | <= 65 535 | PASS |
| C | Index limit gear / metal (80 vertices) | 79 | <= 65 535 | PASS |
| C | Index limit gear / paint (1170 vertices) | 1 169 | <= 65 535 | PASS |
| C | Index limit prop_disc / hud (96 vertices) | 95 | <= 65 535 | PASS |
| C | Index limit propeller / metal (656 vertices) | 655 | <= 65 535 | PASS |
| C | Index limit propeller / paint (241 vertices) | 240 | <= 65 535 | PASS |
| C | Index limit rudder / paint (218 vertices) | 217 | <= 65 535 | PASS |
| C | Index limit wing_l / light (32 vertices) | 31 | <= 65 535 | PASS |
| C | Index limit wing_l / paint (972 vertices) | 971 | <= 65 535 | PASS |
| C | Index limit wing_r / light (32 vertices) | 31 | <= 65 535 | PASS |
| C | Index limit wing_r / paint (972 vertices) | 971 | <= 65 535 | PASS |
| C | Sub-pixel triangles exterior at 8 m | 4.78 % | <= 10.00 % | PASS |
| C | Vertices per triangle | 0.624 | <= 0.650 | PASS |
| D | Degenerate triangles | 0 | <= 0 | PASS |
| D | Duplicate faces | 0 | <= 0 | PASS |
| D | Flipped faces | 5 | <= 0 | **FAIL (hard)** |
| D | Floating islands | 0 | <= 0 | PASS |
| D | Sliver triangles (< 2 deg) | 5.55 % | <= 1.00 % | **FAIL** |
| D | Inconsistent winding (edges) | 0 | <= 0 | PASS |
| E | Height 2.720 m vs published 2.72 m | 0.00 % | <= 2.00 % | PASS |
| E | Length 8.280 m vs published 8.28 m | 0.00 % | <= 2.00 % | PASS |
| E | Span 11.010 m vs published 11 m | 0.09 % | <= 2.00 % | PASS |
| F | Profile belly: share of rays that hit | 100.00 % | >= 95.00 % | PASS |
| F | Profile belly: largest kink | 2.19° | <= 2.00° | **FAIL** |
| F | Profile belly: maxima | 1 | <= 1 | PASS |
| F | Profile belly: minima | 0 | <= 0 | PASS |
| F | Profile belly: largest step | 0.07 mm | <= 2.00 mm | PASS |
| F | Profile cowl_to_windscreen_left: share of rays that hit | 99.43 % | >= 95.00 % | PASS |
| F | Profile cowl_to_windscreen_left: maxima | 0 | <= 0 | PASS |
| F | Profile cowl_to_windscreen_left: minima | 0 | <= 0 | PASS |
| F | Profile cowl_to_windscreen_left: largest step | 0.28 mm | <= 2.00 mm | PASS |
| F | Profile cowl_to_windscreen_right: share of rays that hit | 99.43 % | >= 95.00 % | PASS |
| F | Profile cowl_to_windscreen_right: maxima | 0 | <= 0 | PASS |
| F | Profile cowl_to_windscreen_right: minima | 0 | <= 0 | PASS |
| F | Profile cowl_to_windscreen_right: largest step | 0.28 mm | <= 2.00 mm | PASS |
| F | Profile tailcone_top_left: share of rays that hit | 100.00 % | >= 95.00 % | PASS |
| F | Profile tailcone_top_left: largest kink | 1.47° | <= 2.00° | PASS |
| F | Profile tailcone_top_left: maxima | 0 | <= 0 | PASS |
| F | Profile tailcone_top_left: minima | 0 | <= 0 | PASS |
| F | Profile tailcone_top_left: largest step | 0.07 mm | <= 2.00 mm | PASS |
| F | Profile tailcone_top_right: share of rays that hit | 100.00 % | >= 95.00 % | PASS |
| F | Profile tailcone_top_right: largest kink | 1.56° | <= 2.00° | PASS |
| F | Profile tailcone_top_right: maxima | 0 | <= 0 | PASS |
| F | Profile tailcone_top_right: minima | 0 | <= 0 | PASS |
| F | Profile tailcone_top_right: largest step | 0.07 mm | <= 2.00 mm | PASS |
| G | File size | 0.66 MB | <= 10.00 MB | PASS |
| G | Triangles, group exterior | 22 617 | <= 100 000 | PASS |
| G | Materials | 6 | <= 10 | PASS |
| G | Nodes | 14 | <= 24 | PASS |
| G | Primitives (draw calls) | 25 | <= 40 | PASS |
| G | Triangles, total | 22 617 | <= 250 000 | PASS |

## Model

22 617 triangles, 14 112 vertices (0.62 per triangle), 14 nodes, 25 primitives (draw calls), 6 materials; largest primitive 6 318 vertices. Size 8.280 × 11.010 × 2.720 m (length × span × height). Hidden: 383 triangles (1.69 % of the non-degenerate ones).

## Per node

Shape and shading over the triangles that are measured: seen (hidden and flipped ones are counted by their own checks) and not coinciding with the reference on a curved surface ("not measured", share of the visible area in its own column; the JSON also has the figures over all measured triangles).

| Node | Group | Tris | Verts/tri | Shape p99 / max (mm) | Reverse p99 / max (mm) | Normal p95 / max (°) | Not measured | Hidden | Flipped | Sub-pixel | Thin | Slivers |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| aileron_l | exterior | 196 | 0.83 | 0.30 / 0.30 | 0.26 / 0.27 | 0.34 / 0.50 | 0.0 % | 0.0 % | 0 | 14.8 % | 15.3 % | 30 |
| aileron_r | exterior | 196 | 0.83 | 0.30 / 0.30 | 0.27 / 0.27 | 0.34 / 0.50 | 0.0 % | 0.0 % | 1 | 16.8 % | 17.3 % | 34 |
| door_l | exterior | 540 | 0.97 | 2.10 / 2.78 | 1.45 / 2.70 | 4.92 / 90.00 | 0.0 % | 0.0 % | 0 | 0.0 % | 0.0 % | 0 |
| door_r | exterior | 528 | 0.98 | 2.11 / 2.78 | 1.44 / 2.73 | 4.91 / 90.00 | 0.0 % | 0.0 % | 0 | 0.0 % | 0.0 % | 0 |
| elevator | exterior | 492 | 0.77 | 0.03 / 0.08 | 0.03 / 0.07 | 0.11 / 0.11 | 0.0 % | 0.0 % | 0 | 12.2 % | 13.0 % | 64 |
| flap_l | exterior | 196 | 0.83 | 1.89 / 1.91 | 1.40 / 1.51 | 0.48 / 0.51 | 0.0 % | 0.0 % | 0 | 14.8 % | 15.3 % | 30 |
| flap_r | exterior | 196 | 0.83 | 1.89 / 1.91 | 1.71 / 1.81 | 0.48 / 0.51 | 0.0 % | 0.0 % | 0 | 16.8 % | 17.3 % | 34 |
| fuselage | exterior | 12177 | 0.58 | 1.69 / 15.28 | 1.18 / 14.24 | 2.36 / 86.23 | 0.0 % | 2.1 % | 2 | 2.5 % | 4.0 % | 634 |
| gear | exterior | 3024 | 0.57 | 3.05 / 3.45 | 2.75 / 3.13 | 7.01 / 43.33 | 0.0 % | 3.7 % | 0 | 6.3 % | 7.9 % | 33 |
| prop_disc | exterior | 96 | 1.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.0 % | 0.0 % | 0 | 0.0 % | 0.0 % | 48 |
| propeller | exterior | 1532 | 0.59 | 2.47 / 4.17 | 2.23 / 6.96 | 7.70 / 61.33 | 0.0 % | 0.0 % | 2 | 10.4 % | 14.4 % | 34 |
| rudder | exterior | 296 | 0.74 | 1.40 / 3.65 | 0.55 / 0.93 | 0.36 / 1.04 | 0.0 % | 0.0 % | 0 | 10.5 % | 11.5 % | 34 |
| wing_l | exterior | 1574 | 0.64 | 1.45 / 5.13 | 1.13 / 4.75 | 2.66 / 54.94 | 0.0 % | 0.4 % | 0 | 6.9 % | 8.8 % | 141 |
| wing_r | exterior | 1574 | 0.64 | 1.45 / 5.13 | 1.11 / 4.83 | 2.66 / 55.50 | 0.0 % | 0.4 % | 0 | 6.9 % | 8.8 % | 139 |

Error map (share of each node's area by per-triangle error ÷ the group's p99 gate; < 0.1 could be coarser, > 1 is too coarse):

| Node | < 0.1 | 0.1–0.5 | 0.5–1 | > 1 |
|---|---|---|---|---|
| aileron_l | 2.0 % | 98.0 % | 0.0 % | 0.0 % |
| aileron_r | 2.0 % | 98.0 % | 0.0 % | 0.0 % |
| door_l | 22.0 % | 13.5 % | 28.1 % | 36.5 % |
| door_r | 18.6 % | 12.4 % | 30.2 % | 38.8 % |
| elevator | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| flap_l | 67.3 % | 1.8 % | 1.8 % | 29.2 % |
| flap_r | 67.3 % | 1.8 % | 1.8 % | 29.2 % |
| fuselage | 6.9 % | 54.9 % | 30.1 % | 8.1 % |
| gear | 13.3 % | 1.9 % | 10.3 % | 74.5 % |
| prop_disc | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| propeller | 6.7 % | 41.3 % | 10.7 % | 41.3 % |
| rudder | 18.8 % | 73.4 % | 5.6 % | 2.1 % |
| wing_l | 19.4 % | 68.2 % | 9.1 % | 3.2 % |
| wing_r | 19.4 % | 68.2 % | 9.1 % | 3.2 % |

## Simplification headroom

Large connected pieces, decimated (Blender Decimate, collapse) until the shape error exceeds the gate or the piece's own error + 5 %, whichever is larger; measured like the A gate (visible, not flipped, measured triangles, area-weighted both ways).

| Node | Piece (centre, m) | Tris | Own p99 / max (mm) | Smallest passing ratio | Tris kept | Headroom |
|---|---|---|---|---|---|---|
| fuselage | (0.00, 1.46, 0.19) | 10511 | 0.97 / 1.39 | 0.8 | 8408 | 20.0 % |

## Hidden triangles: largest clusters

| Node | Tris | Area (m²) | Centre (x, y, z) m | Size (m) |
|---|---|---|---|---|
| fuselage | 75 | 0.2638 | (0.00, 0.18, 0.99) | 0.84 × 1.24 × 0.04 |
| fuselage | 73 | 0.1194 | (0.00, 4.36, 0.29) | 0.17 × 0.92 × 0.09 |
| fuselage | 48 | 0.0465 | (-0.16, 4.48, 0.16) | 0.10 × 0.68 × 0.11 |
| fuselage | 47 | 0.0454 | (0.16, 4.48, 0.16) | 0.09 × 0.68 × 0.11 |
| gear | 19 | 0.0109 | (0.00, -1.74, -0.84) | 0.06 × 0.11 × 0.18 |
| gear | 18 | 0.0064 | (0.00, -1.92, -1.07) | 0.13 × 0.08 × 0.04 |
| gear | 18 | 0.0064 | (0.00, -1.68, -1.07) | 0.13 × 0.08 × 0.04 |
| gear | 9 | 0.0044 | (-1.27, 0.35, -1.03) | 0.14 × 0.06 × 0.03 |
| gear | 9 | 0.0044 | (-1.27, 0.61, -1.03) | 0.14 × 0.06 × 0.03 |
| gear | 9 | 0.0044 | (1.27, 0.35, -1.03) | 0.14 × 0.06 × 0.03 |
| gear | 9 | 0.0044 | (1.27, 0.61, -1.03) | 0.14 × 0.06 × 0.03 |
| fuselage | 7 | 0.0454 | (0.02, 0.85, 0.99) | 0.51 × 0.10 × 0.01 |
| wing_l | 6 | 0.0018 | (5.44, 0.18, 1.18) | 0.00 × 0.05 × 0.05 |
| wing_r | 6 | 0.0018 | (-5.44, 0.18, 1.18) | 0.00 × 0.05 × 0.05 |
| fuselage | 5 | 0.0004 | (-2.84, 0.08, 1.06) | 0.02 × 0.07 × 0.03 |

## Flipped triangles: largest clusters

| Node | Tris | Area (m²) | Centre (x, y, z) m | Size (m) |
|---|---|---|---|---|
| fuselage | 2 | 0.0000 | (-1.64, 5.27, 0.16) | 0.00 × 0.03 × 0.01 |
| aileron_r | 1 | 0.0000 | (-3.36, 0.88, 1.09) | 0.00 × 0.05 × 0.00 |
| propeller | 1 | 0.0000 | (-0.05, -2.29, 0.98) | 0.01 × 0.00 × 0.01 |
| propeller | 1 | 0.0000 | (0.05, -2.29, -0.92) | 0.01 × 0.00 × 0.01 |

## Profiles

| Probe | Rays that hit | Maxima | Minima | Largest kink | Largest step | Excluded narrow parts |
|---|---|---|---|---|---|---|
| cowl_to_windscreen_left | 349 of 351 (99.4 %) | 0 | 0 | 5.5° at y = -1.40 | 0.3 mm at y = -1.42 | 0 |
| cowl_to_windscreen_right | 349 of 351 (99.4 %) | 0 | 0 | 5.5° at y = -1.40 | 0.3 mm at y = -1.42 | 0 |
| tailcone_top_left | 551 of 551 (100.0 %) | 0 | 0 | 1.5° at y = 2.56 | 0.1 mm at y = 2.45 | 0 |
| tailcone_top_right | 551 of 551 (100.0 %) | 0 | 0 | 1.6° at y = 1.79 | 0.1 mm at y = 2.45 | 0 |
| belly | 1221 of 1221 (100.0 %) | 1 | 0 | 2.2° at y = 0.64 | 0.1 mm at y = -1.63 | 0 |

## Hygiene

Degenerate 0, slivers 1255, duplicate faces (same winding) 0, back-to-back face pairs (not a fault) 0, flipped 5 (back side visible 0, normals against winding 0, normal error > 90° 5), floating islands 0, inconsistent winding edges 0, non-manifold edges 54, open (boundary) edges 733.

## Warnings

- 83 visible triangles face the opposite way in the reference (nodes door_l, door_r, flap_l, flap_r, fuselage, gear, propeller, rudder): the reference build is flipped there
- node fuselage: 3 triangles on curved surfaces (0.0 % of its visible area) are tessellated exactly as in the reference, so their shape error is not measured and they are left out of checks A and B (the generator does not scale them with --detail)
- node wing_l: 6 triangles on curved surfaces (0.0 % of its visible area) are tessellated exactly as in the reference, so their shape error is not measured and they are left out of checks A and B (the generator does not scale them with --detail)
- node wing_r: 6 triangles on curved surfaces (0.0 % of its visible area) are tessellated exactly as in the reference, so their shape error is not measured and they are left out of checks A and B (the generator does not scale them with --detail)

## Runtime

load 0.3 s, topology 0.1 s, shape+shading 1.1 s, visibility 2.2 s, hygiene 0.1 s, headroom 0.7 s, profiles 0.1 s, total 4.7 s, images 5.8 s; the visibility check ran in 4 worker processes and this one (the only part of this report that changes between runs).
