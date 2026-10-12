# T27 — Interactive Host: fly or drive a vehicle (optional)

Tools track, **optional** (user, 2026-10-12) · Depends on T09b Part 2, T20; T22 (turret nodes) and
T24 (background traffic) recommended · Branch `task/t27-interactive-host`
Follow the shared rules in [README.md](README.md) (rule 6 does not apply).

## Goal
A playable **Host**: a person flies an aircraft or drives a tank with a gamepad or keyboard, and the
Host sends CIGI to `ig_viewer`, or unchanged to a real IG such as Genesis. It shows the library
working end to end in a way people can try for themselves. In CIGI the IG only renders, so all
interactivity lives in this Host; **`ig_viewer` stays a pure IG** (no game input or logic in it).

## Scope
In: one player vehicle, simple physics, input, cameras through View Control, a small instrument
window, optional scenario traffic. Out (the future "level 2", which needs Phase B packets such as
Collision Detection and Special Effects, ICD 4.1.x via T13/T14): weapons, damage, scoring,
multiplayer.

## Design
- `apps/play_host`, built with `CIGI_BUILD_VIEWER` (it reuses raylib for gamepad/keyboard input
  and its own small window: no new dependency). Built on the T09a facade `cigi::Host`.
- Headless `cigi_play` library (unit tested, no raylib): fixed-step physics (for example 120 Hz,
  independent of the CIGI rate) with deterministic integration.
  - Aircraft: thrust, lift proportional to v² times a lift curve with stall, drag, gravity,
    stick/rudder to body rates with simple damping. Plausible to fly, not aerodynamically accurate.
  - Tank: throttle, brake, skid steering, turret yaw and barrel pitch rate limits, slope speed
    penalty.
  - Terrain: tank uses Ground Clamp Conformal (ICD 4.1.2); aircraft uses periodic HAT requests
    (ICD 4.1.24) for AGL and a crash/reset when AGL < 0.
- CIGI output per frame: Entity Control for the player (type from config, so Genesis numbers can
  be used), Articulated Part Control for turret/barrel/control surfaces (part ids from config),
  View Control + View Definition for cameras: cockpit, chase (view offset behind and above), turret
  sight; cycle with a button.
- Input bindings in TOML (`examples/config/play.toml`): axes, buttons, dead zones, inversion;
  keyboard fallback when no gamepad is present.
- Instrument window (raylib + ImGui): speed, altitude/AGL, heading, throttle, link state (from the
  facade), current camera. The out-the-window view is the IG's job.
- `--scenario FILE` (T24) adds non-player entities, sent by the same Host.
- Options: `--ig-addr`, `--ig-port`, `--listen-port`, `--vehicle aircraft|tank`, `--config`.

## Acceptance
README rules 7–9; coverage ≥ 95 % on `cigi_play`. Tests: physics with fixed inputs gives
hand-checked results (level flight trim, stall below a speed, turn radius of the tank), stepping
is deterministic, input mapping (dead zones, inversion). Manual check against `ig_viewer` on
loopback documented with screenshots; the user tries it with a gamepad. Docs:
`docs/guide/play-host.md`.
