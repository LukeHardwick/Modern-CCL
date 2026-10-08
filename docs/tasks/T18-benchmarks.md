# T18 — Benchmarks vs the original CCL

Phase B · Depends on T11 (CCL build) · Branch `task/t18-benchmarks`
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

## Acceptance
Benchmarks build and run; results table and charts in `docs/evaluation/benchmarks.md`; any
fairness caveats (e.g. CCL heap usage by design, version translation overhead) stated plainly.
