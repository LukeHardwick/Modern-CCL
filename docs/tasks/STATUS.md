# Work status / session hand-off

Last updated: **2026-10-12 14:05 BST** (Opus planner). Read this first when starting a new session,
then [README.md](README.md) (task index + shared rules) and the task brief you are continuing.

## Process (unchanged)
- Opus plans and reviews; every implementation task is done by a **Sonnet** sub-agent in its own git
  worktree, using the brief in `docs/tasks/TNN-*.md`. Parallelise wherever dependencies allow.
- Before every merge: `scripts/check.sh --coverage` + `CIGI_LINUX_JOBS=6 scripts/linux-check.sh` on
  the branch, an **independent Opus review** (read-only, against brief + ICD + shared rules), a
  feature breakdown posted in chat, fixes applied, then merge `--no-ff` into `main` and mark the task
  ✅ in README.md.
- Nothing is pushed: `origin/main` is stale (`ce83731`). New agent worktrees can start from it, so
  **every agent prompt must start with "verify base: `git log -1` must be `<main sha>`, otherwise
  `git reset --hard <main sha>`"**.
- Agents stopped by network/usage limits: commit their WIP on the task branch, then either resume
  them (SendMessage, same session) or start a fresh Sonnet agent **in the existing worktree** with
  the brief + the "Remaining" list below (better when the old context is long).

## `main`
`main` contains T00–T08, T10 and T17 (`a57c7f4`; see README.md). All checks green at `5a20b87`
(macOS 416 tests, Linux GCC debug + ASan 413).

## Open task branches

> **14:05: the session "Subagent in Sonnet 5.5" is now the active planner.** It has Sonnet agents
> running in the T09a, T09b and T12 worktrees (T09a: full check incl. the host_minimal tidy fixes;
> T09b: Opus-review fixes; T12: Linux libFuzzer verification + gen_corpus/replay_main tidy fixes).
> Don't start other agents on those worktrees. T17 is merged.

Worktrees live under `.claude/worktrees/` in the main checkout.

| Task | Branch | Worktree | Head | Based on | State |
|---|---|---|---|---|---|
| T09b P1 mock-IG core + `ig_viewer` | `task/t09b-ig-viewer` | `agent-aabbb35402214e1c2` | `a50503f` | `f2dedae` (before T07/T10) | implementation done; debug 437/437 |
| T09a Host/Ig facade | `task/t09a-host-ig-facade` | `agent-a7ac964648ce5a2d4` | `09e82c1` | `5a20b87` | implementation + docs done; debug 478/478 |
| T12 fuzzing | `task/t12-fuzzing` | `agent-afd9869e1efc87280` | `8971714` | `5a20b87`-era (rebased onto T07) | 3 targets done; macOS fuzzing done; debug 419/419 |

Full `check.sh --coverage` results for the four heads above: see "Check results" at the end
(filled in after the run).

### T09b Part 1 — remaining
1. Merge `main` into the branch (it predates T07/T10; expect small conflicts in `CMakeLists.txt`,
   `tests/CMakeLists.txt`, `cmake/StaticAnalysis.cmake`, `.gitignore`).
2. Fix the **ImGui vertex/index overflow** the agent found when stress-testing (likely 16-bit
   `ImDrawIdx` overflow with many entities/trails in the map window: define `ImDrawIdx` as
   `unsigned int` for the ImGui build, or split/limit draw lists). Then measure fps with
   `ig_viewer --stress 200 --bench 600` and record it in the guide.
3. Overlapping entity ID labels (e.g. entities 3 and 4 render as "34"/"43"): offset or leader lines.
4. Write `docs/guide/ig-viewer.md` (build/run, controls, panels, colours, limitations; embed the
   screenshots in `docs/guide/img/`).
5. Full `check.sh --coverage` (asan, tidy, ≥ 95 % on `cigi_mock_ig`) + Linux check; Opus review; merge.
Part 2 (network mode + fault panel) starts after T09a is merged — see the brief.

### T09a — remaining
1. **Fix clang-tidy errors in `examples/host_minimal.cpp`** (only file failing; format, debug and
   asan pass with 478/478): replace `printf`/`fprintf` with `std::println`, `atoi` with
   `std::from_chars`, `argv` pointer arithmetic with `std::span(argv, argc)`, make the static
   `g_stop` initialisation non-throwing (e.g. `std::atomic<bool>` + `std::stop_source` created in
   `main`), `string_view::data()` → pass sizes / use `std::string`. Keep it ≤ 80 lines and matching
   the brief's target usage. Then full `check.sh --coverage` and Linux check.
