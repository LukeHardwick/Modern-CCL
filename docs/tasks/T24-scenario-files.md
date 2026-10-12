# T24 — Scenario files (replace the hardcoded scripted Host)

Tools track · Depends on T20 · Runs beside T21 and T22 · Branch `task/t24-scenario-files`
Follow the shared rules in [README.md](README.md) (rule 6 does not apply).

## Goal
The offline mode is driven by scenario files instead of the hardcoded `ScriptedHost`, so test
cases and demos can be written without recompiling. Hand-written scenarios are TOML; generated
ones (for example converted from a DDS log by the user's Host tooling) are JSON read by glaze into
the **same** structs.

## Design
- `Scenario` structs and `load_scenario(path)` (by extension) in `cigi_mock_ig`; `ScenarioHost`
  produces one Host message per frame through `MessageBuilder`, like `ScriptedHost` does now.
- Contents (finalise names; document in `docs/guide/scenarios.md`): `[scenario]` (name, database
  number, origin override, start UTC, frame rate); `[[entity]]` (id, type, parent, clamp, motion);
  motion kinds `static`, `orbit` (centre, radius, speed, AGL, direction, banked), `waypoints`
  (lat/lon/alt/speed, loop), `attached` (parent, offset, attitude), `rate` (Rate Control only);
  `[[part]]` animations (constant rate or sine sweep); `[[view]]`; `[[hat_hot]]` (one-shot or
  periodic, ids, frames); `[[event]]` timeline (`at` seconds: create, destroy, detach, set state,
  change type, set celestial time).
- **Regression first:** write `scenarios/default.toml` equivalent to today's scenario and prove it
  with a test that the first 600 Host messages of `ScriptedHost` and `ScenarioHost` are
  byte-identical. Only then delete `ScriptedHost` (keep `--stress N` as a generator on top).
- Viewer: `--scenario FILE` (default `scenarios/default.toml`, then the built-in one),
  reload-from-start on file change, scenario name in the Link panel. `demo_host --scenario` is a
  follow-up for T09; note it in the report.
- Validation errors with file:line (TOML) or JSON pointer (JSON); `cigi_db validate` learns
  `--scenario FILE` to check entity types against a database.

## Acceptance
README rules 7–9; coverage ≥ 95 % on headless code. Tests: the byte-identical regression above;
each motion kind against hand-computed positions; event ordering at equal times; TOML and JSON
versions of one scenario give equal structs; error locations.
