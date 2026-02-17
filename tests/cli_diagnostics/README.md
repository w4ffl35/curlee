Golden tests for CLI diagnostics output.

These are intentionally separate from `tests/diagnostics/` (renderer unit tests).

- `missing_file.golden`: expected stderr for `curlee lex tests/fixtures/does_not_exist.cur`.
- `check_requires_divide.golden`: expected stderr for `curlee check tests/fixtures/check_requires_divide.curlee`.
- `check_refinement_implies.golden`: expected stderr for `curlee check tests/fixtures/check_refinement_implies.curlee` (empty on success).
- `check_unknown_name.golden`: expected stderr for `curlee check tests/fixtures/check_unknown_name.curlee`.
- `run_requires_divide.golden`: expected stderr for `curlee run tests/fixtures/check_requires_divide.curlee`.
- `run_success.stdout.golden`: expected stdout for `curlee run tests/fixtures/run_success.curlee`.
- `run_success.stderr.golden`: expected stderr for `curlee run tests/fixtures/run_success.curlee` (empty on success).
- Tests run with working directory set to the repo root so relative paths are stable.

## Ownership conventions

- Every `*.golden` / `*.expected` file under `tests/` must be owned by a test harness.
- Ownership is recognized by either:
	- explicit reference from a `tests/*.cpp` or `tests/*.ipp` file, or
	- the diagnostics convention (`tests/diagnostics/<name>.golden` paired with
		`tests/diagnostics/<name>.curlee`).
- Unowned assets are considered stale and should be removed or wired into a harness.

Check locally:

```bash
python3 scripts/check_orphan_goldens.py --format text
```
