Golden tests for CLI diagnostics output.

These are intentionally separate from `tests/diagnostics/` (renderer unit tests).

- `missing_file.golden`: expected stderr for `curlee lex tests/fixtures/does_not_exist.cur`.
- `check_requires_divide.golden`: expected stderr for `curlee check tests/fixtures/check_requires_divide.curlee`.
- `check_refinement_implies.golden`: expected stderr for `curlee check tests/fixtures/check_refinement_implies.curlee` (empty on success).
- `check_unknown_name.golden`: expected stderr for `curlee check tests/fixtures/check_unknown_name.curlee`.
- `run_requires_divide.golden`: expected stderr for `curlee run tests/fixtures/check_requires_divide.curlee`.
- `run_success.stdout.golden`: expected stdout for `curlee run tests/fixtures/run_success.curlee`.
- `run_success.stderr.golden`: expected stderr for `curlee run tests/fixtures/run_success.curlee` (empty on success).
- Fixed-size arrays (issue #278): `check_array_ring.golden` (empty on success) covers
  acceptance #1-#4; `check_array_oob_*.golden` cover constant OOB / negative / unproven
  symbolic index rejections; `check_array_{len,elem}_mismatch.golden` and
  `check_array_reject_*.golden` cover the MVP rejection diagnostics (multi-dim, bare-name,
  struct field, param, return, ghost, unsupported element kind); `run_array_vm_reject.golden`
  asserts `curlee run` rejects arrays (freestanding-only).
- Address-of a Curlee-owned array (issue #286): `check_addr_of.golden` (empty on success)
  covers the virtio-shaped descriptor-fill pattern (stable opaque addresses, Int -> U64
  widening into a descriptor field, a phys_read_u8 round-trip, the `base >> 12` PFN setup);
  `check_addr_of_{scalar,outside_unsafe,element,opaque}.golden` cover the type/unsafe/
  unprovable-address rejections. Shadowing regressions (review round 1): the addr_of
  constant is keyed by BINDING, so `check_addr_of_shadow_static.golden` and
  `check_addr_of_shadow_local.golden` assert `ensures result == 0` over two same-named
  arrays' address difference FAILS check (previously verified — unsound), while
  `check_addr_of_shadow_scoped.golden` (empty on success) asserts shadowing does not leak
  across scopes and `check_addr_of_shadow_honest.golden` (empty on success) asserts the
  honest shadowing pattern checks cleanly.
- Runtime-address physical memory reads (issue #279): `check_phys_read_runtime.golden`
  (empty on success) covers the mb2.c multiboot2 tag walk (runtime base + mutable byte
  cursor, opaque reads); `check_phys_read_runtime_addr.golden` covers the non-integer
  address rejection; `check_phys_read_runtime_contract.golden` covers the opaque-read
  contract rejection (never silently proven).
- Module-level mutable state (issue #287): `check_static_state.golden` (empty on success)
  covers acceptance #1-#4 (scalar + `[T; N]` array statics read/reassigned from multiple
  functions, the net_stack.c and vbe_state.c shim shapes); `run_global_state.stdout.golden`
  asserts VM cross-call persistence (result 1223); `check_static_contract_global.golden`,
  `check_static_requires_global.golden`, and `check_static_invariant_global.golden` cover
  the opaque-module-state contract rejections (ensures/requires/invariant); the remaining
  `check_static_*.golden` cover the front-end MVP diagnostics (type mismatch, non-literal
  initializer, duplicate / function-conflict names, unsupported type, array length
  mismatch, non-repeat array initializer, the unsigned-scalar `let`-mirror rule, and
  constant out-of-bounds on a global array); `run_static_array_vm_reject.golden` asserts
  `curlee run` rejects module-level arrays (freestanding-only, like local arrays).
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
