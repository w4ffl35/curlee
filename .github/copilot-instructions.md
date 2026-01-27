# Copilot Instructions: Curlee (Verification-First Language)

This file is part of the repository’s policy surface and should remain **tracked in git**.
If you notice it is being ignored, fix `.gitignore` rather than working around it.

You are working on **Curlee**, a C++23 (CMake) compiler/runtime project. Curlee is designed as an **AI-native safety harness**: code should not run unless it satisfies *declared contracts*.

## Core directives (do not violate)

1. Verification-first
   - Prefer compile-time guarantees over runtime checks.
   - If a proof obligation cannot be discharged, fail the build with a clear diagnostic.
   - Never “guess” programmer intent. Unsupported constructs must produce errors.

2. Small, decidable MVP first
   - Keep verification within the MVP scope defined in the wiki page "Verification Scope".
   - Do not expand the logic fragment (e.g., quantifiers, non-linear arithmetic) without updating the wiki and tests.

3. Modular architecture
   - Keep subsystems independent: source, spans/linemap, diagnostics, lexer, parser, resolver, types, verification, bytecode, VM, CLI.
   - Avoid cross-module tangles and global state.

4. Error empathy
   - Diagnostics must point at exact spans (byte offsets → line/col) and include actionable hints.
   - When solver finds a counterexample, render a minimal model for user-relevant variables.

5. Determinism & safety
   - VM execution must be deterministic.
   - Use resource-bounded execution (“fuel”) to prevent runaway programs.
   - Capability-based APIs: no ambient authority for file/network/system actions.

## Development workflow (strict)

Follow this loop for every meaningful change:

Branching model:
- Do day-to-day work on the `develop` branch.
- Land changes in `master` via PRs opened from `develop`.
- Keep `master` green; do not push feature work directly to `master`.

0. Issue-gated: Only work on tasks that are explicitly filed as GitHub Issues in this repo.
   - Prefer using `gh issue list/view` to pick the next task.
   - If a new task arises, file a GitHub Issue first (with clear acceptance criteria), then work it.
   - Treat Issues as the source of truth for execution order and scope.
   - Close the GitHub Issue when the work is complete; if no code changes, close it manually with a brief completion note.
1. Plan: state the smallest viable approach and what will be verified.
2. Tests first: add/adjust unit or golden tests.
3. Implement: keep changes small and focused.
4. Reflect: if tests fail, explain why before changing approach.
5. Commit: once tests/build are green, link the PR/commit in the Issue and close it (or move it to the next state).

Guardrail: **Do not write more than ~50 lines of code without running tests or building** (unless explicitly asked).

Efficiency:
- Prefer batching related terminal commands with `&&` (e.g., build + run tests + stage) to reduce tool overhead, **without** skipping verification steps.

## Repo-specific expectations

- Documentation location: user-facing documentation lives in the **separate GitHub wiki repo**.
   - Do not create `docs/` or other new doc trees in this repository unless an Issue explicitly asks for it.
   - The `wiki/` directory in this workspace is a local clone for convenience and is **gitignored**; do not edit it expecting changes to land.
   - When an Issue requires documentation updates, leave an Issue comment naming the wiki page(s) to update and keep code/tests aligned.

- Keep GitHub Issues up-to-date when you make design decisions or complete milestones.
- Never implement “extra” improvements that are not in the current Issue scope.
- Keep the wiki "Verification Scope" page in sync with implementation (scope drift is not allowed).
- Prefer golden tests for diagnostics and verification failures.
- Keep syntax unambiguous (no indentation-significant parsing).
- Any Python interoperability must be behind an explicit `unsafe` boundary and capability checks.


## Output quality

- Prefer clear naming, minimal cleverness.
- Avoid large refactors unless explicitly requested.
- When unsure, file an Issue and add a failing test, then ask for direction.
