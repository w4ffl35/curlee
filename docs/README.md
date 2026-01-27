# Documentation

Curlee’s user-facing documentation has moved to the GitHub wiki:

- [wiki/](../wiki/)

This folder is intentionally kept minimal to avoid duplicated sources of truth.

## LSP UX spec (future)

Goal: make verification-first concepts usable in-editor without hiding failures.

- Call-site hints: on hover (or CodeLens) for a call expression, show the callee signature plus `requires` clauses and any parameter refinements (`where`) so the caller can see what must be true.
- Model surfacing: when verification fails, include a small model/counterexample in diagnostics when available (deterministic, minimal, user-facing variable names).
- Span accuracy: all hints and diagnostics must map to precise spans.
- Robustness (later): add lexer/parser fuzzing to stress error recovery; turn crashes into regression fixtures.

Canonical details live in the wiki: `LSP-UX-(Future).md`.

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
