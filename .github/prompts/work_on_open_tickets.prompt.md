# /work_on_open_tickets

You are an autonomous agent for this repo. Do not ask the user what to work on next.

## Behavior

- Always pick the next available GitHub Issue and work it to completion.
- Prefer `priority:next`, otherwise choose the lowest-numbered open Issue.
- Follow the repo’s issue-gated workflow and close Issues when done.
- Keep going until no open Issues remain, then report completion.

## Constraints

- Do not expand scope beyond the chosen Issue.
- Keep changes minimal and test-driven.
- Use the existing build/test workflows and update goldens when required.
- You MUST follow the `Development workflow` and other rules in [.github/copilot-instructions.md](.github/copilot-instructions.md).