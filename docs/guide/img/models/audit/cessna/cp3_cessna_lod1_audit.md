# Model audit: cessna_lod1

Model `cessna_lod1.glb` (0.18 MB, sha256 `4c2f769c57f6`) against reference `cessna.glb` (sha256 `e809ba59291f`), config `cessna_lod1.toml`; mode **full**.
Coordinates: modelling frame of tools/models: metres, x = vehicle left, y = aft (nose at -y), z = up.

**55 of 64 gates pass; 9 fail (2 hard).** Overall: **FAIL (hard)**.

| Check | Gate | Value | Limit | Result |
|---|---|---|---|---|
| A | Shape accuracy exterior: model to reference max | 10.21 mm | <= 64.20 mm | PASS |
| A | Shape accuracy exterior: model to reference p99 | 8.28 mm | <= 16.00 mm | PASS |
| A | Group exterior: nodes in the model | 14 | >= 1 | PASS |
| A | Shape accuracy exterior: reference to model max | 338.04 mm | <= 64.20 mm | **FAIL** |
| A | Shape accuracy exterior: reference to model p99 | 180.83 mm | <= 16.00 mm | **FAIL** |
| A | Not measured exterior: share of the visible area | 0.01 % | <= 2.00 % | PASS |
| A | Node cockpit: in the reference, not in the model | - | - | **FAIL** |
| B | Shading exterior: normal error max | 90.00° | <= 90.00° | PASS |
| B | Shading exterior: normal error p95 | 45.84° | <= 10.00° | **FAIL** |
| C | Hidden triangles aileron_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles aileron_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles door_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles door_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles elevator | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles flap_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles flap_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles fuselage | 0.97 % | <= 2.00 % | PASS |
| C | Hidden triangles gear | 0.94 % | <= 2.00 % | PASS |
| C | Hidden triangles prop_disc | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles propeller | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles rudder | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles wing_l | 0.35 % | <= 2.00 % | PASS |
| C | Hidden triangles wing_r | 0.35 % | <= 2.00 % | PASS |
| C | Index limit aileron_l / paint (34 vertices) | 33 | <= 65 535 | PASS |
| C | Index limit aileron_r / paint (34 vertices) | 33 | <= 65 535 | PASS |
| C | Index limit door_l / dark (97 vertices) | 96 | <= 65 535 | PASS |
| C | Index limit door_l / glass (15 vertices) | 14 | <= 65 535 | PASS |
| C | Index limit door_l / paint (147 vertices) | 146 | <= 65 535 | PASS |
| C | Index limit door_r / dark (97 vertices) | 96 | <= 65 535 | PASS |
| C | Index limit door_r / glass (15 vertices) | 14 | <= 65 535 | PASS |
| C | Index limit door_r / paint (147 vertices) | 146 | <= 65 535 | PASS |
| C | Index limit elevator / paint (52 vertices) | 51 | <= 65 535 | PASS |
| C | Index limit flap_l / paint (24 vertices) | 23 | <= 65 535 | PASS |
| C | Index limit flap_r / paint (24 vertices) | 23 | <= 65 535 | PASS |
| C | Index limit fuselage / glass (124 vertices) | 123 | <= 65 535 | PASS |
| C | Index limit fuselage / paint (1304 vertices) | 1 303 | <= 65 535 | PASS |
| C | Index limit gear / dark (216 vertices) | 215 | <= 65 535 | PASS |
| C | Index limit gear / metal (80 vertices) | 79 | <= 65 535 | PASS |
| C | Index limit gear / paint (318 vertices) | 317 | <= 65 535 | PASS |
| C | Index limit prop_disc / hud (96 vertices) | 95 | <= 65 535 | PASS |
| C | Index limit propeller / metal (176 vertices) | 175 | <= 65 535 | PASS |
| C | Index limit propeller / paint (81 vertices) | 80 | <= 65 535 | PASS |
| C | Index limit rudder / paint (50 vertices) | 49 | <= 65 535 | PASS |
| C | Index limit wing_l / light (12 vertices) | 11 | <= 65 535 | PASS |
| C | Index limit wing_l / paint (240 vertices) | 239 | <= 65 535 | PASS |
| C | Index limit wing_r / light (12 vertices) | 11 | <= 65 535 | PASS |
| C | Index limit wing_r / paint (240 vertices) | 239 | <= 65 535 | PASS |
| C | Sub-pixel triangles exterior at 30 m | 11.63 % | <= 10.00 % | **FAIL** |
| C | Vertices per triangle | 0.810 | <= 0.650 | **FAIL** |
| D | Degenerate triangles | 1 | <= 0 | **FAIL (hard)** |
| D | Duplicate faces | 0 | <= 0 | PASS |
| D | Flipped faces | 37 | <= 0 | **FAIL (hard)** |
| D | Floating islands | 0 | <= 0 | PASS |
| D | Sliver triangles (< 2 deg) | 3.19 % | <= 1.00 % | **FAIL** |
| D | Inconsistent winding (edges) | 0 | <= 0 | PASS |
| E | Height 2.717 m vs published 2.72 m | 0.10 % | <= 2.00 % | PASS |
| E | Length 8.280 m vs published 8.28 m | 0.00 % | <= 2.00 % | PASS |
| E | Span 11.010 m vs published 11 m | 0.09 % | <= 2.00 % | PASS |
| G | File size | 0.18 MB | <= 10.00 MB | PASS |
| G | Triangles, group exterior | 4 489 | <= 5 000 | PASS |
| G | Materials | 6 | <= 10 | PASS |
| G | Nodes | 14 | <= 24 | PASS |
| G | Primitives (draw calls) | 24 | <= 40 | PASS |
| G | Triangles, total | 4 489 | <= 5 000 | PASS |

