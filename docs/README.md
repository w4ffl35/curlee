# Documentation

Curlee’s user-facing documentation has moved to the GitHub wiki:

- [wiki/](../wiki/)

This folder is intentionally kept minimal to avoid duplicated sources of truth.

## Versioning policy (summary)

- Dataset / harness versions should increment when the generator changes, the language surface changes, or the verification scope changes.
- Generated artifacts should record parameters (e.g., seed + sample count) in headers for reproducibility.

Canonical details live in the wiki: `Quality-and-Datasets.md`.

## Stable vs unstable features

- **Stable:** syntax/semantics covered by the current verification scope.
- **Unstable:** experimental features (e.g., new contracts, ownership types, interop).

E2E datasets (`tests/run/`, `tests/correct_samples/`) should use only stable features.

## Benchmark harness

- Local benchmark runner: `scripts/benchmark.py` (optional)
- Usage guidance: see the wiki `Quality-and-Datasets.md`.
