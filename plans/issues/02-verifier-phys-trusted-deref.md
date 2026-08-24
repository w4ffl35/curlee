# Title: Verifier: trusted/opaque `Phys<T>` deref semantics (uninterpreted Z3, no OOB obligation)

## Problem statement / motivation

Issue [Front-end support for `Phys<T>`](#) adds the syntax and type-checking for physical memory
access. This issue defines the **verification semantics**. The core soundness question is: how does
"no proof, no run" interact with a raw memory read/write?

The answer must be honest and documented: Curlee cannot prove facts about the *contents* of MMIO
registers or a framebuffer (the values come from hardware, not from the program). What Curlee *can* do:

- **Constrain the address**: the address is a compile-time literal, so there is no data-dependent
  out-of-bounds pointer math to reason about (the front-end enforces this; the verifier enforces it again).
- **Mark the access as trusted**: the deref is an explicit, capability-gated boundary, not an ambient
  operation.
- **Model loaded values as opaque symbols**: `read()` returns an unknown value; contracts therefore
  cannot depend on it, and no proof obligation is generated that would force the program to "prove" a
  value it cannot know.

This is a deliberate, documented exception to the "no proof, no run" rule: the *address* is proven
constant, the *access* is capability-gated, and the *data* is explicitly unprovable (so nothing is
silently assumed — it is structurally modeled).

## Design

In the Z3 lowering ([`checker_impl.ipp`](src/verification/checker_impl.ipp:169), predicates in
[`predicate_lowering.h`](include/curlee/verification/predicate_lowering.h)):

- `Phys<T>` lowers to an **opaque Z3 sort** (one fresh sort per `(element kind)`).
- `phys<U>(addr_literal)` lowers to a constant of that sort.
- `read()` lowers to an **uninterpreted function** `phys_read : Phys<T> -> T`.
- `write(v)` lowers to an uninterpreted function `phys_write : Phys<T> x T -> Unit`.
- **No axioms** are added: the solver cannot derive `read()` values, cannot compare two reads, and
  cannot reason about the written value afterward. This is what makes the model sound-by-omission:
  nothing can be proven *about* the data, and nothing needs to be.
- **Second-line enforcement**: if the verifier ever encounters a `Phys` expression whose address is not
  a literal (should be impossible after the front-end, but defense in depth per
  [copilot-instructions.md](.github/copilot-instructions.md:14)), emit a hard diagnostic:
  `"physical address must be a constant literal"` and fail the build.

Contract interaction (documented limitation, out of MVP scope):

- `requires`/`ensures` clauses that mention a `read()` result will fail to prove in general (symbolic
  value) — the diagnostic should include a note explaining that MMIO reads are opaque and cannot be
  contracted. This is a **documented restriction**, not a silent pass.

## MVP scope

In-scope:

- Z3 sort + uninterpreted read/write lowering for `Phys<T>`.
- Address-literal re-validation inside the verifier (hard error on non-literal).
- Note-attached diagnostic when a contract mentions a `read()` result (clear "cannot prove opaque value"
  message).
- Keep the existing int/bool-only contract fragment unchanged otherwise.

Out-of-scope (explicit):

- Proving bounds on MMIO regions, read-after-write tracking, volatile semantics in the solver,
  bytecode/VM execution of Phys ops (freestanding-only, later issues).
- Any change to the int/bool MVP logic fragment (scope is pinned by
  [copilot-instructions.md](.github/copilot-instructions.md:22)).

## Acceptance criteria

- [ ] `curlee check` **passes** on the positive fixtures from the front-end issue (framebuffer write +
      PCIe register read inside `unsafe` + `cap phys.mem`), with zero proof obligations generated for
      the derefs.
- [ ] A fixture with `requires v > 0` where `v = reg.read()` produces a diagnostic with a clear note
      ("MMIO read is opaque; cannot prove this contract") — build fails, not silently passes.
- [ ] A deliberately hand-crafted non-literal address reaching the verifier produces the hard
      "physical address must be a constant literal" diagnostic (golden).
- [ ] Existing verification suite stays green (no regressions in int/bool obligation handling).

## Verification plan

- Unit tests in `tests/types_extra_tests.ipp` / `tests/verification` fixtures:
  - `check_phys_framebuffer.curlee` (positive — must pass)
  - `check_phys_read_contract.curlee` (negative — opaque-value note)
  - `check_phys_nonliteral_addr.curlee` (negative — constant-literal error)
- Golden diagnostics under `tests/diagnostics/`; run `ctest --preset linux-debug --output-on-failure`.

## Relevant wiki page(s)

- `wiki/Verification-Scope` — **must update**: add the trusted-deref exception, the opaque-value rule,
  and the "cannot contract an MMIO read" restriction. Keep in sync per
  [copilot-instructions.md](.github/copilot-instructions.md:53).
- `wiki/Language-Syntax` — cross-link the `Phys<T>` rules with the verification semantics.
