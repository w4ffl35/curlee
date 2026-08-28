# Title: x86 port I/O builtins (`port_inb/outb/inw/outw/inl/outl`) for freestanding drivers

Issue: https://github.com/w4ffl35/curlee/issues/269

## Problem statement / motivation

Curlee's freestanding target has **MMIO only** (`Phys<T>` + `read()`/`write()` under `unsafe`
with `cap phys.mem`). There is no x86 **port I/O** — `outb`/`inb`/`outw`/`inw`/`outl`/`inl` —
which is a different instruction space (the `in`/`out` CPU instructions, not memory-mapped).

Every x86 device driver that uses legacy ports cannot be written in Curlee. In the JOE OS kernel
(w4ffl35/joeos), four drivers are stuck in C purely for port I/O:

- **`putc_driver.c`** — COM1 serial (ports 0x3F8/0x3FB/0x3FD): `outb`/`inb` busy-wait loop.
  43 lines, would be ~15 in Curlee.
- **`vga_setup.c`** — VGA text-mode-3 programming (ports 0x3C2/0x3C4/0x3D4): 24 sequential
  `outb` calls. 73 lines.
- **`vbe.c`** — Bochs VBE probe (ports 0x1CE/0x1CF): `outw`/`inw` register access. 165 lines.
- **`virtio_net.c`** — PCI config space (ports 0xCF8/0xCFC) + legacy virtio I/O BAR: `outl`/`inl`.
  798 lines.

These are the smallest, most self-contained drivers — they'd be the first to migrate once port I/O
exists.

## Design

### 1. Builtin surface

Add six freestanding-only builtins to the resolver's single source of truth
[`is_builtin_call_name`](src/resolver/resolver.cpp:737):

```curlee
unsafe {
  let v: U8 = port_inb(0x3FD);   // read one byte from port
  port_outb(0x3F8, 0x48);        // write one byte to port
  let w: U16 = port_inw(0x1CE);  // read one word
  port_outw(0x1CE, 0x0001);      // write one word
  let l: U32 = port_inl(0xCFC);  // read one dword
  port_outl(0xCFC, 0x12345678);  // write one dword
}
```

Signatures (fixed arity, checked via the existing `check_builtin_arity` path in
[`type_check.cpp`](src/types/type_check.cpp:1070)):

| Builtin | Args | Returns |
|---|---|---|
| `port_inb` | `(port: Int)` | `U8` |
| `port_outb` | `(port: Int, value: U8)` | `Unit` |
| `port_inw` | `(port: Int)` | `U16` |
| `port_outw` | `(port: Int, value: U16)` | `Unit` |
| `port_inl` | `(port: Int)` | `U32` |
| `port_outl` | `(port: Int, value: U32)` | `Unit` |

- The port address is an `Int` expression. **MVP: constant ports only** — the type checker enforces
  that the port argument is a literal (`IntLiteral`/`PhysAddrLiteral`), mirroring the
  `PhysExpr` constant-address rule; a computed port is a hard error.
- Write values accept an `Int` literal for unsigned kinds, matching the existing `Phys.write()`
  rule in [`type_check.cpp`](src/types/type_check.cpp:1633) (`fb.write(0xFF8800)` on `Phys<U32>`).

### 2. Gating: `unsafe` + capability (mirror `Phys<T>`)

Port I/O is gated identically to `Phys<T>` access
([`type_check.cpp`](src/types/type_check.cpp:1566)):

- Outside `unsafe` (`unsafe_depth_ == 0`) → hard error
  `"port I/O requires an unsafe block"` (mirror the Phys message wording).
