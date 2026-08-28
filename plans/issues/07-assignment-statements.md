# Title: Assignment statements for mutable state (`x = expr;`) — loop-carried mutation, driver state machines

Issue: https://github.com/w4ffl35/curlee/issues/268

## Problem statement / motivation

Curlee has **no assignment statements** — variables are immutable `let` bindings
([`ast.h`](include/curlee/parser/ast.h:216) has `LetStmt`/`ReturnStmt`/`IfStmt`/`WhileStmt`/
`MatchStmt`/`UnsafeStmt`/`BlockStmt`, no `AssignStmt`). This is the single largest blocker to
writing real drivers and state machines in Curlee.

The JOE OS kernel (w4ffl35/joeos) hits this directly. Its drivers are ~3,400 lines of C because
every stateful component cannot be expressed in Curlee:

- **`fb.c`** — framebuffer blitter: `fb_draw_target`, `ring_slot`, `tool_head/count`,
  `loop_frame` are all reassigned every frame.
- **`mb2.c`** — multiboot2 tag parser: walks a list with a mutable cursor
  (`p += (size+7)&~7U`).
- **`net_stack.c`** — TCP/IP stack: `ns_state`, sequence numbers, response-parser flags mutate
  across polls.
- **`virtio_net.c`** — ring DMA: mutable ring indices.

