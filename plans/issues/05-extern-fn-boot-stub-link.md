# Title: `extern fn` declarations + x86-64 boot stub (`crt0.S`) + linker script + `curlee build --link`

## Problem statement / motivation

Two freestanding realities remain after the codegen and runtime issues:

1. **External linkage**: the language must be able to call a tiny Assembly or C stub for the initial CPU
   boot state (stack setup, interrupt vectors, BSS zeroing) before jumping into verified code. Curlee
   needs an `extern fn` declaration form so verified Curlee can call into the stub (and the stub can
   call `curlee_main`).
2. **A real bootable image**: the codegen emits C, the runtime provides mem ops, but nothing produces a
   bootable ELF with an entry point. This issue adds the x86-64 `_start` stub and the linker script and
   wires `curlee build --link` to produce `kernel.elf` loadable by qemu (multiboot2).

Today there is no `extern` concept at all: every function must have a body ([`ast.h`](include/curlee/parser/ast.h:274)),
and the emitter hard-fails on unknown functions ([`emitter_impl.ipp`](src/compiler/emitter_impl.ipp:162)).
The capability machinery (`--cap`, `required_capabilities` in
[`cli_impl.ipp`](src/cli/cli_impl.ipp:1134)) already exists and is reused unchanged.

## Design

### Language: `extern fn`

```curlee
extern fn putc(c: Int) -> Unit;            // no body
extern fn boot_setup() -> Unit
  [ requires true; ensures true; ];        // optional contracts: ASSUMED, with a Note diagnostic
```

- Lexer: `KwExtern`. Parser: body-less function declaration (`extern fn name(params) [contracts];`).
- Resolver/type-check: register the extern symbol in the function table (must not collide with a body'd
  function); type signature checked like a normal function.
- Verifier: an extern function is a **trusted boundary** — its `requires`/`ensures` are assumed, not
  checked. Emit a `Note` diagnostic at the declaration: "extern boundary: contract assumed, not
  verified" so the trust transfer is explicit (aligns with "never silently drop a contract" policy —
  it is loudly documented instead).
- Emitter/VM: `curlee run` on a program containing `extern fn` produces a diagnostic
  (`"extern functions are not supported by the VM; use curlee build --link"`) and fails. Codegen emits
  the C `extern` declaration and a direct call (symbol resolved at link time).

### Boot stub: `runtime/crt0.S` (x86-64)

```asm
.global _start
_start:
  # zero .bss (symbols __bss_start / __bss_end from linker script)
  # set rsp = __stack_top
  mov $__stack_top, %rsp
  call curlee_main
  # deterministic exit: hlt loop (or write to debug port for qemu check)
  cli
1: hlt
  jmp 1b
```

- Optional multiboot2 header (magic 0xE85250D6) so qemu `-kernel` boots it.
- No libc, no CRT (this replaces crt0 from libc entirely).

### Linker script: `runtime/linker.ld`

- `ENTRY(_start)`; sections `.text/.rodata/.data/.bss` at a known base (e.g. `0x100000` multiboot
  convention); symbols `__bss_start`, `__bss_end`, `__stack_top` (stack = static `.bss`-reserved region,
  e.g. 16 KiB).

### CLI: `curlee build --link`

```
curlee build --target freestanding-c --link [-o kernel.elf] <entry.curlee>
```

- Verify → codegen to C → compile `cc -ffreestanding -fno-builtin -nostdlib -c` →
  assemble `crt0.S` → link `ld -nostdlib -T runtime/linker.ld` → `kernel.elf`.
- `-o` accepts `.elf` (and optionally `.bin` via objcopy for raw images; MVP: ELF).

## MVP scope

In-scope:

- `extern fn` syntax/parse/resolve/type-check/verify (assumed contracts + Note), VM rejection, codegen
  emission of extern calls.
- `crt0.S` with BSS zero, stack setup, `call curlee_main`, `cli;hlt` exit, multiboot2 header.
- `linker.ld` with `__bss_start`/`__bss_end`/`__stack_top`, `ENTRY(_start)`.
- `curlee build --link` pipeline producing `kernel.elf`.
- qemu smoke: boot `kernel.elf` and observe the deterministic exit (`curlee_main` returning then hlt —
  verified via qemu `-d` logging or a debug-exit via `0xf4` port write in a host-overridden putc/halt
  during the smoke only).

Out-of-scope (explicit):

- Other architectures (aarch64/riscv), shared libraries/relocations, dynamic linking, `extern` global
  variables, UEFI .efi images (multiboot2 covers the MVP boot path), real interrupt vectors (only the
  stub entry; the user said "interrupt vectors" — the stub is the contract point where a future issue
  can add IDT setup in a stub, not in verified code).

## Acceptance criteria

- [ ] `extern fn putc(c: Int) -> Unit;` parses, resolves, type-checks, and passes `curlee check`
      (with the "contract assumed" Note).
- [ ] A program calling an extern function fails `curlee run` with the clear VM diagnostic.
- [ ] `curlee build --link` on a kernel fixture (extern `putc` + a `main` that calls it) produces
      `kernel.elf`; `objdump -f` shows entry `_start`; `readelf -S` shows `.text/.rodata/.data/.bss`
      with `__bss_start`/`__bss_end`/`__stack_top` symbols present.
- [ ] qemu smoke: `qemu-system-x86_64 -kernel kernel.elf` reaches `curlee_main` (observable via the
      smoke-only debug-exit or serial output) and halts deterministically (exit code check in CI).
- [ ] Existing `curlee run`/VM tests unaffected (extern rejection is the only VM change).

## Verification plan

- Fixtures + golden diagnostics: extern declaration, extern with assumed contract (Note), VM rejection.
- `tests/linker_boot_tests.cpp`-style or CMake custom target: build the sample kernel, run
  `objdump`/`readelf` assertions, run qemu in the CI `freestanding` job (or skip with a clean message
  if qemu is absent — CI job is the issue 6 follow-up).
- Run: `ctest --preset linux-debug --output-on-failure` (focused: cli + codegen + linker smoke).

## Relevant wiki page(s)

- `wiki/Language-Syntax` — document `extern fn`, assumed contracts, VM restriction.
- `wiki/Running-Programs` — document `curlee build --link`, the boot stub, `phys.mem` (link capability
  grants), and the qemu boot instructions.
- `wiki/Verification-Scope` — add "extern boundary: contracts assumed" as a documented exception.

## Dependency note

Depends on codegen issue (emits the C we link) and runtime issue (provides mem ops/halt the stub
needs). The `extern fn` syntax portion can land first and is testable independently of linking.
