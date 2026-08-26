# tests/audit — JOE OS port-readiness language probes

Standalone `.curlee` probes written for the joeos port-readiness audit
(see `plans/joeos-port-readiness.md` for the full report). Each probe exercises
**one** language pattern that a joeos driver migration depends on, and is
compiled by hand with:

```bash
./build/linux-debug/curlee build --link -o <name>.elf tests/audit/<name>.curlee
```

These files are **not** part of the ctest/golden harness — they are audit
fixtures, compiled and booted manually during the audit. Naming:

- `joeos_<nn>_<topic>.curlee` — the probe for audit item `<nn>`.
- `joeos_<nn>_<topic>_fail.curlee` — probes that are **expected to fail**
  (a remaining language gap); the exact compiler error is captured in the
  report, not in a golden file.

Boot checks (when qemu is available) follow `scripts/freestanding_ci.sh`:
the generated C is linked against `runtime/rt.c` + `runtime/crt0.S` +
`tests/freestanding/debug_exit_driver.c`, and a kernel that reaches
`curlee_halt` makes qemu exit with status 3 (`-device isa-debug-exit`).
