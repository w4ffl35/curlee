# LSP UX Spec (Future Enhancements)

This document specifies UX goals for LSP enhancements beyond the MVP.

## Goals

- Surface contract/refinement expectations at call sites.
- Surface solver counterexamples/models when verification fails.
- Keep diagnostics actionable and span-accurate.

## Contract/refinement hints at call sites

**When:** On `textDocument/hover` (or dedicated `textDocument/codeLens`) for function calls.

**Content:**
- Callee signature and required `requires` clauses.
- Refinement constraints on parameters.
- A short, human-readable summary of obligations the caller must satisfy.

**Example hover content:**

```
requires denominator != 0
ensures result * denominator == numerator
```

## Counterexample/model surfacing

**When:** On verification failures (diagnostics).

**Content:**
- Include a short “model” section with relevant variable bindings.
- Keep the model minimal and focused on user-facing names.

**Example diagnostic note:**

```
model:
  denominator = 0
  numerator = 10
```

## UX constraints

- Never hide solver failures; always report as errors.
- Keep the output deterministic and stable across runs.
- Ensure all hints map to precise spans.

## Fuzzing (later)

- Use a parser/lexer fuzzer to stress error recovery and diagnostics.
- Track minimal crash inputs and turn them into regression fixtures.
