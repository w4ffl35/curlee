# Contributing to Curlee

Thanks for contributing! Curlee is a verification-first language: **if it can't
prove a contract, the program doesn't run**. Keep that principle front and
center in every change.

## Project values

- **No proof, no run.** Unsupported constructs must produce clear errors — never
  silent guessing.
- **Small, test-driven changes.** Prefer golden tests for diagnostics and
  verification failures.
- **Deterministic by default.** New runtime behavior must be reproducible
  (fixed seeds, no ambient authority, bounded fuel).
- **Explicit `unsafe` boundaries.** Any host interaction (I/O, interop) must be
  capability-gated and documented.

## Development setup

Requirements (Ubuntu/Debian):

```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build g++ libz3-dev pkg-config
```

Build and test:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
bash scripts/smoke.sh
```

If `libz3-dev` is not installed, CMake fetches and builds Z3 from source
automatically (vendored path). CI also builds this path, so it must keep
working.

Other useful scripts:

- `bash scripts/coverage.sh` — coverage report (default threshold 94%).
- `bash scripts/tidy.sh` — clang-tidy over the build's compile_commands.json.
- `bash scripts/format.sh` — clang-format over tracked sources.
- `bash scripts/rc_validation.sh` — release-candidate soak campaign.
- `bash scripts/quality_audit.py --format json --out build/report.json` — quality gate.

## Code style

- C++23, `-Wall -Wextra -Wpedantic`, `.clang-format` + `.clang-tidy` enforced.
- Prefer `std::expected`/`std::optional` over exceptions for control flow in
  the front end (see `include/curlee/base/expected.h`).
- Keep functions small (quality gate: max 80 LOC / complexity 15).
- No `using namespace std;`.

### Asserts in production code

Curlee uses `assert()` for internal invariants in the parser, resolver,
verifier, and LSP. These compile out under `NDEBUG` (release builds). This is
an accepted trade-off during stabilization; prefer returning a diagnostic where
the invariant is reachable from untrusted input.

## Tests

- Every change should come with tests. Prefer golden tests for diagnostics,
  verification failures, and CLI output (see `tests/diagnostics/`,
  `tests/cli_diagnostics/`, `tests/lsp_hover/`).
- Deterministic unit tests live next to their subsystem (e.g.,
  `tests/resolver_tests.cpp`, `tests/vm_tests.cpp`).
- The `tests/correct_samples/` corpus is generated deterministically
  (`scripts/generate_correct_samples.py --seed 1337`). If you change the
  generator or language scope, bump `DATASET_VERSION` and regenerate.
- Keep the coverage gate green (CI enforces 100% over the non-excluded
  surface). Avoid `GCOVR_EXCL_LINE` markers where a real test is possible.

## Commit and PR process

- Work on a branch off `develop`; open a PR against `develop`.
- Conventional-style commit subjects (`feat:`, `fix:`, `tests:`, `docs:`, ...)
  match the existing history.
- CI must pass: debug+release build/test, coverage gate, quality audit, and
  golden/orphan checks.
- For agent-driven changes: see `.github/copilot-instructions.md` and the
  prompts in `.github/prompts/`.

## License

By contributing, you agree that your contributions are licensed under the MIT
License (see `LICENSE`). Please include a sign-off note in the PR body if you
are not the sole author of the project.

## Repository administration

- If this repository is transferred to a different owner/organization, update:
  - the wiki links in `README.md` and `docs/README.md`;
  - the anchors in `tests/readme_claims_tests.cpp`;
  - the `SECURITY.md` reporting URL.
