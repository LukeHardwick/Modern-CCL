# T18 — Benchmarks vs the original CCL, and network performance

Phase B · Depends on T11 (CCL build), T07, T08 · Branch `task/t18-benchmarks`
Follow the shared rules in [README.md](README.md) (rule 2 exception as in T11: the CCL is a
black-box comparison target under `bench/` only).

## Goal
Quantitative evaluation for the report: throughput, latency and allocations of Modern-CCL vs CCL
for realistic frames.

## Design (planner decision)
- Google Benchmark via FetchContent (latest release at least two weeks old; pin URL + SHA256),
  option `CIGI_BUILD_BENCHMARKS` (default OFF), Release preset `clang-bench` (`-O2 -DNDEBUG`, no
  sanitizers/coverage). CCL benchmarks only when `CIGI_BUILD_CCL_INTEROP=ON`.
- Scenarios (identical content for both libraries):
  1. Encode a typical Host frame: IG Control + 50 Entity Control + 20 Articulated Part + 10 Component
     Control + 2 HAT/HOT Request + View Control.
  2. Decode + dispatch the same frame (native order).
  3. Decode the same frame in foreign byte order.
  4. IG side: build SOF + 2 HAT/HOT responses; host side receive.
  5. Session round trip in memory (HostSession ↔ IgSession, MVP packets).
  6. Scaling: 1 → 200 entities per frame.
- Allocation counting: replace global `operator new/delete` in the benchmark binary and report
  allocations per frame as a counter.
- Methodology notes in `docs/evaluation/benchmarks.md`: machine, compiler, flags, repetitions
  (`--benchmark_repetitions=10`, report median and stddev), CPU frequency caveats on Apple Silicon.
- Output JSON to `bench/results/` and a plotting script (Python + matplotlib, optional) producing
  charts for the report.

## Network performance test (added 2026-10-08, user request)
In-memory benchmarks say nothing about what a Host sees on the wire. Add a real UDP measurement
that uses the actual stack (T07 sessions + T08 `UdpEndpoint` + `FramePacer`), not a mock.

- Tool `bench/net_perf` (CLI11 + spdlog), two roles in one binary: `--role ig` (a minimal IG that
  answers every IG Control with a Start of Frame immediately, echoing the host frame number in
  Last Host Frame Number, and answers HAT/HOT requests) and `--role host` (drives the run and
  reports). Address/port/rate/duration/payload options; both roles can run on one machine or on
  two machines.
- Measurements, all correlated by CIGI frame numbers (no extra probe packets):
  1. **Round-trip latency**: host send time of IG Control *n* → receive time of the SOF whose Last
     Host Frame Number is *n*. Report min/p50/p90/p99/p99.9/max. Only RTT is reported (one-way
     would need synchronised clocks; RTT/2 is shown but labelled as an estimate).
  2. **Pacing jitter** at 60 Hz (and a configurable rate): actual send interval vs nominal, and
     `FramePacer` overrun count. This is where Windows timer granularity (P3) shows up.
  3. **Loss and reordering**: from gaps/regressions in frame numbers, both directions.
  4. **Throughput**: (a) messages per second at a fixed realistic frame (the T18 scenario 1 frame,
     ~2.5 KB) with the pacer disabled; (b) frame size sweep — entities per frame from 1 up to the
     largest message that fits in one datagram, plus one run above the path MTU to show the IP
     fragmentation cost. Report bytes/s, messages/s, and where loss begins.
  5. HAT/HOT request → response latency (application-level request round trip).
- Steady-clock timestamps taken immediately around `send`/`receive`; warm-up period discarded;
  results written as JSON to `bench/results/` and plotted by the same plotting script.
- **Representativeness rules** (state these in `docs/evaluation/network.md` next to the results):
  - Loopback runs measure library + OS stack overhead only — a lower bound, useful for regression
    comparison and for comparing platforms, not a network figure.
  - The representative run is **two machines on a wired LAN** (no Wi-Fi), with the switch/NIC
    and link speed recorded. Document the exact commands so the user can repeat it on the lab
    network used with Genesis.
  - The responder is a minimal IG, so figures exclude IG render time; they are the
    communication-path cost a Host adds, not end-to-end visual latency.
  - Record machine, OS, compiler, build type, CPU governor/power mode, and run length (≥ 60 s per
    configuration, ≥ 3 runs; report median of runs).
  - Note sync vs async mode results separately.
- Not a ctest gate (results are machine-dependent); a 2-second loopback run is registered under
  label `network` only to check the tool works.
- No CCL comparison here (network cost dominates and the CCL has no transport); the CCL comparison
  stays with the in-memory scenarios.

## Acceptance
Benchmarks build and run; results table and charts in `docs/evaluation/benchmarks.md`; any
fairness caveats (e.g. CCL heap usage by design, version translation overhead) stated plainly.
`net_perf` loopback results on macOS (and Linux in Docker, noted as virtualised) in
`docs/evaluation/network.md` with the two-machine procedure documented for the user to run.
