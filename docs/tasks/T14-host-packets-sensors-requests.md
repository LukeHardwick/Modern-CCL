# T14 — Sensors, motion tracking, ERM, trajectory, collision and request packets

Phase B · Depends on M1 · Parallel with T13, T15–T17 · Branch `task/t14-host-packets-sensors-requests`
Follow the shared rules in [README.md](README.md).

## Packets (all Host→IG, fixed size)
| Op | Packet | Size | Section (≈ line) | Notes |
|---|---|---|---|---|
| 17 | Sensor Control | 24 | §4.1.17 (6811) | |
| 18 | Motion Tracker Control | 8 | §4.1.18 (7245) | View vs View Group scope. |
| 19 | Earth Reference Model Definition | 24 | §4.1.19 (7451) | Semi-major axis / flattening f64; relates to SOF ERM flag. |
| 20 | Trajectory Definition | 24 | §4.1.20 (7546) | |
| 22 | Collision Detection Segment Definition | 40 | §4.1.22 (7947) | Errata: wording only. |
| 23 | Collision Detection Volume Definition | 48 | §4.1.23 (8146) | Sphere vs cuboid; fields unused per type. |
| 25 | Line of Sight Segment Request | 64 | §4.1.25 (8654) | 3.2 added Update Period and Destination Entity ID / Valid (reserved before). |
| 26 | Line of Sight Vector Request | 56 | §4.1.26 (9007) | 3.2 added Update Period. |
| 27 | Position Request | 8 | §4.1.27 (9312) | Object class enum; update mode. |
| 28 | Environmental Conditions Request | 32 | §4.1.28 (9433) | Request-type bit mask (combinable flags) — model as a flags type, not an enum. |

## Scope
Same as T13: layout comment from figure + table, table-driven structs, `EnumTraits`, ranges,
cross-field `validate()` where stated (e.g. collision volume type determines meaningful fields),
register in `HostToIgPackets`, extend the Wireshark and CCL suites, tests per README rule 6.
For combinable flag fields add a small `Flags<E>` helper (bitwise ops on an enum) in
`packets/common.hpp` if none exists, with tests.

## Acceptance
README rules 6–9, plus Wireshark and CCL suites green or discrepancies documented.
