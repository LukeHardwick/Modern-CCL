# T19 — Documentation: Doxygen site, README, coming-from-CCL guide

Phase B · Depends on all · Branch `task/t19-docs`
Follow the shared rules in [README.md](README.md).

## Design (planner decision)
- Doxygen 1.18 + Graphviz 16.1 (installed). `docs/Doxyfile.in` configured by CMake; target `docs`
  (option `CIGI_BUILD_DOCS`, default OFF). `HAVE_DOT = YES` (include-dependency and call graphs),
  `WARN_AS_ERROR = YES` for the public headers, Markdown support so `docs/design/*.md` and
  `docs/guide/*.md` become pages, doxygen-awesome-css optional (FetchContent, pinned) for styling.
- Every public header gets complete `///` documentation (packet structs: one line per field with
  units and ICD section).
- README rewrite: what it is, status/packet coverage table (generated or maintained), quick start,
  build presets, verification story (tests, coverage, fuzzing, Wireshark, CCL), licensing note.
- `docs/guide/getting-started.md`, `docs/guide/coming-from-ccl.md`: maps CCL concepts (sessions,
  CigiOutgoingMsg `<<`, event processors/callbacks, version translation, `CIGI_NO_EXCEPT`) to
  Modern-CCL equivalents. Write it from the CCL **user manual and public API docs**, not its source.
- `docs/evaluation/`: link benchmarks, coverage report instructions, interop results.

## Acceptance
`cmake --build --preset clang-debug --target docs` produces the site with zero Doxygen warnings;
README and guides reviewed for accuracy against the code.
