# Mock-IG databases

In CIGI a *database* is what the Host selects with the **Database Number** in IG Control
(ICD 4.1.1) and the IG confirms in Start of Frame (4.2.1). For the mock IG (`cigi_mock_ig`,
used by `ig_viewer` and the demos) a database is a **folder with a hand-edited TOML manifest**,
so people can add entity types, materials and (later) models and terrain without recompiling.

```
databases/
  default/                 number 1: synthetic terrain, the original type convention
    database.toml
  <name>/
    database.toml
    models/  terrain/  CREDITS.md        (T22 / T21 use these)
```

A *database root* is a folder holding such sub-folders (or itself a database). Numbers
(1..127) must be unique within a root; a repeated number is reported and the first folder wins.
Paths inside a manifest are relative to the manifest. Hand-edited files are TOML (parsed with
toml++); the machine-written report of `cigi_db` is JSON (glaze).

## Tools

```
cigi_db validate <root-or-manifest> [--json FILE]   # errors with line:column, exit 1 on errors
cigi_db list <root>                                 # databases, entry counts, broken manifests
ig_viewer --db-root databases --database 1          # run the viewer on a root
```

`validate` prints one line per database and every warning/error as `file:line:column: message`.
With `--json` it also writes the report (`tool`, `format`, `path`, `ok`, `databases`, `errors`,
`warnings`; layout in `apps/cigi_db/report.hpp`). Unknown keys and missing model files are
**warnings**, so a newer manifest still loads in an older tool; everything else below is an error.
The first error stops loading that manifest.

The viewer re-scans the root about once a second; an edited manifest is applied without a restart,
and one that no longer parses keeps the previous version (the error is kept in the catalog).

## `database.toml` reference

```toml
[database]
number = 1                    # required, 1..127
name = "Default (synthetic)"  # required
origin = { lat = 51.5, lon = -0.12, alt = 0.0 }   # optional (default 51.5, -0.12, 0); alt optional
bounds = { lat_min = 51.25, lat_max = 51.75, lon_min = -0.37, lon_max = 0.13 }  # optional
load_frames = 30              # optional, 0..65535: Start of Frame reports -n for this long

[terrain]                     # optional (default: synthetic)
kind = "synthetic"            # "synthetic" | "heightmap" (heightmap rendering and HAT/HOT: T21)
# file = "terrain/height.png" # required for heightmap

[default]
category = "other"            # category of Entity Types no [[entity]] maps (default other)

[[material]]                  # HAT/HOT material codes (ICD 4.2.3) and their map colour
code = 0                      # required, 0..4294967295, unique
name = "water"                # optional
colour = "#295c99"            # required, "#rrggbb"

[[entity]]
types = [100, 199]            # required: one number or an inclusive [first, last] range (0..65535)
category = "aircraft"         # required: aircraft | ground_vehicle | sea | lifeform | munition | other | none
name = "generic aircraft"     # optional
model = "models/jet.glb"      # optional (T22); OR lods = ["jet.glb", "jet_lod1.glb"], LOD0 first
scale = 1.0                   # optional, > 0
offset = [0.0, 0.0, 0.0]      # optional, metres
rotation = [0.0, 0.0, 0.0]    # optional, yaw/pitch/roll degrees: model axes -> CIGI body axes
attached_view = "cockpit"     # cockpit | full | hidden
eyepoint_front = "eyepoint_front"   # default eyepoint node names (View Control offsets win)
eyepoint_rear = "eyepoint_rear"
lod_thresholds = [0.25, 0.08, 0.02] # screen-height fractions, descending (one per LOD after 0)
cull_below = 0.001                  # below this screen height nothing is drawn

[[entity.part]]               # Articulated Part ID -> model node (ICD 4.1.6)
id = 1                        # required, 0..255, unique per entity
node = "turret"               # required
axis = "x"                    # optional rotation axis of a spinning part: x | y | z

[[entity.component]]          # Component ID -> model node (ICD 4.1.4)
id = 3                        # required, 0..65535, unique per entity
node = "gear"                 # required
kind = "visibility"           # visibility (default) | rotation | translation
```

Rules:

* **An exact type beats a range.** `types = 150` inside `types = [100, 199]` is allowed and wins
  (the "exception" form). Two entries with the same exact type, or two overlapping ranges, are an
  error; adjacent ranges are fine.
* An unmapped type uses `[default] category` and is listed in the viewer's scene statistics
  (`SceneStats::unmapped_entity_types`), so you can see which types still need an entry.
* `model` and `lods` are mutually exclusive; `model = "x"` is `lods = ["x"]`. The number of LODs
  must not exceed `lod_thresholds` + 1, and `cull_below` must be below the smallest threshold.
* `origin` must lie within `bounds`. HAT/HOT test points outside the bounds are answered
  "invalid" (ICD 4.1.24, 4.2.2). When a load completes, the scene's origin and bounds become the
  database's; entities are kept and their poses recomputed.
* Model files and the heightmap file are not required to exist (a missing one is a warning; the
  viewer then draws the category's primitive shape).

The built-in `default_database()` and `databases/default/database.toml` are the same database
(a unit test keeps them equal): type 0 `none`, 1-99 `other`, 100-199 `aircraft`,
200-299 `ground_vehicle`, everything else `other`.

## Adding an entity type

1. Open (or copy) a `database.toml` and add an `[[entity]]` table with `types` and `category`.
2. Run `cigi_db validate <root>`; fix what it reports.
3. The viewer picks the change up within a second (or restart it).

Entity Type values are database-specific in CIGI (ICD 4.1.2): the numbers mean what *your*
database says. To mirror a Genesis (or any other IG's) database, write one manifest whose
`types` follow that database's numbering; no code change is needed.

## The Database Number protocol

`MockIg` (and the reusable `DatabaseSelector`) follow ICD 4.1.1 / 4.2.1 (Figure 38):

| Host sends (Operate/Debug) | Start of Frame reports |
|---|---|
| nothing / 0 | `+n` of the database in force (0 if the IG is global) |
| `n` > 0, database exists | `-n` for at least its `load_frames` frames **and** until the Host has sent 0, then `+n` |
| `n` again after a 0 | reload: `-n` ... `+n` |
| a different `n` while loading | the load restarts with that number |
| `n` that no database has | error in the IG log; the current database and its number stay |
| anything in Reset/Standby (or Offline Maintenance) | ignored |

The ICD does not say what an IG does with an unknown number, whether entities survive a load, or
how long a load lasts; the mock IG keeps entities, keeps the old database in force until the load
completes, and uses `load_frames` for the duration. A Host that never sends 0 stays at `-n`.
Without `use_catalog()` (no `--db-root`) the IG has one global database and reports 0.

Wiring: `MockIg::use_catalog(&catalog, initial_number)` enables it, and `refresh_database()`
follows a hot-reloaded catalog. A facade-based IG feeds the same `DatabaseSelector` from
`cigi::Ig::on_database_request` / `set_database` (T09b Part 2 wires this; the headless
`demo_ig` does not serve databases yet).

## Model axes (T22)

`scale`, `offset` and `rotation` map a model's own axes onto the CIGI body axes (x forward,
y right, z down, ICD 3.4.1). The conventions for glTF models are filled in by T22.
