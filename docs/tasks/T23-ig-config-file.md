# T23 — Config file for the mock IG and viewer (`ig.toml`)

Tools track · Depends on T09b Part 2 (CLI11 options), T20 · Branch `task/t23-ig-config`
Follow the shared rules in [README.md](README.md) (rule 6 does not apply).

## Goal
Every option of `ig_viewer` and `demo_ig` can live in one hand-edited TOML file, so a test rig is
set up once rather than with long command lines.

## Design
- One `IgConfig` struct in `cigi_mock_ig` (headless), shared by `demo_ig` (if T09 has merged;
  otherwise T09 adopts it) and `ig_viewer`. Sections: `[network]` (listen port, host address and
  port, rate), `[database]` (root, default number, poll interval), `[viewer]` (size, fullscreen,
  camera, ui, start hour), `[faults]` (the T09 / T09b fault defaults), `[record]` (directory,
  T25), `[log]`.
- Precedence: built-in defaults < config file < command line. File from `--config FILE`, else
  `$CIGI_IG_CONFIG`, else `./ig.toml` if present. No platform config directories (keep it
  portable and obvious).
- `--print-config` writes the effective config as TOML (with comments naming the source of each
  value) and exits. Unknown keys are warnings; type errors are fatal with file:line.
- Ship `examples/config/ig.toml` with every key commented.

## Acceptance
README rules 7–9; coverage ≥ 95 % on headless code. Tests: precedence per key, env var, missing
file, type errors with line numbers, `--print-config` round trip (print, then load, gives the same
struct). Docs section in `docs/guide/ig-viewer.md`.
