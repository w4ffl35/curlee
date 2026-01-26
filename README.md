# Curlee

Curlee is an experimental **verification-first programming language** and C++23 compiler/runtime.

It is built for the “AI code era”: instead of optimizing primarily for humans to *write* code, Curlee is designed to help humans (and agents) **refuse to run code unless it satisfies declared contracts**.

---

## Why build Curlee?

Modern LLMs can generate a lot of code quickly—but a common failure mode is “almost correct” logic that compiles, runs, and silently does the wrong thing.

Curlee’s goal is to be a **safety harness**:

- You write *intent* as machine-checkable contracts (`requires` / `ensures`) and refinements (`where`).
- The compiler uses an SMT solver (Z3) to prove obligations.
- If an obligation can’t be proven (or the contract is outside the supported logic), Curlee **fails the build**.

This shifts trust from “I hope the generated code is safe” to “I have a proof (or the program doesn’t run).”

---

## What problem does it solve?

### The core problem

AI-generated code is often:

- syntactically valid,
- type-correct,
- but logically wrong in edge cases.

Curlee introduces a new default:

> **No proof, no run.**

### The bigger picture: multi-agent sovereignty

Curlee aims to support a world where agents exchange tasks safely.

- An agent can send another agent a *bundle* (bytecode + metadata + declared capabilities).
- The receiver re-verifies the bundle deterministically before executing.
- Execution is capability-scoped (no ambient authority) and resource-bounded (fuel/gas).

---

## Key ideas (at a glance)

| Theme | Python/JS baseline | Curlee target |
| --- | --- | --- |
| Correctness | Tests + review + runtime errors | Compile-time contract proofs |
| Security | Ambient authority + sandboxing | Capabilities + proofs + fuel |
| AI-generated code | “Probably ok” | “Prove it or reject it” |
| Interop | Big ecosystems | “Shield” legacy ecosystems via explicit `unsafe` boundaries |

---

## Architecture

Curlee is structured as a non-toy compiler toolchain.

```mermaid
flowchart LR
  S[SourceFile] --> L[Lexer]
  L --> P[Parser]
  P --> R[Resolver]
  R --> T[Type Checker]
  T --> V[Verifier (Z3)]
  V -->|only if proven| C[Bytecode Compiler]
  C --> VM[Deterministic VM (fuel)]
```

### Contracts and proof obligations

Example (intended syntax):

```curlee
fn divide(numerator: Int, denominator: Int) -> Int
  requires denominator != 0
  ensures result * denominator == numerator
{
  return numerator / denominator;
}
```

The compiler checks obligations like:

- At call sites: prove the callee’s `requires` from the caller’s facts.
- At returns: prove the function’s `ensures`.

The MVP logic fragment is intentionally small and decidable.

---

## Project status

This repository is early-stage.

### MVP scope (current)

Curlee currently supports two useful workflows:

- **MVP-check**: `curlee check <file.curlee>` runs lex → parse → resolve → type-check → verify (Z3). If a proof obligation can’t be discharged (or is out of scope), Curlee fails with a diagnostic.
- **MVP-run**: `curlee run <file.curlee>` runs `check` first, then executes a small verified subset on the deterministic VM (fuel-bounded).

The runnable subset is intentionally small:

- Expressions: `Int` / `Bool` literals, names, `+`, grouping.
- Statements: `let`, `return`, `if/else`, `while`.
- Calls: simple **no-arg** calls to a named function.

Out of scope (for now): strings, general unary/binary ops, function parameters, modules/import execution.

---

## Docs

User-facing documentation lives in the GitHub wiki:

- https://github.com/w4ffl35/curlee/wiki

---

## Build & run (Linux)

### Dependencies (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build g++ libz3-dev pkg-config
```

By default, Curlee uses the system Z3 if available. To force the vendored build:

```bash
cmake --preset linux-debug -DCURLEE_USE_SYSTEM_Z3=OFF
```

### Configure

```bash
cmake --preset linux-debug
```

### Build

```bash
cmake --build --preset linux-debug
```

### Run

```bash
./build/linux-debug/curlee --help
./build/linux-debug/curlee check examples/mvp_run_int.curlee
./build/linux-debug/curlee run examples/mvp_run_control_flow.curlee
```

---

## Contributing / development rules

- Curlee is verification-first: unsupported constructs must produce clear errors (no guessing).
- Keep changes small and test-driven.
- Prefer golden tests for diagnostics and verification failures.

For agent guidance, see [.github/copilot-instructions.md](.github/copilot-instructions.md).
