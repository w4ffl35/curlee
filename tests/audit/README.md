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
- `joeos_11_phys_write_runtime.curlee` — runtime-address physical writes for
  fb.c's pixel-write half (issue #285): a write loop over a runtime base +
  mutable cursor, read back through `phys_read_u32` and confirmed over COM1.
- `joeos_12_addr_of_roundtrip.curlee` — address-of a Curlee-owned array
  (issue #286): an array-index write read back through the obtained address
  via `phys_read_u8`, a `phys_write_u8` through the address landing in the
  array, and the address widening into a vring-descriptor-shaped struct's U64
  field — the DMA-buffer-address half of virtio_net.c, confirmed over COM1.
- `joeos_13_addr_of_virtio_desc.curlee` — the full virtio_net.c pattern
  (issue #286): descriptor U64 field filled from `addr_of`, the legacy
  virtqueue PFN (`base >> 12`) with the over-allocate + round-up 4096
  alignment done as Int arithmetic on the addr_of value, and the raw
  alignment of static/local arrays measured and reported over COM1 (the
  language guarantees only element-type alignment; the driver must round up).
- Shadowing regression (issue #286 review round 1): `addr_of`'s opaque
  constant is keyed by the array BINDING, not its name, so a local shadowing
  a static (or nested shadowed locals) lowers to DISTINCT constants — the
  solver can no longer prove `addr_of(static q) == addr_of(local q)`. The
  false-contract fixtures (`tests/fixtures/check_addr_of_shadow_static.curlee`
  / `check_addr_of_shadow_local.curlee`) now FAIL `curlee check` with
  "ensures clause not satisfied"; the honest shadow pattern checks cleanly
  and the emitted C keeps each shadow in its own compound statement (see
  `tests/codegen/shadow_arrays.curlee`).
- `joeos_09_net_stack_globals.curlee` — the net_stack.c shim's shape (scalar
  phase/seq/port fields + a 256-byte response body, read/written from several
  functions across separate poll calls) in pure Curlee (issue #287).
- `joeos_10_vbe_globals.curlee` — the vbe_state.c shim's shape (a probe
  writing 4 scalar globals, read later by a separate function) in pure Curlee
  (issue #287).
- `joeos_14_build_sized_static.curlee` — per-build-target static array sizing
  (issue #296): the SAME source declares a static array sized by `--define`
  build constants (`[U32; ASSET_W * ASSET_H]`), a define-initialized build
  discriminator static, and the addr_of-based base getter — the fb.c
  `#ifndef JOE_PVH_BOOT` pattern. Built twice (`--define ASSET_W=4
  --define ASSET_H=4 --define JOE_PVH_BOOT=0` vs `=1`), the emitted C declares
  `static uint32_t asset_region[16]` vs `[1]` (the linked ELF's asset_region
  symbol is 64 B vs 4 B; the .bss SECTION size is dominated by the runtime's
  fixed buffers, so the large-array case — the joeos 2.4 MB frame ring — is
  what moves the section size), and the serial log reports the folded size +
  build discriminator ('OK16G' vs 'OK01P'). The negative-case fixture is the
  same file WITHOUT defines: `[T; ASSET_W * ASSET_H]` fails to parse with
  "array length must be an integer literal or a build-time constant" (an
  undefined define name is not a constant).
