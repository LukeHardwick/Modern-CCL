# Reference drawings (T22e fidelity gate F1)

| File | Source | Author | Licence | SHA-256 | Retrieved |
|---|---|---|---|---|---|
| `cessna_172_skyhawk_line_drawing.svg` | [Wikimedia Commons: File:Cessna 172 Skyhawk line drawing.svg](https://commons.wikimedia.org/wiki/File:Cessna_172_Skyhawk_line_drawing.svg) | Werneuchen (own work, 2008) | Public domain (PD-self) | `5d14e7351e330ebf75d1712e535d80872cd2154005e2b3427546b92714ba5760` | 2026-10-14, by the planner, with the user's approval |
| `cessna_172_cel.svg` (reference B) | [Wikimedia Commons: File:Cessna 172.svg](https://commons.wikimedia.org/wiki/File:Cessna_172.svg) | Marek Cel (own work, 2019) | CC0 1.0 | `a69083a129aae0e6bd129520abd3dd99d56db38f20a2b428866af6ccc9e7338d` | 2026-10-14, by the planner, with the user's approval |

Planner's check (2026-10-14): a modern 172 (swept fin with dorsal fin, rear "Omni-Vision" window,
wheel fairings), so reference B was not needed. Limits: hand-drawn (slightly wavy lines), and the
**side view omits the wing** (only the strut is drawn), so F1's side view must exclude the wing
region (or compare the wing in the front and top views only). Views: front (top left), side (top
right, nose left), top (bottom left). Used for measurement only; nothing from it is shipped in a model.

## Derived silhouettes (T22e Phase 1, 2026-10-14)

User decision: drawing A is the reference. `make_reference_masks.py` turns it into the three
filled silhouettes the F1 gate uses, so the gate never needs an SVG renderer:

| File | What |
|---|---|
| `cessna_172_front_mask.png`, `_side_mask.png`, `_top_mask.png` | 8-bit masks (0 / 255), 2x the drawing's pixel size, one per view (front: top left, side: top right, nose left, top: bottom left) |
| `cessna_172_reference.toml` | provenance: SVG hash, rsvg-convert version and command, extraction parameters, per-view mask hash and pixel extent |

Reproduce (from the repository root; needs `rsvg-convert`, here 2.63.2, and numpy; the result is
byte-identical for the same SVG and renderer version):

```
python3 tools/models/reference/make_reference_masks.py
```

which runs `rsvg-convert -z 2 -b white cessna_172_skyhawk_line_drawing.svg -o drawing.png`, cuts the
three views, drops specks (the dashed ground line), bridges pen gaps up to 5 drawing-px at 2x,
fills everything the outside cannot reach, and erodes by the bridge radius plus half the 3 px
outline stroke so the mask edge lies on the CENTRE of the drawn line. Two things are marked by hand
in the script (in drawing pixels, listed in the toml): in the front view the open triangle between
each wing strut, the wing underside and the fuselage side is sky, not aircraft, and the two
slot outlines inside it are erased. The side view has no wing; the F1 side check leaves the
model's wing out (`[F1_outline.views.side].exclude_nodes`).

Measured limits of the drawing (`docs/guide/img/models/audit/cessna/fidelity_noise.md`): scaled on each
axis to the published length / span / height its views disagree by up to 5 % (the top view is 5.4 %
shorter in length than the side view), and, scaled to the published span and length, its inboard wing chord
measures about 1.9 m against the 1.63 m of the T22d model (built from published figures), so the
top-view wing edges follow the drawing and may not follow the aircraft. The F1 gates therefore
mean "agrees with drawing A".

Planner's note on B (2026-10-14): precise CAD-style drawing of a modern 172 with panel lines; the
**side view also omits the wing**; the **top view is drawn nose-left** (rotated 90°) and the
**front view is rotated 90°** (span vertical) — the mask extraction must rotate them. User decision:
F1 is hard-gated only where A, B and the published figures agree; where they disagree the
published figures win and the drawings are advisory (A's inboard chord ≈ 1.9 m vs published 1.63 m).
