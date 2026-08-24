# Title: Freestanding runtime: `runtime/rt.c` (mem ops, halt, weak putc) — no malloc, no libc

## Problem statement / motivation

The codegen issue emits freestanding C, but the emitted code still needs a tiny runtime:

- The compiler (GCC/Clang) may emit calls to `memset`/`memcpy`/`memcmp` for struct copies and zeroing —
  these must exist in the freestanding image.
- The kernel needs a defined way to *stop*: `curlee_halt` (x86-64 `cli; hlt` loop) and `curlee_panic`.
- A minimal output primitive `curlee_putc` (weak, host-overridable) gives early bring-up a way to
  print without any driver.

This runtime is deliberately **not** a libc: no malloc, no printf, no filesystem, no threads. It is
the "you write malloc and printf yourself" contract from the freestanding reality made concrete.

## Design

New `runtime/rt.c` (+ `runtime/rt.h`) compiled with the same flags as generated code
(`-ffreestanding -fno-builtin -nostdlib`). Contents:

```c
void* memset(void* dst, int c, unsigned long n);
void* memcpy(void* dst, const void* src, unsigned long n);
int   memcmp(const void* a, const void* b, unsigned long n);

[[noreturn]] void curlee_halt(void);   // cli; hlt loop (x86-64)
[[noreturn]] void curlee_panic(const char* msg);

__attribute__((weak)) void curlee_putc(char c); // default: no-op; host overrides
```

- All functions are static-allocation free (no global state); the compiler builtins use plain loops.
- `curlee_halt`/`curlee_panic` use inline `asm volatile("cli; hlt")` in an infinite loop (x86-64;
  other arches are future work).
- `curlee_putc` default is a no-op so linking always succeeds; a UEFI/serial driver (later) overrides
  the weak symbol — this is the same pattern as the `python_ffi` "shield" philosophy in
  [copilot-instructions.md](.github/copilot-instructions.md:36), scoped to freestanding.
- The runtime must compile with **zero** libc dependencies: only freestanding `<stdint.h>`, and
  intrinsics for volatile accesses. `rt.h` must be includable from generated C.
- No heap allocator in this issue (documented out of scope; a future issue can add a bump allocator
  gated by capability).

## MVP scope

In-scope:

- `runtime/rt.c` + `runtime/rt.h` with the functions above.
- CMake: a header-only/object interface target `curlee_rt` so generated code + runtime can be linked
  together; a `-freestanding` compile check in CI.
- A standalone compile test: `rt.c` compiles clean under `gcc -ffreestanding -fno-builtin -nostdlib -c`
  with `-Wall -Wextra -Wpedantic -Werror` (matching the repo's warning policy, [`CMakeLists.txt`](CMakeLists.txt:96)).

Out-of-scope (explicit):

- Heap allocator (`malloc`/`free`), `printf`/formatting, filesystem, threading, FP support, UEFI or
  serial drivers (only the weak `putc` hook), other CPU architectures.
- The boot stub (`crt0.S`), linker script, and `curlee build --link` — separate issue.

## Acceptance criteria

- [ ] `rt.c` compiles under `gcc -ffreestanding -fno-builtin -nostdlib -c` with the repo's warning
      flags and `-Werror`.
- [ ] `memset`/`memcpy`/`memcmp` pass a unit test in a freestanding context (or an equivalent
      host-visible test that exercises the same code paths without libc).
- [ ] `curlee_halt`/`curlee_panic` contain the x86-64 `cli; hlt` sequence (verified via objdump in the
      smoke target or an explicit disassembly check).
- [ ] `curlee_putc` is weak (linking succeeds without a definition; a host override replaces it —
      tested with a tiny C test that overrides it and links).
- [ ] `rt.h` includes cleanly from generated freestanding C.

## Verification plan

- Unit tests for the mem ops (host build exercising the same loops, e.g. `tests/runtime_mem_tests.cpp`).
- CMake custom target: compile `rt.c` freestanding; link a tiny C kernel (from the codegen issue or a
  raw test) with `-nostdlib` and `-T` placeholder, then `objdump` assertions on the `hlt` sequence and
  symbol presence.
- Run: `ctest --preset linux-debug --output-on-failure` (focused: runtime tests + smoke).

## Relevant wiki page(s)

- `wiki/Running-Programs` — document the freestanding runtime surface: mem ops, `curlee_halt`,
  `curlee_panic`, weak `curlee_putc`; state explicitly that malloc/printf are user-provided.
- `wiki/Stability-and-Supported-Fragment` — add the runtime to the freestanding fragment table.

## Dependency note

Depends on the codegen issue (to have generated C to link against in the smoke test). The runtime
itself can be written and unit-tested independently.
