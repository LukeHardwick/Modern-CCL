# Model audit: cessna_lod2

Model `cessna_lod2.glb` (0.10 MB, sha256 `d8f367f706d7`) against reference `cessna.glb` (sha256 `fc5a6a5617ea`), config `cessna_lod2.toml`; mode **full**.
Coordinates: modelling frame of tools/models: metres, x = vehicle left, y = aft (nose at -y), z = up.

**54 of 64 gates pass; 10 fail (2 hard).** Overall: **FAIL (hard)**.

| Check | Gate | Value | Limit | Result |
|---|---|---|---|---|
| A | Shape accuracy exterior: model to reference max | 46.42 mm | <= 256.60 mm | PASS |
| A | Shape accuracy exterior: model to reference p99 | 38.30 mm | <= 64.20 mm | PASS |
| A | Group exterior: nodes in the model | 14 | >= 1 | PASS |
| A | Shape accuracy exterior: reference to model max | 337.15 mm | <= 256.60 mm | **FAIL** |
| A | Shape accuracy exterior: reference to model p99 | 142.88 mm | <= 64.20 mm | **FAIL** |
| A | Not measured exterior: share of the visible area | 0.02 % | <= 2.00 % | PASS |
| A | Node cockpit: in the reference, not in the model | - | - | **FAIL** |
| B | Shading exterior: normal error max | 90.00° | <= 90.00° | PASS |
| B | Shading exterior: normal error p95 | 78.41° | <= 10.00° | **FAIL** |
| C | Hidden triangles aileron_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles aileron_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles door_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles door_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles elevator | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles flap_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles flap_r | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles fuselage | 0.77 % | <= 2.00 % | PASS |
| C | Hidden triangles gear | 1.72 % | <= 2.00 % | PASS |
| C | Hidden triangles prop_disc | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles propeller | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles rudder | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles wing_l | 0.00 % | <= 2.00 % | PASS |
| C | Hidden triangles wing_r | 0.00 % | <= 2.00 % | PASS |
| C | Index limit aileron_l / paint (18 vertices) | 17 | <= 65 535 | PASS |
| C | Index limit aileron_r / paint (18 vertices) | 17 | <= 65 535 | PASS |
| C | Index limit door_l / dark (54 vertices) | 53 | <= 65 535 | PASS |
| C | Index limit door_l / glass (6 vertices) | 5 | <= 65 535 | PASS |
| C | Index limit door_l / paint (96 vertices) | 95 | <= 65 535 | PASS |
| C | Index limit door_r / dark (54 vertices) | 53 | <= 65 535 | PASS |
| C | Index limit door_r / glass (6 vertices) | 5 | <= 65 535 | PASS |
| C | Index limit door_r / paint (96 vertices) | 95 | <= 65 535 | PASS |
| C | Index limit elevator / paint (36 vertices) | 35 | <= 65 535 | PASS |
| C | Index limit flap_l / paint (24 vertices) | 23 | <= 65 535 | PASS |
| C | Index limit flap_r / paint (24 vertices) | 23 | <= 65 535 | PASS |
| C | Index limit fuselage / glass (74 vertices) | 73 | <= 65 535 | PASS |
| C | Index limit fuselage / paint (712 vertices) | 711 | <= 65 535 | PASS |
| C | Index limit gear / dark (105 vertices) | 104 | <= 65 535 | PASS |
| C | Index limit gear / metal (80 vertices) | 79 | <= 65 535 | PASS |
| C | Index limit gear / paint (126 vertices) | 125 | <= 65 535 | PASS |
| C | Index limit prop_disc / hud (72 vertices) | 71 | <= 65 535 | PASS |
| C | Index limit propeller / metal (40 vertices) | 39 | <= 65 535 | PASS |
| C | Index limit propeller / paint (65 vertices) | 64 | <= 65 535 | PASS |
| C | Index limit rudder / paint (24 vertices) | 23 | <= 65 535 | PASS |
| C | Index limit wing_l / light (12 vertices) | 11 | <= 65 535 | PASS |
| C | Index limit wing_l / paint (120 vertices) | 119 | <= 65 535 | PASS |
| C | Index limit wing_r / light (12 vertices) | 11 | <= 65 535 | PASS |
| C | Index limit wing_r / paint (120 vertices) | 119 | <= 65 535 | PASS |
| C | Sub-pixel triangles exterior at 120 m | 56.99 % | <= 10.00 % | **FAIL** |
| C | Vertices per triangle | 0.965 | <= 0.650 | **FAIL** |
| D | Degenerate triangles | 3 | <= 0 | **FAIL (hard)** |
| D | Duplicate faces | 0 | <= 0 | PASS |
| D | Flipped faces | 67 | <= 0 | **FAIL (hard)** |
| D | Floating islands | 0 | <= 0 | PASS |
| D | Sliver triangles (< 2 deg) | 5.90 % | <= 1.00 % | **FAIL** |
| D | Inconsistent winding (edges) | 8 | <= 0 | **FAIL** |
| E | Height 2.720 m vs published 2.72 m | 0.00 % | <= 2.00 % | PASS |
| E | Length 8.280 m vs published 8.28 m | 0.00 % | <= 2.00 % | PASS |
| E | Span 11.010 m vs published 11 m | 0.09 % | <= 2.00 % | PASS |
| G | File size | 0.10 MB | <= 10.00 MB | PASS |
| G | Triangles, group exterior | 2 067 | <= 2 500 | PASS |
| G | Materials | 6 | <= 10 | PASS |
| G | Nodes | 14 | <= 24 | PASS |
| G | Primitives (draw calls) | 24 | <= 40 | PASS |
| G | Triangles, total | 2 067 | <= 2 500 | PASS |