## Model

4 489 triangles, 3 635 vertices (0.81 per triangle), 14 nodes, 24 primitives (draw calls), 6 materials; largest primitive 1 304 vertices. Size 8.280 × 11.010 × 2.717 m (length × span × height). Hidden: 29 triangles (0.65 % of the non-degenerate ones).

## Per node

Shape and shading over the triangles that are measured: seen (hidden and flipped ones are counted by their own checks) and not coinciding with the reference on a curved surface ("not measured", share of the visible area in its own column; the JSON also has the figures over all measured triangles).

| Node | Group | Tris | Verts/tri | Shape p99 / max (mm) | Reverse p99 / max (mm) | Normal p95 / max (°) | Not measured | Hidden | Flipped | Sub-pixel | Thin | Slivers |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| aileron_l | exterior | 26 | 1.31 | 1.18 / 1.18 | 0.93 / 1.05 | 0.97 / 0.97 | 0.0 % | 0.0 % | 0 | 0.0 % | 0.0 % | 0 |
| aileron_r | exterior | 26 | 1.31 | 1.18 / 1.18 | 0.93 / 1.05 | 0.97 / 0.97 | 0.0 % | 0.0 % | 0 | 7.7 % | 7.7 % | 2 |
| door_l | exterior | 228 | 1.14 | 7.38 / 7.38 | 7.11 / 7.34 | 89.99 / 90.00 | 0.2 % | 0.0 % | 2 | 0.0 % | 0.0 % | 0 |
| door_r | exterior | 228 | 1.14 | 7.38 / 7.38 | 7.11 / 7.34 | 89.99 / 90.00 | 0.2 % | 0.0 % | 2 | 0.0 % | 0.0 % | 0 |
| elevator | exterior | 32 | 1.62 | 0.79 / 0.79 | 0.76 / 0.76 | 0.54 / 0.54 | 0.0 % | 0.0 % | 0 | 6.2 % | 6.2 % | 2 |
| flap_l | exterior | 14 | 1.71 | 3.76 / 3.76 | 3.35 / 3.35 | 1.75 / 1.75 | 0.0 % | 0.0 % | 0 | 0.0 % | 0.0 % | 0 |
| flap_r | exterior | 14 | 1.71 | 3.76 / 3.76 | 3.35 / 3.35 | 1.75 / 1.75 | 0.0 % | 0.0 % | 0 | 0.0 % | 0.0 % | 0 |
| fuselage | exterior | 1953 | 0.73 | 8.86 / 10.21 | 282.54 / 338.04 | 57.08 / 88.72 | 0.0 % | 1.0 % | 32 | 8.1 % | 10.3 % | 129 |
| gear | exterior | 850 | 0.72 | 8.06 / 9.38 | 6.56 / 8.34 | 26.72 / 90.00 | 0.0 % | 0.9 % | 0 | 36.0 % | 8.8 % | 8 |
| prop_disc | exterior | 160 | 0.60 | 2.28 / 2.28 | 0.00 / 0.00 | 0.00 / 0.00 | 0.0 % | 0.0 % | 0 | 0.0 % | 0.0 % | 0 |
| propeller | exterior | 336 | 0.76 | 7.46 / 8.55 | 6.37 / 8.61 | 46.03 / 68.68 | 0.0 % | 0.0 % | 1 | 8.3 % | 0.6 % | 0 |
| rudder | exterior | 46 | 1.09 | 4.34 / 4.34 | 2.66 / 3.49 | 1.46 / 89.86 | 0.0 % | 0.0 % | 0 | 2.2 % | 4.3 % | 2 |
| wing_l | exterior | 288 | 0.88 | 8.03 / 9.57 | 7.09 / 179.42 | 41.35 / 87.98 | 0.0 % | 0.4 % | 0 | 4.2 % | 3.1 % | 0 |
| wing_r | exterior | 288 | 0.88 | 8.03 / 9.89 | 7.09 / 23.31 | 41.35 / 87.98 | 0.0 % | 0.4 % | 0 | 4.2 % | 3.1 % | 0 |

