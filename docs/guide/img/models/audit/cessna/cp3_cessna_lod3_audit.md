# Model audit: cessna_lod3

Model `cessna_lod3.glb` (0.09 MB, sha256 `e0618dd7b4ba`) against reference `cessna.glb` (sha256 `e809ba59291f`), config `cessna_lod3.toml`; mode **full**.
Coordinates: modelling frame of tools/models: metres, x = vehicle left, y = aft (nose at -y), z = up.

**56 of 64 gates pass; 8 fail (2 hard).** Overall: **FAIL (hard)**.

| Check | Gate | Value | Limit | Result |
|---|---|---|---|---|
| A | Shape accuracy exterior: model to reference max | 104.96 mm | <= 855.30 mm | PASS |
| A | Shape accuracy exterior: model to reference p99 | 100.64 mm | <= 213.80 mm | PASS |
| A | Group exterior: nodes in the model | 14 | >= 1 | PASS |
| A | Shape accuracy exterior: reference to model max | 330.17 mm | <= 855.30 mm | PASS |
| A | Shape accuracy exterior: reference to model p99 | 99.42 mm | <= 213.80 mm | PASS |
| A | Not measured exterior: share of the visible area | 0.02 % | <= 2.00 % | PASS |
| A | Node cockpit: in the reference, not in the model | - | - | **FAIL** |
| B | Shading exterior: normal error max | 90.00° | <= 90.00° | PASS |
| B | Shading exterior: normal error p95 | 85.85° | <= 10.00° | **FAIL** |
| C | Hidden triangles aileron_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles aileron_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles door_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles door_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles elevator | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles flap_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles flap_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles fuselage | 0.56 % | <= 2.00 % | PASS |
| C | Hidden triangles gear | 1.95 % | <= 2.00 % | PASS |
| C | Hidden triangles prop_disc | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles propeller | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles rudder | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles wing_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles wing_r | 0.00 % | <= 2.00 % | PASS |
| C | Index limit aileron_l / paint (18 vertices) | 17 | <= 65 535 | PASS |
| C | Index limit aileron_r / paint (18 vertices) | 17 | <= 65 535 | PASS |
| C | Index limit door_l / dark (40 vertices) | 39 | <= 65 535 | PASS |
| C | Index limit door_l / glass (4 vertices) | 3 | <= 65 535 | PASS |
| C | Index limit door_l / paint (80 vertices) | 79 | <= 65 535 | PASS |
| C | Index limit door_r / dark (40 vertices) | 39 | <= 65 535 | PASS |
| C | Index limit door_r / glass (4 vertices) | 3 | <= 65 535 | PASS |
| C | Index limit door_r / paint (80 vertices) | 79 | <= 65 535 | PASS |
| C | Index limit elevator / paint (36 vertices) | 35 | <= 65 535 | PASS |
| C | Index limit flap_l / paint (24 vertices) | 23 | <= 65 535 | PASS |
| C | Index limit flap_r / paint (24 vertices) | 23 | <= 65 535 | PASS |
| C | Index limit fuselage / glass (74 vertices) | 73 | <= 65 535 | PASS |
| C | Index limit fuselage / paint (589 vertices) | 588 | <= 65 535 | PASS |
| C | Index limit gear / dark (105 vertices) | 104 | <= 65 535 | PASS |
| C | Index limit gear / metal (80 vertices) | 79 | <= 65 535 | PASS |
| C | Index limit gear / paint (105 vertices) | 104 | <= 65 535 | PASS |
| C | Index limit prop_disc / hud (72 vertices) | 71 | <= 65 535 | PASS |
| C | Index limit propeller / metal (28 vertices) | 27 | <= 65 535 | PASS |
| C | Index limit propeller / paint (65 vertices) | 64 | <= 65 535 | PASS |
| C | Index limit rudder / paint (24 vertices) | 23 | <= 65 535 | PASS |
| C | Index limit wing_l / light (12 vertices) | 11 | <= 65 535 | PASS |
| C | Index limit wing_l / paint (94 vertices) | 93 | <= 65 535 | PASS |
| C | Index limit wing_r / light (12 vertices) | 11 | <= 65 535 | PASS |
| C | Index limit wing_r / paint (94 vertices) | 93 | <= 65 535 | PASS |
| C | Sub-pixel triangles exterior at 400 m | 93.92 % | <= 10.00 % | **FAIL** |
| C | Vertices per triangle | 1.017 | <= 0.650 | **FAIL** |
| D | Degenerate triangles | 3 | <= 0 | **FAIL (hard)** |
| D | Duplicate faces | 0 | <= 0 | PASS |
| D | Flipped faces | 62 | <= 0 | **FAIL (hard)** |
| D | Floating islands | 0 | <= 0 | PASS |
| D | Sliver triangles (< 2 deg) | 6.08 % | <= 1.00 % | **FAIL** |
| D | Inconsistent winding (edges) | 4 | <= 0 | **FAIL** |
| E | Height 2.720 m vs published 2.72 m | 0.00 % | <= 2.00 % | PASS |
| E | Length 8.280 m vs published 8.28 m | 0.00 % | <= 2.00 % | PASS |
| E | Span 11.010 m vs published 11 m | 0.09 % | <= 2.00 % | PASS |
| G | File size | 0.09 MB | <= 10.00 MB | PASS |
| G | Triangles, group exterior | 1 693 | <= 2 000 | PASS |
| G | Materials | 6 | <= 10 | PASS |
| G | Nodes | 14 | <= 24 | PASS |
| G | Primitives (draw calls) | 24 | <= 40 | PASS |
| G | Triangles, total | 1 693 | <= 2 000 | PASS |

