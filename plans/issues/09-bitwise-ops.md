# Title: Bitwise operators and shifts (`& | ^ ~ << >>`) plus modulo `%` for protocol/packing code

Issue: https://github.com/w4ffl35/curlee/issues/270

## Problem statement / motivation

Curlee rejects **all bitwise operators and shifts** (`& | ^ ~ << >>`) and modulo (`%`) as
"unsupported binary operator". This blocks an entire class of code that is otherwise pure
computation: byte packing, checksums, descriptor flags, alignment math, bit-field extraction.

In the JOE OS kernel (w4ffl35/joeos), three drivers are stuck in C largely for bitwise ops:

- **`net_stack.c`** — TCP/IP protocol: `ns_wr16`/`ns_wr32` big-endian header packing
  (`v >> 8`, `v & 0xFF`), ARP/IPv4/TCP field extraction, checksums. ~700 of its 1056 lines are
  protocol logic that needs shifts/masks.
- **`mb2.c`** — multiboot2 parser: tag alignment `(size + 7) & ~7U`, u32 field reads from the
  packed info structure. ~80 pure-parser lines.
- **`fb.c`** — glyph bit tests (`(bits >> (4 - col)) & 1`), color channel masking.
- **`virtio_net.c`** — vring descriptor flags (`VRING_DESC_F_NEXT`/`F_WRITE`), PCI config address
  assembly (`(bus << 16) | (dev << 11)`).

The pure-glyph module (`glyphs.curlee`) already works around the missing shifts with division-based
bit extraction ("bits - (bits / 16) * 16 >= 8") — a correctness-preserving but ugly workaround that
should be replaced by `>>`/`&` once supported.

## Design

### 1. Lexer: new tokens

The lexer currently has **no** `&`, `|`, `^`, `~`, `%` tokens — single `&`/`|` are only matched as
part of `&&`/`||` ([`lexer.cpp`](src/lexer/lexer.cpp:171)), and `^`, `~`, `%` fall through to
`"invalid character"` ([`lexer.cpp`](src/lexer/lexer.cpp:250)). Add to
[`token.h`](include/curlee/lexer/token.h:58) and [`lexer.cpp`](src/lexer/lexer.cpp:135):

| Token | Lexeme | Notes |
|---|---|---|
| `Amp` | `&` | single char (must be checked **after** the `&&` two-char case) |
| `Pipe` | `\|` | single char (checked after `\|\|`) |
| `Caret` | `^` | new single-char case |
| `Tilde` | `~` | new single-char case |
| `Percent` | `%` | new single-char case |
| `ShiftLeft` | `<<` | two-char case (checked **before** `<` and after `<=`) |
| `ShiftRight` | `>>` | two-char case (checked before `>` and after `>=`) |

Add the corresponding `to_string` entries in [`token.h`](include/curlee/lexer/token.h:100).

### 2. Parser: precedence levels (expression + predicate paths)

The expression grammar is layered `parse_expression → parse_equality → parse_comparison →
parse_term → parse_factor → parse_unary` ([`parser.cpp`](src/parser/parser.cpp:1828)). Insert new
levels with C-like precedence (each new level calls the next-lower one, mirroring the existing
`parse_comparison`/`parse_term` loops):

1. **Shift** (between comparison and term): `<<`, `>>` — left-assoc.
2. **Bitwise AND** (between shift and equality, i.e. above equality in C terms): `&`.
3. **Bitwise XOR** (above AND): `^`.
4. **Bitwise OR** (above XOR): `|`.

Order (lowest to highest binding), matching C:
`equality` (`==`/`!=`) < `&` < `^` < `|` < comparison `< <= > >=` < shift `<< >>` < term
`+ -` < factor `* / %` < unary `! - ~`.

- `%` joins the existing `parse_factor` loop alongside `*`/`/`
  ([`parser.cpp`](src/parser/parser.cpp:1930)) — same precedence as multiplication.
