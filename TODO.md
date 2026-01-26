# Curlee TODO (Verification-First + C++23 Compiler/VM)

Use this as the execution checklist for building Curlee as an “AI-native safety harness” language:

- Classic non-toy pipeline (source → lexer → parser → diagnostics → resolver → types → bytecode → VM)
- Plus contracts/refinement constraints verified by an SMT solver (Z3)
- Plus capability-based security + resource-bounded execution to support multi-agent “proof-carrying tasks”

## 0) Project setup

- [x] Minimum supported platforms: Linux
- [x] Use baseline toolchain C++23
- [x] Use CMake for the build system
- [x] Add formatting + linting (repo config)
  - [x] clang-format config (run: `./scripts/format.sh`)
  - [x] clang-tidy config (run: `./scripts/tidy.sh build/linux-debug`)
  - [x] install tools (Ubuntu/Debian: `sudo apt-get install clang-format clang-tidy`)
- [x] Add CI (build + tests on at least Linux)
- [x] Switch conventional source extension from `.cur` to `.curlee` (rename fixtures/tests/docs; compiler stays extension-agnostic)

## 0.1) Lock the early decisions (write these down, then implement)

- [x] Execution model: bytecode VM first (deterministic), native backend later
- [x] Contract model: `requires` / `ensures` + `where` refinements (MVP: arithmetic + boolean predicates)
- [x] Verification scope (MVP): decidable fragment (e.g., linear integer arithmetic + booleans)
- [x] Security model: capability-based APIs (no ambient authority)
- [x] Resource model: instruction “fuel” / gas limit (runtime metering first; static bounds later)
- [x] Interop strategy: Python bridge as an explicitly `unsafe` boundary (later milestone)
- [x] Artifact story: what is shipped between agents? (bytecode + metadata + optional proof bundle)
- [x] MVP syntax locked (blocks `{}` + mandatory `;` + bracketed contract blocks)
  - [x] wiki/Language-Syntax.md (local)

## 1) Repo structure (suggested)

- [x] Create directories
  - [x] src/
  - [x] include/
  - [x] tests/
  - [x] examples/
  - [x] docs/

## 1.1) CLI + milestones (end-to-end loop)

- [x] Create a `curlee` CLI binary target
- [x] Parse CLI args + subcommands (`lex`, `parse`, `check`, `run`)
- [x] Read `.cur` file into memory (path + contents)
- [x] `curlee lex file.cur` (debug: read file; tokens next)
- [x] `curlee parse file.cur` (debug: print AST)
- [x] `curlee check file.cur` (parse + bind + types + contracts/prove; no execution)
  - [x] Add golden test for `curlee check`
  - [x] Run name resolution (bind identifiers) and report binder diagnostics
- [x] `curlee run file.cur` (full pipeline; run only after `check` passes)

## 2) Source + diagnostics foundation

- [x] Implement `SourceFile` (path + contents)
- [x] Implement `Span` (byte offsets) + `LineMap` (offset -> line/col)
- [x] Implement `Diagnostic` (severity, message, span, notes)
- [x] Implement pretty printing for diagnostics (caret + line snippet)
- [x] Add golden-test harness for diagnostics output

## 2.1) Dogfood diagnostics in the CLI

- [x] Route CLI errors through `Diagnostic` + renderer (start with missing file / load errors)
- [x] Add golden tests for CLI diagnostics output (separate from renderer unit golden tests)

## 3) Lexer (include contracts + refinements tokens)

- [x] Define `TokenKind` enum
- [x] Define `Token { kind, lexeme, span }`
- [x] Implement lexer
  - [x] whitespace + comments
  - [x] identifiers + keywords
    - [x] keywords for verification: `requires`, `ensures`, `where`
    - [x] keywords for security boundaries (reserved): `unsafe`, `cap`, `import`
  - [x] numbers
  - [x] strings (escapes + unterminated string error)
  - [x] operators/punctuators
    - [x] include `[` and `]` tokens for contract blocks
