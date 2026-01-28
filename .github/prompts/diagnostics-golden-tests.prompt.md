---
description: Implement diagnostics printing + golden test harness
---

Goal: Implement Curlee diagnostics (spans → line/col, caret rendering) and a golden test harness.

Requirements:
- SourceFile, Span, LineMap mapping (byte offsets).
- Diagnostic type: severity, message, primary span, optional notes.
- Pretty output: file:line:col header, snippet line(s), caret, actionable hint.
- Golden tests:
  - store input `.cur` and expected `.txt`
  - harness runs compiler stage (lex/parse/check) and compares output

Constraints:
- Deterministic output (stable ordering; avoid pointer addresses).
- Error empathy: point at exact spans and include hints.

Deliverables:
- Implementations under include/curlee/source and include/curlee/diag
- Tests under tests/diagnostics/

After coding:
- Run tests and show the command used.