- `~` joins `parse_unary` alongside `!`/`-` ([`parser.cpp`](src/parser/parser.cpp:1956)).
- The **predicate** grammar (`parse_pred_*`, [`parser.cpp`](src/parser/parser.cpp:860)) gets the
  same new levels so contracts can use bitwise/shift/`%` expressions.

### 3. Type checker ([`type_check.cpp`](src/types/type_check.cpp:823))

- `& | ^ << >> %` require **Int operands** (or matching unsigned U8/U16/U32/U64 operands in
  freestanding) and produce the operand type. Bitwise on Bool is rejected (Bool has `&&`/`||`).
- `~` requires an Int/unsigned operand, produces the same type.
- Shifts: LHS must be Int/unsigned, RHS must be Int (shift amount), result is LHS type.
  Out-of-scope: signed right-shift semantics are defined as **arithmetic shift** and documented
  (see MVP scope).
- `%` requires Int operands and produces Int. MVP restricts to **non-negative operands** in the
  verifier (below); the type checker allows any Int and the verifier enforces the domain.
- Mirror the existing diagnostic wording style (`"'&' expects Int operands"`, etc.).

### 4. Verifier: bit-vector theory vs. documented Int model

Z3 has a native bit-vector theory, but Curlee's solver fragment is Int/Bool-based (the small
decidable fragment rule, [`.github/copilot-instructions.md`](.github/copilot-instructions.md:132)).

**MVP decision: lower bitwise ops on Int to the documented bit-precise Int model**, matching the
repo's existing non-linear-arithmetic discipline:

- `& | ^` on non-negative Int operands lower to Z3 `bvand`/`bvor`/`bvxor` after converting both
  operands to a fixed bit-vector width (decide and document the width — recommended 64-bit,
  matching the `Int` VM width), then convert the result back to Int. This is decidable and keeps
  the solver in the existing fragment boundary.
- `<<`/`>>` lower to `bvshl`/`bvashr` on the same fixed width. `>>` on a negative LHS is rejected
  as a hard error (documented arithmetic-shift semantics are only defined for non-negative Int).
- `~` lowers to `bvnot`.
- `%` on non-negative operands is lowered via the division-remainder identity
  `a % b == a - (a/b)*b` ([`predicate_lowering.cpp`](src/verification/predicate_lowering.cpp:246)
  already constrains `*` to one-literal operands; `%` follows the same one-literal constraint when
  the divisor is symbolic) — exactly the identity `glyphs.curlee` hand-encodes.
- The **predicate lowering** (`PredBinary`/`PredUnary` in
  [`predicate_lowering.cpp`](src/verification/predicate_lowering.cpp:156)) gets the same operator
  cases so `requires`/`ensures`/`invariant`/`decreases`/`fuel` can use the new ops.
