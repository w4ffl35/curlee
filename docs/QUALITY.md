# Quality, Datasets, and Versioning

This document defines the quality harness, dataset versioning, and stable/unstable feature gates for Curlee.

## End-to-end tests

- `tests/run/` contains curated end-to-end programs.
- The E2E harness (`curlee_e2e_tests`) verifies and (for `tests/run`) executes programs.
- CI runs `ctest`, which includes the E2E harness.

## Correct samples dataset

- `tests/correct_samples/` contains verified programs generated deterministically.
- The generator is `scripts/generate_correct_samples.py`.
- The dataset is exported to `training_data.txt` for downstream RAG/training use.

## Deterministic generation

- The generator uses a fixed seed (default `1337`).
- The output is deterministic for the same seed and count.
- Regenerating with different parameters must update `training_data.txt` and the dataset.

## Versioning policy

- Dataset versions increment when:
  - The generator changes,
  - The language surface changes,
  - The verification scope changes.
- Each dataset export should annotate the seed and sample count in the header.

## Stable/unstable feature gates

- **Stable:** syntax and semantics covered by MVP verification scope.
- **Unstable:** experimental features (e.g., new contracts, ownership types, interop).
- E2E datasets must use only **stable** features.
- Unstable features must be flagged in docs and excluded from `tests/correct_samples/`.
