# T09b — Mock-IG core and visual `ig_viewer`

Phase A (MVP) · Part 1 depends on T06 · Part 2 depends on T09a · Branch `task/t09b-ig-viewer`
Follow the shared rules in [README.md](README.md).

## Goal
A mock IG that **shows** what a Host is telling it, so the user can debug their DDS→GenesisIG Host
without Genesis: a 3-D out-the-window view, a top-down map, and debug panels (entity table, packet
inspector, link state, fault injection). It is a test tool, not a real IG: no terrain database, no
models, no imagery. Tools approved by the user on 2026-10-12: **raylib**, **Dear ImGui**, **rlImGui**.

The task has two parts. **Do Part 1 only** unless the planner tells you Part 2 can start.

## Layering (planner decision)
```
apps/mock_ig/   cigi_mock_ig  (static lib, NO graphics deps; built in every preset incl. Linux)
                  - SceneModel, HatHotResponder, synthetic terrain, geo/local frames, describe()
apps/ig_viewer/ ig_viewer     (exe; raylib + Dear ImGui + rlImGui; only when CIGI_BUILD_VIEWER=ON)
apps/demo_ig/   (T09)         headless mock IG = cigi::Ig + cigi_mock_ig + logging
```
`cigi_mock_ig` is shared by the headless `demo_ig` (T09) and `ig_viewer`, so both behave
identically. Put all logic that can be tested without a window in `cigi_mock_ig`; `ig_viewer`
should be thin rendering/UI code. Namespace `cigi::mock`. No exceptions, C++23, project warnings.
Allocation is allowed in `cigi_mock_ig` and the viewer (they are apps, not the codec), but not per
packet in steady state where it is easy to avoid (reserve; reuse buffers).

## Spec
ICD 3.3: §4.1.2 Entity Control (states, attach/detach, child offsets in parent body frame,
Entity Type), §4.1.6 Articulated Part Control, §4.1.8 Rate Control, §4.1.4 Component Control,
§4.1.9 Celestial Sphere Control, §4.1.16 View Control, §4.1.21 View Definition, §4.1.24 HAT/HOT Request,
§4.2.2/§4.2.3 HAT/HOT Response/Extended Response (incl. Update Period, Host Frame Number LSN),
§3.4 coordinate systems (§3.4.1 geodetic WGS-84; §3.4.2 entity body axes x fwd, y right,
z down, yaw from true north clockwise; §3.4.3 submodel/articulated part frames). Cite sections in comments.

## Part 1 (start now; no networking)

### `cigi_mock_ig` (headless library, fully unit-tested, coverage ≥ 95%)
1. **Synthetic terrain** `apps/mock_ig/terrain.hpp`: deterministic, cheap, with visible local
   relief so terrain clamping can be judged by eye:
   `h(lat, lon) = 100 + 40·sin(2π·lat/0.02)·cos(2π·lon/0.02)` metres (lat/lon in degrees; ~2 km
   wavelength). Provide `height(lat, lon)`, `normal(lat, lon)` (analytic derivative, as unit
   vector in local ENU and as azimuth/elevation for the Extended Response), and
   `material(lat, lon)` (height bands: 0 = water < 70 m, 1 = grass, 2 = rock > 130 m). Golden values
   in tests computed by hand / independently (not from the function itself).
2. **Frames** `geo.hpp`: geodetic ↔ local tangent plane (ENU, flat-earth around an origin
   lat/lon/alt, document the error at 10 km and 50 km); CIGI attitude (yaw/pitch/roll, NED body) →
   rotation matrix / quaternion in ENU; child-entity offset composition (parent body frame).
   Plain structs (`Vec3d`, `Quat`), no graphics types.
3. **SceneModel** `scene.hpp`: entity table keyed by Entity ID applying Entity Control (Active,
   Inactive/Standby, Destroyed; attach/detach to a parent; ground clamp flag → clamp to terrain
   in the model; Entity Type kept for shape selection; alpha), Articulated Part Control (per-part
   enables and offsets), Rate Control (entity and part rates; `advance(dt)` extrapolates in the
   coordinate system the packet specifies), Component Control (stored + logged, not interpreted),
   View Control/Definition (view ID → entity, offsets, FOV, near/far), Celestial Sphere (date,
   time of day, sun/moon/star enables). Also the per-frame history needed by the viewer: last N
   positions per entity (trails), recent HAT/HOT requests with results.
   `register_handlers(Dispatcher&)` wires every Host→IG MVP packet into the model (use T06's
   `Dispatcher`). Unknown entity IDs in Articulated Part/Rate Control: count + log, don't crash.
4. **HatHotResponder** `hat_hot.hpp`: given a request (+ the scene, for entity-relative requests)
   and the current IG frame number, produce Response or Extended Response per the request type;
   periodic requests honour Update Period and keep emitting until cancelled per the ICD; fill
   Host Frame Number LSN. Pure logic: returns packets, does not send.
5. **describe()** `describe.hpp`: `std::string describe(const P&)` for every MVP packet (one line,
   `name=value` pairs with units, enums via `EnumTraits`). Used by the packet inspector and by
   `demo_ig` logging. Plus `describe_any(const AnyPacket&)` over T06's decoded variant.
