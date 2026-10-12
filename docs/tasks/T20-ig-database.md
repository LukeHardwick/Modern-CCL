# T20 — Mock-IG database: manifest format, loader, Database Number handling

Tools track (after v0.2.0) · Depends on T09b Part 1 ✅ · Runs beside T09b Part 2 · Branch `task/t20-ig-database`
Follow the shared rules in [README.md](README.md) (rule 6 golden bytes does not apply: no packets).

## Goal
Replace the hardcoded entity-type convention, terrain choice and database bounds of the mock IG
with a **database on disk** that people can add to and update without recompiling. In CIGI terms a
database is what IG Control's Database Number selects (ICD 4.1.1, 4.2.1); here it is a folder with a
hand-edited TOML manifest. This task delivers the format, the headless loader, validation and
the Database Number protocol; models (T22) and heightmaps (T21) plug into it.

## Decisions (user, 2026-10-12)
- Hand-edited files are **TOML via toml++**; machine-generated / machine-read files are **JSON via
  glaze**. Both are new FetchContent dependencies (pinned, latest release at least two weeks old),
  built without exceptions (toml++: `TOML_EXCEPTIONS=0`; glaze is exception-free), excluded from
  clang-tidy and coverage like the other third-party code.
- Also evaluate in the report (do not implement): can glaze's own TOML **reader** handle this
  manifest (nested tables, arrays of tables, useful error locations)? If yes the planner may drop
  toml++ later.
- Entity-type numbers are arbitrary and per database, so a Genesis-matching database can be
  written once the Genesis docs are available. The shipped default database reproduces today's
  convention exactly.

## Layout and format
```
databases/
  default/                 # number 1: synthetic terrain, today's 100-199 / 200-299 convention
    database.toml
  <name>/
    database.toml
    models/  terrain/  CREDITS.md
```
Sketch of `database.toml` (finalise the field names; document every field in
`docs/guide/databases.md`):
```toml
[database]
number = 1                    # 1..127, unique within a database root
name = "Default (synthetic)"
origin = { lat = 51.5, lon = -0.12, alt = 0.0 }
bounds = { lat_min = 51.25, lat_max = 51.75, lon_min = -0.37, lon_max = 0.13 }
load_frames = 30              # simulated load time reported as -n in Start of Frame

[terrain]
kind = "synthetic"            # T21 adds "heightmap"; parse + validate the table here

[[material]]                  # HAT/HOT material codes (ICD 4.2.3) and map/terrain colours
code = 0
name = "water"
colour = "#295c99"

[[entity]]
types = [100, 199]            # a single number or an inclusive range
name = "generic aircraft"
category = "aircraft"         # aircraft | ground_vehicle | sea | lifeform | munition | other | none
# model = "models/jet.glb"    # T22; optional, falls back to the category's primitive shape
# scale = 1.0, offset = [x, y, z], rotation = [yaw, pitch, roll]  (model -> CIGI body axes)
# [[entity.part]]  id = 1, node = "turret"                         (T22)
# [[entity.component]] id = 3, node = "gear", kind = "visibility"  (T22)
```
Rules: an exact type beats a range; overlapping ranges are an error; an unmapped type uses
`[default] category` (default `other`). Paths are relative to the manifest.

## Headless API (`cigi_mock_ig`, namespace `cigi::mock`)
- `Database` value type (number, name, origin, bounds, load_frames, terrain spec, materials, entity
  table) with `lookup(std::uint16_t type) -> const EntityModel*`.
- `load_database(path) -> std::expected<Database, DbError>`; `DbError` carries file, line, column
  and a message. Unknown keys are warnings (collected, not fatal), so newer manifests load in older
  tools.
- `DatabaseCatalog`: scans a root folder, indexes by number, reports duplicates.
- `default_database()`: the built-in database used when no root is given. `classify_entity_type`
  and `ShapeClass` go away in favour of `EntityModel::category` (keep the viewer working; the
  existing tests must keep passing with the default database).
- Hot reload: `DatabaseCatalog::reload_if_changed()` (polls mtimes; the caller decides how often).
  A manifest that fails to parse keeps the previous version and reports the error.
- `SceneStats` gains the set of entity types seen that no entry mapped (for T22's Database panel
  and the report).

## Database Number protocol (MockIg)
Follow ICD 4.1.1 and 4.2.1 (Figure 38): in Operate/Debug, IG Control Database Number `n > 0`
starts a load. Start of Frame reports `-n` for at least `load_frames` frames **and** until the Host
has sent 0, then `+n`. Same number = reload. Ignored in Reset/Standby. A global database reports
0. **Planner decisions** (the ICD is silent; record them as ICD ambiguities in the report):
an unknown number logs an error, keeps the current database and keeps reporting its number;
entities are kept across a load. Wire this into `MockIg::build_reply`. If T09b Part 2 is already
merged, also wire `cigi::Ig::on_database_request` / `set_database`; otherwise leave a note and
T09b Part 2 wires it.

## Tool and report
- `apps/cigi_db` (headless, CLI11 if T09b P2 / T09 has added it, otherwise a minimal parser):
  `cigi_db validate <root-or-manifest> [--json FILE]` and `cigi_db list <root>`. The JSON report
  (glaze) lists databases, entries, errors and warnings with locations; exit code 1 on errors.
- `docs/guide/databases.md`: format reference, how to add an entity type, how numbers map to
  Genesis later, axis conventions (filled in by T22).

## Acceptance
README rules 7–9; coverage ≥ 95 % on the new headless code. Tests: every schema error has a
test with its line number; range overlap/precedence; unknown-key warnings; hot reload keeps the old
database on error; the Database Number sequence (load, reload, Standby ignore, unknown number,
Host never sending 0); a glaze round trip of the report. The viewer looks unchanged with the
default database (compare `--screenshot` before and after).
