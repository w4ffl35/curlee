## Workspace-wide rules — Curlee (apply to every mode)

These load for every mode regardless of slug. Mode-specific rules
(`.roo/rules-<slug>/`) supplement this — they don't replace it.

For repo workflow policy (issue gating, branching, coverage gate, C++23
style, debugging discipline) see `.github/copilot-instructions.md` — that
file is NOT auto-loaded into the session prompt, so re-read it directly
when doing compiler-internals work. This file's job is narrower: give any
session enough of the **Curlee language syntax** up front that it doesn't
have to rediscover it by grepping test fixtures every time.

---

## What this repo is

**Curlee** is a verification-first programming language and its C++23
compiler/runtime/VM. Downstream consumers (e.g. `~/Projects/joeos`) treat
this repo as the source of truth for syntax and the toolchain. Work here
often means either (a) implementing/testing the compiler itself, or
(b) writing `.curlee` source — test fixtures, examples, stdlib modules —
that must be syntactically and semantically valid Curlee.

> No proof, no run. Curlee refuses to build/run a program unless it can
> prove declared contracts within a small, decidable scope (Z3). The
> supported fragment is intentionally small; unsupported constructs are
> **hard errors**, never silently ignored or downgraded.

---

## Curlee language reference (how to write Curlee)

### CLI

| Command | Meaning |
|---|---|
| `curlee check <file.curlee>` | lex → parse → resolve → type-check → verify (Z3). Fails if any obligation can't be proven or is out of scope. |
| `curlee run <file.curlee>` | Runs `check` first, then executes on a deterministic, fuel-bounded VM. |
| `curlee build --target freestanding-c -o out.c <file.curlee>` | Verify, then emit freestanding C (no proof, no build). |
| `curlee build --link -o out.elf <file.curlee>` | Emit C, compile freestanding, link → bootable ELF (kernel use case). |
| `curlee build --define NAME=VALUE ...` | Inject build-time integer defines usable as array lengths and in expression position (no `#ifdef`). |

The VM (`curlee run`) never runs freestanding-only constructs — it
rejects `Phys`, `extern` bodies, and (as of the last check — verify
against current test fixtures, this may have changed) fixed-size arrays.
`curlee build` is the freestanding execution path. **Do not assume VM
scope from memory — grep `tests/run/*.curlee` and `tests/codegen/*.curlee`
for the current, authoritative supported surface before relying on it.**

### Types

- `Int` — integer (`int64_t` in freestanding codegen)
- `Bool` — boolean (`_Bool`)
- `Unit` — like void; functions return `return;`
- `U8` / `U16` / `U32` / `U64` — unsigned fixed-width (freestanding only)
- `String`, `Vec` — hosted-only builtins (rejected in freestanding target)
- `Phys<T>` — raw physical memory pointer (freestanding only)
- `enum` — tagged unions with `variant_is` / `variant_unwrap`
- `struct` — records
- Capabilities — `cap <name>` parameter types (see below)

### Function declarations

```curlee
fn name(a: Int, b: Int) -> Int {
  return a + b;
}

fn serial_hello() -> Unit {
  curlee_putc(72);   // H
  return;
}
```

`main` may return `Int` (exit code; VM) or `Unit` (freestanding entry;
codegen exports it as `curlee_main`).

### Contracts (`requires` / `ensures`) and refinements (`where`)

```curlee
fn vga_cell(ch: Int, attr: Int) -> Int
  [ requires ch >= 0 && ch < 256; ensures true; ] {
  return ch + attr * 256;
}

// Alternative accepted bracket placement (seen in test fixtures):
fn take_nonzero(x: Int) -> Int [
  requires x != 0;
] {
  return x;
}
```

- Obligations checked: at call sites prove callee `requires` from caller
  facts; at returns prove `ensures`. Inside `ensures`, `result` refers to
  the return value.
