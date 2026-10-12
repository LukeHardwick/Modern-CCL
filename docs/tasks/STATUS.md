# Work status / session hand-off

Last updated: **2026-10-12 (tools-track planning merged)** (Opus planner). Read this first when starting a new session,
then [README.md](README.md) (task index + shared rules) and the task brief you are continuing.

## Process (unchanged)
- Opus plans and reviews; every implementation task is done by a **Sonnet** sub-agent in its own git
  worktree, using the brief in `docs/tasks/TNN-*.md`. Parallelise wherever dependencies allow.
- Before every merge: `scripts/check.sh --coverage` + `CIGI_LINUX_JOBS=6 scripts/linux-check.sh` on
  the branch, an **independent Opus review** (read-only, against brief + ICD + shared rules), a
  feature breakdown posted in chat, fixes applied, then merge `--no-ff` into `main` and mark the task
  ✅ in README.md.
- The user pushes `main` and tags themselves (last push: `f64a910` + `v0.2.0`). Agent worktrees can
  still start from an older base, so **every agent prompt must start with "verify base:
  `git log -1` must be `<main sha>`, otherwise `git reset --hard <main sha>`"**.
- Agents stopped by network/usage limits: commit their WIP on the task branch, then either resume
  them (SendMessage, same session) or start a fresh Sonnet agent **in the existing worktree** with
  the brief + the "Remaining" list below (better when the old context is long).

## `main`
`main` contains T00–T08, T09a, T09b Part 1, T10, T12, T17 and the viewer resize fix (see
README.md). All checks green on the release candidate: macOS 658/658 (98.3 % lines), Linux GCC
debug + ASan 655/655.

