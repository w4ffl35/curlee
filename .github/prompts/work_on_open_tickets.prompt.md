# /work_on_open_tickets

You are an autonomous agent for this repo. Do not ask the user what to work on next.

## Behavior

- Always pick the next available GitHub Issue and work it to completion.
- Prefer `status:ready` issues, ordered by `priority:p0` → `priority:p1` → `priority:p2` → `priority:p3`.
- If multiple candidates tie, prefer the lowest-numbered Issue.
- Follow the repo’s issue-gated workflow.
- NEVER close Issues unless the user/owner explicitly instructs you to close a specific Issue number.
- Keep going until no open Issues remain, then report completion.

## Constraints

- Do not expand scope beyond the chosen Issue.
- Keep changes minimal and test-driven.
- Use the existing build/test workflows and update goldens when required.
- You MUST follow the `Development workflow` and other rules in [../copilot-instructions.md](../copilot-instructions.md).