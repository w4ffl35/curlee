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

## Item index

- `joeos_01_mutable_loop_state.curlee` — loop-carried mutation (issue #268).
- `joeos_02_port_constant_busywait.curlee` — constant-port I/O (issue #269).
- `joeos_03_port_16bit_readback.curlee` — 16-bit port readback (issue #269).
- `joeos_04_port_runtime_computed.curlee` / `joeos_04b_port_let_bound.curlee`
  — let-bound/runtime-computed port I/O (issue #276).
- `joeos_05_bitwise_pack_pci.curlee` — bitwise packing for PCI config (issue #270).
- `joeos_06a_u64_from_int_fail.curlee` / `joeos_06b_u64_literal_fail.curlee` /
  `joeos_06c_u64_phys_ok.curlee` — U64 construction (issue #277).
- `joeos_07a_array_ring.curlee` / `joeos_07b_ring_bookkeeping.curlee` —
  fixed-size arrays + ring bookkeeping (issue #278).
- `joeos_08a_phys_write_loop.curlee` / `joeos_08b*` / `joeos_08c` / `joeos_08d`
  — runtime-address physical reads for the mb2.c tag walk (issue #279).
- `joeos_09_net_stack_globals.curlee` — the net_stack.c shim's shape (scalar
  phase/seq/port fields + a 256-byte response body, read/written from several
  functions across separate poll calls) in pure Curlee (issue #287).
- `joeos_10_vbe_globals.curlee` — the vbe_state.c shim's shape (a probe
  writing 4 scalar globals, read later by a separate function) in pure Curlee
  (issue #287).
