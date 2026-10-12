# T22 — glTF models with primitive fallback, articulated parts, Database panel

Tools track · Depends on T20 · Runs beside T21 and T24 · Branch `task/t22-gltf-models`
Follow the shared rules in [README.md](README.md) (rule 6 does not apply).

## Goal
Entities are drawn with real models named in the database manifest. Articulated parts move the
right mesh nodes, and an unmapped type or a model that fails to load falls back to today's
primitive shape **and is flagged**, so gaps in a database are visible.

## Design
- Loader: raylib's `LoadModel` bakes glTF node transforms into meshes and does not keep node names,
  so it cannot drive articulated parts. Use **cgltf** directly (raylib vendors it under
  `src/external/cgltf.h`; use that copy, so there is no new dependency, and check that its
  licence is MIT) to build a `ModelAsset`: node tree (name, parent, rest transform), raylib meshes
  per node, base-colour factor and texture. GLB and glTF with external buffers; OBJ through
  raylib's `LoadModel` (no parts). Report what you do not support (skins, morph targets, PBR beyond
  base colour).
- Axes: glTF is +Y up, +Z forward (glTF 2.0 spec, 3.4); CIGI body axes are x forward, y right,
  z down (ICD 3.4). A fixed conversion plus the manifest's `scale`, `offset` and `rotation`.
  Document it with a Blender export recipe in `docs/guide/databases.md`.
- Articulated parts (ICD 4.1.6): `[[entity.part]] id -> node`. The node's transform is its rest
  transform composed with the part's offset and attitude (absolute values). An unmapped part id
  keeps today's arm marker. Components: optional `kind = "visibility"` (state 0 hides the node,
  anything else shows it); everything else is still stored, not interpreted.
- Model cache keyed by path and shared by entities. Hot reload together with T20's
  `reload_if_changed()` (poll about once a second in the viewer).
- Fallback: the category primitive (today's shapes) plus a magenta outline and "(no model)" in the
  label. Map icons by category (aircraft, ground vehicle, sea, lifeform, munition, other).
- **Database panel**: database name and number, load state (the -n / +n sequence), models
  loaded/failed with errors, entity types in use that are unmapped (from T20's stats),
  parts/components that reference missing nodes, and a "Reload" button.
- `cigi_db validate` checks node names when the viewer is built (cgltf is available then).
- Assets: the user supplies small **CC0** models (e.g. Kenney / Quaternius: jet, helicopter, tank,
  truck; total ≤ 5 MB) in `databases/sample/models/` with `CREDITS.md` before launch. **Do not
  download anything.** Tests use a tiny glTF generated or hand-written by the test.

## Acceptance
README rules 7–9; coverage ≥ 95 % on headless code (manifest-side mapping and validation). Tests:
node lookup, part composition (hand-computed matrices), axis conversion, visibility components,
fallback selection, cache reuse. `--stress 200 --bench 600` with models ≥ 60 fps in Release
(numbers in `docs/guide/ig-viewer.md`). Screenshots: sample database in chase view, Database
panel with one unmapped type.
