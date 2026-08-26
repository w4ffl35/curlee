# Curlee documentation

This directory holds in-repo documentation. The detailed, evolving language
reference (supported fragment, syntax, modules, execution model, release
policy) currently lives in the project's GitHub wiki, which is the source of
truth while Curlee is in "production-readiness stabilization":

- Wiki home: https://github.com/w4ffl35/curlee/wiki
- Supported fragment + stability: https://github.com/w4ffl35/curlee/wiki/Stability-and-Supported-Fragment

> If this repository is transferred to a different owner/organization, update
> the wiki links above (and in the README) to the new location.

## In-repo resources

| File | Purpose |
| --- | --- |
| [`../README.md`](../README.md) | Project overview, build & run instructions |
| [`cpp23-best-practices.md`](cpp23-best-practices.md) | C++23 reference for LLM agents contributing code |
| [`loop-contracts.md`](loop-contracts.md) | Loop invariants and `decreases` variant clauses (issue #261) |
| [`fuel-contracts.md`](fuel-contracts.md) | Static fuel bounds / WCET contract clauses (issue #262) |
| [`addr-of.md`](addr-of.md) | `addr_of(arr)` — physical address of a Curlee-owned array (issue #286) and the alignment guarantee |
| [`../CONTRIBUTING.md`](../CONTRIBUTING.md) | Contributor guide |
| [`../SECURITY.md`](../SECURITY.md) | Vulnerability reporting |
| [`../.github/compatibility-policy.json`](../.github/compatibility-policy.json) | Semver/bundle-format compatibility policy |
| [`../LICENSE`](../LICENSE) | MIT license |

## Build & test quick reference

```bash
# Dependencies (Ubuntu/Debian): cmake ninja-build g++ libz3-dev pkg-config
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure

bash scripts/smoke.sh
```

If `libz3-dev` is not installed, CMake automatically fetches and builds Z3 from
source (vendored path). This is exercised in CI so it keeps working.

## Status notes

- **Python interop is stubbed.** `python_ffi.call` is not yet implemented;
  see the README for details.
- **Coverage gate.** `scripts/coverage.sh` defaults to a 94% line threshold;
  the CI gate enforces 100% line/branch/function over the non-excluded surface.
- **`assert()` in release builds.** Curlee uses `assert()` for internal
  invariants in the parser, resolver, verifier, and LSP. These compile out
  under `NDEBUG` (release builds). This is an accepted trade-off while the
  codebase stabilizes; see CONTRIBUTING.md.