## Model

1 693 triangles, 1 722 vertices (1.02 per triangle), 14 nodes, 24 primitives (draw calls), 6 materials; largest primitive 589 vertices. Size 8.280 × 11.010 × 2.720 m (length × span × height). Hidden: 10 triangles (0.59 % of the non-degenerate ones).

## Per node

Shape and shading over the triangles that are measured: seen (hidden and flipped ones are counted by their own checks) and not coinciding with the reference on a curved surface ("not measured", share of the visible area in its own column; the JSON also has the figures over all measured triangles).

| Node | Group | Tris | Verts/tri | Shape p99 / max (mm) | Reverse p99 / max (mm) | Normal p95 / max (°) | Not measured | Hidden | Flipped | Sub-pixel | Thin | Slivers |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| aileron_l | exterior | 8 | 2.25 | 4.32 / 4.32 | 3.96 / 4.03 | 2.43 / 2.43 | 0.0 % | 0.0 % | 0 | 50.0 % | 50.0 % | 0 |
| aileron_r | exterior | 8 | 2.25 | 4.32 / 4.32 | 3.96 / 4.03 | 2.43 / 2.43 | 0.0 % | 0.0 % | 0 | 50.0 % | 50.0 % | 0 |
| door_l | exterior | 98 | 1.27 | 71.88 / 71.98 | 50.49 / 69.42 | 89.39 / 90.00 | 0.3 % | 0.0 % | 2 | 96.9 % | 92.9 % | 0 |
| door_r | exterior | 98 | 1.27 | 71.88 / 71.98 | 50.49 / 69.42 | 89.39 / 89.99 | 0.3 % | 0.0 % | 2 | 96.9 % | 92.9 % | 0 |
| elevator | exterior | 16 | 2.25 | 3.32 / 3.32 | 3.11 / 3.11 | 1.17 / 1.17 | 0.0 % | 0.0 % | 0 | 50.0 % | 50.0 % | 0 |
| flap_l | exterior | 14 | 1.71 | 3.76 / 3.76 | 3.35 / 3.35 | 1.75 / 1.75 | 0.0 % | 0.0 % | 0 | 71.4 % | 42.9 % | 0 |
| flap_r | exterior | 14 | 1.71 | 3.76 / 3.76 | 3.35 / 3.35 | 1.75 / 1.75 | 0.0 % | 0.0 % | 0 | 71.4 % | 42.9 % | 0 |
| fuselage | exterior | 709 | 0.94 | 101.65 / 104.96 | 281.95 / 330.17 | 89.26 / 89.56 | 0.0 % | 0.6 % | 39 | 95.6 % | 87.3 % | 97 |
| gear | exterior | 310 | 0.94 | 59.33 / 59.33 | 63.24 / 71.00 | 81.32 / 90.00 | 0.0 % | 1.9 % | 0 | 100.0 % | 98.1 % | 6 |
| prop_disc | exterior | 120 | 0.60 | 5.24 / 5.24 | 0.00 / 0.00 | 0.00 / 0.00 | 0.0 % | 0.0 % | 0 | 100.0 % | 100.0 % | 0 |
| propeller | exterior | 132 | 0.70 | 6.21 / 6.21 | 7.55 / 10.38 | 31.05 / 31.05 | 0.0 % | 0.0 % | 19 | 100.0 % | 100.0 % | 0 |
| rudder | exterior | 14 | 1.71 | 11.88 / 11.88 | 9.27 / 9.63 | 89.87 / 89.87 | 0.0 % | 0.0 % | 0 | 85.7 % | 42.9 % | 0 |
| wing_l | exterior | 76 | 1.39 | 97.12 / 97.12 | 97.65 / 184.75 | 85.85 / 89.77 | 0.0 % | 0.0 % | 0 | 73.7 % | 68.4 % | 0 |
| wing_r | exterior | 76 | 1.39 | 97.12 / 97.12 | 97.12 / 97.65 | 85.85 / 89.77 | 0.0 % | 0.0 % | 0 | 73.7 % | 68.4 % | 0 |

Error map (share of each node's area by per-triangle error ÷ the group's p99 gate; < 0.1 could be coarser, > 1 is too coarse):

