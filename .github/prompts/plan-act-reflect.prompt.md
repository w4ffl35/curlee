---
description: Plan → Tests → Implement → Reflect loop for Curlee
---

You are working in the Curlee C++23 compiler/runtime repo.

Task: <$TASK>

Constraints (do not violate):
- Verification-first: never guess intent; unsupported constructs must error.
- Keep changes small; do not write > ~50 LOC without running tests/build.
- Prefer golden tests for diagnostics/verification failures.
- Maintain modular boundaries (source/diag/lexer/parser/resolver/types/verify/vm/cli).

Process:
1) Propose a short plan (3–6 steps) and identify what you will verify.
2) Write/adjust tests first.
3) Implement minimal code changes.
4) Run tests/build.
5) Reflect: if anything fails, explain why before changing approach.

Output:
- List modified files.
- Provide the exact command(s) to run to verify locally.