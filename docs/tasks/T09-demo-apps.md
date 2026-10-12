# T09 — Demo apps: `demo_host` and mock `demo_ig`

Phase A (MVP) · Depends on T09a (facade), T09b Part 1 (`cigi_mock_ig`), T10 (pcap writer) · Branch `task/t09-demo-apps`
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
  start-up sequence. All IG-side logic comes from T09b's headless **`cigi_mock_ig`** library (do not
  re-implement it): `SceneModel` keeps the entity table (log creation/destruction/attach), articulated
  parts, rates, components, views, celestial time; `HatHotResponder` answers HAT/HOT requests from the
  shared synthetic terrain (Update Period, Host Frame Number LSN, extended responses); `describe()`
  formats packets for `--log-level debug`. `demo_ig` and `ig_viewer` must behave identically.
  **Fault injection flags** (via T09a's hooks / `FaultInjectingTransport`): `--refuse-operate`,
  `--pause-sof-after <s>` + `--resume-sof-after <s>`, `--restart-after <s>`, `--drop <0..1>`,
  `--duplicate <0..1>`, `--latency <ms>`, `--seed`. Optional `--table` prints a live entity table
  each second (plain text, no extra deps).
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
README rules 7–9 (unit tests for any non-trivial demo logic and the fault flags; coverage
target applies to library code only). `scripts/demo.sh` output pasted (trimmed) in the report.
- Fault-flag integration tests (label `network`): `demo_host` against `demo_ig --refuse-operate`
  never reaches Operate and reports it; `--pause-sof-after 1 --resume-sof-after 2` → host goes Lost
  then recovers; `--drop 0.2` still exits 0 with stats showing drops.