| Node | < 0.1 | 0.1–0.5 | 0.5–1 | > 1 |
|---|---|---|---|---|
| aileron_l | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| aileron_r | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| door_l | 82.0 % | 18.0 % | 0.0 % | 0.0 % |
| door_r | 82.0 % | 18.0 % | 0.0 % | 0.0 % |
| elevator | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| flap_l | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| flap_r | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| fuselage | 41.5 % | 58.5 % | 0.0 % | 0.0 % |
| gear | 59.2 % | 40.8 % | 0.0 % | 0.0 % |
| prop_disc | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| propeller | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| rudder | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| wing_l | 10.1 % | 89.9 % | 0.0 % | 0.0 % |
| wing_r | 10.1 % | 89.9 % | 0.0 % | 0.0 % |

## Hidden triangles: largest clusters

| Node | Tris | Area (m²) | Centre (x, y, z) m | Size (m) |
|---|---|---|---|---|
| fuselage | 3 | 0.0028 | (0.12, 4.85, 0.16) | 0.08 × 0.60 × 0.02 |
| fuselage | 1 | 0.0065 | (-0.01, 4.85, 0.26) | 0.02 × 0.60 × 0.04 |
| gear | 1 | 0.0014 | (-1.34, 0.41, -1.04) | 0.00 × 0.09 × 0.03 |
| gear | 1 | 0.0014 | (-1.20, 0.41, -1.04) | 0.00 × 0.09 × 0.03 |
| gear | 1 | 0.0008 | (-0.06, -1.86, -1.08) | 0.00 × 0.07 × 0.03 |
| gear | 1 | 0.0008 | (0.06, -1.86, -1.08) | 0.00 × 0.07 × 0.03 |
| gear | 1 | 0.0014 | (1.20, 0.41, -1.04) | 0.00 × 0.09 × 0.03 |
| gear | 1 | 0.0014 | (1.34, 0.41, -1.04) | 0.00 × 0.09 × 0.03 |

## Flipped triangles: largest clusters

| Node | Tris | Area (m²) | Centre (x, y, z) m | Size (m) |
|---|---|---|---|---|
| fuselage | 10 | 0.8411 | (0.01, -1.82, -0.31) | 1.03 × 0.84 × 0.55 |
| fuselage | 4 | 0.2860 | (-1.67, 0.18, 0.38) | 2.37 × 0.27 × 1.35 |
| fuselage | 4 | 1.9878 | (0.00, 3.25, -0.14) | 0.89 × 2.60 × 0.55 |
| fuselage | 4 | 0.2860 | (1.67, 0.18, 0.38) | 2.37 × 0.27 × 1.35 |
| propeller | 4 | 0.0596 | (-0.02, -2.29, 0.42) | 0.12 × 0.08 × 0.48 |
| propeller | 4 | 0.0081 | (-0.02, -2.30, 0.94) | 0.11 × 0.03 × 0.08 |
| propeller | 4 | 0.0081 | (0.02, -2.30, -0.89) | 0.11 × 0.03 × 0.08 |
| propeller | 4 | 0.0596 | (0.02, -2.29, -0.36) | 0.12 × 0.08 × 0.48 |
| fuselage | 3 | 0.0028 | (-0.12, 4.85, 0.16) | 0.08 × 0.60 × 0.02 |
| door_l | 2 | 0.3981 | (0.53, -0.42, 0.62) | 0.06 × 0.88 × 0.55 |
| door_r | 2 | 0.3981 | (-0.53, -0.42, 0.62) | 0.06 × 0.88 × 0.55 |
| fuselage | 2 | 0.1517 | (-0.52, 0.45, -0.16) | 0.08 × 0.70 × 0.43 |
| fuselage | 2 | 0.0682 | (-0.42, -1.82, 0.20) | 0.21 × 0.84 × 0.18 |
| fuselage | 2 | 0.0719 | (-0.27, -1.82, 0.37) | 0.31 × 0.84 × 0.21 |
| fuselage | 2 | 0.1863 | (0.04, 1.59, 0.85) | 0.51 × 0.71 × 0.21 |

## Hygiene

Degenerate 3, slivers 103, duplicate faces (same winding) 0, back-to-back face pairs (not a fault) 0, flipped 62 (back side visible 3, normals against winding 0, normal error > 90° 59), floating islands 0, inconsistent winding edges 4, non-manifold edges 8, open (boundary) edges 319.

## Warnings

- 113 visible triangles face the opposite way in the reference (nodes aileron_l, aileron_r, door_l, door_r, fuselage, gear, propeller, rudder, wing_l, wing_r): the reference build is flipped there
- node door_l: 3 triangles on curved surfaces (0.3 % of its visible area) are tessellated exactly as in the reference, so their shape error is not measured and they are left out of checks A and B (the generator does not scale them with --detail)
- node door_r: 3 triangles on curved surfaces (0.3 % of its visible area) are tessellated exactly as in the reference, so their shape error is not measured and they are left out of checks A and B (the generator does not scale them with --detail)
- reference node cockpit is missing from the model

## Runtime

load 0.1 s, topology 0.0 s, shape+shading 1.4 s, visibility 0.6 s, hygiene 0.0 s, headroom 0.0 s, profiles 0.0 s, total 2.1 s (the only part of this report that changes between runs).
