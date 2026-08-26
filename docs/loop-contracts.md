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
condition assumed as a fact, and everything learned inside is then discarded —
with one exception: names the body **assigns** (loop-carried) are rebound to
fresh *unconstrained* symbols in the continuation, because without an invariant
their post-loop values are unknown (letting the pre-loop `let` facts leak would
verify false postconditions vacuously). Loops that do not assign keep the
pre-loop bindings, preserving backward compatibility (kernel-style infinite
loops in `unsafe` blocks, existing fixtures) while the invariant/variant
machinery is opt-in.

The intent is that contracts in scope requiring soundness will eventually
reject unannotated loops (decide and document when that gate lands; see the
Zero-External-Test roadmap's "no proof, no run" rule).

## Assignment and loop-carried mutation (issue #268)

Since issue #268, the language has **assignment statements** (`x = expr;`
reassigns an existing local `let` binding). This is the mechanism that makes
loop-carried mutation expressible:

```curlee
fn sum_to(n: Int) -> Int {
  let total: Int = 0;
  let i: Int = 0;
  while (i < n)
    [ invariant 0 <= i && i <= n && total >= 0;
      decreases n - i; ]
  {
    i = i + 1;
    total = total + i;
  }
  return total;
}
```

Here `i` (and `total`) are *loop-carried*: each iteration reassigns them, so the
`decreases n - i` variant strictly decreases every iteration and the invariant
is re-checked against the post-mutation state. Bounded sums, cursor walks
(`p += (size + 7) & ~7`), and driver state machines are all expressible this
way.

**Verifier semantics.** Each declaration *and* each assignment binds the name to
a **fresh solver symbol** (`name@N`), constrained to the RHS value
([`fresh_symbol`](src/verification/checker.cpp:559) /
[`assign_var`](src/verification/checker.cpp:588)). The pre-mutation symbol stays
in the solver's history, so facts referencing it (a loop `decreases` variant
lowered before the assignment) remain valid and the post-mutation re-check is
non-vacuous. Assignment targets are limited to local `let` bindings: parameters
and ghost snapshots are read-only (call-site `ensures` inheritance assumes
callee parameters keep their entry values; ghost snapshots freeze a pre-state
value).

**Preservation against the post-mutation state.** The preservation obligation
runs the body **once** with `invariant && cond` assumed as facts, then
obligation-checks each invariant and the variant at body end. The loop-carried
variables — the names the body *assigns*, collected by
[`collect_assigned_names`](src/verification/checker.cpp:1950) — are first
abstracted: each is rebound to a fresh *unconstrained* symbol before the
invariant/cond facts are assumed, so the proof quantifies over every reachable
iteration state (the standard partial-correctness induction hypothesis) rather
than the specific entry values. A body that mutates a loop-carried variable in a
way that breaks the invariant therefore **fails loudly** with a span-precise
counterexample model. Because every `let` also binds a fresh symbol, a shadowing
`let i: Int = -5;` inside the body is a *distinct* constant: the invariant is
re-proven against the shadowed value, so shadowing can no longer make
preservation vacuous.

**Post-state carries the loop-carried bindings.** After the loop, the
continuation sees the loop-carried names rebound to their **post-body** symbols
(the values after the final iteration), constrained by `invariant && !cond`
lowered against those same post-loop symbols. The pre-loop `let` facts do *not*
leak: `let total = 0; while (i < n) [...] { total = total + 1; } return total;`
proves `ensures result == n` but rejects `ensures result == 0` (the continuation
no longer sees the entry `total == 0`). Names shadowed by a `let` inside the
body are excluded — their assignments target the loop-local shadow, and the
outer binding is unchanged by the loop. Loops without a contract block
(legacy single-pass mode) rebind names the body assigns to fresh *unconstrained*
symbols, since there is no invariant to constrain the post-loop value.

**Branch joins.** An `if`/`else` (or `match`) that reassigns a name in a branch
joins the branch post-states into the continuation: the name is rebound to a
fresh symbol constrained to be one of the branch-end values (falling back to
the pre-branch value for a branch that leaves it untouched, or for the if's
not-taken path). The continuation therefore proves disjunctive facts
(`ensures result == 0 || result == 1` after `if (c) { x = 1; }`) but cannot
assume a specific branch value. Branch-local facts that pin the branch-end
symbols are retained; the branch condition assumption itself is not (the
continuation must not assume the branch was taken).

**Fuel.** The static cost model lowers a loop's `decreases` variant against the
PRE-loop bindings (recorded at loop entry), so the fuel formula
`(D_0 + 1) * (cost(c) + cost(B))` is tied to the real entry value of the variant
— see `docs/fuel-contracts.md`.

## Remaining limitations

- **Opaque effects.** `vec.set` / `vec.push` / `phys write` are lowered to
  uninterpreted functions, so a `decreases`/`invariant` expression referencing
  such a value is opaque to the solver. Effectful operations with real axioms
  (which would require re-checking invariants against a *post-effect* state)
  are future work.
- **No global mutable state / references.** Mutation is local to a function
  body via assignment; affine/linear typing is tracked separately (issue #263).
