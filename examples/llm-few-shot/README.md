# LLM few-shot example set

A curated teaching set for issue #238 — meant to be used alongside
`docs/llm-system-prompt.md` (#237) as few-shot examples when prompting an
LLM to write Curlee, or as a training set for fine-tuning. Every example
here was verified directly against a real build before being committed.

Unlike `tests/correct_samples/` (fuzz-generated nested arithmetic, no
contracts, no structs, no enums — a regression corpus, not a teaching
set), every file here demonstrates one real, distinct language feature or
pattern, with a header comment explaining what it shows and why.

## How to run

Most examples run directly:

```sh
curlee run examples/llm-few-shot/01_array_fill_assign.curlee
```

Two need special invocation (documented in their own header comments):

- `10_extern_fn.curlee` — `extern fn` has no VM execution support; use
  `curlee check` only, or `curlee build --link` for a freestanding target.
- `11_capability.curlee` — needs the capability granted explicitly:
  `curlee run --cap io.stdout examples/llm-few-shot/11_capability.curlee`

The two `15_`/`16_` files are **rejected programs** — `curlee check` on
them exits non-zero by design. Their header comments include the exact
real diagnostic output.

## Contents

| File | Demonstrates |
|---|---|
| `01_array_fill_assign.curlee` | Fixed-size array, fill-then-assign (array literals are repeat-fill only) |
| `02_loop_contract.curlee` | `while` loop with `invariant`/`decreases` |
| `03_fn_contract.curlee` | `requires`/`ensures`, nested if/else over early returns |
| `04_nested_if_else.curlee` | Control flow (no `else if` in the grammar) |
| `05_modulo.curlee` | `%` operator |
| `06_u8_array.curlee` | `U8` array element type |
| `07_refinement_where.curlee` | Binding refinement (`let x: Int where ... = ...`) |
| `08_struct.curlee` | `struct` declaration, construction, field access |
| `09_enum.curlee` | `enum`, `variant_is`, `variant_unwrap` |
| `10_extern_fn.curlee` | `extern fn` (no body, external linkage) |
| `11_capability.curlee` | Capability parameter (`cap io.stdout`), no ambient authority |
| `12_bitwise.curlee` | Bitwise ops and shifts (`& \| ^ ~ << >>`) |
| `13_byte_lookup_table.curlee` | A realistic fixed-byte-sequence accessor |
| `14_packed_value.curlee` | A realistic packed-value function with a range contract |
| `15_rejected_array_oob.curlee` | **Rejected**: constant out-of-bounds array access |
| `16_rejected_ensures.curlee` | **Rejected**: an unsatisfiable `ensures` clause (Z3 counterexample) |

## Verification

Every example (including the two rejected ones, checked against their
documented error text) was run directly against a real build before this
set was committed — see the PR this landed in for the exact commands.