- Requires a capability via `require_capability(...)` ([`type_check.cpp`](src/types/type_check.cpp:320)).
  **Capability decision: reuse `cap phys.mem`.** Rationale: the port space is a second address space
  governed by the same driver-owner capability in the JOE OS kernel; a separate `cap io.port` adds
  an orthogonal surface with no MVP consumer. The `TypeInfo.required_capabilities` list gains
  `phys.mem` for port-I/O uses, so a `curlee build` invocation without the capability fails closed
  exactly like MMIO. (If a future hosted or userspace consumer needs to distinguish, a new
  `cap io.port` can be introduced without breaking this issue's surface.)
- Defense-in-depth in the verifier: the same "port address must be a constant literal" check is
  re-validated in the verifier, mirroring the Phys non-literal guard
  ([`checker.cpp`](src/verification/checker.cpp:1634)).

### 3. Verifier semantics: trusted/opaque (mirror `Phys<T>` reads)

Port reads/writes lower to **uninterpreted functions with no axioms**, exactly like the Phys
machinery ([`checker.cpp`](src/verification/checker.cpp:568)):

- `port_inb_k : Int -> Int`, ... — one uninterpreted function per (op, element kind) over the
  constant port address. No axioms: the solver cannot derive read values, compare two reads, or
  reason about written values afterward.
- Ports have no address-range fault model in the verifier (no OOB obligation), and a contract
  cannot see through a port read: a read result is **opaque** (transitively, via
  `expr_is_opaque_read` / `opaque_read_vars_`, [`checker.cpp`](src/verification/checker.cpp:737)).
  A `requires`/`ensures`/refinement mentioning a `port_in*` result produces the same
  "cannot prove opaque value" diagnostic as MMIO reads (the reject path in
  [`checker.cpp`](src/verification/checker.cpp:1687)).
- **Cost model** ([`cost_model.cpp`](src/verification/cost_model.cpp:372)): each port op costs
  `1 + cost(port)` (a single inline-asm statement), mirroring the Phys read/write cost entries.

### 4. Codegen (freestanding target): inline asm for constant ports

In [`codegen.cpp`](src/codegen/codegen.cpp:1542) (the builtin call handling that currently rejects
hosted builtins on freestanding), add a case for the six port builtins:

- **Constant port** (the only form the front-end allows): emit `asm volatile` for the `in`/`out`
  instructions with the port embedded as an immediate. Examples:
  ```c
  uint8_t v;
  __asm__ volatile ("inb %w1, %0" : "=a"(v) : "Nd"((uint16_t)0x3FD));
  __asm__ volatile ("outb %0, %w1" : : "a"((uint8_t)0x48), "Nd"((uint16_t)0x3F8));
  ```
  `inw`/`outw` use `%w` + `uint16_t`; `inl`/`outl` use `%k` + `uint32_t`.
- Prefer the compiler builtin when available (`__builtin_ia32_inb`/`outb`/`inw`/`outw`/`inl`/`outl`
  under `-march` coverage) but fall back to inline asm so the target stays `-ffreestanding
  -fno-builtin -nostdlib` compatible; the emitted asm must not require libgcc or host headers.
- The emitted C is consumed by the existing freestanding pipeline (gcc `-ffreestanding` smoke in
  [`scripts/freestanding_ci.sh`](scripts/freestanding_ci.sh)).

## MVP scope

In-scope:

- Six builtins, constant-port only, gated by `unsafe` + `cap phys.mem`.
- Verifier: uninterpreted/opaque lowering, no OOB obligation, opaque-read contract rejection.
- Codegen: inline asm emission for the constant-port case.
- Freestanding fixture mirroring `putc_driver.c` (COM1 `outb` + LSR `inb` busy-wait).

Out-of-scope (explicit):

- Variable/dynamic port addresses (all JOE OS drivers use constants today; reject computed ports).
- `ins`/`outs` string instructions and port-mapped DMA.
- Port I/O in the hosted VM (`curlee run` rejects port builtins — they are freestanding-only, like
  `Phys`).
- A separate `cap io.port` capability (decision recorded above: reuse `cap phys.mem`).

## Acceptance criteria

- [ ] `port_inb`/`port_outb`/`port_inw`/`port_outw`/`port_inl`/`port_outl` parse, type-check
      (constant-port enforced), and codegen to inline asm on the freestanding target.
- [ ] Outside `unsafe` → hard error; without `cap phys.mem` in scope → hard error (both mirror the
      `Phys<T>` gate, with span-precise diagnostics).
- [ ] A computed (non-literal) port argument → hard error in both the type checker and the verifier
      (defense in depth).
- [ ] A freestanding fixture that writes COM1 via `port_outb` and reads the LSR via `port_inb`
      (mirroring `putc_driver.c`) verifies and codegens; the emitted C compiles with
      `gcc -ffreestanding -fno-builtin -nostdlib -c`.
- [ ] The verifier treats port reads as opaque: a contract mentioning a `port_in*` result produces
      the "cannot prove opaque value" diagnostic (build fails, not silently passes).
- [ ] `curlee run` rejects port builtins (freestanding-only), consistent with `Phys` handling.

## Verification plan

- Type-check + verifier fixtures (`tests/fixtures/check_io_port_*.curlee`):
  - `check_io_port_com1.curlee` — positive: COM1 `port_outb`/`port_inb` inside `unsafe` +
    `cap phys.mem` (must verify, zero proof obligations for the port ops).
  - `check_io_port_no_unsafe.curlee` — negative: outside `unsafe`.
  - `check_io_port_missing_cap.curlee` — negative: no capability value in scope.
  - `check_io_port_computed.curlee` — negative: computed port address.
  - `check_io_port_opaque_contract.curlee` — negative: contract on a `port_in*` result (opaque note).
- Codegen fixture (`tests/codegen/io_port.curlee` + `.expected`) asserting the emitted `asm
  volatile` lines for all six builtins.
- Golden diagnostics under `tests/diagnostics/`; run
  `ctest --preset linux-debug --output-on-failure` and `bash scripts/smoke.sh`.

## Relevant wiki page(s)

- `wiki/Language-Syntax` — **must update**: add the six port-I/O builtins to the freestanding
  fragment (signatures, `unsafe` + capability gating, constant-port rule).
- `wiki/Stability-and-Supported-Fragment` — **must update**: freestanding section: MMIO + port I/O;
  note `cap phys.mem` covers both address spaces.
- `wiki/Verification-Scope` — **must update**: port reads are opaque (uninterpreted), no OOB
  obligation, cannot be contracted.
- Keep in sync per [`.github/copilot-instructions.md`](.github/copilot-instructions.md:53).

## Dependency note

Depends on the `Phys<T>` front-end/verifier/codegen surface (issues 1–3, shipped) whose gating and
opaque-read machinery is reused verbatim. No dependency on #268 (assignment) or #270 (bitwise),
though the COM1 busy-wait fixture's loop benefit from those when they land.
