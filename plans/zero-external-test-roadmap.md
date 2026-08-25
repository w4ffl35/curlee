# Curlee Zero-External-Test Roadmap

**Status:** Technical audit + architectural proposal
**Author:** Compiler engineering / formal verification audit
**Date:** 2026-08-25
**Scope:** Parser, Resolver, Type-checker, Z3 Verifier, VM, Codegen

---

## 0. Executive Summary

Curlee's current MVP implements a **contract-checking harness**, not yet a
self-proving language. The pipeline
`lex -> parse -> resolve -> type-check -> verify(Z3)` already discharges
*intra-procedural* proof obligations (call-site `requires`, return-site
`ensures`, `where` refinements) over a deliberately small Int/Bool arithmetic
fragment. What is missing — and what the Zero-External-Test vision requires —
is the machinery to express and discharge **state**, **relational**,
**resource**, and **cross-module** obligations. This document identifies the
four architectural gaps and prescribes concrete syntax and C++ lowering
strategies for each.

The audit is grounded in these observed facts about the current codebase:

| Component | File | Observed capability |
|---|---|---|
| AST | [`include/curlee/parser/ast.h`](include/curlee/parser/ast.h) | `Pred` is Int/Bool-only; no loop invariants, no state, no ghost code; `Function` carries `requires`/`ensures` only |
| Type system | [`include/curlee/types/type.h`](include/curlee/types/type.h) | `Type` has no mutability/linearity/state parameter; `TypeKind` is a flat enum; `Vec`/`Set` modeled structurally with `element_kind` |
| Type checker | [`src/types/type_check.cpp`](src/types/type_check.cpp) | Name-based `Scope` maps; capability *presence* checked, but capability *consumption/reissuance* (linearity) is not modeled |
| Resolver | [`src/resolver/resolver.cpp`](src/resolver/resolver.cpp) | Imports are parsed for *export names only*; **imported function bodies and contracts are discarded** |
| Verifier | [`src/verification/checker.cpp`](src/verification/checker.cpp) | Hoare-style walk over `facts_`; loops are treated as one unrolled iteration (no invariants); call sites check `requires` only; `ensures` are used to bind `result` at returns, never propagated to callers |
| Predicate lowering | [`src/verification/predicate_lowering.cpp`](src/verification/predicate_lowering.cpp) | Int/Bool arith + comparisons; non-linear `*` rejected unless one side is literal; no arrays, no sequences, no uninterpreted functions for user code |
| Solver | [`src/verification/solver.cpp`](src/verification/solver.cpp) | Thin `push`/`add`/`check`/`model_for` wrapper; no tactic configuration, no timeout budgets, no unsat-core extraction |
| VM | [`src/vm/vm.cpp`](src/vm/vm.cpp) | Fuel is a **runtime, per-instruction decrement** (`--fuel`); `VmProfile` records `fuel_used` *after* execution; fuel is not a compile-time proof obligation |
| CLI | [`src/cli/cli_impl.ipp`](src/cli/cli_impl.ipp) | Pipeline is linear and sequential; `verify` is a pass-fail gate; notes for extern trust boundaries |

---

## 1. Lifecycle / State Verification (Replaces State Tests)

### 1.1 Missing components in the current architecture

The type system and verifier have **no notion of object state**:

1. **Flat `TypeKind`.** [`types/type.h`](include/curlee/types/type.h:18) has
   no state parameter. `Struct` and `Enum` are identified by name alone, so
   `File<Open>` and `File<Closed>` are the same type.
2. **No state in `TypeInfo`.** [`type_check.h`](include/curlee/types/type_check.h:27)
   maps expr-id -> `Type`; nothing tracks *which* state a value is in at any
   program point.
3. **No linearity/affinity tracking.** [`type_check.cpp`](src/types/type_check.cpp)
   uses name-based `Scope` stacks; capability *presence* is checked
   ([`has_capability_value`](src/types/type_check.cpp:466)), but nothing
   prevents using a consumed capability twice or using a value after it was
   moved. The `linear_capability` example
   ([`examples/linear_capability.curlee`](examples/linear_capability.curlee))
   is explicitly marked *"not executable yet"*.
4. **Verifier ignores structs.** [`checker.cpp`](src/verification/checker.cpp:340)
   maps non-scalar types to `TypeKind::Unit` and drops them from reasoning;
   `supported_type` refuses Struct/Enum in contracts entirely.