- [x] Lexer tests
  - [x] token sequence tests
  - [x] error tests (invalid char, unterminated string)

## 4) Parser (AST includes predicates/contracts)

- [x] Define AST node types (Expr/Stmt/Item) with spans
- [x] Define predicate AST for contracts/refinements (MVP: `&&`, `||`, `!`, comparisons, arithmetic)
- [x] Implement expression parser (precedence climbing or Pratt)
- [x] Implement statement parsing (`let`, `if`, `while`, `return`, blocks)
- [x] Implement function declarations (`fn name(params) { ... }`)
- [x] Parse function contracts
  - [x] `requires <pred>` (0+)
  - [x] `ensures <pred>` (0+; allow `result` identifier)
- [x] Parse refinement annotations
  - [x] `let x: Int where x > 0 = ...;`
  - [x] `fn f(x: Int where x > 0) -> Int { ... }`
- [x] Implement parse error recovery (synchronize on `;` and `}`)
- [x] Parser tests
  - [x] valid AST shape tests
  - [x] recovery tests (multiple errors in one file)
- [x] Support `return;` (Unit return) in parser/AST

## 5) Name resolution (binder) + modules

- [x] Define `SymbolId`
- [x] Implement lexical scopes (block/function)
- [x] Bind identifiers to `SymbolId`
- [x] Duplicate definition diagnostics
- [x] Unknown name diagnostics
- [x] Binder/resolver unit tests (unknown + duplicate + happy path)
- [x] Prepare module/import story (even if minimal initially)

## 6) Types + effects (static typing with a path to refinement/linear types)

- [x] Define core types: `Int`, `Bool`, `String`, `Unit`
- [x] Define function types: `(T...) -> T`
- [x] Define “capability types” (MVP: opaque capability values; no ambient I/O)
- [ ] Future: linear/ownership types for single-use resources
- [x] Typed representation after type checking (TypeInfo side table keyed by Expr.id)
- [x] Implement type checking rules
  - [x] arithmetic operators
  - [x] comparisons
  - [x] boolean operators
  - [x] `if/else` condition type
  - [x] `while` condition type
  - [x] `return` matches function return type
  - [x] function call argument checking
- [x] Type error diagnostics with spans
- [x] Decide inference strategy
  - [x] No inference (require annotations)

## 7) Verification engine (SMT “safety harness”)

- [x] Choose and document the supported logic fragment for MVP (keep it small + decidable)
  - [x] wiki/Verification-Scope.md (local)
- [x] Integrate Z3 in the CMake build (system dependency or vendored)
- [x] Implement a thin solver wrapper (create context, assert, check, model formatting)
- [x] Lower Curlee predicate AST → Z3 constraints
  - [x] integer arithmetic + comparisons
  - [x] boolean connectives
  - [x] `result` binding for `ensures`
- [x] Implement contract checking rules
  - [x] At function entry: assume `requires`
  - [x] At each call site: prove callee `requires` from caller facts
  - [x] At `return`: prove `ensures`
  - [x] Refinements: treat `where` as additional facts about a variable
- [x] Verification diagnostics
  - [x] point at the violated contract span
  - [x] include a minimal counterexample/model when available
- [x] Verification tests (golden)
  - [x] `divide` rejects `denominator == 0`
  - [x] simple range proof: `x: Int where x > 0` implies `x != 0`

## 8) Runtime target: bytecode VM (deterministic + resource-bounded)

- [x] Define runtime value representation (tagged union / variant)
- [x] Define bytecode instruction set (minimal, then expand)
- [x] Define constants table
- [x] Implement VM
  - [x] stack operations
  - [x] locals
  - [ ] call frames
  - [ ] control flow (jumps)
- [x] Add resource-bounded execution
  - [x] per-run “fuel” budget
  - [x] per-instruction fuel charging
  - [x] deterministic halt when fuel is exhausted
