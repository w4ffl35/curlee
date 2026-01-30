# Copilot Instructions: Curlee (Verification-First Language)

This file is part of the repository’s policy surface and should remain **tracked in git**.
If you notice it is being ignored, fix `.gitignore` rather than working around it.

You are working on **Curlee**, a C++23 (CMake) compiler/runtime project. Curlee is an **AI-native safety harness**:

> No proof, no run.

Curlee rejects programs unless it can prove *declared contracts* within a small, decidable verification scope.

---

## Core Directives (non-negotiable)

1. **Verification-first soundness**
   - Prefer compile-time guarantees over runtime checks.
   - If an obligation cannot be discharged, compilation must fail with a clear diagnostic.
   - Never silently drop, weaken, or “best-effort” a contract/refinement.
   - Never guess programmer intent. Unsupported constructs are hard errors.

2. **MVP scope is pinned (decidable by design)**
   - The wiki page **Verification-Scope** defines the MVP scope.
   - Do not expand the logic fragment (e.g., quantifiers, non-linear arithmetic) without:
     - an Issue with acceptance criteria,
     - tests (prefer golden tests),
     - and an update to the wiki page.

3. **Modular architecture boundaries**
   - Keep subsystems independent (no cross-module tangles, no global state):
     `source`, `spans/linemap`, `diag`, `lexer`, `parser`, `resolver`, `types`, `verification`, `bytecode`, `vm`, `cli`, `bundle`, `interop`, `lsp`.
   - Don’t “reach across layers” (e.g., VM shouldn’t depend on parser; verifier shouldn’t depend on CLI).

4. **Determinism + capability safety**
   - VM execution must be deterministic.
   - Use resource-bounded execution (“fuel”) to prevent runaway programs.
   - Capability-based APIs only: no ambient authority for file/network/system actions.
   - Any host interop must be explicit and gated (see Python interop rules below).

5. **Diagnostics are part of the product**
   - Diagnostics must point at exact spans (byte offsets → line/col) and include actionable hints.
   - When the solver finds a counterexample, render a **minimal**, **stable**, user-relevant model.
   - Keep CLI and diagnostic output stable enough for golden tests.

---

## Wiki & Docs (single source of truth)

- User-facing documentation lives in the **GitHub wiki**.
- This workspace includes a local clone at `wiki/` which is a **separate git repo** and is **gitignored by the main repo**.
  - To update wiki docs: commit/push inside `wiki/` (main-repo commits do not affect wiki pages).
- Do not create `docs/` or other new doc trees in the main repo unless an Issue explicitly asks.
- Keep the wiki page **Verification-Scope** in sync with implementation (scope drift is not allowed).
- If code changes alter behavior, syntax, CLI flags, verification scope, or bundle format, update the relevant wiki page(s) in the `wiki/` repo in the same work item.

---

## Formatting Policy

- Formatting-only diffs are valid changes when they improve readability and match the project’s style (e.g., `clang-format` output).
- Do not revert sensible formatting just to keep a “clean diff”.
- Avoid broad, unrelated reformatting across the repo: keep formatting changes scoped to files you’re already touching, or to an explicit formatting-only Issue.

---

## Development Workflow (strict)

Branching model:
- Do day-to-day work on the `develop` branch.
- Land changes in `master` via PRs opened from `develop`.
- Keep `master` green; do not push feature work directly to `master`.

### Issue gating

0. **Issue-gated (code changes)**: Only work on code tasks that are explicitly filed as GitHub Issues in this repo.
   - Use `gh issue list/view` to pick the next task.
   - If a new code task arises, file an Issue first (with clear acceptance criteria), then work it.

Policy-only changes (like improving this file) do **not** require filing an Issue.

Guardrail (Issue lifecycle):
- NEVER close an Issue just because it is labeled “icebox”, “post-MVP”, “backlog”, or “not scheduled”.
- NEVER close an Issue to “clean up” the issue list.
- NEVER run `gh issue close` unless the user/owner explicitly instructs you to close a specific Issue number, or the Issue’s acceptance criteria are demonstrably complete.

Guardrail (GitHub CLI editing):
- When using `gh issue edit` for bodies/comments containing backticks, code blocks, or shell-sensitive characters, ALWAYS use `--body-file` / `--body-file -`.

### The loop (run this every time)

1. **Plan first**: state the smallest viable approach and what will be verified.
2. **Tests/build first (when relevant)**: add/adjust unit or golden tests before changing behavior.
3. **Implement**: keep changes small, focused, and scoped to the Issue.
4. **Verify**: run the smallest relevant build/test target(s) for the code you touched.
5. **Reflect**: if tests fail, explain why before changing approach.

Guardrail: **Do not write more than ~50 lines of code without running tests or building** (unless explicitly asked).

---

## Debugging Policy (required for bugfixes)

For bug reports (especially verifier/diagnostic correctness or performance issues):

- Start with a short debug plan: 1–3 hypotheses + what evidence would confirm/deny each.
- Instrument before changing behavior:
  - Prefer summary logs over per-event spam.
  - Gate noisy logs behind an env var (e.g., `CURLEE_DEBUG_*`).
  - Keep instrumentation deterministic and avoid altering timing-sensitive behavior.
- Measure, then fix: don’t make speculative behavior changes without evidence.

---

## Verification-Specific Rules

When working in `src/verification/` (or anything that changes what is provable):

- Treat **wiki/Verification-Scope.md** as the spec for what is in/out of scope.
- Out-of-scope predicates/constructs must be **hard errors** with precise spans (never silently ignored).
- Preserve the “small decidable fragment” (no quantifiers; avoid non-linear arithmetic).
- Any change that affects solver queries, predicate lowering, or model rendering must come with tests
  (prefer the existing golden test harnesses).

---

## Build & Test (use CMake presets)

Prefer CMake presets over invoking Ninja directly.

- Configure: `cmake --preset linux-debug`
- Build: `cmake --build --preset linux-debug`
- Test: `ctest --preset linux-debug --output-on-failure`

Testing discipline:
- Prefer the smallest relevant test selection first (e.g., `ctest --preset linux-debug -R <regex> --output-on-failure`) before running the full suite.
- If you add/adjust diagnostics, prefer/extend the existing golden tests rather than asserting on unstable text in unit tests.

When reporting work as “done”, include the exact command(s) you ran to verify.

---

## Repo-Specific Expectations

- Prefer golden tests for diagnostics and verification failures.
- Keep syntax unambiguous (no indentation-significant parsing).
- Any Python interoperability must be behind an explicit `unsafe` boundary and capability checks.
- Avoid large refactors unless explicitly requested or required by the Issue.
- Do not implement “extra” improvements outside the current Issue scope.

---

## Common Failure Modes (avoid these)

- **Skipping the Issue gate**: if it’s a code change and there’s no Issue, stop and file one (unless the user explicitly directs otherwise).
- **Scope creep in verification**: do not “just support” a new predicate/operator/encoding; add an Issue + tests + update **Verification-Scope**.
- **Silent weakening**: never drop a contract clause to “get it compiling”; unsupported constructs are hard errors with precise spans.
- **Unstable diagnostics**: don’t churn output formatting casually; golden tests depend on stability.
- **Non-deterministic debugging**: don’t add timing- or ordering-dependent logs; gate debug output behind `CURLEE_DEBUG_*`.
- **Unfocused verification**: don’t claim “verified” without running build/tests (or explicitly stating why you couldn’t and what to run).

---

## Output Quality

- Prefer clear naming and minimal cleverness.
- If the spec is unclear or missing: stop, identify the relevant wiki page(s), and ask for direction (or file an Issue if needed).