2. Opus review (focus: state machine vs ICD §2.8, Lost/restart handling incl. "> stale_window
   consecutive stale frames = restart" with an IG restarted after 3 frames, fault transport,
   steady-state allocation, `examples/host_minimal.cpp` matches the brief's target usage).
3. Merge. Then start **T09b Part 2** and **T09** in parallel (T09 also needs T09b P1 merged).

### T12 — remaining
1. Verify the Linux libFuzzer path: commit `12134c4` switched the Docker image/preset to libc++
   (Ubuntu's libstdc++ 13 hides `std::expected` from Clang 18) — untested. Run
   `scripts/fuzz.sh --linux <target> 60` for each target.
2. Fuzz results so far (macOS, Homebrew LLVM libFuzzer, ~661 s each, **no crashes**):
   `fuzz_message_view` 17.3 M runs (26 k exec/s, cov 950), `fuzz_roundtrip` 8.2 M (12 k/s, cov 1304),
   `fuzz_sessions` 4.9 M (7.4 k/s, cov 1618). Logs in the worktree's `build/logs/` (not committed).
3. **Fix 38 clang-tidy errors** in `fuzz/gen_corpus.cpp` (30: trailing commas, unchecked `operator[]`,
   `reinterpret_cast` for writing bytes — use `std::as_bytes`/`std::bit_cast` or `.at()`) and
   `fuzz/replay_main.cpp` (8). Format is fixed (`8971714`); debug and asan pass with 419/419.
   Then full `check.sh --coverage`, Linux check, Opus review, merge; record results in the brief.

### T17 — merged `a57c7f4`
Opus review: merge after fixes. Applied: `SessionConfig::ignore_user_packets` wired into both
sessions (free `dispatch()` gained a trailing `bool ignore_user = false`), always-true opcode compare
removed (GCC `-Wtype-limits`), `if constexpr` builder size check, distinct-type duplicate test,
foreign-namespace user packet test. Checks: macOS 432/432 + coverage 97.8 %, Linux GCC debug + ASan
429/429. **Deferred to the facade (T09a follow-up / T09):** typed user packets in sessions (sessions
are not templated on the packet set; user packets arrive via `on_unknown`).

## Next tasks after these
T09 (demo apps; needs T09a, T09b P1, T10) → T11 (CCL interop) → **M1** (user trial + MSVC build by
the user). Phase B: T13–T16 (independent), T18, T19.

## Environment notes
- Colima: 6 CPU / 8 GB (raised 2026-10-12). Use `CIGI_LINUX_JOBS=6 scripts/linux-check.sh`.
- `linux-check.sh` uses one Docker volume with a per-checkout subdirectory; deps cache shared.
- Under heavy parallel load, gtest discovery has a 60 s timeout; if a check fails only on
  "Process terminated due to timeout" or "Killed signal", re-run it.
- Spec (git-ignored): `/Users/luke/Documents/Projects/Assignments/Final_Year_Project/Modern-CCL/third_party/spec/`.

## Check results (`scripts/check.sh --coverage`, macOS, 2026-10-12 ~13:40)
| Branch @ head | format | debug | asan | tidy | coverage (lines, total) |
|---|---|---|---|---|---|
| T17 @ `f70c635` | ✅ | ✅ 429 | ✅ | ✅ | ✅ 97.7 % |
| T09b P1 @ `a50503f` | ✅ | ✅ 540 | ✅ | ✅ | ✅ 98.3 % (viewer exe excluded) |
| T09a @ `09e82c1` | ✅ | ✅ 478 | ✅ | ❌ `examples/host_minimal.cpp` only | not reached |
| T12 @ `8971714` | ✅ (fixed) | ✅ 419 | ✅ | ❌ `fuzz/gen_corpus.cpp`, `fuzz/replay_main.cpp` | not reached |
Linux checks have not been run on any of the four yet (`CIGI_LINUX_JOBS=6 scripts/linux-check.sh`).
T09b and T17 are therefore ready for Opus review now.