- [x] VM tests (execute bytecode sequences)

## 8.1) Hardware-aware backend (future)

- [ ] Define a tensor-first IR surface (minimal ops; separate from core bytecode)
- [ ] Add an optional NPU/GPU backend (later milestone; not required for MVP)

## 9) Compiler: AST -> bytecode

- [ ] Implement bytecode emitter
- [ ] Compile literals + binary/unary ops
- [ ] Compile `print(...)` (temporary builtin)
- [ ] Compile `let` bindings + variable loads
- [ ] Compile assignment
- [ ] Compile `if/else`
- [ ] Compile `while`
- [ ] Compile functions + calls
- [ ] Add source mapping to bytecode for runtime errors/debugging

## 10) Standard library + builtins (capability-based)

- [ ] Decide what is a builtin vs stdlib module
- [ ] Implement minimal builtins
  - [ ] `print`
  - [ ] string concatenation behavior
- [ ] Define “capability-bearing” stdlib APIs (no global file/network access)
  - [ ] e.g., `std.fs` requires an explicit handle/capability
  - [ ] e.g., networking requires an explicit capability
- [ ] Design stdlib module format and loading

## 11) Modules/packages (non-toy requirement)

- [ ] Define module syntax (`import foo.bar` etc.)
- [ ] Implement module resolution (search paths)
- [ ] Implement incremental compilation boundaries (later)
- [ ] Define package layout conventions

## 12) Multi-agent sovereignty (proof-carrying tasks)

- [x] Define a portable bundle format: `Bundle = bytecode + manifest + (optional) proof/claims`
- [ ] Define what is proven vs what is enforced at runtime (MVP: capabilities + fuel)
- [x] Add bundle hashing + versioning (reproducible build inputs)
- [x] Add “capability manifest” to bundles (declared required capabilities)
- [ ] Prevent “hallucinated dependency” imports
  - [ ] pin imports to package IDs + hashes
  - [ ] forbid dynamic package resolution in verified mode
- [ ] Define a “universal ontology” for shared types across agents (future)

## 13) Python interoperability (later, but plan it early)

- [ ] Define an explicit `unsafe` boundary for FFI calls
- [ ] Define a `python_ffi` module boundary and calling convention
- [ ] Decide embedding approach (link `libpython` vs subprocess)
- [ ] Define a “Curlee shield” pattern: Curlee validates/contracts → Python does work
- [ ] Mark Python calls as capability-requiring effects

## 14) Tooling

- [x] Formatter (`curlee fmt`)
- [x] Language Server (LSP)
  - [x] diagnostics on change
  - [x] go-to-definition
  - [x] hover types
- [x] Optional accelerator: minimal LSP early (parse + diagnostics only)
- [ ] LSP “spec injection” support
  - [ ] surface contracts/refinement expectations near call sites
  - [ ] show counterexample/model from solver when a proof fails
- [ ] Fuzzer / robustness testing for lexer+parser (later)

## 15) Quality + release hygiene

- [ ] Add end-to-end tests in `tests/run/`
- [ ] Add `tests/correct_samples/` as the “golden dataset” for verified programs
- [ ] Add synthetic data generator (MVP: generate ~500 small verified samples)
- [ ] Scale synthetic data generation (later: ~100k LOC + natural-language descriptions)
- [ ] Export synthetic dataset as a single file for RAG (e.g., `training_data.txt`) (generated artifact; keep untracked)
- [ ] Add benchmark harness (optional)
- [ ] Document versioning policy
- [ ] Define stable/unstable feature gates (optional)

## Immediate next tasks (recommended order)

- [ ] Expand AST → bytecode emitter (unary ops, if/while, calls)
- [ ] Expand VM instruction set to match emitter needs (jumps/calls)
- [ ] Implement runtime capability enforcement plumbing (capability manifest + VM/interop boundary)