## Open task branches
Phase A wave 1 launched 2026-10-12 from `567bee8` (Sonnet agents, parallel). **T09 merged `47775ef`**
(review fixes applied); merged main (T09 + T09c) check.sh: 705/705 all green. Next: **T09d** (launched),
then T09b Part 2 (uses T09d's peer hook; serialised because both touch `apps/demo_common`).
- **T09** `task/t09-demo-apps`: demo_host + demo_ig, CLI11. Told not to touch the facade/examples
  (report facade gaps instead) or `apps/ig_viewer/`.
- **T09c merged `d348d73`** (Opus review fixes applied; macOS 671/671, 98.3 % lines; Linux 668/668).
  Notes for T09d: IG dispatches before latching the sender (ig.cpp ~147-161); latch-retry test needs a seam.
- **T22a** `task/t22a-sample-models` @ `cbf595a`: CP1 (Hawk exterior) accepted with fixes.
  **Handoff:** see the "Handoff — 2026-10-12 evening" section at the end of the T22a brief. Next is
  CP2 (fold-in fixes + cockpit) then CP3 (jet LODs + wrap-up) with a **fresh Opus 5.5** agent/session,
  images at each checkpoint. Aircraft only today; other vehicles later. No worktree holds the branch.
- **T09d** `task/t09d-facade-peer`: Sonnet agent running (from `da7a213`). Then T09b Part 2.
Next: T09b Part 2 after T09 merges (reuses its CLI11/options) → T11 (ask the user before it
downloads/builds the LGPL CCL) → M1.

### T09b Part 1 — merged
Opus review fixes applied (CIGI_BUILD_MOCK_IG guard, Host view aimed at the convoy, SOF echoes
Operate/Debug, MockIg error-path tests, Inactive-parent extrapolation, label/buffer nits). Checks:
macOS 547/547 (mock_ig 99.2 % lines), Linux GCC debug + ASan 544/544 (after a -Wsign-conversion
test fix). `--stress` capped at 1000 (guess; measure the datagram limit in Part 2).
Part 2 (network mode + fault panel) can start now that T09a is merged — see the brief.

### T09a — merged
Three Opus review rounds (blocker: close() from callbacks used a null transport; database replay on
restart / not on a mere Lost blip; stale+duplicate restart rule with `saw_stale_`; Ig latches the
first parsed Host; FaultInjectingTransport release semantics; latch hooks made internal; MSVC
aligned new/delete in tests/support/counting_new.hpp). Deviation: `HostConfig::probe_interval`.
**Follow-ups (small, not blocking):**
- Standby false positive: Host-requested Standby + one reordered stale frame before the IG's
  first Standby SOF counts as a restart (and replays the database). Skip the rule when the
  requested mode is Standby.
- FaultInjectingTransport header: a failing `send()` returns its own error and drops (still
  counts) the deferred one — document it.
- examples/host_minimal.cpp:19 comment still says "a plain flag is async-signal-safe".
- Tests: "-n after Lost", "request_database while Lost", IgCore-level latch retry; Ig logs
  "Host connected" on every failed latch attempt (ig.cpp:149-151).
- `on_user<P>` → T09c.

### T12 — merged
Linux libFuzzer path: Ubuntu 26.04 + clang 21 image (libc++ approach failed: Ubuntu's libFuzzer is
built against libstdc++). Review fixes: full round-trip equality for every packet, tidy back on for
replay/gen_corpus, session invariants, decorrelated seeds, fuzz.sh via preset. No crashes found.
Results in the T12 brief. **Open questions for the user:** lengthen Linux fuzz runs (60 s) to
~10 min? Move the other Linux presets to Ubuntu 26.04 / clang 21? Docker needs
`DOCKER_HOST=unix:///Users/luke/.colima/default/docker.sock` + an empty `DOCKER_CONFIG` from agent
shells (`docker-credential-desktop` is not on PATH).

### T17 — merged `a57c7f4`
Opus review: merge after fixes. Applied: `SessionConfig::ignore_user_packets` wired into both
sessions (free `dispatch()` gained a trailing `bool ignore_user = false`), always-true opcode compare
removed (GCC `-Wtype-limits`), `if constexpr` builder size check, distinct-type duplicate test,
foreign-namespace user packet test. Checks: macOS 432/432 + coverage 97.8 %, Linux GCC debug + ASan
429/429. **Deferred to the facade (T09a follow-up / T09):** typed user packets in sessions (sessions
are not templated on the packet set; user packets arrive via `on_unknown`).

## Release v0.2.0 — tagged 2026-10-12 on `192392c` (annotated; pushed to origin with `main` at `f64a910`)
Contents: T00–T08, T09a, T09b Part 1, T10, T12, T17 + the viewer resize/fullscreen fix
(`fix/viewer-resize`). Release candidate checks: macOS check.sh --coverage 658/658 (98.3 % lines),
Linux GCC debug + ASan 655/655. Notes: `docs/releases/v0.2.0.md`. `v0.1.0` (2026-10-08, pushed)
was the foundation. Tagged at the user's request before their hand test of the viewer: still to
check by hand: green button, live F/F11/Esc, edge-drag (listed as a known limitation). New work: **T09c**, T09b Part 2, T09.

## Tools track: productionise `ig_viewer` / mock IG (planned 2026-10-12, NOT launched)
Briefs T20–T28 merged into `main` (`f11ec6a`); the user wants to review them before any agent runs. User decisions: aimed at both dev/test and demo use; database
= folder + TOML manifest per CIGI Database Number; glTF models with primitive fallback; heightmap
terrain shared by HAT/HOT and rendering (in `cigi_mock_ig`, so `demo_ig` agrees); network mode,
scenario files, record/replay (pcap) and config file all in scope; Genesis-matching entity
numbers via a configurable mapping; macOS + Linux + Windows; small CC0 sample assets in the repo.
**Libraries approved:** toml++ (hand-edited files) + glaze (JSON, machine files). T20 also
evaluates glaze's TOML reader. T27 (interactive Host) is optional.
**T22a progress (2026-10-12, planner spike, inline at the user's request):** Blender 5.2.2 installed (Homebrew); `tools/models/make_models.py` builds `databases/sample/models/jet.glb` (35 KB, nodes + hinge pivots checked) and previews in `docs/guide/img/models/`. **User requirements added 2026-10-12 (format + fidelity research, improve jet); agent launched.** Remaining: helicopter, tank, truck, `databases/sample/database.toml` (needs T20's format), CREDITS.md, `scripts/make_models.sh`.
**Before launching T21/T22:** models come from **T22a** (Blender script; the user installs
Blender first). The user downloads one SRTM3 tile for T21. Agents must not download; candidate
online models are listed in the T22a brief (optional, each download needs the user's approval).
Order: T09b P2 ‖ T20 → T22a → T21 ‖ T22 ‖ T24 → T23 ‖ T25 (‖ T27) → T26.
**T28 (Unreal IG) is 🔒 locked:** optional future work. Never start, brief or launch it without the
user's express permission for T28 specifically; approval of other tasks does not cover it.

## Next tasks after these
T09 (demo apps; needs T09a, T09b P1, T10) → T11 (CCL interop) → **M1** (user trial + MSVC build by
the user). Phase B: T13–T16 (independent), T18, T19.

## Environment notes
- Colima: 6 CPU / 8 GB (raised 2026-10-12). Use `CIGI_LINUX_JOBS=6 scripts/linux-check.sh`.
- `linux-check.sh` uses one Docker volume with a per-checkout subdirectory; deps cache shared.
- Under heavy parallel load, gtest discovery has a 60 s timeout; if a check fails only on
  "Process terminated due to timeout" or "Killed signal", re-run it.
- Spec (git-ignored): `/Users/luke/Documents/Projects/Assignments/Final_Year_Project/Modern-CCL/third_party/spec/`.

## Check results
Per-branch results from before the v0.2.0 merges have been removed (all those branches are merged); the
release candidate figures are under `main` above. The tools-track merge (`f11ec6a`) added docs, a
Python script and assets only, so no C++ checks were run on it.

## Worktrees (pruned 2026-10-12)
All merged `agent-*` task worktrees and their branches have been removed. Only the T10 worktree
(`agent-a356b2bd9342d0073`, untracked `__pycache__/` only) is left for the user to force-remove.
Session worktrees (`hello-df812a`, `subagent-sonnet-5-5-7fd7fe`, `ig-viewer-mock-ig-productionize-96da85`)
are kept; their branches are all merged.
