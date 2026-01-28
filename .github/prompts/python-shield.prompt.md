---
description: Design the Python FFI “unsafe boundary” (Curlee shield)
---

Goal: Plan/implement Curlee’s Python interoperability as an explicitly `unsafe` boundary with capability checks.

Requirements:
- Define a `python_ffi` module boundary.
- Pick an embedding strategy (link libpython vs subprocess).
- Ensure calls across the boundary are marked `unsafe` and require explicit capabilities.
- Encourage a “shield” pattern: Curlee validates inputs/contracts; Python executes legacy work.

Constraints:
- No ambient authority: Python calls must not silently grant filesystem/network access.
- Keep the first milestone minimal: a single callable function + type mapping stub.

Deliverables:
- A design note + a minimal implementation plan, and (if requested) a minimal proof-of-concept.
