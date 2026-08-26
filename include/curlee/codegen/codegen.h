// SPDX-License-Identifier: MIT
#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/parser/ast.h>
#include <string>
#include <variant>
#include <vector>

/**
 * @file codegen.h
 * @brief Freestanding C code generation from verified Curlee programs.
 *
 * The codegen module emits freestanding C from a fully checked and verified
 * `curlee::parser::Program`. It is the second execution target of the compiler
 * (after the bytecode VM) and is intended for freestanding/kernel environments:
 * the emitted C only depends on the freestanding `<stdint.h>` header and is
 * compiled with `gcc -ffreestanding -fno-builtin -nostdlib -c`.
 *
 * The verification gate is identical to the `run` path: callers are expected
 * to run the full lex -> parse -> resolve -> type-check -> verify pipeline
 * before invoking codegen. The codegen module itself re-checks structural
 * invariants and rejects hosted-only builtins with clear diagnostics.
 */

namespace curlee::codegen
{

/** @brief Supported codegen targets (only freestanding-c in this milestone). */
enum class Target
{
    FreestandingC,
};

/**
 * @brief Result of code generation: emitted C source or diagnostics.
 *
 * On success the variant holds the complete freestanding C translation unit as
 * a string. On failure it holds a list of diagnostics describing why emission
 * was rejected (e.g. a hosted-only builtin or an unsupported construct).
 */
using CodegenResult = std::variant<std::string, std::vector<curlee::diag::Diagnostic>>;

/**
 * @brief Emit freestanding C from a verified Program.
 *
 * @param program The fully checked and verified program AST.
 * @return Emitted C source text or diagnostics on rejection.
 *
 * Emitted C conventions:
 *  - Every top-level internal name is prefixed with `curlee_` to avoid
 *    collisions with the runtime/ABI.
 *  - `main` is emitted as `int64_t curlee_main(void)`.
 *  - `Phys<T>` read/write lower to volatile dereferences of embedded literal
 *    addresses, e.g. `(*(volatile uint32_t*)(0xFD000000))`. A `Phys<T>`
 *    function parameter becomes a `uintN_t*` parameter and read()/write() on
 *    it dereference the pointer; forwarding a Phys parameter to another
 *    function passes the pointer as-is. The runtime-address builtins
 *    (`phys_read_u8/u16/u32/u64`, issue #279, and `phys_write_u8/u16/u32/u64`,
 *    issue #285) lower to volatile derefs of a general address expression,
 *    e.g. `(*(volatile uint32_t*)(uintptr_t)(p))` and
 *    `(*(volatile uint32_t*)(uintptr_t)(p)) = (v)`.
 *  - Hosted-only builtins (`print`, `__read_line`, `__fs_*`, `__tty_*`,
 *    `__rng_*`, vec/set ops, python_ffi) are rejected with the diagnostic
 *    "builtin X is not supported in freestanding target". The builtin surface
 *    is the resolver's public `is_builtin_call_name`.
 *  - Structs/enums are forward-declared first so definitions may reference
 *    each other in any order (the type checker allows forward references);
 *    definitions are emitted in dependency order so by-value members are
 *    complete.
 *  - A trailing fallback `return 0;` is emitted only when the function body
 *    does not provably terminate in a return (the verifier proves
 *    reachability of explicit final returns).
 *  - The emitted C is strict C11 (compiled with `-std=c11 -pedantic`): no
 *    GNU extensions such as empty structs/unions or `void` members.
 *  - Types with no storable C representation are rejected with a clear
 *    diagnostic in every storage position:
 *      - function parameters: `Unit`, `String`, `Vec`, `Set` (capabilities are
 *        dropped, `Phys<T>` becomes `uintN_t*`);
 *      - function return types: `Phys<T>`, `Unit` (they lower to `void`, so a
 *        `return <value>;` would be invalid C);
 *      - struct fields and enum payloads: `Phys<T>`, `Unit`, capabilities,
 *        `String`, `Vec`, `Set`;
 *      - locals: `Unit`, `String`, `Vec`, `Set` (Phys locals are opaque
 *        bindings with no storage).
 *  - Diagnostics are emitted against the owning source file: the CLI records
 *    which file each (possibly imported) function came from and renders spans
 *    against that file.
 */
[[nodiscard]] CodegenResult codegen_freestanding_c(const curlee::parser::Program& program);

} // namespace curlee::codegen
