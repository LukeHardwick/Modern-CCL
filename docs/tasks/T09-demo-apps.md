# T09 — Demo apps: `demo_host` and mock `demo_ig`

Phase A (MVP) · Depends on T09a (facade), T10 (pcap writer) · Branch `task/t09-demo-apps`
Follow the shared rules in [README.md](README.md).

## Goal
Let a person trial the library **without a real IG**: a mock IG and a demo Host that talk CIGI 3.3
over UDP on one machine, log everything readably, and can record traffic for Wireshark.

## Design (planner decision)
- Both apps are built on the T09a facade (`cigi::Host`, `cigi::Ig`), so `demo_host` doubles as
  the facade's worked example; drop to sessions/UDP only where the facade can't express something
  (report any such gap — it is a facade bug).
- `apps/demo_ig` (mock IG): CLI11 (FetchContent, pinned, latest release at least two weeks old) +
  spdlog. Options: `--listen-port`, `--host-addr`, `--host-port`, `--rate` (Hz, default 60),
  `--load-frames` (frames spent "loading" in Standby before accepting Operate), `--pcap <file>`,
  `--duration`, `--log-level`.
  Behaviour: sends Start of Frame every frame via `cigi::Ig`; follows the §2.8
  start-up sequence; keeps an entity table from Entity Control (logs creation/destruction/attach);
  logs articulated parts, rates, components, views, celestial time; answers HAT/HOT requests with a
  synthetic, deterministic terrain `h(lat, lon) = 100 + 50·sin(lat)·cos(lon)` metres (extended
  responses include HAT, HOT, a material code and the terrain normal); honours Update Period for
  periodic responses; fills Host Frame Number LSN.
- `apps/demo_host`: options `--ig-addr`, `--ig-port`, `--listen-port`, `--sync/--async`,
  `--duration`, `--pcap`, `--log-level`. Behaviour: waits for SOF, requests Operate, then creates an
  ownship entity flying a circle, attaches a view to it, sets Celestial Sphere time, animates an
  articulated part, sends a one-shot and a periodic HAT/HOT request, prints responses and session
  stats each second and a summary at exit (exit code 0 only if responses were received and no
  protocol errors occurred).
- Both apps: `--pcap` writes every sent/received datagram with T10's `cigi::tools::PcapWriter`.
- `scripts/demo.sh`: starts `demo_ig` and `demo_host` on loopback for N seconds and prints both
  summaries.
- A ctest integration test (label `network`) runs the pair for ~3 s and asserts exit code 0.
- `docs/guide/trying-it-out.md`: how to run the demos, what the logs mean, and how to open the pcap
  in Wireshark (and live capture on `lo0` on macOS, which needs Wireshark's ChmodBPF helper).

## Acceptance
README rules 7–9 (unit tests for the terrain function and any non-trivial demo logic; coverage
target applies to library code only). `scripts/demo.sh` output pasted (trimmed) in the report.