5. **No state-transition axioms.** Even the `Phys` model has read/write as
   *uninterpreted functions with no axioms* ([`checker.cpp`](src/verification/checker.cpp:451)),
   so `write(); read()` cannot be proven to return the written value.

### 1.2 Proposed syntax additions

**Typestate states as type parameters** (surface syntax):

```curlee
// Declare a stateful resource type. States are a finite set of tokens.
state File { Open, Closed }

// `File<Open>` is a different type than `File<Closed>` at compile time.
fn read(f: File<Open>, buf: Vec<Int>) -> Int
  [ requires len(buf) > 0;
    ensures result >= 0; ]
{
  ...
}

// State transition: consumes File<Open>, produces File<Closed>.
fn close(f: File<Open>) -> File<Closed> {
  ...
}
```

**Transitions as part of the function signature** (preferred over `->` return
type only, because it also expresses in-place state change):

```curlee
fn read_advance(f: File<Open>) -> (Int, File<Open>)
  [ ensures result.0 >= 0; ]
```

**Affine/linear annotations** (for exactly-once consumption):

```curlee
fn consume(token: @linear Token) -> Unit { ... }   // token must be consumed, not copied
```

Combined with the existing `where` refinement, a stateful buffer can carry a
refined invariant:

```curlee
struct Buffer where length <= capacity { len: Int; cap: Int; ... }
```

### 1.3 C++ implementation strategy

**Parser / AST** ([`ast.h`](include/curlee/parser/ast.h)):

- Extend `TypeName` with `std::vector<std::string_view> state_args;` so
  `File<Open>` parses `File` with `state_args = ["Open"]`.
- Add `StateDecl` to the `Program` node (mirrors `EnumDecl`), and add an
  optional `state_transitions` map to `Function`:
  `std::unordered_map<std::string_view, std::string_view> transitions;`
  (param name -> target state), or a structured `TransitionClause`.

**Type checker** ([`type_check.cpp`](src/types/type_check.cpp)):

- Extend `types::Type` with `std::string_view state;` and make equality
  state-aware (mirror the existing `operator==`).
- Introduce a **flow-sensitive "typed program point" pass**: instead of a
  name->Type map, maintain `name -> (Type, State)` and update it at each
  statement. This is a classic *type-state analysis*:
  - `fn close(f: File<Open>) -> File<Closed>` updates the binding of `f`
    (or a returned fresh variable) from state `Open` to `Closed`.
  - Use-after-state-change and use-in-wrong-state become type errors, so
    `read(closed_file)` fails in **this pass**, before Z3.

**Verifier** ([`checker.cpp`](src/verification/checker.cpp)):

- Add the state lattice to `ScopeState`: `std::unordered_map<std::string_view,
  std::string_view> state_vars;` (mirrors the existing `phys_vars_`).
- For verification, encode the current state of each tracked value as a fresh
  Z3 constant per state, and add transition axioms on calls:
  `state_after(f) == Closed` when `close(f)` is called. Contracts can then
  mention state predicates: `requires state(f) == Open;`.

**Z3 obligation generated** (for a call `close(f)` on `File<Open>`):

```
(declare-sort File)                     ; opaque sort for the value
(declare-fun file_state (File) State)   ; State = { Open, Closed }
(assert (or (= (file_state f) Open) (= (file_state f) Closed)))
; call-site check for close():
(assert (not (= (file_state f) Open)))  ; negated precondition
; → UNSAT proves f is Open before the call.
```

For the affine/linear half, implement **linear capability analysis** in the
type checker: a `@linear` value must be consumed exactly once on all control
flow paths. This is a standard *liveness + use-count* dataflow over the AST,
solvable with an `unordered_map<SymbolId, int> use_counts` per scope and a
"moved" marker set; diagnostics fire on double-use or non-consumption.

---

## 2. Equivalence Checking (Replaces Regression Tests)

### 2.1 Missing components in the current architecture

1. **No relational verification.** The verifier proves a single function's
   obligations; it has no notion of comparing two functions' semantics. The
   `FunctionSig` table ([`checker.cpp`](src/verification/checker.cpp:233)) is
   used only for `requires` at call sites.
2. **No ghost variables.** There is no `result` snapshot mechanism for
   *pre-state*, so you cannot state "the refactored version returns the same
   thing the old version returned" without inlining both bodies.
