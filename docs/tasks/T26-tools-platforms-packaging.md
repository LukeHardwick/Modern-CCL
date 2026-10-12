# T26 — Viewer on Linux and Windows, release packaging

Tools track · Depends on T20–T25 (run last) · Branch `task/t26-tools-packaging`
Follow the shared rules in [README.md](README.md) (rule 6 does not apply).

## Goal
The mock-IG tools build and run on macOS, Linux and Windows and ship as a self-contained bundle
(tools + sample databases + scenarios + example config), so someone can use them without building
from source.

## Design
- **Linux:** build `ig_viewer` in `scripts/linux-check.sh` (add raylib's X11/GL development
  packages to the Docker images). Smoke test (ctest label `viewer`): `ig_viewer --scripted
  --screenshot` under Xvfb with Mesa llvmpipe; it skips (does not fail) when no display can be
  created. Report whether Wayland-only hosts need anything else.
- **Windows/MSVC:** the user verifies by hand. Make it buildable (presets, `/EHs-c-` for the
  libraries, no POSIX-only calls; mark anything platform-specific `// PLATFORM:`) and write the
  steps in `docs/guide/ig-viewer.md`. File watching for hot reload must use the portable mtime
  polling from T20.
- **Asset lookup:** databases, scenarios and config are found relative to the executable
  (`<exe>/../share/cigi/`) when not given explicitly, so an unpacked bundle works anywhere.
- **Packaging:** CPack (ships with CMake, no new dependency): `.tar.gz` (macOS, Linux) and `.zip`
  (Windows) with `ig_viewer`, `demo_ig`, `demo_host`, `cigi_db`, `databases/`, `scenarios/`,
  `examples/config/ig.toml`, third-party licences and asset credits, and a quick-start README.
  `--version` on every tool prints the project version and git hash. A macOS `.app` is optional;
  report it rather than build it if it gets complicated (signing is out of scope).
- **Docs:** bring `docs/guide/ig-viewer.md`, `databases.md`, `scenarios.md` and `recording.md`
  together into one consistent tools guide, linked from the top-level README.

## Acceptance
`scripts/check.sh --coverage` and `CIGI_LINUX_JOBS=6 scripts/linux-check.sh` green with the viewer
built on Linux; the smoke test runs on Linux under Xvfb; `cpack` produces the macOS and Linux
archives; an unpacked archive runs `ig_viewer --scripted` from a different working directory. MSVC
steps written for the user.
