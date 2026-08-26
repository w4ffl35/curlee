# JOE OS port-readiness audit — Curlee language features

**Status:** Complete (audit only — no compiler/verifier changes, no joeos changes)
**Date:** 2026-08-26
**Audited commit:** [`a836376`](README.md) (merge of PR #275 / [`c4a39c5`](tests/codegen/unsigned_widen.curlee:1), issue #274), build `./build/linux-debug/curlee` rebuilt via `cmake --build --preset linux-debug`
**Scope:** the 8 audit items below, each exercised by a standalone probe in [`tests/audit/`](tests/audit/README.md), compiled with `./build/linux-debug/curlee build --link`, and (where feasible) booted under `qemu-system-x86_64 -kernel` (qemu **is** available — no item was skipped for lack of it).

---

## Summary

| # | Pattern (joeos driver) | Result | Blocking issue(s) |
|---|------------------------|--------|-------------------|
| 1 | Mutable loop-carried state (fb.c, net_stack.c, virtio_net.c) | **PASS** | — |
| 2 | Port I/O read-then-branch, constant port (putc_driver.c) | **PASS** | — |
| 3 | Port I/O 16-bit readback, constant port (vbe.c) | **PASS** | — |
| 4 | Port I/O, runtime-computed port (virtio_net.c `io_base + off`) | **FAIL** | [#276](https://github.com/w4ffl35/curlee/issues/276) |
| 5 | Bitwise pack/unpack + PCI addr assembly (net_stack.c, virtio_net.c) | **PASS** (with caveats) | — |
| 6 | U64 from pointer-sized value (virtio_net.c vring `addr`) | **FAIL** | [#277](https://github.com/w4ffl35/curlee/issues/277) |
| 7 | Fixed-size mutable array ring buffer (fb.c tool_queue, virtio_net.c rx_buf_state) | **FAIL** | [#278](https://github.com/w4ffl35/curlee/issues/278) |
| 8 | MMIO `Phys<T>` write loop + packed-struct read (fb.c, mb2.c) | **PASS** (write loop), **PARTIAL** (packed struct) | — |

**New issues filed in w4ffl35/curlee:**
- [#276](https://github.com/w4ffl35/curlee/issues/276) — port I/O to runtime-computed port addresses (let-bound base + constant offset)
- [#277](https://github.com/w4ffl35/curlee/issues/277) — construct U64 from Int/literal (widening + literal adaptation)
- [#278](https://github.com/w4ffl35/curlee/issues/278) — fixed-size mutable arrays (`[T; N]` + indexed access)

`kernel/libgcc32.c` remains **out of scope forever** per the joeos C-boundary policy (GCC ABI shim).

---

## 1. Mutable loop-carried state — **PASS**

Probe: [`tests/audit/joeos_01_mutable_loop_state.curlee`](tests/audit/joeos_01_mutable_loop_state.curlee:1)

Mirrors fb.c's frame/ring counters and the ring-index bookkeeping: a bounded
`while` reassigning a counter and an accumulator each iteration, plus a ring
slot advancing by modulo. Issue #268 (assignment) is required for the
`decreases` variant to be provable — and it is.

Working pattern (copy-paste for a follow-up port):

```curlee
let frame: Int = 0;
let slot: Int = 0;
let n: Int = 4;
while (frame < n)
  [ invariant 0 <= frame && frame <= n && 0 <= slot && slot < 4;
    decreases n - frame; ]
{
  frame = frame + 1;
  slot = (slot + 1) % 4;   // fb.c: ring_slot = (ring_slot + 1) % FRAME_RING_SLOTS
}
```

**Build:** PASS. **Verification:** PASS (`sum_to` `ensures result >= 0`, loop
invariants + variants all discharged). **Boot:** PASS — qemu `-kernel` exits
status 3 (kernel reached `curlee_halt`). Emitted C is plain assignments:

```c
while ((frame < n)) { frame = (frame + 1); slot = (((slot + 1)) % 4); }
```

## 2. Port I/O read-then-branch on a constant port — **PASS**

Probe: [`tests/audit/joeos_02_port_constant_busywait.curlee`](tests/audit/joeos_02_port_constant_busywait.curlee:1)

Mirrors putc_driver.c's COM1 busy-wait `while ((inb(0x3FD) & 0x20) == 0)`.
The port read is opaque to the verifier (issues #269/#274) and the busy-wait
loop (no contract block) verifies via the legacy single-pass mode.

Working pattern:

```curlee
let lsr: U8 = port_inb(0x3FD);        // COM1 LSR (opaque read)
while ((lsr & 32) == 0) {             // LSR bit 5 = THR empty
  // busy-wait
}
port_outb(0x3F8, lsr);                // COM1 THR
```

**Build:** PASS. **Boot:** PASS (qemu rc=3). Emitted C is the exact driver
shape: `while ((((lsr & 32)) == 0)) { }`. putc_driver.c's `pause` intrinsic
has no Curlee equivalent, but the emulated UART drains fast and the plain loop
is the portable equivalent — **putc_driver.c is fully portable today.**

## 3. Port I/O 16-bit readback validation on a constant port — **PASS**

Probe: [`tests/audit/joeos_03_port_16bit_readback.curlee`](tests/audit/joeos_03_port_16bit_readback.curlee:1)

Mirrors vbe.c: `outw(0x1CE, idx)` / `inw(0x1CF)`, then
`if (xres != 640 || yres != 480 || bpp != 32) return 0;` — U16 compares
directly against Int literals (issue #274).

**Build:** PASS. **Boot:** PASS (qemu rc=3). Emitted C:
`if ((((xres != 640) || (yres != 480)) || (bpp != 32))) { return 0; }`.

> **Note (secondary finding):** hex literals (`0xFFF0`, `0xB0C0`) only parse
> in `phys()` / port / `write()`-value positions. In general expressions
> (comparisons, `let` initializers, struct fields, call args, contract bounds)
> they must be written in decimal (`0xFFF0` → `65520`, `0xB0C0` → `45248`).
> This is a real ergonomics gap for porting (joeos is full of hex masks), but
> not a hard block. **vbe.c is portable today** (with decimal masks).

## 4. Port I/O to a runtime-computed port — **FAIL** → issue [#276](https://github.com/w4ffl35/curlee/issues/276)

Probes: [`tests/audit/joeos_04_port_runtime_computed_fail.curlee`](tests/audit/joeos_04_port_runtime_computed_fail.curlee:1),
[`tests/audit/joeos_04b_port_let_bound_fail.curlee`](tests/audit/joeos_04b_port_let_bound_fail.curlee:1)

The README's "constant ports only" caveat is a **real, unclosed gap** for
virtio_net.c, which computes its ports at runtime (`io_base + QUEUE_SEL`, etc.,
with `io_base` from a PCI BAR probe). The audit confirmed precisely what
"constant" means: **a single literal token only** — enforced at parse time
(`parse_port_io_call` requires `IntLiteral`/`PhysAddrLiteral`), then re-checked
by the type checker and verifier via lexeme inspection.

Exact errors:

```
error: port_outw() expects a constant integer port
error: port_inw() expects a constant integer port
error: port_inb() expects a constant integer port
```

A `let`-bound value that is never reassigned does **not** count as constant
(probe 4b fails identically). So virtio_net.c's legacy-virtio register access
cannot be expressed in Curlee today. **Issue [#276](https://github.com/w4ffl35/curlee/issues/276)**
files the MVP scope: accept a let-bound base + constant offset, lower the
runtime port as an opaque extra argument to the existing uninterpreted read
functions, and emit the DX-register asm form (the existing `Nd` constraint
already falls back to DX for non-immediates).

## 5. Bitwise pack/unpack + PCI config address assembly — **PASS** (with caveats)

Probe: [`tests/audit/joeos_05_bitwise_pack_pci.curlee`](tests/audit/joeos_05_bitwise_pack_pci.curlee:1)

Issue #270's operators all work: `>>`, `&`, `<<`, `|`, `%`. The byte
split/assemble helpers mirror net_stack.c's `ns_wr16`/`ns_wr32`/`ns_rd16`/
`ns_rd32`, and the PCI address assembly mirrors virtio_net.c's
`pci_config_read32` (`0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | (off & 0xFC)`).

Working pattern (the PCI assembly, with a verbatim-mirror contract):

```curlee
fn pci_config_addr(bus: Int, dev: Int, func: Int, off: Int) -> Int
  [ requires 0 <= bus && bus <= 255 && 0 <= dev && dev <= 31 &&
            0 <= func && func <= 7 && 0 <= off && off <= 4095;
    ensures result == (2147483648 | (bus << 16) | (dev << 11) |
                       (func << 8) | (off & 252)); ]
{
  return 2147483648 | (bus << 16) | (dev << 11) | (func << 8) | (off & 252);
}
```

**Build:** PASS. **Verification:** PASS (PCI contract + round-trip identities
discharged; the verifier uses a 64-bit bit-vector model). **Boot:** PASS
(qemu rc=3).

Three caveats (all documented in the fixture header; none blocks the port but
they shape how a porting task must write the code):

1. **User calls are not lowerable in verification predicates.** A
   contract-bearing function's `return` must be a single call-free expression
   — `return be_rd32(be_byte0(v), ...)` fails with
   `calls are not supported in verification expressions`. Helpers must be
   inlined into the contract body, or called via `let` bindings in a
   contract-free body (nested calls as arguments also fail).
2. **Symbolic bitwise identity proofs only terminate over bounded ranges.**
   The full symbolic 32-bit round-trip proof (`0 <= v <= 0xFFFFFFFF`) does not
   terminate in the mixed int/bv model (observed >2 min, killed; the 16-bit
   bound alone >30 s). An 8-bit symbolic bound proves instantly, and a
   **concrete** 32-bit value pinned by `requires v == <literal>` also proves
   instantly. net_stack.c's checksum/packing logic should prove per concrete
   value or per bounded range.
3. Hex constants must be decimal in general expressions (see item 3 note).

## 6. U64 construction from a pointer-sized value — **FAIL** → issue [#277](https://github.com/w4ffl35/curlee/issues/277)

Probes:
[`tests/audit/joeos_06a_u64_from_int_fail.curlee`](tests/audit/joeos_06a_u64_from_int_fail.curlee:1)
(variable), [`tests/audit/joeos_06b_u64_literal_fail.curlee`](tests/audit/joeos_06b_u64_literal_fail.curlee:1)
(literal), [`tests/audit/joeos_06c_u64_phys_ok.curlee`](tests/audit/joeos_06c_u64_phys_ok.curlee:1)
(control).

The README's "U64 widening is not yet supported" is confirmed — and the gap is
wider than widening: **a U64 cannot be constructed from an Int OR from a
literal at all.** Exact errors:

```
error: type mismatch in let: expected U64, got Int     // let addr: U64 = base;
error: type mismatch in let: expected U64, got Int     // let addr: U64 = 1048576;
```

What *does* work (probe 6c): `U64` exists as a core type with a storable
`uint64_t` codegen mapping; a `Phys<U64>` read produces a U64; `U64 -> U64`
bindings and `U64 == U64` comparisons are legal. The **only** construction
path is a `Phys<U64>` read. Struct fields are affected too: `struct { addr:
U64; }` cannot be initialized from an Int literal (`field 'addr' type
mismatch: expected U64, got Int`).

This blocks virtio_net.c's vring descriptor fill
`rx_desc[s].addr = (uint64_t)(unsigned long)rx_buf[s];` (virtio_net.c:375,
also :671). **Issue [#277](https://github.com/w4ffl35/curlee/issues/277)** files
the MVP scope: value-preserving `Int -> U64` widening (addresses < 2^32, with
a range check), plus U64 literal adaptation in `let`/struct fields.

Probe 6c boots (qemu rc=3) once the halt is unconditional. Boot note: halting
behind an `if (y == x)` branch does **not** fire under QEMU's PVH loader at
`-O0` — reproduced with a hand-written C kernel of the identical shape, both
branch polarities; an unconditional halt works. This is a QEMU/TCG -O0
environment quirk, **not** a Curlee issue (the same comparison verifies in the
repo's own fixture `tests/fixtures/check_unsigned_inspect.curlee` and codegens
to `if ((y == x))`).

## 7. Fixed-size mutable array ring buffer — **FAIL** → issue [#278](https://github.com/w4ffl35/curlee/issues/278)

Probes:
[`tests/audit/joeos_07a_array_ring_fail.curlee`](tests/audit/joeos_07a_array_ring_fail.curlee:1)
(full ring), [`tests/audit/joeos_07b_ring_bookkeeping.curlee`](tests/audit/joeos_07b_ring_bookkeeping.curlee:1)
(bookkeeping half).

Curlee has **no array type** — the MVP grammar (wiki Language-Syntax §6) has
no array form; `Vec` is hosted-only and rejected on the freestanding target.
Exact errors on `let q: [Int; 8] = [0; 8];`:

```
error: expected type name
error: expected ';' after expression
```

The **bookkeeping half is fully expressible** (probe 7b PASS — compiles,
verifies, boots qemu rc=3): head/count/idx arithmetic with modulo wraparound,
exactly the fb.c `fb_tool_enqueue`/`fb_run_loop` cursor math and virtio_net.c
`rx_buf_state` index logic:

```curlee
head = (head + 1) % slots;                 // producer (fb.c tool_head)
let idx: Int = (head + slots - count) % slots;  // oldest live slot (fb.c)
count = count - 1;                         // consumer drain
```

What's missing is the **storage** array (`tool_queue[]`, `rx_buf_state[]`,
`rx_desc[]`, `rx_buf[][]`). The joeos C-boundary policy lets a "raw ring/state
owner" keep its static arrays in C, but the audit item as stated FAILs on the
array itself. **Issue [#278](https://github.com/w4ffl35/curlee/issues/278)**
files the MVP scope: a fixed-size array type for freestanding locals
(`let q: [T; N] = ...;`, indexed read/write, verifier index-bounds
obligations, C-array codegen).

## 8. MMIO `Phys<T>` write loop + packed-struct read — **PASS** (write loop), **PARTIAL** (packed struct)

### 8a. Phys write loop with a mutable index — **PASS**

Probe: [`tests/audit/joeos_08a_phys_write_loop.curlee`](tests/audit/joeos_08a_phys_write_loop.curlee:1)

Mirrors fb.c's `fb_clear`/`fb_fill_rect` inner loops: a bounded loop writing a
run of pixels to a `Phys<U32>` framebuffer address with a loop-carried index
(#268) — MMIO composes cleanly with assignment. Requires a
`[ requires pixels >= 0; ]` precondition for the variant to hold.

**Build:** PASS. **Boot:** PASS (qemu rc=3). Emitted C:

```c
while ((i < pixels)) {
    (*(volatile uint32_t*)(uintptr_t)(0xFD000000)) = (0xFF8800);
    i = (i + 1);
}
```

A bonus probe confirmed that the PVH boot path can actually read the low RAM
IVT (0x0), BDA (0x4000), the VGA text buffer (0xB8000), the kernel image
(0x100000), and the QEMU stdvga LFB (0xFD000000) — so these addresses are
valid under `-kernel`.

### 8b. Packed multi-field struct read (mb2.c's `mb2_fb_tag`) — **no packed-struct feature; byte-at-a-time is the pattern**

Findings (probes [`tests/audit/joeos_08b_struct_layout_probe.curlee`](tests/audit/joeos_08b_struct_layout_probe.curlee:1),
[`tests/audit/joeos_08b_layout_only.curlee`](tests/audit/joeos_08b_layout_only.curlee:1),
[`tests/audit/joeos_08b_layout_unsigned.curlee`](tests/audit/joeos_08b_layout_unsigned.curlee:1),
[`tests/audit/joeos_08b2_struct_phys_read_fail.curlee`](tests/audit/joeos_08b2_struct_phys_read_fail.curlee:1),
[`tests/audit/joeos_08c_tag_read_bytes.curlee`](tests/audit/joeos_08c_tag_read_bytes.curlee:1)):

1. **There is no packed-struct/layout feature.** The MVP grammar has no
   packing attribute, and codegen emits plain C structs with **natural
   alignment** — confirmed on both an Int-field struct (`int64_t` members, 8
   bytes each) and a U32/U8 struct (`uint32_t a; uint32_t b; uint8_t c;
   uint8_t d;` → offsets 0/4/8/9, padding to 12), which is NOT the mb2_fb_tag
   packed layout (u64 at +8, u32s at +16/20/24, u8s at +28/29).
2. **A struct cannot be read directly from a raw physical address.**
   `Phys<T>` element kinds are restricted to U8/U16/U32/U64:
   `error: unsupported Phys element kind 'Mb2FbTag' (expected U8, U16, U32 or U64)`.
3. **Struct literals do not adapt Int literals to unsigned field types**
   (unlike `Phys.write`/`port_out*`): `field 'pitch' type mismatch: expected
   U32, got Int`. Unsigned struct fields can only be initialized from
   unsigned-typed sources (port/Phys reads), and a `U64` field cannot be
   initialized at all (item 6). This is an additional constraint on writing
   driver structs in Curlee today.

The expected pattern is therefore **byte-at-a-time reads via per-field
`Phys` bindings**, exactly the way net_stack.c already reads with
`net_rx_byte(i)` + `ns_rd32_byte()`. Probe 8c implements the mb2_fb_tag read
this way — **PASS** (compiles, verifies, boots qemu rc=3):

```curlee
let type_lo: Phys<U32> = phys<U32>(0x1_0000);   // tag type
let bpp: Phys<U8>      = phys<U8>(0x1_001C);    // u8 bpp (fixed offset)
...
if (type_lo.read() != 8) { return 0; }           // tag type == 8
if (bpp.read() != 32) { return 0; }              // mb2.c gate
let ah: U32 = addr_hi.read(); let al: U32 = addr_lo.read();
if (ah == 0 && al == 0) { return 0; }            // fb_addr != 0 (u64 as hi/lo)
```

**Sufficiency verdict:** the byte-at-a-time workaround is sufficient and
boot-proven for a **fixed** base address (the addresses must be compile-time
literals). It is **awkward** for mb2.c, whose base (`mb2_info_addr`) is a
runtime pointer: Curlee has no pointer arithmetic / dynamic-address `Phys`, so
mb2.c's tag walk (`p += (size + 7) & ~7U`) cannot be expressed even
byte-at-a-time without either a runtime-address `Phys` or a packed-struct +
runtime-base feature. That combination of gaps (no packed layout, no
runtime-address `Phys`, no arrays for the walk) keeps mb2.c in C for now; the
missing pieces are substantial enough that a packed-struct/layout feature is
**worth a dedicated feature request** beyond the three issues filed here (the
byte-at-a-time pattern covers vbe.c's fixed-base reads and net_stack.c's
fixed-buffer reads; it does not cover mb2.c's runtime walk).

---

## Secondary findings (not blocking, but shape the port)

- **Hex literals are positional.** `0x…` parses only in `phys()` / port /
  `write()`-value positions; every other expression position (let initializers,
  struct fields, call args, contract bounds, general masks) requires decimal.
  Affects all six drivers (they are written in hex). Candidate follow-up issue.
- **User calls cannot appear in verification predicates.** A contract-bearing
  function's body must be call-free (inline the expression) or use `let`
  bindings in a contract-free body; nested calls as arguments also fail. This
  constrains how net_stack.c's packing helpers get contracts.
- **Bitwise symbolic proofs scale poorly.** Full-width symbolic identities
  (16/32-bit) do not terminate in the mixed int/bv model; bounded (≤8-bit)
  or concrete-pinned proofs do. Candidate follow-up issue for checksum
  verification.
- **QEMU PVH -O0 branch quirk** (environment, not Curlee): a `curlee_halt()`
  behind a copy-compare `if (y == x)` does not fire under `qemu -kernel` at
  `-O0`; reproduced in a hand-written C kernel, both branch polarities. Audit
  boot probes that need observable halts put the halt unconditionally.

## Repository layout added by this audit

- [`tests/audit/README.md`](tests/audit/README.md) — fixture index + build/boot instructions
- [`tests/audit/joeos_0{1,2,3,4,4b,5,6a,6b,6c,7a,7b,8a,8b,8b2,8b_layout_only,8b_layout_unsigned,8c}_*.curlee`](tests/audit/joeos_01_mutable_loop_state.curlee:1) — the probes
- [`scripts/audit_qemu_boot.sh`](scripts/audit_qemu_boot.sh:1) — qemu boot helper (mirrors `scripts/freestanding_ci.sh` step 5)
- [`plans/joeos-port-readiness.md`](plans/joeos-port-readiness.md:1) — this report

The probes are standalone audit fixtures (not wired into ctest/goldens);
`build/` artifacts and the `.headlesscode/` scratch probes are gitignored.

## Migration-readiness per driver

| joeos driver | Ready? | Remaining blockers |
|---|---|---|
| `putc_driver.c` | **Ready** — constant-port busy-wait fully expressible (item 2) | none |
| `vga_setup.c` | **Ready** — constant-port `outb` sequences (issue #269) | none |
| `vbe.c` | **Ready** — 16-bit readback + masks (items 3, 5; decimal masks) | none |
| `net_stack.c` (protocol logic) | **Ready with caveats** — bitwise + assignment + byte helpers (items 1, 5, 8c) | symbolic-proof scaling for checksums; no arrays for the TX/RX byte buffers (keep in C per policy) |
| `fb.c` (blitter/ring logic) | **Mostly** — loops, counters, ring arithmetic (items 1, 7b, 8a) | `tool_queue[]` storage array → [#278](https://github.com/w4ffl35/curlee/issues/278) |
| `virtio_net.c` (legacy-virtio) | **Blocked** | runtime ports → [#276](https://github.com/w4ffl35/curlee/issues/276); U64 descriptor addr → [#277](https://github.com/w4ffl35/curlee/issues/277); ring arrays → [#278](https://github.com/w4ffl35/curlee/issues/278) |
| `mb2.c` (tag walk) | **Blocked** | runtime-address `Phys` / packed-struct + no arrays (item 8b) |

The three issues [#276](https://github.com/w4ffl35/curlee/issues/276),
[#277](https://github.com/w4ffl35/curlee/issues/277), and
[#278](https://github.com/w4ffl35/curlee/issues/278) are the remaining hard
language gaps between the current closed issues and a full joeos driver
migration. The actual driver port is a separate follow-up task, to be
undertaken once these land.