The docs are explicit that this is the gap: [`docs/loop-contracts.md`](docs/loop-contracts.md:62)
("no assignment statements ... a `decreases` clause can never be satisfied"),
[`docs/fuel-contracts.md`](docs/fuel-contracts.md:96) ("no assignment statements ... the
loop-variant cost formula cannot yet be exercised"). Loop invariants (#261) and static fuel bounds
(#262) shipped but are **inert until mutation lands** — the roadmap's own M3/M4 prerequisite.

## Design

### 1. Lexer + parser: `AssignStmt`

- Add a new statement node `AssignStmt` to [`ast.h`](include/curlee/parser/ast.h:331) with
  `std::string_view name; Expr value; Span span;` and add it to the `Stmt` variant.
- In [`parser.cpp`](src/parser/parser.cpp:1383) `parse_statement`: when a statement begins with an
  `Identifier` followed by `Equal` (i.e. not `KwLet`, not `KwGhost`, not a control keyword), parse
  `name = expr;`. The `Equal` token already exists
  ([`token.h`](include/curlee/lexer/token.h:73)); the lexer needs no change.
- The LHS must be a bare identifier (a `NameExpr`). Reject compound LHS forms (`a.b = ...`,
  `f(x) = ...`) with a span-precise "assignment target must be a local variable" diagnostic.
- Add `dump_stmt_node(const AssignStmt&)` for the AST dump path
  ([`parser.cpp`](src/parser/parser.cpp:2528)).

### 2. Resolver: reassignment requires an existing binding

- In the resolver, `AssignStmt` requires `name` to be bound in the current scope to a **scalar
  `let` binding** (Int/Bool, or the freestanding U8/U16/U32/U64 kinds). Assigning to a parameter
  is allowed (parameters are scalar bindings); assigning to a `ghost let` snapshot or a
  Phys binding is rejected.
- Unknown name → "cannot assign to undeclared variable 'x'" (mirror the existing unresolved-name
  diagnostic path).
- Assigning a value of a different type than the binding → type error via the existing binary/let
  type-mismatch path in [`type_check.cpp`](src/types/type_check.cpp:823).

### 3. Verifier: fresh Z3 symbols per binding (the documented M3/M4 soundness gap)

Today [`declare_var`](src/verification/checker.cpp:531) binds solver variables **by name** via
`insert_or_assign`; the docs' "name-based constant reuse makes this sound only with fresh symbols"
gap ([`docs/loop-contracts.md`](docs/loop-contracts.md:93)) is exactly what assignment must close.

- **Fresh symbol on assignment.** When an `AssignStmt` mutates a scalar binding, declare a **fresh**
  Z3 constant for that name (e.g. name-mangled `x@N`), `insert_or_assign` it into the lowering
  context, and add the equality fact `x@N == lowered(value)`. Subsequent reads of `x` (in
  predicates, refinements, variant expressions, ghost snapshots) resolve to the *current* symbol.
- **Pre-state preservation for ghost snapshots.** A `ghost let g = x;` before the assignment must
  keep referencing the pre-assignment symbol (ghost snapshots already freeze a fresh constant with
  an equality fact — [`checker.cpp`](src/verification/checker.cpp:1574) — so this works unchanged as
  long as the snapshot captures the symbol *at snapshot time*).
- **Loop semantics (issue #261 machinery).** In [`check_stmt_node(const WhileStmt&)`]
  (src/verification/checker.cpp:1849):
  1. **Entry**: invariants proved against pre-loop facts (unchanged).
  2. **Preservation**: assuming `invariant && cond`, run the body once with assignment producing
     fresh symbols; obligation-check each invariant against the **post-mutation** state (this closes
     the vacuous-preservation gap documented in [`docs/loop-contracts.md`](docs/loop-contracts.md:76)).
  3. **Variant decrease**: `D_after < D_before` must be re-lowered **after** the body's mutations
     (fresh symbols), so a genuine `decreases n - i;` with `i = i + 1;` becomes provable.
  4. **Post-state**: after the loop, `invariant && !cond` facts reference the loop-exit symbols
     (fresh per last mutation) so the continuation sees the mutated values.
- **Cost model (`fuel`)** — [`cost_model.cpp`](src/verification/cost_model.cpp:204): an
  `AssignStmt` costs `1 + cost(expr)` (one VM `StoreLocal`), matching the existing `let` model. The
  `(D_0 + 1) * (cost(c) + cost(B))` loop formula ([`docs/fuel-contracts.md`](docs/fuel-contracts.md:45))
  becomes exercisable by a genuinely bounded loop once variants can strictly decrease.
- **Shadowing rejection stays.** The fail-closed "variant references a name shadowed in the loop
  body" check ([`cost_model.cpp`](src/verification/cost_model.cpp:220)) is *not* relaxed: a `let`
  rebind in a loop body is still a shadow, distinct from an `AssignStmt` mutation of the loop-carried
  binding (which the solver now tracks via fresh symbols).

### 4. Codegen: plain C assignment (freestanding) and VM `StoreLocal`

- **Freestanding C** ([`codegen.cpp`](src/codegen/codegen.cpp)): emit the C statement
  `<name> = <expr>;` where `<name>` is the existing local's C identifier (the freestanding target
  already tracks locals in scope). Assigning to a Phys variable is rejected at the front-end (see
  §2), so no Phys-deref assignment is emitted.
- **VM bytecode** ([`emitter.cpp`](src/compiler/emitter.cpp:415)): emit `StoreLocal` on the
  binding's existing slot — the VM already implements `StoreLocal` as an in-place local write
  ([`vm.cpp`](src/vm/vm.cpp:645)) and `let` already uses it, so no new opcode is required. The
  emitter must look up the *existing* slot for `name` (not allocate a new one as `let` does).
- **Cost**: `1 + cost(expr)` as above, consistent with `StoreLocal` consuming one fuel unit at
  runtime ([`docs/fuel-contracts.md`](docs/fuel-contracts.md:51)).

## MVP scope

In-scope:

- `x = expr;` reassignment of an existing scalar binding inside function bodies (hosted VM + freestanding).
- Mutating a loop-carried variable inside a `while` body so `decreases` variants become provable.
- Verifier: fresh Z3 symbols per binding (closing the name-reuse gap), invariant re-check against
  post-mutation state, variant decrease after mutation.
- Codegen: plain C assignment + VM `StoreLocal` on the existing slot.
- Parser/resolver/type-check/verifier/codegen tests + golden diagnostics.

Out-of-scope (explicit):

- Affine/linear typing and typestate tracking (tracked separately in #263).
- Global mutable state and references.
- Compound assignment operators (`+=`, `-=`, ...) and `++`/`--`.
- Assignment to struct fields (`a.b = ...`) or enum payloads.
- Let-rebinding relaxation: `let x = ...;` remains immutable (shadowing keeps its current semantics).

## Acceptance criteria

- [ ] `let x: Int = 0; x = x + 1;` parses, type-checks, verifies, and codegens (VM + freestanding).
- [ ] A `while` loop with a `decreases` variant that mutates the loop-carried variable verifies
      (the bounded-sum case from [`docs/loop-contracts.md`](docs/loop-contracts.md:12) — e.g.
      `fn sum_to(n: Int) -> Int` with `total = total + i; i = i + 1;`).
- [ ] Loop invariants re-check against the **post-mutation** state, not vacuously from the assumed
      fact (a loop that breaks its own invariant via assignment now fails).
- [ ] The `fuel` clause's loop formula `(D_0 + 1) * (cost(c) + cost(B))` is exercisable by a
      genuinely bounded loop with a verified `decreases` variant and `fuel` bound.
- [ ] Freestanding codegen emits a plain C assignment; the VM executes the reassignment
      (`curlee run` on a bounded-sum program prints the correct result).
- [ ] `ghost let` snapshots still reference pre-assignment values (assignment does not retroactively
      change snapshot facts).
- [ ] Existing verification/loop/fuel suite stays green (no regressions in #261/#262 behavior);
      the previously-documented vacuous-preservation regression test in
      `tests/verification_checker_tests.cpp` is updated only where its behavior legitimately
      changes (a loop that mutates to break its invariant now fails).

## Verification plan

- Parser/resolver/type-check unit tests + golden diagnostics:
  - `x = expr;` positive; assign-to-undeclared; assign-with-wrong-type; `a.b = ...` rejected;
    assign-to-`ghost let`/Phys rejected.
- Verifier fixtures (`tests/fixtures/`):
  - `check_assign_bounded_sum.curlee` — `while` + `decreases` + mutation (positive, must verify).
  - `check_assign_breaks_invariant.curlee` — assignment that breaks an invariant (negative, must fail).
  - `check_assign_ghost_snapshot.curlee` — snapshot before/after mutation (positive).
  - `check_assign_fuel_loop.curlee` — `fuel` clause on a genuinely bounded loop (positive).
- Codegen fixture (`tests/codegen/assign.curlee` + `.expected`) asserting the emitted C assignment
  and the VM `StoreLocal` sequence.
- Run `ctest --preset linux-debug --output-on-failure` and `bash scripts/smoke.sh`.

## Relevant wiki page(s)

- `wiki/Language-Syntax` — **must update**: add `x = expr;` to the supported statement fragment.
- `wiki/Stability-and-Supported-Fragment` — **must update**: remove "no assignment" from the
  documented limitations; document the mutable-binding rule (reassignment only of scalar `let`
  bindings; `let` itself stays immutable).
- `wiki/Verification-Scope` — **must update**: fresh-symbol-per-mutation semantics; loop
  invariants/variants re-checked against post-mutation state; ghost snapshots freeze pre-state.
- Keep in sync per [`.github/copilot-instructions.md`](.github/copilot-instructions.md:53).

## Dependency note

Depends on the loop-contract machinery from #261 and the fuel cost model from #262 (both shipped);
this issue makes their bounded-loop semantics exercisable. Affine/typestate mutation is #263
(out of scope here).