3. **Calls are opaque in predicates.** [`lower_expr`](src/verification/checker.cpp:922)
   rejects `CallExpr` outright ("calls are not supported in verification
   expressions"). You cannot write `ensures result == old_func(x)`.
4. **No inlining or function uninterpretation.** There is no machinery to
   treat `old_func` as an uninterpreted function symbol (the natural SMT
   encoding for "same semantics for all inputs").
5. **No product-construction of two bodies.** Relational checks require
   *symbolic execution of both functions in lockstep*, which the single-`facts_`
   walk cannot express because two copies of the input variables must coexist.

### 2.2 Proposed syntax additions

**Ghost functions + equivalence block** (ghost code is erased at codegen):

```curlee
// legacy implementation (kept for reference; ghost = never emitted to VM/C)
ghost fn old_sum(v: Vec<Int>) -> Int {
  var total: Int = 0;
  var i: Int = 0;
  while i < len(v) {
    total = total + get(v, i);
    i = i + 1;
  }
  return total;
}

fn new_sum(v: Vec<Int>) -> Int
  [ requires len(v) >= 0;
    ensures result == old_sum(v); ]   // <-- relational clause
{
  var total: Int = 0;
  for i in 0..len(v) { total = total + get(v, i); }
  return total;
}
```

**Dedicated equivalence declaration** (syntactic sugar that lowers to the same
obligation; prefer this for explicit regression-replacement):

```curlee
equiv new_sum ≡ old_sum
  forall v: Vec<Int> where len(v) >= 0;
```

**Ghost state for pre-state snapshots** (the relational cornerstone):

```curlee
fn transfer(src: Vec<Int>, dst: Vec<Int>) -> Unit
  [ ghost let src_before = src;          // snapshot for post-state reasoning
    ensures len(dst) == len(src_before); ]
```

### 2.3 C++ implementation strategy

**Parser / AST:**

- Add `KwGhost` to [`token.h`](include/curlee/lexer/token.h) and lex
  `ghost fn`, `ghost let`.
- Add `GhostLetStmt` (snapshot binding) to the `Stmt` variant.
- Add `PredEq` — or more generally `PredRelational` — a new `Pred` variant:
  `{ std::string_view lhs_fn; std::string_view rhs_fn; std::vector<std::string_view> bound_vars; }`.
- Add top-level `EquivDecl` to `Program` with two `Function*` and an optional
  `where` guard.

**Type checker:**

- `ghost fn` bodies are type-checked normally but recorded in a separate
  `ghost_functions_` map and never handed to the emitter.
- `ghost let x = e;` introduces a *fresh constant* `x` equal to the pre-state
  value of `e`; subsequent mutations of `e` rebind `e`, not `x`.

**Verifier — the relational lowering:**

The core trick is **product construction with two disjoint variable copies**:

1. In `Verifier`, allocate a *fresh* `LoweringContext` per function side.
   For `equiv f ≡ g forall xs where W`:
   - Copy A: bind `xs` to fresh constants `x_A`.
   - Copy B: bind the same `xs` to *different* fresh constants `x_B`.
   - Add axiom `x_A == x_B` (inputs are shared).
   - Add guard `W` (the `where` guard) to *both* sides.
2. Symbolically execute body A and body B to their return expressions
   `r_A`, `r_B` (reusing the existing `lower_expr` walk, parameterized by the
   context).
3. Emit the obligation `r_A != r_B` and `solver.check()`. If SAT, the solver
   returns a counterexample input — a *compile-time regression test failure*.

This maps directly onto the existing `check_obligation` pattern
([`checker.cpp`](src/verification/checker.cpp:965)): the negation is asserted
and a model is extracted via `model_for`.

For the `ensures result == old_sum(v)` form, lower `old_sum` (and any ghost
function referenced in a predicate) as an **uninterpreted function symbol**
with the same signature, *and* add its body as a definitional axiom (or inline
it when the body is in the decidable fragment). This is exactly how
`phys_read` is already modeled as an uninterpreted function
([`checker.cpp`](src/verification/checker.cpp:471)) — the same machinery
extends to user ghost functions.

**Loop handling prerequisite:** equivalence proofs over loops require loop
invariants (Section 1/2 dependency). Until `invariant` clauses exist, restrict
the `equiv` fragment to straight-line and bounded-loop ghost bodies, and
reject unprovable shapes with the existing "out of supported fragment"
diagnostic style.

---

## 3. Resource / Fuel Bounds (Replaces Performance Tests)

### 3.1 Missing components in the current architecture

1. **Fuel is runtime-only.** [`vm.cpp`](src/vm/vm.cpp:600) decrements a
   counter per instruction; `curlee run --fuel <n>` is a *sandbox limit*, not
   a proof obligation. Nothing in the verifier reasons about fuel.
2. **No cost model.** There is no per-opcode cost. The profile records
   `steps` ([`vm.h`](include/curlee/vm/vm.h:23)), but a "step" is an
   instruction, and the verifier has no mapping from AST nodes to instruction
   counts.
3. **No WCET vocabulary.** `requires`/`ensures` are boolean predicates over
   Int/Bool; there is no way to say "this loop runs at most N times" or "this
   function consumes at most K fuel".
4. **No loop bound analysis.** The verifier's `WhileStmt` handling
   ([`checker.cpp`](src/verification/checker.cpp:1451)) checks the body once
   with the condition assumed; it never proves termination or bounds.
5. **No compile-time budget propagation.** Call-graph fuel costs are not
   summed; `main`'s declared budget cannot be checked against callee costs.

### 3.2 Proposed syntax additions

**Static fuel contract clause** (extends the existing `[ ... ]` contract
block in [`parser_impl.ipp`](src/parser/parser_impl.ipp:1014)):

```curlee
fn accumulate(v: Vec<Int>) -> Int
  [ requires len(v) <= 1000;
    ensures result >= 0;
    fuel 3 * len(v) + 10; ]      // WCET bound as an Int expression over inputs
{
  var total: Int = 0;
  var i: Int = 0;
  while i < len(v) {
    total = total + get(v, i);
    i = i + 1;
  }
  return total;
}
```

**Loop variant/bound clause** (makes the fuel proof tractable; required for
loops):

```curlee
while i < len(v)
  [ invariant 0 <= i && i <= len(v);
    decreases len(v) - i; ]       // fuel = sum over iterations of body cost
{ ... }
```

**Per-function budget keyword**:

```curlee
fn main() -> Int [ fuel 100000; ] { ... }   // top-level budget gate
```

**Cost-annotation on extern/FFI boundaries** (trusted cost, since the body is
opaque):

```curlee
extern fn __vec_get_int(v: Vec<Int>, i: Int) -> Int [ fuel 1; ];
```

### 3.3 C++ implementation strategy

**Parser / AST:**

- Add `KwFuel`, `KwInvariant`, `KwDecreases` to the lexer token set
  (mirroring `KwRequires`/`KwEnsures`/`KwWhere` in
  [`token.h`](include/curlee/lexer/token.h:40)).
- Extend `Function` with `std::optional<Pred> fuel_bound;`.
- Add `LoopClause` to `WhileStmt`:
  `{ std::vector<Pred> invariants; std::optional<Pred> decreases; }`.
- Add `std::optional<Pred> cost;` to extern functions.

**Cost model (compiler-side, single source of truth):**

Introduce `include/curlee/verification/cost_model.h` defining a per-AST-node
cost function `cost_of_stmt(const Stmt&) -> z3::expr` mirroring the emitter's
instruction encoding (`emitter_impl.ipp`). Each statement lowers to a symbolic
cost expression in terms of its inputs:

| Construct | Cost formula |
|---|---|
| `let x = e;` | `1` |
| `if c {A} else {B}` | `cost(c) + max(cost(A), cost(B))` |
| `while c [decreases D;] {B}` | `(D_0 + 1) * (cost(c) + cost(B))` (requires proving D decreases) |
| `f(args)` | `declared_fuel_of(f)` (from the call-graph table) |

**Fuel obligation lowering in the verifier:**

1. Build a **call-graph fuel table**: for each function, an entry
   `fuel(f) = declared_bound` if present, else a symbolic expression computed
   by the cost model.
2. For `main` (or any function with a `fuel` clause), emit the obligation:
   `total_cost(body) <= declared_fuel`. Negate and `check()`:
   ```
   (assert (not (<= total_cost declared_fuel)))
   ```
   SAT → the solver returns an input assignment where the WCET bound is
   violated. This is a compile-time performance regression test.
3. For loops, emit two obligations:
   - **Termination/well-foundedness:** `decreases(D) >= 0` and each iteration
     strictly decreases `D` (encode as `D_after < D_before` under the loop
     guard, with the invariant as a fact).
   - **Invariant preservation:** standard Hoare loop rule —
     prove `invariant` holds before the loop, and prove
     `invariant ∧ cond ∧ decreases ⇒ invariant'` for one iteration
     (the existing single-pass body check in
     [`checker.cpp`](src/verification/checker.cpp:1468) is the seed; add the
     invariant as an assumed fact at loop entry and as an obligation at the
     body's end).
4. **Cross-check VM profile vs static bound** (defense in depth): the CLI can
   optionally emit a warning when `VmProfile.fuel_used > declared_fuel` — the
   runtime profile becomes an *oracle* that catches cost-model drift, never
   the primary gate.

---

## 4. Component Composition (Replaces Integration Tests)

### 4.1 Missing components in the current architecture

1. **Imported contracts are discarded.** [`resolver.cpp`](src/resolver/resolver.cpp:247)
   exports only *names* of imported functions; imported bodies, `requires`,
   and `ensures` are thrown away after the export pass. The verifier therefore
   cannot reason about calls into imported modules.
2. **Single-program verification.** [`checker.cpp`](src/verification/checker.cpp:259)
   iterates `program.functions` of the *entry* program only. Cross-module call
   sites fall into the "callee not found → silent skip" path
   ([`checker.cpp`](src/verification/checker.cpp:1008)).
3. **No contract interface table.** There is no `ModuleInterface` structure
   (function signature + requires + ensures + fuel) that the verifier can
   consume uniformly for local and imported modules.
4. **`ensures` never flow to callers.** At call sites only `requires` is
   checked ([`checker.cpp`](src/verification/checker.cpp:1094)); the callee's
   `ensures` are not added as post-state facts in the caller's scope. So even
   *intra*-module, post-conditions do not compose.
5. **No directionality check.** Nothing proves the *entailment*
   `Post_A ⇒ Pre_B` across a module boundary; the pieces to express it
   (facts + obligation + model) exist, but no pass assembles them.

### 4.2 Proposed syntax additions

**Explicit interface contracts on imported modules** (optional, gives the
verifier a stable seam and lets modules be checked independently):

```curlee
import std.vec as vec;

interface vec.vec {
  fn get_int(v: Vec<Int>, i: Int) -> Int
    [ requires i >= 0 && i < len(v); ]
  fn push_int(v: Vec<Int>, x: Int) -> Unit
    [ ensures len(v) == len(old_v) + 1; ]   // ghost old_v: snapshot of v
}
```

**Call-site composition is then automatic**: `Module B` calling
`vec.get_int(v, i)` must prove `i >= 0 && i < len(v)` from `B`'s own facts,
and afterwards `B` inherits `get_int`'s `ensures` as new facts.

**Contract refinement / implementation obligation** (for when a module *must*
honor a declared interface):

```curlee
impl vec.get_int for MyVecImpl
  [ ensures result == peek(MyVecImpl, i); ]
```

### 4.3 C++ implementation strategy

**Resolver:**

- Extend `ModuleInfo` ([`resolver.cpp`](src/resolver/resolver.cpp:137)) to
  retain the *full* `Function` AST of each export (bodies optionally; at
  minimum signatures + contracts). Keep the parsed imported `Program` alive
  (the resolver already owns `imported_files_` for string lifetime) so
  `std::string_view`s stay valid.
- Produce a **merged `Program`** (or a `ModuleRegistry` of `Program`s) that the
  type checker and verifier consume instead of the entry program alone.

**Type checker / verifier — module contract table:**

- Build `ContractInterface { FunctionSig sig; std::vector<Pred> requires;
  std::vector<Pred> ensures; std::optional<Pred> fuel; }` per function, keyed
  by fully-qualified name (`module.fn`), in a new
  `include/curlee/verification/contract_table.h`.
- `Verifier::functions_` becomes a `ContractInterface` map; the existing
  `FunctionSig` ([`checker.cpp`](src/verification/checker.cpp:233)) is
  subsumed.

**Cross-module obligation (Post_A ⇒ Pre_B):**

At a call `B.f(args)` where `f` is imported from module A:

1. **Precondition check (caller→callee):** existing
   `check_call` logic — assert `¬(facts_B ⇒ Pre_f)`; SAT is a diagnostic.
   Facts now include any *inherited* post-conditions of earlier calls.
2. **Postcondition inheritance (callee→caller):** after checking
   `Pre_f`, add each `Post_f` clause (with callee `result` and any ghost
   `old_*` snapshots) to `facts_` in the caller's scope, with parameters
   substituted by the lowered argument expressions. This makes
   `Module B`'s later obligations provable from `Module A`'s post-state.
3. **Contract-chaining obligation (A's post ⇒ B's pre):** when the compiler
   can see both sides (e.g. `B` calls `A.f` and passes `f`'s result to
   `A.g`), emit the chained obligation:
   ```
   Post_f(x) ⇒ Pre_g(result_of_f)
   ```
   as a standalone `check_obligation` — the compiler-proven analogue of an
   integration test. The existing `check_obligation` helper
   ([`checker.cpp`](src/verification/checker.cpp:965)) handles this unchanged.

**Module dependency graph for soundness:**

The CLI's existing import-cycle detection (see `cli_check_import_cycle_tests`)
is the seed for a **dependency-ordered verification pass**: verify module
interfaces bottom-up, so that when module B is checked, every imported
interface is already proven. This gives the "no integration tests" property:
*boundary correctness is proven by induction over the import graph*.

---

## 5. Cross-Cutting Architectural Dependencies

The four directives are not independent. Implementation order matters:

| Gap | Blocks | Unblocked by |
|---|---|---|
| Ghost code (`ghost fn`, `ghost let`) | Equivalence (2), pre-state snapshots (4) | — |
| Loop invariants + decreases | Fuel WCET (3), Equivalence over loops (2) | — |
| Retained imported contracts | Composition (4) | — |
| `ensures` post-state inheritance | Composition (4), Equivalence (2) | — |
| State-aware `Type` | Typestates (1), pre-state snapshots (4) | — |

**Recommended milestone order:**

1. **M1 — Ghost code + `ensures` inheritance** (foundation): ghost functions,
   ghost `let` snapshots, post-state inheritance at call sites. Enables 2 and
   4 partially.
   **Status (issue #260): partially implemented.** `ghost fn`, contract-block
   `ghost let` snapshots, and call-site `ensures` inheritance landed (lexer
   `KwGhost`, `PredCall`, `GhostLetStmt`, `Function::ghost_lets`,
   ghost-function uninterpreted lowering, post-state fact injection in
   [`checker.cpp`](src/verification/checker.cpp), callee-snapshot re-lowering
   at call sites). Ghost bodies are erased from VM bytecode and freestanding C;
   ghost function calls from runnable code are rejected. Remaining for full M1:
   inlining ghost function bodies into predicates (currently uninterpreted) and
   a `var`/assignment form to exercise snapshot stability under mutation. M5
   (`equiv`) and M6 (composition) build on this foundation.
2. **M2 — Loop invariants & decreases** (foundation): enables bounded loops in
   the verifier; unblocks 3.
3. **M3 — Static fuel bounds** (directive 3): cost model + `fuel` clause +
   loop variant proof + VM-profile oracle.
4. **M4 — Typestates / linear types** (directive 1): state-aware `Type`,
   state-transition analysis, `@linear` consumption pass.
5. **M5 — Equivalence checking** (directive 2): `equiv` declarations, product
   construction, uninterpreted ghost functions.
6. **M6 — Module contract table + composition** (directive 4): retained
   imports, `ContractInterface`, chained obligations, dependency-ordered
   verification.

Each milestone keeps the "no proof, no run" gate: a program that uses a new
feature but fails its new obligations is rejected with a counterexample
diagnostic, exactly like today's `requires`/`ensures` failures.

## 6. Verification-Fragment Expansion Notes

The current decidable fragment (linear arithmetic over Int/Bool, one-literal
multiplication, opaque `Phys`/capability sorts) is the pragmatic ceiling for
Z3's `QF_LIA`/`QF_IDL` fragments. The proposals above stay within or near that
ceiling:

- **Typestates:** finite enumerated sorts + equality — trivially decidable.
- **Equivalence:** requires uninterpreted functions (ghost functions as
  `UF` symbols) → moves to `QF_UF+QF_LIA`, still decidable.
- **Fuel bounds:** pure LIA over symbolic costs — decidable.
- **Composition:** inherits whatever fragment the participating contracts use.

Any obligation escaping the supported fragment must *fail the build* (per the
existing philosophy in [`checker.cpp`](src/verification/checker.cpp:369):
"verification does not support type …"), never silently pass. The `Solver`
wrapper should add `solver_.set("timeout", …)` and unsat-core extraction for
the larger `QF_UF` queries in M5/M6 so that `Unknown` results remain
diagnosable.
