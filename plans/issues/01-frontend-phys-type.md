# Title: Front-end support for `Phys<T>`: physical memory type, `phys()` literal, and gated `read`/`write`

## Problem statement / motivation

Curlee's long-term goal is to be a verification harness for systems/kernel code, not just application
code. To write a UEFI framebuffer driver or a PCIe register device driver, the language needs a way to
safely express *hardcoded physical memory addresses* and typed loads/stores to them. Today there is no
pointer-like type at all: the type system is `Int/Bool/String/Unit/Vec/Set/Struct/Enum/Capability`
([`type.h`](include/curlee/types/type.h:18)), and `unsafe` blocks only gate capability-bearing host
calls (e.g. `python_ffi.call`) — they do not denote raw memory access.

This issue adds the **front-end surface** for raw pointer contracts. It is the first of two pointer
issues: this one adds syntax/type-checking; the follow-up ([Verifier trusted deref semantics](#)) makes
the verifier treat derefs as trusted/opaque and is a prerequisite for the freestanding C codegen issue.

## Design

New primitive integer types `U8`, `U16`, `U32`, `U64` and a physical-memory pointer type:

```curlee
fn main() -> Unit {
  unsafe {
    let fb: Phys<U32> = phys<U32>(0xFD00_0000); // framebuffer base (constant only)
    fb.write(0xFF8800);                          // pixel write
    let v: U32 = fb.read();                      // register/framebuffer read
  }
}
```

Rules enforced at type-check time (defense in depth; the verifier issue adds a second layer):

1. The address argument to `phys<U>(...)` **must be a compile-time integer literal** (decimal or hex,
   underscore separators allowed). Any computed expression is a hard error:
   `"physical address must be a constant literal"`.
2. `read()` / `write(v)` may appear **only inside an `unsafe` block** (reuse `unsafe_depth_`, see
   [`type_check.cpp`](src/types/type_check.cpp:478)).
3. The enclosing function **must have an in-scope `cap phys.mem` parameter** (same mechanism that gates
   `python.ffi`, see [`cli_impl.ipp`](src/cli/cli_impl.ipp:1134) and [`capabilities.h`](include/curlee/runtime/capabilities.h:20)).
4. `Phys<T>` is not arithmetic-able: no `+`, `-`, comparison, or indexing. `read` returns `T`;
   `write` takes `T` and returns `Unit`.

## MVP scope

In-scope:

- Lexer keyword `KwPhys` and tokens for `U8/U16/U32/U64` type names.
- Parser + AST nodes: `PhysExpr{element_kind, lexeme}`, `PhysReadExpr{base}`, `PhysWriteExpr{base, value}`;
  primary/expression grammar for `phys<U>(literal)`, `.read()`, `.write(v)`.
- Type system: new `TypeKind::U8/U16/U32/U64`, `TypeKind::Phys` with element-kind/name; type-check
  rules (1)–(4) above with golden diagnostics.
- Resolver and LSP visitor support for the new nodes (dump/hover parity with existing nodes).
- Type-check-only unit fixtures. No bytecode emission, no VM opcodes, no codegen (freestanding-only by design).

Out-of-scope (explicit):

- Verifier semantics (separate issue — this issue must NOT add verifier support).
- Bytecode/VM execution of Phys operations (VM programs containing Phys produce a diagnostic; the
  freestanding C target is the only execution path).
- Pointer arithmetic, casting between Phys element kinds, non-literal addresses, heap pointers,
  string/struct element kinds for `Phys<T>`.

## Acceptance criteria

- [ ] `KwPhys` lexed; `U8/U16/U32/U64` parse as type names; `phys<U32>(0xFD00_0000)`, `.read()`,
      `.write(v)` parse and dump correctly (`curlee parse` + `curlee dump` parity).
- [ ] Type-check passes on a fixture that writes a framebuffer pixel and reads a PCIe register inside
      `unsafe` with `cap phys.mem`.
- [ ] Golden diagnostics for each of: non-literal address (`phys<U32>(base + 4)`),
      deref outside `unsafe`, missing `cap phys.mem` parameter, `Phys<T>` used in arithmetic,
      `Phys<String>` (unsupported element kind).
- [ ] `curlee check` passes on the positive fixture and fails on each negative fixture with a stable,
      span-accurate diagnostic.
- [ ] Resolver/LSP dumps do not crash on the new nodes.

## Verification plan

- Add fixtures under `tests/fixtures/` (positive + one per negative case) and golden files under
  `tests/diagnostics/`.
- Unit tests: `tests/lexer_tests.cpp` (KwPhys), `tests/parser_internal_tests.cpp` / `tests/parser_tests.ipp`
  (new nodes), `tests/types_tests.ipp` (new kinds + gating rules), `tests/resolver_tests.cpp`,
  `tests/lsp_internal_tests.ipp` (hover/dump).
- Run: `ctest --preset linux-debug --output-on-failure` (focused subset first:
  lexer/parser/types/resolver/lsp).

## Relevant wiki page(s)

- `wiki/Language-Syntax` — document `Phys<T>`, `phys<U>(literal)`, `.read()`, `.write(v)`, the
  constant-address rule, and the `unsafe` + `cap phys.mem` requirement.
- `wiki/Stability-and-Supported-Fragment` — add `Phys<T>` to the fragment table with "freestanding-only"
  note (no VM support).
- `wiki/Running-Programs` — note that `curlee run` rejects Phys programs; the freestanding target
  (later issue) is the execution path.
