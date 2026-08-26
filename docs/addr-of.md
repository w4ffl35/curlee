# `addr_of(arr)` — address-of a Curlee-owned fixed-size array (issue #286)

## Motivation

JOE OS's `kernel/virtio_net.c` (w4ffl35/joeos) must hand **physical addresses
of its own DMA buffers** to the virtio hardware:

```c
// virtio_net.c:375 — vring descriptor addr field
rx_desc[s].addr = (uint64_t)(unsigned long)rx_buf[s];

// virtio_net.c:334-357 — legacy virtqueue PFN register
// base = 4096-aligned address of the driver's own rx_qmem/tx_qmem region
outl(VIRTIO_PCI_QUEUE_PFN, base >> 12);
```

This is fundamentally different from the runtime-address phys builtins
(`phys_read_*` / `phys_write_*`, issues #279/#285), which dereference an
address the kernel does **not** necessarily own: `addr_of` asks Curlee for
the address **of memory the kernel already owns**, to hand to a device for
DMA. It is the single hardest remaining language gap in the C-to-Curlee
migration (w4ffl35/joeos#14); the other grandfathered drivers do not need it.

## The builtin

```curlee
let buf: [U8; 16] = [0; 16];
unsafe {
  let a: Int = addr_of(buf);   // physical address of buf's storage
}
```

- **Target:** a plain fixed-size array **binding** — a freestanding-local
  `let q: [T; N] = [v; N];` (issue #278) or a module-level `static q: [T; N]`
  (issue #287). Grouping parens around the name are accepted; an indexed
  element (`addr_of(q[0])`), a scalar, or any other expression is a type
  error. No pointer arithmetic / pointer types — this is exactly "the address
  of ONE array I already own".
- **Result type:** `Int`. `Int` is the natural address type: the runtime phys
  builtins accept `Int`/`U64` addresses, `>> 12` PFN arithmetic and the
  `(base + 4095) & ~4095` alignment round-up are `Int` arithmetic, and a
  vring descriptor's `uint64_t addr` field is reachable via the `Int -> U64`
  widening (issue #277) in a struct-literal field position.
- **Gating:** requires an `unsafe` block and `cap phys.mem`, mirroring
  `Phys<T>`, the port I/O builtins and the runtime phys builtins.
- **Verifier semantics:** trusted/opaque. `addr_of(arr)` lowers to a
  **per-array opaque `Int` constant** (created once per array binding per
  function). The same array binding therefore always yields the **same**
  address — `let a = addr_of(q); ...; a == addr_of(q)` is provable — but the
  numeric **value** is never assumed by the solver (an `ensures` claiming a
  specific address is unprovable, see `check_addr_of_opaque`). Unlike an MMIO
  read, `addr_of` is not an *opaque read*: two `addr_of(q)` calls cannot
  differ, so bindings from it are ordinary `Int` bindings with equality
  facts.

## Codegen

```c
// Curlee                              // emitted C
let a: Int = addr_of(buf);             int64_t a = (int64_t)(uintptr_t)(buf);
let desc: VringDesc = VringDesc{       curlee_VringDesc desc = ((curlee_VringDesc){
  addr: addr_of(buf),                    .addr = (int64_t)(uintptr_t)(buf) });
};
```

The C array name decays to a pointer to its first byte; the cast through
`uintptr_t` to `int64_t` yields the storage address as an `Int`. The kernel
boots with identity paging (crt0.S maps the first 2 MiB identity), so the
address is the **physical** address the device needs. `curlee run` rejects
`addr_of` (freestanding-only), like every other phys/port primitive.

## Alignment guarantee (acceptance criterion #4)

Investigation result: Curlee's codegen **cannot guarantee 4096-byte
alignment directly**, so the virtio ring setup uses the driver's existing
over-allocate-and-round-up trick, now expressible as ordinary `Int`
arithmetic on the `addr_of` value.

What Curlee actually guarantees — the alignment the C compiler gives a plain
C array declaration, measured, not assumed:

- **Locals** (`uint8_t q[16];` on the stack): the x86-64 ABI's stack/frame
  alignment (16 bytes in practice under our crt0.S, which sets `%rsp` to a
  16-aligned `__stack_top`); scalar element alignment below that
  (`U16` → 2, `U32` → 4, `U64`/`Int` → 8).
- **Statics** (`static uint8_t rx_buf[4096];` in `.bss`/`.data`): the C
  compiler's placement. GCC (the `--link` toolchain) puts byte arrays at
  ≥ 16 bytes and large byte arrays at 32 (measured: `.comm rx_buf,4096,32`);
  the linked address additionally depends on the section layout from
  `runtime/linker.ld` (base 0x100000). **Not** guaranteed to be 4096.
- C's `_Alignof(uint8_t[N])` is 1 — there is no size-forced-alignment
  shortcut for `[U8; 4096]`.

The 4096-aligned region the legacy virtqueue needs therefore comes from the
driver, exactly as in the real virtio_net.c:

```curlee
static rx_qmem: [U8; 8192] = [0; 8192];   // over-allocate one extra page

unsafe {
  let raw: Int = addr_of(rx_qmem);
  let base: Int = (raw + 4095) & 0xFFFFF000;  // round up to a page boundary
  let pfn: Int = base >> 12;                   // VIRTIO_PCI_QUEUE_PFN
  // ... base .. base+4096 is the page-aligned ring/descriptor region
}
```

The boot probe `tests/audit/joeos_13_addr_of_virtio_desc.curlee` verifies
end-to-end that the round-up lands on a page boundary, that `pfn * 4096 ==
base`, that the aligned region fits inside the over-allocated `rx_qmem`, and
reports the raw alignments observed (`OKABc` in the serial log: `A` = static
`% 16 == 0`, `B` = static happened to be page-aligned in that build, `C` =
local `% 16 == 0` — none of which is a language guarantee).

## Fixtures

- Type/verifier: `tests/fixtures/check_addr_of.curlee` (positive),
  `check_addr_of_scalar` / `check_addr_of_element` /
  `check_addr_of_outside_unsafe` / `check_addr_of_opaque` (negative) +
  goldens.
- Codegen: `tests/codegen/addr_of.curlee` + `.expected` + freestanding
  compile-smoke.
- Fuel: `verification_checker_tests.cpp` prices `addr_of` as a 1-instruction
  leaf.
- Boot: `tests/audit/joeos_12_addr_of_roundtrip.curlee` (array-index write /
  `phys_read_u8` / `phys_write_u8` round-trip + descriptor U64 field) and
  `joeos_13_addr_of_virtio_desc.curlee` (full virtio shape + alignment).