6. **Scripted host** `scripted_host.hpp`: an in-process generator producing a realistic Host
   message per frame (ownship flying a circle at 300 m AGL over the hills with a ground-clamped
   vehicle convoy of 5 attached/unattached entities, an articulated turret rotating, a view
   attached to the ownship, Celestial time advancing 1 min/s, one-shot + periodic HAT/HOT
   requests). It must go through the real codec: encode with `MessageBuilder` → bytes →
   `MessageView` → dispatch into `SceneModel`. This drives the viewer in Part 1 and doubles as an
   end-to-end test.

Tests (GoogleTest, `cigi_add_test`): terrain golden values; frame conversions (round-trip,
known points, attitude matrices for cardinal headings); scene semantics for each packet incl.
attach/detach, destroy, clamp, rates over `advance`; HAT/HOT one-shot/periodic/cancel/extended;
describe() for every packet; scripted host for 600 frames → expected entity count/positions.

### `ig_viewer` (exe, macOS-verified; Linux build of it not required)
- Dependencies via FetchContent in `cmake/Dependencies.cmake`, **only when `CIGI_BUILD_VIEWER=ON`**
  (new option, default OFF): raylib, Dear ImGui, rlImGui. Pick the latest release of raylib and
  Dear ImGui that is at least two weeks old (verify tags with `git ls-remote --tags`); rlImGui has no
  releases — pin a commit at least two weeks old. Use URL tarballs + `URL_HASH SHA256` (compute the
  hashes yourself). Build them with `-w`, `SYSTEM`, excluded from clang-tidy and from coverage.
  Disable raylib examples/games. Turn the option ON in the `clang-debug` preset only (so
  `check.sh` builds the viewer once; asan/tidy/coverage/linux stay OFF).
- `// PLATFORM:` notes: OpenGL is deprecated on macOS (raylib still uses it); high-DPI handling;
  MSVC is expected to work but is verified by the user.
- **3-D view.** Terrain mesh from the synthetic terrain (tiles around the camera, colour by
  material/height, simple directional light), entities as primitive shapes chosen by Entity Type
  (aircraft = cone/arrow, ground vehicle = box, other = sphere), coloured by Entity ID, with
  heading indicator and billboard ID label; articulated parts drawn as child shapes with their
  offsets/angles; trails. Sky colour and light direction from Celestial time of day
  (day/dusk/night gradient). Camera modes (key `C` cycles): **Host view** (the View Control/
  Definition the Host sent: attached entity + offsets + FOV), **Chase** (orbit selected entity,
  mouse), **Free** (WASD + mouse). Coordinate mapping into raylib's Y-up world documented in code.
- **Top-down map** (ImGui window, `ImDrawList`): terrain shading, entity icons with heading,
  trails, the Host-view frustum wedge, HAT/HOT probe points (colour by valid/invalid).
- **Panels** (ImGui): Link (mode, IG/Host frame numbers, frame rate, packets/s, bytes/s — Part 1
  shows "scripted"); Entities (sortable table: ID, type, state, parent, lat/lon/alt, AGL,
  attitude; click to select); Selected entity (all fields, articulated parts, components);
  Packet inspector (last ~120 frames, pause/step, per-packet `describe()`); HAT/HOT log.
- CLI (CLI11 is not yet in the repo — use plain `argv` parsing for Part 1 and keep it tiny; T09
  adds CLI11 and Part 2 switches to it): `--scripted` (default in Part 1), `--origin lat,lon`,
  `--width/--height`, and **`--screenshot <file.png> --frames N`** (render N frames, save PNG,
  exit 0) so the result can be checked without a person. Use it: save screenshots of each camera
  mode under `docs/guide/img/` (≤ 300 KB each) and look at them yourself before reporting.
- 60 fps with 200 entities on the user's Mac (report what you measured).

### Part 1 acceptance
README rules 6–9 for `cigi_mock_ig` (golden-byte rule does not apply: it has no packets).
`check.sh --coverage` passes with the viewer built in `clang-debug`; `ig_viewer --scripted
--screenshot ...` produces sensible images; `docs/guide/ig-viewer.md` (how to run, controls,
panels, what the colours mean, limitations). Report the dependency versions/hashes.

## Part 2 (only after T09a is merged; planner will message you)
- Network mode (new default): `ig_viewer` becomes a real mock IG over UDP using `cigi::Ig`
  (T09a), with the same options as `demo_ig` (T09; switch to CLI11): `--listen-port`,
  `--host-addr`, `--host-port`, `--rate`, `--load-frames`, `--pcap`, plus `--scripted` to keep the
  offline mode. Received packets feed the same `SceneModel`; HAT/HOT responses are sent via
  `IgFrame`.
- Link panel shows real session state; **Fault injection panel** drives T09a's fault hooks:
  refuse Operate, pause Start of Frame, simulate database load for N frames, drop %, added
  latency, duplicate %, "restart IG" (frame counter reset).
- Network test (label `network`, built only with the viewer option): not required — `demo_ig`
  covers the protocol; add a smoke test that runs `ig_viewer --scripted --screenshot` only if a
  window can be created (skip otherwise).
