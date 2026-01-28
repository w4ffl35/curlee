---
description: Generate synthetic verified Curlee samples
---

Goal: Create a synthetic program generator to address the “training data moat”.

MVP target:
- Generate ~500 small `.curlee` programs into tests/correct_samples/
- Each sample must pass `curlee check`

Later target:
- Scale toward ~100k LOC, optionally producing natural-language descriptions alongside programs.

Constraints:
- Samples must stay within the implemented language subset and wiki/Verification-Scope.md.
- If a sample fails verification, it must be discarded (never keep failing samples).
- Determinism: seeded generation.

Deliverables:
- A generator tool/script under scripts/ (language of your choice, but keep dependencies minimal)
- A manifest of generated files + seed used
- An exported `training_data.txt` file for downstream RAG/training use (generated artifact; keep **untracked** / gitignored)
- A CI-friendly way to run generation in “check-only” mode (no massive regeneration every run)

After coding:
- Provide the command to (a) generate and (b) verify the dataset.