- Refinement on a binding: `let x: Int where x > 0 = 5;`
- Verifier is **path-sensitive**: it assumes the `if` condition as a fact
  inside the true branch, and assumes the `while` condition inside the
  loop body.
- Contract logic is Int/Bool only. Out-of-scope predicates are hard
  errors — never weaken or drop a contract to "get it compiling."

### Control flow

```curlee
if (cond) {
  ...
} else {
  ...
}

while (cond) {
  ...
}
```

- **NO `else if` chains** — there is no such construct in the grammar.
  Chain conditions by nesting a full `if` inside the `else` block.
- No `for` loops, no `switch`, no ternary.

### Operators (supported fragment)

- Arithmetic: `+` `-` `*` `/` `%` (Euclidean modulo — verified directly
  2026-08-27: `7 % 3` runs and `%` is provable inside `ensures`/contracts
  via Z3's `Z3_OP_MOD`, so it is NOT freestanding-only or unsupported)
- Comparison: `==` `!=` `<` `>` `<=` `>=`
- Logic: `&&` `||` `!`
- **NO shifts, NO bitwise ops (`& | ^ ~`)** — confirmed absent from the
  lexer/parser as of this writing. This surface can keep changing; if a
  claim here matters for what you're about to do, verify it against
  `src/lexer/`, `src/parser/`, or a throwaway `curlee run` probe rather
  than trusting this doc.

### Literals

- Decimal integers are the norm: `753664`, `23`, `0`, `72`.
- Hex literals like `0xB8000` are **ONLY valid inside `phys<T>(...)`** —
  normal `Int` expressions must use decimal.
- Hex addresses may use `_` separators: `0xFD00_0000`.
- String literals: `"x"`.

### Physical memory (`Phys<T>`) — freestanding only

```curlee
fn main(pm: cap phys.mem) -> Unit {
  unsafe {
    let fb: Phys<U32> = phys<U32>(0xFD00_0000);
    fb.write(0xFF8800);
    let v: U32 = fb.read();
  }
  return;
}
```

- The `phys<T>(...)` address argument **must be a compile-time integer
  literal**. Any computed expression → hard error.
- `read()`/`write()` only inside `unsafe { }`, and the enclosing function
  must take a `cap phys.mem` parameter.
- Verifier treats `Phys<T>` as opaque: `read`/`write` are uninterpreted
  (no axioms, no out-of-bounds obligation). Address is constrained,
  access is trusted, data is opaque.

### `extern fn` / `extern static` — external linkage

```curlee
extern fn curlee_putc(c: Int) -> Unit;
extern fn curlee_halt() -> Unit;

// Optional assumed contracts (diagnostic: "extern boundary: contract assumed"):
extern fn boot_setup() -> Unit
  [ requires true; ensures true; ];

// External static storage (freestanding linkage handoff):
extern static mb2_info_addr: Int = 0;
```

- No body for `extern fn`. Optional `requires`/`ensures` are assumed
  (trusted boundary).
- `extern static NAME: Type = expr;` drops `static`/mangling in
  freestanding codegen, emitting a bare external-linkage C global.
- Non-extern fns get a `curlee_` prefix in codegen (`curlee_main`, etc.).

### Fixed-size arrays

**Scope determines the keyword** — this is a common mistake, verified
directly 2026-08-27 (a live session got this wrong and misdiagnosed the
resulting error as "arrays aren't supported," when the real issue was
just the wrong keyword for the scope):
- Module (top) level: MUST use `static`. A top-level `let` is a hard
  parse error ("expected 'import', 'struct', 'enum', 'static', 'fn', ...").
- Inside a function body: use `let` (a `static` also works file-locally,
  but `let` is the normal local-binding form).

```curlee
// Module level — static only:
static buf: [Int; 4] = [0; 4];

fn main() -> Int {
  // Inside a function — let:
  let local_buf: [Int; 4] = [0; 4];
  buf[0] = 42;
  local_buf[0] = 7;
  return buf[0] + local_buf[0];
}
```

- `N` must be a compile-time literal (or a `--define`d build constant).
- Every access must be provably in bounds.
- Arrays now work in the VM (`curlee run`), not just freestanding builds
  — landed via issue #300 / curlee#301, merged 2026-08-27. If a stale
  doc or your own memory says otherwise, verify against
  `tests/run/*.curlee` rather than trust the stale claim.

### Imports / modules

```curlee
import std.io as io;
import mymod.math as m;   // relative module (dir/module pairs)

fn main(input: cap io.stdin) -> String {
  return io.read_line(input);
}
```

- Module-qualified calls work both via alias and full path.
- Stdlib lives at `stdlib/v1/std/`: `io`, `fs`, `math`, `rng`, `tty`,
  `vec`, `python` (stubbed).

### Capabilities (no ambient authority)

Capabilities are explicit parameter types threaded through call chains:

| Capability | Purpose |
|---|---|
| `cap io.stdout` | print / host output |
| `cap io.stdin` | read line |
| `cap io.tty` | `__tty_clear`, `__tty_write_at`, `__tty_flush` |
| `cap fs.read` / `cap fs.write` | file access |
| `cap rng.seeded` | seeded random |
| `cap python.ffi` | python interop (stubbed) |
| `cap phys.mem` | `Phys<T>` read/write (freestanding) |

The `--cap` CLI flag grants capabilities at run time.

### Enums

```curlee
enum Maybe {
  Some(Int);
  None;
}

fn main() -> Int {
  let m: Maybe = Maybe::Some(1);
  if (variant_is(m, Maybe::Some)) {
    return variant_unwrap(m, Maybe::Some) + 41;
  }
  return 0;
}
```

### Freestanding target constraints

- **No `print`, no `String`, no `Vec`, no `malloc`** — rejected with a
  clear diagnostic.
- `Int` → `int64_t`, `Bool` → `_Bool`, `U8/16/32/64` → `uintN_t`.
- `main` → `int64_t curlee_main(void)`.
- Phys load/store → `*(volatile uintN_t*)(ADDR)` with the constant
  embedded.

### Comments

- `//` line comments only (no `/* */`).
- Files start with an SPDX header: `// SPDX-License-Identifier: MIT`
  (Curlee itself is MIT-licensed; downstream consumers may use their own).

---

## Documentation references (source of truth)

When unsure about syntax or scope, read these before guessing — do not
invent syntax from memory of other languages:

- **This repo's README:** `README.md` — overview, build/run, MVP scope.
- **Docs index:** `docs/README.md`
- **Verification scope:** `docs/` and the wiki `Verification-Scope` page
  — the pinned decidable logic fragment. Do not expand it without an
  Issue + tests + wiki update.
- **Examples:** `examples/*.curlee` — runnable syntax samples.
- **Test fixtures:** `tests/fixtures/*.curlee`, `tests/codegen/*.curlee`,
  `tests/run/*.curlee` — the most authoritative, currently-accurate
  source for what the compiler and VM actually accept RIGHT NOW.
- **Repo workflow policy:** `.github/copilot-instructions.md` — issue
  gating, branching, coverage gate, C++23 idioms, debugging discipline.
- **Wiki (remote, source of truth for the supported fragment):**
  - Stability & supported fragment: https://github.com/w4ffl35/curlee/wiki/Stability-and-Supported-Fragment
  - Syntax reference: https://github.com/w4ffl35/curlee/wiki/Language-Syntax
  - Modules & imports: https://github.com/w4ffl35/curlee/wiki/Modules-and-Imports
  - Running programs (fuel, capabilities, interop): https://github.com/w4ffl35/curlee/wiki/Running-Programs

**When in doubt, mirror existing test fixtures over this document** —
this file can go stale as the language evolves; `tests/**/*.curlee` is
exercised by CI and cannot lie about what currently compiles and runs.
