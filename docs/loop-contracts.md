# Loop contracts (invariants and variant clauses)

Issue #261 (M2 of the Zero-External-Test roadmap) adds loop contract clauses to
`while` loops in the verifier: loop invariants and `decreases` variant
expressions.

## Syntax

A loop contract block follows the `while` condition, before the body:

```curlee
fn sum_to(n: Int) -> Int {
  let total: Int = 0;
  let i: Int = 0;
  while (i < n)
    [ invariant 0 <= i && i <= n;
      decreases n - i; ]
  {
    // ... body ...
  }
  return total;
}
```

- `invariant` clauses are Boolean predicates over in-scope variables
  (loop-carried bindings, parameters, ghost snapshots). Multiple `invariant`
  clauses are allowed.
- `decreases` is a single integer-valued expression (`D`) that must be
  well-founded (`D >= 0`) at loop entry and strictly decrease on every
  iteration (`D_after < D_before`).

## Proof obligations

For each loop with a contract block, the verifier discharges:

1. **Entry**: every invariant holds at loop entry (proved from the pre-loop
   facts; a failing invariant reports a span-precise counterexample).
2. **Preservation**: assuming `invariant && cond`, one body pass re-establishes
   every invariant (obligation-checked at body end).
3. **Variant well-foundedness**: `D >= 0` at loop entry.
4. **Variant decrease**: `D_after < D_before` under `invariant && cond`.
5. **Post-state**: after the loop, `invariant && !cond` become facts in the
   continuation, so later `requires`/`ensures` (and refinements) can use them.

All obligations reuse the standard counterexample-model diagnostic path, so a
failure reports the offending clause with a Z3 model of the violating values.

## Loops without a contract block (single-pass fallback)

Loops written without an `[ invariant ...; decreases ...; ]` block keep the
legacy single-pass behavior: the body is checked exactly once with the loop
condition assumed as a fact, and everything learned inside is then discarded.
This preserves backward compatibility (kernel-style infinite loops in `unsafe`
blocks, existing fixtures) while the invariant/variant machinery is opt-in.

The intent is that contracts in scope requiring soundness will eventually
reject unannotated loops (decide and document when that gate lands; see the
Zero-External-Test roadmap's "no proof, no run" rule).

## Current fragment limitations

The MVP language has **no assignment statements** (variables are immutable
`let` bindings), so a loop body cannot mutate a loop-carried variable. This has
two consequences for the current implementation:

- **Preservation** is trivially satisfied for invariant-only loops (the state
  never changes), and
- **`decreases`** can never be satisfied: `D_after == D_before`, so a
  non-decreasing variant fails loudly — the fail-closed behavior the
  Zero-External-Test epic mandates.

Once assignment (or affine/typestate mutation) lands in the language, the same
machinery proves real bounded-sum loops; the solver-side rename/fresh-symbol
work is the M3/M4 roadmap's prerequisite.

## Preservation semantics under immutability (documented MVP semantic)

The preservation obligation runs the body **once** with `invariant ∧ cond`
assumed as facts, then obligation-checks each invariant again at body end. Under
the current language (no assignment, no mutation of loop-carried bindings), the
invariant fact is assumed at entry and re-proven from itself, so preservation is
**vacuously satisfied** — even a body that shadows the loop variable with an
adversarial value (e.g. `let i: Int = -5;` inside the loop) verifies today. This
is the intended fail-open-for-preservation behavior of the MVP.

Two things will make preservation non-vacuous (and must be handled by M3/M4):

1. **Opaque effects.** `vec.set` / `vec.push` / `phys write` are lowered to
   uninterpreted functions today, so a `decreases`/`invariant` expression
   referencing such a value is opaque to the solver. When effectful operations
   land with real axioms, the preservation obligation will need to re-check the
   invariant against the *post-effect* state, not the assumed fact.
2. **Assignment / affine mutation.** Once loop-carried variables can be
   reassigned, `D_after` and the invariant must be checked against the mutated
   state; the current name-based Z3 constant reuse makes this sound only with
   fresh symbols per binding.

## Shadowing in a loop body (known limitation, not fixed)

The verifier binds solver variables **by name** ([`declare_var`](src/verification/checker.cpp:497)).
A `let` that shadows a loop-carried variable inside the body (`let i: Int = -5;`)
reuses the *same* Z3 constant as the outer binding, and the preservation
obligation re-proves the invariant from the invariant fact assumed at loop
entry. The net effect: shadowing in a loop body **verifies today** even when it
would break the invariant or variant. This is documented as a known limitation;
M3/M4 must introduce fresh symbols per binding to make shadowing sound. A
regression test in `tests/verification_checker_tests.cpp` locks in the current
behavior so a future change cannot silently alter it.
