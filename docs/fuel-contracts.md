# Static fuel bounds (WCET contract clauses)

Issue #262 (M3 of the Zero-External-Test roadmap) adds compile-time, solver-checked
Worst-Case Execution Time (WCET) bounds as `fuel` contract clauses. A `fuel`
bound turns "this function consumes at most K fuel" into a theorem the Z3
verifier must prove, rather than a runtime observation.

## Syntax

A `fuel` clause is an Int-valued expression over the function's inputs, written
inside the existing contract block:

```curlee
fn accumulate(n: Int) -> Int
  [ requires n >= 0;
    ensures result >= 0;
    fuel 3 * n + 10; ]      // WCET bound as an Int expression over inputs
{
  // ... body ...
}
```

- At most one `fuel` clause per function (a duplicate is a parse error).
- `fuel` may appear on any function, including `extern fn` declarations:

```curlee
extern fn __memcpy(dst: Int, src: Int, n: Int) -> Int [ fuel 10; ];
```

For an **extern** function the bound is a *trusted* annotation of the opaque
body's cost: it is never verified against a body (there is none) and is used as
the callee's cost at every call site. An extern without a `fuel` annotation
fails closed when its cost is needed.

## Cost model

New `include/curlee/verification/cost_model.h` + `src/verification/cost_model.cpp`
lower each statement/expression to a symbolic Z3 Int cost expression in terms of
the function's inputs:

| Construct | Cost formula |
|---|---|
| `let x = e;` | `1 + cost(e)` |
| `if c {A} else {B}` | `cost(c) + max(cost(A), cost(B))` |
| `while c [decreases D;] {B}` | `(D_0 + 1) * (cost(c) + cost(B))` |
| `f(args)` | `declared_fuel_of(f)` (call-graph table) + call-site overhead |
| `ghost let x = e;` | `0` (erased from codegen) |
| `return e;` | `1 + cost(e)` |
| block / expression stmt | sum of statement costs / `cost(e)` |

The model mirrors the emitter's instruction encoding: each VM instruction costs
one fuel unit at runtime, so the static cost is the *instruction count*, and the
model is the compiler-side single source of truth.

## Verifier lowering

1. A **call-graph fuel table** records every function's declared `fuel` bound.
   A callee's bound is an expression over its parameters; at each call site the
   cost model substitutes the caller's argument expressions, so an input-
   dependent bound such as `fuel 3 * n + 10;` is evaluated against the actual
   arguments.
2. For each non-extern function with a `fuel` clause, the verifier emits the
   obligation `total_cost(body) <= declared_fuel` through the standard solver
   path. A SAT result is a WCET-violating input assignment, reported with a
   span-precise counterexample model (mirroring `requires`/`ensures`).
3. **Fail closed.** A loop without a provable `decreases` variant makes the
   `fuel` obligation reject as unsupported ("fuel cost requires a 'decreases'
   variant on every loop"). A call to a callee with no declared `fuel` bound
   rejects too ("no fuel cost computable for callee …"). This is consistent
   with the roadmap's "no proof, no run" gate: an unbounded construct never
   silently passes.
4. **Extern costs are trusted.** An extern `fuel` annotation is never checked
   against a body; it is honored as the call-site cost. Missing extern cost →
   fail closed.

## CLI profile drift oracle (defense in depth)

`curlee run --profile` compares the runtime `VmProfile.fuel_used` against the
declared static bound on `main` (when the bound is a plain integer literal) and
emits an advisory warning when the runtime exceeds it:

```
curlee warning: runtime fuel_used (200) exceeds the declared static fuel bound (100) on 'main'; the cost model may be too optimistic
```

The warning is **non-gating**: the static proof is the primary gate, and the
runtime profile only surfaces cost-model drift. Note that the VM currently
reports `fuel_used` only on the out-of-fuel failure path. Because the cost model
is now a sound over-approximation, a statically-verified program cannot exceed
its declared bound at runtime under the current language (loopless bodies
report `fuel_used == 0` on success); the oracle remains as a defense-in-depth
signal for when the cost model can drift.

## Current fragment limitations

- A loop's `decreases` clause is satisfiable for genuinely bounded loops since
  issue #268 landed assignment (loop-carried variables can be mutated), so the
  loop-variant cost formula `(D_0 + 1) * (cost(c) + cost(B))` is exercisable:
  `D_0` is the variant's value at loop ENTRY (the verifier records the
  pre-loop lowering context at each `while`, so a body assignment that rebinds
  a loop-carried name to a fresh symbol cannot leak a post-mutation value into
  `D_0`). A `fuel`-annotated function containing a loop *without* `decreases`
  still fails closed — the intended behavior.
- The cost model is a **sound over-approximation**: every expression node
  (including calls nested inside operands, conditions, initializers, and
  arguments) is charged its callee fuel, so the static cost never under-charges.
- A loop whose body shadows (rebinds) a name that the `decreases` variant
  references is rejected fail-closed: the termination and fuel bounds would
  both be vacuous.
- Recursive (or mutually recursive) calls fail closed with a clear diagnostic —
  re-charging a declared bound per nesting site is not a sound bound for
  unbounded recursion.
- A `fuel` clause (and its call-argument substitution) is lowered against the
  function's parameters + ghost snapshots only; it cannot silently depend on
  local `let` bindings.

Backward compatibility: programs without `fuel` clauses are completely
unaffected — no fuel obligation is emitted, and the runtime `--fuel` sandbox
behaves exactly as before.

## Known limitations and future work

The following are tracked as future work in
[issue #267](https://github.com/w4ffl35/curlee/issues/267); they are not
defects in the shipped M3 behavior.

- **Mutual-recursion diagnostics.** Self-recursion is detected at the call
  site (`call_stack_` in `cost_model.cpp`) and fails closed with a clear
  `recursive call: fuel cost is not bounded` diagnostic. *Mutual* recursion
  (`a -> b -> a`) is not yet detected as a cycle: because the cost model
  charges a callee's *declared bound* rather than walking its body, the cycle
  is charged one level deep. In practice the whole program still fails closed
  (each cycle member's own obligation includes the other member's declared
  bound, so a cycle can never verify), but the diagnostic is the generic
  "fuel bound may be exceeded" rather than the recursion diagnostic. Future
  hardening: walk the fuel-table call graph for cycles during signature
  collection and emit the recursion diagnostic for mutual cycles too.
- **Drift oracle fires only on the out-of-fuel path.** `VmProfile.fuel_used`
  is populated only when the VM exhausts the `--fuel` sandbox limit, so a run
  that completes successfully reports `fuel_used == 0` and can never trigger
  the advisory. The oracle is defense-in-depth; it becomes a live signal when
  the VM reports consumed fuel on successful runs too.
- **Fuel bounds are Int-expressions over inputs.** The bound itself is not
  a *proof of termination* — termination comes from `decreases`. A `fuel`
  clause on a non-terminating function is meaningless; the fail-closed paths
  above (loops without `decreases`, recursion) are what keep it sound.

## See also

- `docs/loop-contracts.md` — loop invariants and `decreases` variants (#261).
- `plans/zero-external-test-roadmap.md` §3 — the fuel/WCET roadmap section.