## Model

2 067 triangles, 1 994 vertices (0.96 per triangle), 14 nodes, 24 primitives (draw calls), 6 materials; largest primitive 712 vertices. Size 8.280 × 11.010 × 2.720 m (length × span × height). Hidden: 13 triangles (0.63 % of the non-degenerate ones).

## Per node

Shape and shading over the triangles that are measured: seen (hidden and flipped ones are counted by their own checks) and not coinciding with the reference on a curved surface ("not measured", share of the visible area in its own column; the JSON also has the figures over all measured triangles).

| Node | Group | Tris | Verts/tri | Shape p99 / max (mm) | Reverse p99 / max (mm) | Normal p95 / max (°) | Not measured | Hidden | Flipped | Sub-pixel | Thin | Slivers |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| aileron_l | exterior | 8 | 2.25 | 4.32 / 4.32 | 3.96 / 4.03 | 2.43 / 2.43 | 0.0 % | 0.0 % | 0 | 25.0 % | 0.0 % | 0 |
| aileron_r | exterior | 8 | 2.25 | 4.32 / 4.32 | 3.96 / 4.03 | 2.43 / 2.43 | 0.0 % | 0.0 % | 0 | 25.0 % | 0.0 % | 0 |
| door_l | exterior | 124 | 1.26 | 28.84 / 28.84 | 27.82 / 28.84 | 89.86 / 90.00 | 0.4 % | 0.0 % | 1 | 66.1 % | 54.8 % | 0 |
| door_r | exterior | 124 | 1.26 | 28.84 / 28.84 | 27.82 / 28.84 | 89.86 / 89.99 | 0.4 % | 0.0 % | 1 | 66.1 % | 54.8 % | 0 |
| elevator | exterior | 16 | 2.25 | 3.32 / 3.32 | 3.11 / 3.11 | 1.17 / 1.17 | 0.0 % | 0.0 % | 0 | 12.5 % | 25.0 % | 0 |
| flap_l | exterior | 14 | 1.71 | 3.76 / 3.76 | 3.35 / 3.35 | 1.75 / 1.75 | 0.0 % | 0.0 % | 0 | 0.0 % | 0.0 % | 0 |
| flap_r | exterior | 14 | 1.71 | 3.76 / 3.76 | 3.35 / 3.35 | 1.75 / 1.75 | 0.0 % | 0.0 % | 0 | 0.0 % | 0.0 % | 0 |
| fuselage | exterior | 913 | 0.86 | 37.88 / 37.88 | 279.47 / 337.15 | 80.95 / 89.10 | 0.0 % | 0.8 % | 43 | 50.2 % | 41.6 % | 113 |
| gear | exterior | 352 | 0.88 | 46.42 / 46.42 | 40.02 / 52.14 | 89.52 / 90.00 | 0.0 % | 1.7 % | 0 | 93.8 % | 85.8 % | 9 |
| prop_disc | exterior | 120 | 0.60 | 5.24 / 5.24 | 0.00 / 0.00 | 0.00 / 0.00 | 0.0 % | 0.0 % | 0 | 20.0 % | 0.0 % | 0 |
| propeller | exterior | 136 | 0.77 | 6.21 / 6.21 | 7.47 / 10.38 | 35.01 / 35.01 | 0.0 % | 0.0 % | 22 | 100.0 % | 89.7 % | 0 |
| rudder | exterior | 14 | 1.71 | 11.88 / 11.88 | 9.27 / 9.63 | 89.87 / 89.87 | 0.0 % | 0.0 % | 0 | 0.0 % | 0.0 % | 0 |
| wing_l | exterior | 112 | 1.18 | 38.30 / 38.30 | 37.44 / 184.75 | 78.41 / 88.08 | 0.0 % | 0.0 % | 0 | 26.8 % | 19.6 % | 0 |
| wing_r | exterior | 112 | 1.18 | 38.30 / 38.30 | 37.44 / 37.90 | 78.41 / 88.08 | 0.0 % | 0.0 % | 0 | 26.8 % | 19.6 % | 0 |

Error map (share of each node's area by per-triangle error ÷ the group's p99 gate; < 0.1 could be coarser, > 1 is too coarse):

