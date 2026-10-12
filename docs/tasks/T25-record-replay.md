# T25 — Record and replay (pcap + JSON index)

Tools track · Depends on T09b Part 2, T10 ✅ · Branch `task/t25-record-replay`
Follow the shared rules in [README.md](README.md) (rule 6 does not apply).

## Goal
Record a live Host session to a file that Wireshark opens, and replay any such file (including
captures made with Wireshark itself) into the viewer with pause, seek and speed control, so a
problem seen once can be examined again and again.

## Design
- `cigi::tools::PcapReader` beside T10's `PcapWriter`: classic pcap (both byte orders, micro- and
  nanosecond) and **pcapng** (SHB, IDB, EPB, SPB; skip other blocks), since Wireshark saves pcapng
  by default. Link types: RAW (101), Ethernet (1), NULL/loopback (0, macOS `lo0`), Linux SLL
  (113). IPv4 + UDP only; fragmented IP is reported and skipped. Allocating is fine here (tools).
- Direction: Host -> IG datagrams are picked by UDP port (`--host-port` / `--ig-port`, defaulting
  from the config) or, if none is given, by the first packet being IG Control (ICD 4.1.1) vs Start
  of Frame (ICD 4.2.1).
- Record: `ig_viewer` / `demo_ig` `--record DIR` (or `[record]` in T23's config) writes
  `<timestamp>.pcap` (both directions, T10 writer) plus `<timestamp>.json` (glaze): tool version,
  start time, ports, database number/name, config path, and a frame index (Host frame number ->
  file offset, timestamp).
- Replay: `ig_viewer --replay FILE` feeds Host -> IG datagrams into `MockIg` at recorded timing.
  The index is built on load if the sidecar is missing. Replay panel: play/pause, step frame,
  speed 0.25x-8x, timeline scrubber, jump to a Host frame number. Seek = reset the scene and
  re-ingest from the start without rendering (measure; add periodic scene snapshots only if a
  10-minute capture seeks slower than ~1 s). IG -> Host packets of the capture are shown in the
  packet inspector, marked as recorded.
- `cigi_db` (or a new `cigi_pcap`) `info FILE`: datagrams, duration, frame range, packets by
  opcode, as text or JSON.

## Acceptance
README rules 7–9; coverage ≥ 95 % on the reader and replay logic. Tests: hand-built pcap and
pcapng files for each link type and byte order; truncated or corrupt files return errors (never
crash; consider adding the reader to T12's fuzz targets); direction detection; seek gives the
same scene as playing straight through (compare `SceneModel` state). Round trip: record a
scripted session, replay it, and get identical scene state at the end.
