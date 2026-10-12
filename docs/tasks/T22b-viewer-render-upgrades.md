# T22b — Viewer render upgrades: cockpit pass, instancing, textures/PBR

Tools track · Depends on T22 · Branch `task/t22b-render-upgrades`
Follow the shared rules in [README.md](README.md). Research: [docs/research/03-model-format-and-fidelity.md](../research/03-model-format-and-fidelity.md) (Addendum A wins where it differs).

## Scope (split from T22 at the user's request, 2026-10-12)
1. **Cockpit pass:** when the view is attached to an entity with `attached_view = "cockpit"`, draw
   the cockpit nodes in a second pass with their own projection (near ≈ 0.05–0.1 m) after the world
   pass (far ≈ 100 km); hide the exterior from inside.
2. **Instancing:** `DrawMeshInstanced` per (mesh, material) across entities sharing a model/LOD,
   with per-instance matrices (articulated parts included). Benchmark 200 entities; target 60 fps.
3. **Textures/PBR (later phase, needs a go-ahead):** phase A albedo + baked AO (~1.5 days); phase B
   metallic-roughness/normal/ORM + tone mapping (~4–5 days). Texture cache shared by entities.

## Acceptance
README rules 7–9; benchmark numbers in the report; screenshots of the cockpit view.