| Node | < 0.1 | 0.1–0.5 | 0.5–1 | > 1 |
|---|---|---|---|---|
| aileron_l | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| aileron_r | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| door_l | 34.0 % | 66.0 % | 0.0 % | 0.0 % |
| door_r | 34.0 % | 66.0 % | 0.0 % | 0.0 % |
| elevator | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| flap_l | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| flap_r | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| fuselage | 30.8 % | 65.4 % | 3.8 % | 0.0 % |
| gear | 25.8 % | 69.0 % | 5.2 % | 0.0 % |
| prop_disc | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| propeller | 100.0 % | 0.0 % | 0.0 % | 0.0 % |
| rudder | 43.3 % | 56.7 % | 0.0 % | 0.0 % |
| wing_l | 39.5 % | 36.0 % | 24.5 % | 0.0 % |
| wing_r | 39.5 % | 36.0 % | 24.5 % | 0.0 % |

## Hidden triangles: largest clusters

| Node | Tris | Area (m²) | Centre (x, y, z) m | Size (m) |
|---|---|---|---|---|
| fuselage | 4 | 0.0418 | (0.00, 4.85, 0.26) | 0.10 × 0.60 × 0.04 |
| fuselage | 3 | 0.0028 | (0.12, 4.85, 0.16) | 0.08 × 0.60 × 0.02 |
| gear | 1 | 0.0014 | (-1.34, 0.41, -1.04) | 0.00 × 0.09 × 0.03 |
| gear | 1 | 0.0014 | (-1.20, 0.41, -1.04) | 0.00 × 0.09 × 0.03 |
| gear | 1 | 0.0008 | (-0.06, -1.86, -1.08) | 0.00 × 0.07 × 0.03 |
| gear | 1 | 0.0008 | (0.06, -1.86, -1.08) | 0.00 × 0.07 × 0.03 |
| gear | 1 | 0.0014 | (1.20, 0.41, -1.04) | 0.00 × 0.09 × 0.03 |
| gear | 1 | 0.0014 | (1.34, 0.41, -1.04) | 0.00 × 0.09 × 0.03 |

## Flipped triangles: largest clusters

| Node | Tris | Area (m²) | Centre (x, y, z) m | Size (m) |
|---|---|---|---|---|
| fuselage | 13 | 0.5186 | (0.01, -1.68, -0.32) | 1.03 × 0.56 × 0.54 |
| fuselage | 6 | 0.2860 | (1.67, 0.18, 0.38) | 2.37 × 0.27 × 1.35 |
| propeller | 6 | 0.0702 | (-0.02, -2.29, -0.39) | 0.13 × 0.09 × 0.54 |
| propeller | 6 | 0.0702 | (0.02, -2.29, 0.45) | 0.13 × 0.09 × 0.54 |
| fuselage | 4 | 0.1906 | (-1.67, 0.15, 0.38) | 2.37 × 0.22 × 1.35 |
| propeller | 4 | 0.0081 | (-0.02, -2.30, -0.89) | 0.11 × 0.03 × 0.08 |
| propeller | 4 | 0.0081 | (0.02, -2.30, 0.94) | 0.11 × 0.03 × 0.08 |
| fuselage | 3 | 0.0028 | (-0.12, 4.85, 0.16) | 0.08 × 0.60 × 0.02 |
| fuselage | 2 | 0.1517 | (-0.52, 0.45, -0.16) | 0.08 × 0.70 × 0.43 |
| fuselage | 2 | 0.0446 | (-0.48, -1.68, 0.21) | 0.10 × 0.56 × 0.16 |
| fuselage | 2 | 0.3130 | (-0.35, 2.56, -0.05) | 0.19 × 1.22 × 0.33 |
| fuselage | 2 | 0.0473 | (-0.30, -1.68, 0.42) | 0.24 × 0.56 × 0.11 |
| fuselage | 2 | 0.1863 | (0.04, 1.59, 0.85) | 0.51 × 0.71 × 0.21 |
| fuselage | 2 | 0.0474 | (0.32, -1.68, 0.41) | 0.16 × 0.56 × 0.20 |
| door_l | 1 | 0.0666 | (0.53, -0.70, 0.61) | 0.06 × 0.32 × 0.53 |

## Hygiene

Degenerate 3, slivers 122, duplicate faces (same winding) 0, back-to-back face pairs (not a fault) 0, flipped 67 (back side visible 4, normals against winding 0, normal error > 90° 63), floating islands 0, inconsistent winding edges 8, non-manifold edges 12, open (boundary) edges 337.

## Warnings

- 97 visible triangles face the opposite way in the reference (nodes aileron_l, aileron_r, door_l, door_r, fuselage, gear, rudder, wing_l, wing_r): the reference build is flipped there
- node door_l: 4 triangles on curved surfaces (0.4 % of its visible area) are tessellated exactly as in the reference, so their shape error is not measured and they are left out of checks A and B (the generator does not scale them with --detail)
- node door_r: 4 triangles on curved surfaces (0.4 % of its visible area) are tessellated exactly as in the reference, so their shape error is not measured and they are left out of checks A and B (the generator does not scale them with --detail)
- reference node cockpit is missing from the model

## Runtime

load 0.1 s, topology 0.0 s, shape+shading 0.8 s, visibility 0.5 s, hygiene 0.0 s, headroom 0.0 s, profiles 0.0 s, total 1.4 s (the only part of this report that changes between runs).
