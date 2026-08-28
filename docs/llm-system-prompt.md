# Curlee LLM system prompt

A self-contained syntax reference meant to be pasted directly into an
LLM's system prompt to teach it Curlee — no other context needed. This is
the deliverable for issue #237.

Verified against a real build (see `.roo/rules/rules.md`, this repo's own
CI, and `tests/**/*.curlee`) as of 2026-08-28. If a claim here ever
contradicts `tests/**/*.curlee`, the tests are the source of truth — file
an issue rather than trust this doc blindly.

---

## System prompt text (copy everything below this line)

You are writing **Curlee**, a verification-first systems language. Curlee
refuses to build or run a program unless it can prove every declared
contract within a small, decidable logic fragment (backed by Z3). Unlike
most languages you know, an unsupported construct is a **hard parse/type
error**, never silently accepted or downgraded — do not invent syntax
from memory of Rust, C, or Python; if you're unsure, prefer the simplest
construct below over guessing.

### Types

- `Int` — integer (64-bit).
- `Bool` — boolean.
- `Unit` — like void.
- `U8` / `U16` / `U32` / `U64` — unsigned fixed-width (freestanding-target
  builds only; array element types work everywhere).
- `struct` — records. `enum` — tagged unions.

### Functions

```curlee
fn add(a: Int, b: Int) -> Int {
  return a + b;
}
```

### Contracts (`requires` / `ensures`)

```curlee
fn clamp(x: Int, lo: Int, hi: Int) -> Int
  [ requires lo <= hi;
    ensures result >= lo && result <= hi; ]
{
  if (x < lo) {
    return lo;
  } else {
    if (x > hi) {
      return hi;
    } else {
      return x;
    }
  }
}
```

- Inside `ensures`, `result` refers to the return value.
- Use explicit nested `if`/`else`, not a chain of early returns, when a
  function needs to prove a postcondition across multiple branches — the
  verifier is path-sensitive per branch and early-return chains have
  produced spurious "ensures clause not satisfied" failures in practice.
- A binding refinement: `let x: Int where x > 0 = 5;`

### Control flow

```curlee
if (cond) {
  ...
} else {
  ...
}

while (cond)
  [ invariant <condition true on every iteration>;
    decreases <expression that strictly decreases and stays >= 0>; ]
{
  ...
}
```

- **There is no `else if`.** Chain conditions by nesting a full `if`
  inside the `else` block (see `clamp` above).
- No `for`, no `switch`, no ternary.
- `decreases` must be **non-negative at loop entry**, not just while the
  loop body runs — an ascending loop bounded by `n` needs
  `decreases n - i` (the remaining distance to the bound), never the
  ascending variable `i` itself; a descending loop uses `decreases i`
  directly.

### Operators

- Arithmetic: `+ - * / %` (Euclidean modulo — fully supported, provable
  inside contracts).
- Bitwise: `& | ^ ~` and shifts `<< >>` — fully supported.
- Comparison: `== != < > <= >=`. Logic: `&& || !`.

### Fixed-size arrays

**Array literals are repeat-fill only**: `[value; count]` fills every
slot with the *same* value. There is no literal syntax for a list of
distinct values.

```curlee
// Module level: MUST use `static`. A top-level `let` is a hard parse
// error.
static buf: [Int; 4] = [0; 4];

fn main() -> Int {
  // Inside a function: use `let`.
  let local: [Int; 4] = [0; 4];
  buf[0] = 42;
  local[0] = 7;
  return buf[0] + local[0];
}
```

To build an array of **distinct** values (e.g. a lookup table), declare it
filled with a placeholder, then set each slot with an individual indexed
assignment:

```curlee
fn char_codes() -> Int {
  let arr: [Int; 3] = [0; 3];
  arr[0] = 72;   // 'H'
  arr[1] = 105;  // 'i'
  arr[2] = 33;   // '!'
  return arr[0];
}
```

`N` must be a compile-time literal. Every access must be provably in
bounds.

### `extern fn` — external linkage

```curlee
extern fn curlee_putc(c: Int) -> Unit;
```

No body. Not runnable on the VM (`curlee run` rejects it — use
`curlee build --link` for freestanding targets that need it).

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

### Structs

```curlee
struct Point {
  x: Int;
  y: Int;
}

fn main() -> Int {
  let p: Point = Point{ x: 1, y: 2 };
  return p.x + p.y;
}
```

### Capabilities (no ambient authority)

Explicit parameter types thread capabilities through call chains:

```curlee
fn main(out: cap io.stdout) -> Int {
  print("x");
  return 0;
}
```

Run with `curlee run --cap io.stdout <file>` to grant it.

### Literals and comments

- Decimal integers: `753664`, `23`, `0`.
- String literals: `"x"` (hosted/VM target only, not freestanding).
- `//` line comments only (no `/* */`).

### CLI quick reference

| Command | Meaning |
|---|---|
| `curlee check <file>` | lex → parse → resolve → type-check → verify. |
| `curlee run <file>` | `check`, then execute on the deterministic VM. |
| `curlee build --link -o out.elf <file>` | Verify, emit freestanding C, link. |

---

## Notes for maintainers of this doc

Pulled from `.roo/rules/rules.md` (this repo's own agent-facing rules
file) and re-verified directly against a real build rather than copied
blind — in particular the `decreases` direction guidance and the
early-return-vs-nested-if guidance for `ensures` came from live failures
observed while fine-tuning a local model on this exact syntax (see
w4ffl35/curlee#240 for the write-up). Keep this in sync with
`.roo/rules/rules.md` when either changes.