- **Opaque values stay opaque**: bitwise on an opaque MMIO/port read result (from #269) remains
  unprovable — a contract mentioning such an expression hits the existing opaque-value rejection.

### 5. Codegen: emit the corresponding C operators

- **Freestanding C** ([`codegen.cpp`](src/codegen/codegen.cpp:1350) `emit_binary`): emit `&`,
  `|`, `^`, `<<`, `>>`, `%` and (in `emit_unary`, line ~1303) `~` directly — C has all of these
  natively. U8/U16/U32/U64 locals already lower to `uint8_t`/`uint16_t`/`uint32_t`/`uint64_t`
  ([`codegen.cpp`](src/codegen/codegen.cpp:126)), so C's own integer-promotion semantics apply.
- **VM bytecode** ([`emitter.cpp`](src/compiler/emitter.cpp:961) `emit_binary` + `emit_unary`):
  add `OpCode::BitAnd`, `BitOr`, `BitXor`, `BitNot`, `ShiftLeft`, `ShiftRight`, `Mod` to
  [`bytecode.h`](include/curlee/vm/bytecode.h:19) and their `case` handlers in
  [`vm.cpp`](src/vm/vm.cpp:666) (implemented on the existing Int `Value`). This keeps the hosted
  VM able to execute the new operators deterministically.

### 6. Cost model ([`cost_model.cpp`](src/verification/cost_model.cpp))

Each new binary/unary op costs `1` (matching the existing arithmetic ops at
[`cost_model.cpp`](src/verification/cost_model.cpp:89)); `%`/shift on symbolic operands carry no
extra charge. No changes to the loop/fuel formulas.

## MVP scope

In-scope:

- `& | ^ ~ << >>` on Int (hosted + freestanding) and U8/U16/U32/U64 (freestanding).
- `%` on Int, non-negative operands, via the division-remainder identity.
- Verifier: fixed-width bit-vector lowering to Int (documented bit-precise model); `%` identity;
  predicate-path support for contracts.
- Codegen: C operators (freestanding) + new VM opcodes (hosted).
- `glyphs.curlee` equivalence fixture (division-based vs. shift-based extraction).

Out-of-scope (explicit):

- Signed right-shift semantics on negative values: `>>` on a negative LHS is a hard error
  (arithmetic shift is defined only for non-negative Int; document this).
- Rotation and named bit-field types (future).
- Variable-width bit-vectors or bv-to-int conversions beyond the documented fixed width.
- Bitwise on Bool (rejected — Bool keeps `&&`/`||` only).

## Acceptance criteria

- [ ] `a & b`, `a | b`, `a ^ b`, `~a`, `a << n`, `a >> n`, `a % b` parse, type-check, verify
      (where in-scope), and codegen (freestanding C + VM bytecode).
- [ ] A verified function using `>>`/`&` for glyph bit extraction verifies where today's
      division-based workaround does; the equivalence fixture proves both forms return the same
      values.
- [ ] `%` on non-negative operands verifies (the identity `a % b == a - (a/b)*b`).
- [ ] A big-endian pack helper (`ns_wr32`-style, `(v >> 8) & 0xFF` etc.) verifies with `>>`/`&`.
- [ ] `>>` on a negative LHS produces a hard, span-precise diagnostic (documented restriction).
- [ ] Bitwise on an opaque MMIO/port read result in a contract produces the opaque-value diagnostic
      (not a silent pass).
- [ ] The existing `glyphs.curlee` division-based workaround is replaceable by the native operators
      (fixture proves equivalence).

## Verification plan

- Lexer/parser tests: each new token; precedence fixtures (`a | b & c`, `a << 2 + 1`, `a % 8`).
- Type-check fixtures (`tests/fixtures/check_bitwise_*.curlee`):
  - each operator on Int (positive); on U8/U16/U32/U64 freestanding (positive);
  - Bitwise-on-Bool (negative); `%` with a negative operand domain (negative);
  - shift with a negative LHS (negative); big-endian pack helper (positive).
- Verifier fixtures: `%` identity, `>>`/`&` glyph extraction, `ns_wr32` pack, all contract-context
  uses (`requires`/`ensures`/`invariant`/`decreases`/`fuel`).
- Codegen fixtures (`tests/codegen/bitwise.curlee` + `.expected`) asserting the emitted C
  operators; VM execution fixtures for `& | ^ ~ << >> %`.
- Equivalence fixture: division-based vs. shift-based glyph extraction returns the same values.
- Golden diagnostics under `tests/diagnostics/`; run
  `ctest --preset linux-debug --output-on-failure` and `bash scripts/smoke.sh`.

## Relevant wiki page(s)

- `wiki/Language-Syntax` — **must update**: add `& | ^ ~ << >> %` to the supported operators with
  precedence and the arithmetic-shift-on-non-negative rule.
- `wiki/Stability-and-Supported-Fragment` — **must update**: remove "no bitwise / no modulo" from
  the documented limitations.
- `wiki/Verification-Scope` — **must update**: the fixed-width bit-vector-to-Int lowering model,
  the `%` identity, and the negative-LHS/negative-operand restrictions.
- Keep in sync per [`.github/copilot-instructions.md`](.github/copilot-instructions.md:53).

## Dependency note

Independent of #268 (assignment) and #269 (port I/O), but the `glyphs.curlee` and driver
migrations benefit from all three landing. The opaque-value contract rule assumes the Phys (#2)
and port-I/O (#269) read opacity is in place.
