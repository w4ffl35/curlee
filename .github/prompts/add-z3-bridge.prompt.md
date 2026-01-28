---
description: Add/extend Z3 integration (CMake + thin solver wrapper)
---

Goal: Implement Z3 integration for Curlee’s MVP verification engine per wiki/Verification-Scope.md.

Requirements:
- Integrate Z3 in CMake (system dependency or vendored; prefer system first).
- Add a thin wrapper API (context, assert, check, model pretty-print).
- Add golden tests that demonstrate:
  - `requires` failure at call-site with a counterexample
  - `ensures` failure at return
- If Z3 is unavailable, tests should fail with a clear message (or skip explicitly if that’s the chosen policy).

Constraints:
- Do not expand the logic fragment beyond MVP scope.
- No silent weakening: unsupported predicates must hard-error.
- Keep deterministic output ordering in diagnostics/model rendering.

Deliverables:
- New/updated files under include/curlee/verify/ and src/verify/
- CMakeLists updates
- Tests under tests/ (golden preferred)

Before coding:
- Brief plan + which obligations you’ll prove first.

After coding:
- Run the smallest relevant test suite and report results.