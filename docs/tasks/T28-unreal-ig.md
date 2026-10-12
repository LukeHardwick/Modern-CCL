# T28 — Unreal Engine IG plugin (optional, locked)

> **🔒 LOCKED: do not start, brief, schedule or launch this task without the user's express
> permission given in chat for T28 specifically.** Approval of other tasks, of "the tools track"
> or of "everything remaining" does **not** cover T28. The user has not decided whether to use it
> (2026-10-12).

Future work, **optional** · Would depend on M1, T20 (entity-type manifest idea); T27 recommended ·
Branch `task/t28-unreal-ig`
Follow the shared rules in [README.md](README.md) (rule 6 does not apply).

## Goal
A minimal **CIGI 3.3 IG built on Unreal Engine 5**, driven by the same Hosts as `ig_viewer` and
Genesis, to show the library working with a second, high-fidelity IG. A proof of concept, not a
product.

## Background (planner notes, 2026-10-12)
- Licence (Epic's 2024 terms; re-check the current EULA before starting): free for students,
  educators and companies under $1M a year; the engine is source-available, not open source.
  The plugin is our own code.
- CIGI makes sense here because Unreal is used as an **IG** (simulation stays in the Host). It is
  not the right tool for making a game.
- No established Unreal CIGI plugin was found (Unigine ships one), so there is little prior art.

## Sketch (to be turned into a full brief only once unlocked)
1. Build integration: link `cigi` as a prebuilt static library. Risk: Unreal defaults to C++20
   while our public headers need C++23 (`std::expected`), so either build the plugin as C++23 or
   add a thin C++20 wrapper. Unreal builds without exceptions, as the library does.
2. Network and frame loop: the existing UDP transport on a worker thread, or an Unreal-socket
   `DatagramTransport`. Fixed frame rate for synchronous mode.
3. Entities: entity type -> Blueprint/mesh mapping in an Unreal data asset (mirrors the T20
   manifest); spawn/destroy, transforms, articulated parts via bones or components.
4. Geodetic placement and terrain via Cesium for Unreal (Apache 2.0; Cesium ion account for hosted
   terrain); HAT/HOT by line trace.
5. Views (View Control / Definition -> camera, FOV) and celestial (SunSky).

Estimate: 4–6 tasks, about 3–6 weeks. Unreal is a 100+ GB install, and agents cannot judge the
editor visually, so the user does most of the visual checking.
