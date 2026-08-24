# Title: Codegen backend: emit freestanding C from verified Curlee programs (`--target freestanding-c`)

## Problem statement / motivation

Curlee today has a single execution target: the bytecode interpreter VM ([`src/vm/vm.cpp`](src/vm/vm.cpp:546)),
which is deeply libc-dependent (`iostream`, `fstream`, `fork/exec/poll/pipe`, `getenv`, `signal`;
`Value` uses `std::string`/`shared_ptr`/`unordered_set` in [`value.h`](include/curlee/vm/value.h:48)).
Porting that VM to a freestanding kernel is impractical and low-value.

The right architecture: a **new codegen backend** that emits **freestanding C** from the verified
`Program` AST. The compiler controls the full ABI surface, and the emitted C is compiled with
`gcc -ffreestanding -fno-builtin -nostdlib -c` and linked (link step is a separate issue) against a tiny
freestanding runtime (separate issue). This issue delivers the codegen module and the `curlee build`
subcommand — **without** the link step and without the boot stub.

The verification gate is unchanged: **no proof, no build.** `curlee build` runs the full
lex → parse → resolve → type-check → verify pipeline and refuses to emit anything unless verification
succeeds (mirror of the `run` path in [`cli_impl.ipp`](src/cli/cli_impl.ipp:1105)).

## Design

New module `src/codegen/` + public API in `include/curlee/codegen/codegen.h`:

```cpp
namespace curlee::codegen {
  using CodegenResult = std::variant<std::string, std::vector<curlee::diag::Diagnostic>>;
  [[nodiscard]] CodegenResult codegen_freestanding_c(const curlee::parser::Program& program);
}
```

Emission rules (walk the AST like the emitter in
[`emitter_impl.ipp`](src/compiler/emitter_impl.ipp:100), but produce C text):

| Curlee | Emitted C |
| --- | --- |
| `Int` | `int64_t` |
| `Bool` | `_Bool` |
| `U8/U16/U32/U64` | `uint8_t/16/32/64_t` (from `<stdint.h>`, a freestanding header — no libc needed) |
| `Unit` | `void` |
| `main` | `int64_t curlee_main(void)` |
| internal function `f` | `static int64_t curlee_f(...)` |
| `extern fn f` (issue 5) | `extern ... f(...)`; direct call (symbol resolved at link) |
| `phys<U32>(0xADDR).read()` | `(*(volatile uint32_t*)(0xADDR))` |
| `phys<U32>(0xADDR).write(v)` | `(*(volatile uint32_t*)(0xADDR)) = (v);` |
| if/while/return/let/expr-stmt | structured C statements (if/while/return/local) |

- All top-level names are `curlee_`-prefixed to avoid collisions with the runtime/ABI; `extern` names
  pass through verbatim.
- Phys addresses are **embedded as integer literals** in the cast — no runtime pointer arithmetic.
- Int ops (`Add/Sub/Mul/Div/...`) map to C operators; the verifier has already proven the contracts
  that bound them.
- **Rejected-on-freestanding**: builtins that require the hosted VM/OS (`print`, `__read_line`,
  `__fs_*`, `__tty_*`, `__rng_*`, `vec/set` ops, `python_ffi`) produce a clear diagnostic:
  `"builtin X is not supported in freestanding target"`. (This keeps the freestanding subset honest and
  small; the runtime issue adds only mem/halt/putc primitives.)
- No VM bytecode is produced; the existing emitter/VM are untouched.

CLI integration ([`cli_impl.ipp`](src/cli/cli_impl.ipp:1116) style):

```
curlee build [--target freestanding-c] [-o out.c] <entry.curlee>
```

- Runs the full check pipeline first; on verification failure, prints diagnostics and exits non-zero
  (no output file).
- `--target` defaults to `freestanding-c` (only target in this issue); `-o -` writes to stdout.

## MVP scope

In-scope:

- Codegen module + CLI `curlee build` with full verification gate.
- Emission for: int/bool/unsigned arithmetic, control flow (if/else/while), function calls,
  structs/enums/fields (as C structs + `curlee_`-prefixed enum constants + match lowering via if-chain),
  `Phys<T>` read/write, `extern fn` declarations (syntax must already exist from issue 5 — see
  dependency note), locals/params.
- Golden tests comparing emitted C text.
- Compile check of emitted C with `gcc -ffreestanding -fno-builtin -nostdlib -c` in tests/CI.

Out-of-scope (explicit):

- The **link step** (`--link`, crt0, linker script) — separate issue.
- The **freestanding runtime** (`rt.c`: mem ops, halt, putc) — separate issue.
- Optimization, multiple targets, macOS/Windows support, VM parity, `String`/`Vec` on freestanding.

## Acceptance criteria

- [ ] `curlee build --target freestanding-c` emits compilable C for: integer arithmetic + contracts,
      if/while control flow, function calls, a struct fixture, an enum/match fixture.
- [ ] `curlee build` on a Phys fixture emits `volatile uintN_t*` derefs with the literal address
      embedded and **passes verification first**.
- [ ] A fixture using a hosted builtin (`print`) emits the clear "not supported in freestanding target"
      diagnostic.
- [ ] A fixture with a failing contract **fails the build** (no C emitted, non-zero exit) — the
      verification gate is enforced.
- [ ] Emitted C for all positive fixtures compiles with `gcc -ffreestanding -fno-builtin -nostdlib -c`
      (CI smoke target).
- [ ] `curlee --help` shows `build` usage.

## Verification plan

- New `tests/codegen_tests.cpp` with fixtures + `.expected` golden files (mirror the
  `cli_fmt/formatted.expected` pattern).
- CMake custom target: codegen each positive fixture, then `gcc -ffreestanding -fno-builtin -nostdlib -c`
  the result (a `-freestanding` compile is itself the primary test).
- CLI golden tests for the error paths (hosted builtin, failed contract).
- Run: `ctest --preset linux-debug --output-on-failure` (focused: codegen + cli tests first).

## Relevant wiki page(s)

- `wiki/Running-Programs` — document `curlee build --target freestanding-c`, the verification gate,
  and the supported freestanding subset (and that the VM does not run Phys/extern programs).
- `wiki/Stability-and-Supported-Fragment` — add the freestanding fragment table.
- `wiki/Verification-Scope` — confirm "no proof, no build" applies to `build` as well.

## Dependency note

Emission of `extern fn` calls requires the `extern` syntax from [extern fn + boot stub + link step](#);
if issues are implemented in order, land that issue's syntax portion first or coordinate on the AST
shape. The codegen module itself can be written and tested against the non-extern subset independently.