Error map (share of each node's area by per-triangle error ÷ the group's p99 gate; < 0.1 could be coarser, > 1 is too coarse):

| Node | < 0.1 | 0.1–0.5 | 0.5–1 | > 1 |
|---|---|---|---|---|
| aileron_l | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| aileron_r | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| door_l | 37.5 % | 62.5 % | 0.0 % | 0.0 % |
| door_r | 37.5 % | 62.5 % | 0.0 % | 0.0 % |
| elevator | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| flap_l | 56.0 % | 44.0 % | 0.0 % | 0.0 % |
| flap_r | 56.0 % | 44.0 % | 0.0 % | 0.0 % |
| fuselage | 10.3 % | 86.3 % | 3.4 % | 0.0 % |
| gear | 16.2 % | 82.7 % | 1.1 % | 0.0 % |
| prop_disc | 95.7 % | 4.3 % | 0.0 % | 0.0 % |
| propeller | 21.1 % | 77.9 % | 1.0 % | 0.0 % |
| rudder | 74.2 % | 25.9 % | 0.0 % | 0.0 % |
| wing_l | 31.1 % | 67.8 % | 1.2 % | 0.0 % |
| wing_r | 31.1 % | 67.8 % | 1.2 % | 0.0 % |

## Hidden triangles: largest clusters

