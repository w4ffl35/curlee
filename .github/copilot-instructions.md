# Copilot Instructions: Curlee (Verification-First Language)

This file is part of the repository’s policy surface and should remain **tracked in git**.
If you notice it is being ignored, fix `.gitignore` rather than working around it.

You are working on **Curlee**, a C++23 (CMake) compiler/runtime project. Curlee is designed as an **AI-native safety harness**: code should not run unless it satisfies *declared contracts*.

## Wiki & Docs (single source of truth)

- User-facing docs live in the **GitHub wiki**.
- This workspace includes a local clone at `wiki/` which is a **separate git repo** and is **gitignored by the main repo**.
   - To update wiki docs: commit/push inside `wiki/` (do not expect main-repo commits to affect wiki pages).
- Do not create `docs/` or other new doc trees in the main repo unless an Issue explicitly asks.
- Keep the wiki page **Verification-Scope** in sync with implementation (scope drift is not allowed).
- When starting an Issue, identify the relevant wiki page(s) and treat them as the spec.
- If code changes alter behavior, syntax, CLI flags, verification scope, or bundle format, update the relevant wiki page(s) in the `wiki/` repo in the same work item.

## Formatting policy

- Formatting-only diffs are valid changes when they improve readability and match the project’s
   style (e.g., `clang-format` output).
- Do not revert sensible formatting just to keep a "clean diff".
- Still avoid broad, unrelated reformatting across the repo: keep formatting changes scoped to
   files you’re already touching, or to an explicit formatting-only issue.

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

Guardrail (Issue lifecycle):
- NEVER close an Issue just because it is labeled “icebox”, “post-MVP”, “backlog”, or “not scheduled”.
- NEVER close an Issue to “clean up” the issue list.
- NEVER run `gh issue close` (or otherwise close Issues) unless the user/owner explicitly instructs you to close a specific Issue number, or the Issue’s acceptance criteria are demonstrably complete.
- Only close an Issue when one of these is true:
  - The acceptance criteria are satisfied (work actually complete), OR
  - The user/owner explicitly instructs you to close it (e.g., “close as wontfix/duplicate/icebox”), OR
  - The team explicitly does not intend to pursue it and the user/owner instructs you to mark it that way.
- If an Issue is out-of-scope or unscheduled and you were about to close it: STOP and ask for confirmation, or leave it open and add a brief comment summarizing the state.

Guardrail (GitHub CLI editing):
- When using `gh issue edit` to set or replace issue bodies/comments that contain backticks, code blocks, or shell-sensitive characters, ALWAYS use `--body-file` / `--body-file -` (stdin). Do not inline complex bodies in a shell string.
1. Plan: state the smallest viable approach and what will be verified.
2. Tests first: add/adjust unit or golden tests.
3. Implement: keep changes small and focused.
4. Reflect: if tests fail, explain why before changing approach.
5. Commit: once tests/build are green, link the PR/commit in the Issue and close it (or move it to the next state).

Guardrail: **Do not write more than ~50 lines of code without running tests or building** (unless explicitly asked).

## Verification (mandatory)

- Always verify changes locally before reporting progress as “done”.
   - Run the smallest relevant build and/or test target(s) for the code you touched.
   - If you can’t run verification (missing toolchain, CI-only target, etc.), say so explicitly and explain what to run instead.
- When you comment on an Issue or produce a final summary, include the exact command(s) you ran to verify.

Efficiency:
- Prefer batching related terminal commands with `&&` (e.g., build + run tests + stage) to reduce tool overhead, **without** skipping verification steps.

## Repo-specific expectations

- When an Issue requires documentation updates, leave an Issue comment naming the wiki page(s) to update and keep code/tests aligned.

- Keep GitHub Issues up-to-date when you make design decisions or complete milestones.
- Never implement “extra” improvements that are not in the current Issue scope.
- Prefer golden tests for diagnostics and verification failures.
- Keep syntax unambiguous (no indentation-significant parsing).
- Any Python interoperability must be behind an explicit `unsafe` boundary and capability checks.


## Output quality

- Prefer clear naming, minimal cleverness.
- Avoid large refactors unless explicitly requested.
- When unsure, file an Issue and add a failing test, then ask for direction.
