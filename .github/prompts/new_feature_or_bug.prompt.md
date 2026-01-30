```prompt
---
description: File a new feature/bug issue (with acceptance criteria) and then implement after confirmation
---

You are working in the Curlee C++23 compiler/runtime repo.

Request: <$TASK>

## Goals

- Turn the request into a high-quality GitHub Issue (clear scope + acceptance criteria + test expectations).
- Ask the user to confirm labels/priority before implementation.
- After confirmation, implement using a tests-first plan-act-reflect loop.

## Constraints

- You MUST follow [../copilot-instructions.md](../copilot-instructions.md).
- Issue-gated: do not start implementation until an Issue exists.
- Never close issues unless the user explicitly instructs you to close a specific issue number.
- Treat the wiki as the spec: identify relevant `wiki/` page(s); update them when behavior/syntax/CLI/bundles/verification scope changes.

## Process

1) Determine whether this is a bug, enhancement, or docs-only change.
2) Draft an Issue:
   - Title: concise and specific.
   - Body must include:
     - Problem statement / motivation
     - MVP scope (explicitly in-scope vs out-of-scope)
     - Acceptance criteria (checklist)
     - Verification plan (which tests to add/run)
     - Relevant wiki page(s) that define the spec
3) Create the Issue via `gh issue create`.
4) Propose labels (area + priority + `status:needs-spec` or `status:ready`) and apply them.
5) STOP and ask the user to confirm priority/labels and whether to proceed with implementation.
6) After confirmation, proceed with:
   - Plan (3–6 steps)
   - Tests first
   - Implement minimal changes
   - Run targeted tests/build
   - Commit on `develop`
   - Comment on the Issue with verification commands

Output requirements
- Link the created issue number.
- Show the drafted acceptance criteria.
- Ask for explicit confirmation before implementing.
```