| Node | Tris | Area (m²) | Centre (x, y, z) m | Size (m) |
|---|---|---|---|---|
| fuselage | 6 | 0.0543 | (-0.21, -0.09, 0.99) | 0.42 × 0.62 × 0.04 |
| fuselage | 5 | 0.0574 | (0.00, 4.53, 0.29) | 0.10 × 1.25 × 0.10 |
| fuselage | 5 | 0.0432 | (0.37, 0.41, 0.98) | 0.09 × 0.78 × 0.03 |
| fuselage | 3 | 0.0028 | (0.12, 4.85, 0.16) | 0.08 × 0.60 × 0.02 |
| gear | 1 | 0.0014 | (-1.34, 0.41, -1.04) | 0.00 × 0.09 × 0.03 |
| gear | 1 | 0.0014 | (-1.20, 0.41, -1.04) | 0.00 × 0.09 × 0.03 |
| gear | 1 | 0.0004 | (-0.06, -1.90, -1.08) | 0.01 × 0.04 × 0.03 |
| gear | 1 | 0.0008 | (-0.06, -1.86, -1.08) | 0.00 × 0.07 × 0.03 |
| gear | 1 | 0.0008 | (0.06, -1.86, -1.08) | 0.00 × 0.07 × 0.03 |
| gear | 1 | 0.0004 | (0.06, -1.70, -1.08) | 0.01 × 0.04 × 0.03 |
| gear | 1 | 0.0014 | (1.20, 0.41, -1.04) | 0.00 × 0.09 × 0.03 |
| gear | 1 | 0.0014 | (1.34, 0.41, -1.04) | 0.00 × 0.09 × 0.03 |
| wing_l | 1 | 0.0008 | (5.44, 0.18, 1.18) | 0.00 × 0.04 × 0.04 |
| wing_r | 1 | 0.0008 | (-5.44, 0.19, 1.18) | 0.00 × 0.04 × 0.04 |

## Flipped triangles: largest clusters

| Node | Tris | Area (m²) | Centre (x, y, z) m | Size (m) |
|---|---|---|---|---|
| fuselage | 24 | 0.3254 | (0.01, -1.59, -0.32) | 1.03 × 0.39 × 0.54 |
| fuselage | 3 | 0.0028 | (-0.12, 4.85, 0.16) | 0.08 × 0.60 × 0.02 |
| door_l | 2 | 0.0337 | (0.53, -0.78, 0.58) | 0.06 × 0.16 × 0.48 |
| door_r | 2 | 0.0337 | (-0.53, -0.78, 0.58) | 0.06 × 0.16 × 0.48 |
| fuselage | 2 | 0.0309 | (-0.49, -1.59, 0.21) | 0.07 × 0.39 × 0.16 |
| fuselage | 2 | 0.0309 | (0.49, -1.59, 0.21) | 0.04 × 0.39 × 0.26 |
| fuselage | 1 | 0.0503 | (0.51, 0.51, -0.28) | 0.05 × 0.58 × 0.19 |
| propeller | 1 | 0.0019 | (0.03, -2.27, -0.16) | 0.05 × 0.08 × 0.07 |

## Hygiene

Degenerate 1, slivers 143, duplicate faces (same winding) 0, back-to-back face pairs (not a fault) 0, flipped 37 (back side visible 3, normals against winding 0, normal error > 90° 34), floating islands 0, inconsistent winding edges 0, non-manifold edges 24, open (boundary) edges 397.

## Warnings

- 129 visible triangles face the opposite way in the reference (nodes aileron_l, aileron_r, door_l, door_r, fuselage, gear, propeller, rudder, wing_l, wing_r): the reference build is flipped there
- node door_l: 2 triangles on curved surfaces (0.2 % of its visible area) are tessellated exactly as in the reference, so their shape error is not measured and they are left out of checks A and B (the generator does not scale them with --detail)
- node door_r: 2 triangles on curved surfaces (0.2 % of its visible area) are tessellated exactly as in the reference, so their shape error is not measured and they are left out of checks A and B (the generator does not scale them with --detail)
- node prop_disc: reference has only 3.0x the model's triangles (want >= 4x); its errors are underestimated
- reference node cockpit is missing from the model

## Runtime

load 0.1 s, topology 0.0 s, shape+shading 0.5 s, visibility 0.8 s, hygiene 0.0 s, headroom 0.0 s, profiles 0.0 s, total 1.4 s (the only part of this report that changes between runs).
