// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <curlee/lexer/token.h>
#include <curlee/source/span.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

/**
 * @file ast.h
 * @brief Abstract syntax tree (AST) node types produced by the parser.
 */

namespace curlee::parser
{

/** @brief A (possibly qualified) type name with its source span. */
struct TypeName
{
    curlee::source::Span span;
    bool is_capability = false;
    std::string_view name;
    std::optional<std::string_view> type_arg;

    // For the fixed-size array form `[T; N]`, the compile-time length literal
    // lexeme (decimal, underscores preserved). When set, `name` holds the
    // element type name (Int/U8/U16/U32/U64) and `type_arg` is unused. This is
    // the MVP array form (issue #278): freestanding-local `let` bindings only.
    // Default-initialized so pre-existing designated initializers that omit
    // the new member do not trip -Wmissing-field-initializers.
    std::optional<std::string_view> array_len = std::nullopt;

    [[nodiscard]] bool is_array() const { return array_len.has_value(); }
};

/** Forward declaration for predicate nodes. */
struct Pred;

/** @brief Integer literal predicate (lexeme preserved). */
struct PredInt
{
    std::string_view lexeme;
};

/** @brief Boolean literal predicate. */
struct PredBool
{
    bool value = false;
};

/** @brief Named predicate (identifier). */
struct PredName
{
    std::string_view name;
};

/**
 * @brief Function call predicate (e.g. `old_sum(v)`).
 *
 * Calls in predicates are restricted to ghost functions (verification-only).
 * They are lowered to uninterpreted function applications by the verifier,
 * mirroring the phys_read machinery.
 */
struct PredCall
{
    std::string_view callee;
    std::vector<Pred> args;
};

/** @brief Unary predicate (e.g. `!p`). */
struct PredUnary
{
    curlee::lexer::TokenKind op;
    std::unique_ptr<Pred> rhs;
};

/** @brief Binary predicate (e.g. `a == b`). */
struct PredBinary
{
    curlee::lexer::TokenKind op;
    std::unique_ptr<Pred> lhs;
    std::unique_ptr<Pred> rhs;
};

/** @brief Parenthesized predicate. */
struct PredGroup
{
    std::unique_ptr<Pred> inner;
};

/**
 * @brief A predicate node with source span and concrete variant.
 */
struct Pred
{
    curlee::source::Span span;
    std::variant<PredInt, PredBool, PredName, PredCall, PredUnary, PredBinary, PredGroup> node;
};

/** Forward declaration for expression nodes. */
struct Expr;

/** @brief Integer literal expression. */
struct IntExpr
{
    std::string_view lexeme;
};

/** @brief Boolean literal expression. */
struct BoolExpr
{
    bool value = false;
};

/** @brief String literal expression (lexeme includes quotes). */
struct StringExpr
{
    std::string_view lexeme; // includes quotes, preserves escapes
};

/** @brief Simple name expression (identifier). */
struct NameExpr
{
    std::string_view name;
};

/** @brief Scoped name expression (e.g. module::name). */
struct ScopedNameExpr
{
    std::string_view lhs;
    std::string_view rhs;
};

/** @brief Member access expression (base.member). */
struct MemberExpr
{
    std::unique_ptr<Expr> base;
    std::string_view member;
};

/**
 * @brief Indexed array element read: `arr[i]`.
 *
 * The base must resolve to a fixed-size array binding (`let q: [T; N] = ...`).
 * The index is a general Int expression; the verifier discharges a bounds
 * obligation (`0 <= i && i < N`) on every access. Multi-dimensional indexing
 * (`q[i][j]`) is out of the MVP scope and rejected by the type checker.
 */
struct IndexExpr
{
    std::unique_ptr<Expr> base;
    std::unique_ptr<Expr> index;
};

/**
 * @brief Fixed-size array repeat literal: `[v; N]`.
 *
 * Builds an array of length `N` (a compile-time literal) whose every element
 * is `v`. This is the only array literal form in the MVP (element-wise
 * `[1, 2, 3]` is out of scope). It is only valid as a `let` initializer for a
 * `[T; N]` binding.
 */
struct ArrayLiteralExpr
{
    std::unique_ptr<Expr> value;
    // The count literal lexeme (decimal, underscores preserved).
    std::string_view count_lexeme;
};

/** @brief Unary expression (e.g. `-x`). */
struct UnaryExpr
{
    curlee::lexer::TokenKind op;
    std::unique_ptr<Expr> rhs;
};

/** @brief Binary expression (e.g. `a + b`). */
struct BinaryExpr
{
    curlee::lexer::TokenKind op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
};

/** @brief Function call expression. */
struct CallExpr
{
    std::unique_ptr<Expr> callee;
    std::vector<Expr> args;
};

/** @brief Field in a struct literal with source span. */
struct StructLiteralExprField
{
    curlee::source::Span span;
    std::string_view name;
    std::unique_ptr<Expr> value;
};

/** @brief Struct literal expression (e.g. `T { a: 1 }`). */
struct StructLiteralExpr
{
    std::string_view type_name;
    std::vector<StructLiteralExprField> fields;
};

/** @brief Parenthesized or grouped expression. */
struct GroupExpr
{
    std::unique_ptr<Expr> inner;
};

/** @brief Physical memory address literal expression: `phys<U>(literal)`. */
struct PhysExpr
{
    // Element kind as spelled in source (e.g. "U32").
    std::string_view element_kind;
    // The address literal lexeme (decimal or hex, underscores preserved).
    std::string_view lexeme;
};

/** @brief Typed physical memory read: `phys_value.read()`. */
struct PhysReadExpr
{
    std::unique_ptr<Expr> base;
};

/** @brief Typed physical memory write: `phys_value.write(v)`. */
struct PhysWriteExpr
{
    std::unique_ptr<Expr> base;
    std::unique_ptr<Expr> value;
};

/**
 * @brief x86 port I/O builtin: `port_inb(0x3FD)`, `port_outb(0x3F8, v)`.
 *
 * The port argument is a general Int expression (issue #276): a compile-time
 * constant literal (hex or decimal, underscores preserved), a `let`-bound
 * base, or a `let`-bound base plus a constant offset (the virtio_net.c
 * pattern). Constant ports lower/emit exactly as before; runtime ports are
 * lowered as an opaque extra argument to the uninterpreted read/write
 * functions and emitted with the port in the DX register (the `Nd` asm
 * constraint falls back to DX for non-immediates). The optional value
 * expression (out* variants only) may be any expression of the matching
 * width, mirroring `PhysWriteExpr`. Reads (`port_in*`) are trusted/opaque to
 * the verifier, exactly like `Phys<T>` reads: no axioms constrain the
 * returned value.
 */
struct PortIOExpr
{
    // Builtin name as spelled in source (port_inb/port_outb/port_inw/port_outw/
    // port_inl/port_outl).
    std::string_view op;
    // The port expression: a constant literal IntExpr or a runtime Int
    // expression (let-bound base, optional constant offset).
    std::unique_ptr<Expr> port;
    // The value expression for out* variants; null for in* variants.
    std::unique_ptr<Expr> value;
};

/**
 * @brief True if `name` is an x86 port I/O builtin (`port_inb`/`port_outb`/
 * `port_inw`/`port_outw`/`port_inl`/`port_outl`).
 *
 * Single source of truth for the port I/O builtin surface, shared by the
 * parser, resolver, type checker, verifier, codegen and VM emitter.
 */
[[nodiscard]] inline bool is_port_io_builtin_name(std::string_view name)
{
    return name == "port_inb" || name == "port_outb" || name == "port_inw" ||
           name == "port_outw" || name == "port_inl" || name == "port_outl";
}

/**
 * @brief Runtime-address physical memory read builtin: `phys_read_u8(addr)`,
 * `phys_read_u16(addr)`, `phys_read_u32(addr)`, `phys_read_u64(addr)`.
 *
 * Unlike `Phys<T>` (whose address must be a compile-time literal), the
 * address is a general Int/U64 expression (issue #279): a runtime value such
 * as the multiboot2 info base the 32-bit boot stub captures into a global
 * (`mb2_info_addr`), possibly plus a mutable byte cursor advanced by
 * assignment (#268). The read is trusted/opaque to the verifier exactly like
 * `Phys<T>.read()` and `port_in*`: no axioms constrain the returned value.
 */
struct RuntimePhysReadExpr
{
    // Builtin name as spelled in source (phys_read_u8/.../phys_read_u64).
    std::string_view op;
    // The address expression: a general Int/U64 expression (runtime allowed).
    std::unique_ptr<Expr> addr;
};

/**
 * @brief True if `name` is a runtime-address physical memory read builtin
 * (`phys_read_u8`/`phys_read_u16`/`phys_read_u32`/`phys_read_u64`).
 *
 * Single source of truth for the runtime phys read builtin surface, shared by
 * the parser, resolver, type checker, verifier, codegen and VM emitter.
 */
[[nodiscard]] inline bool is_runtime_phys_read_builtin_name(std::string_view name)
{
    return name == "phys_read_u8" || name == "phys_read_u16" ||
           name == "phys_read_u32" || name == "phys_read_u64";
}

/**
 * @brief Runtime-address physical memory write builtin: `phys_write_u8(addr,
 * value)`, `phys_write_u16(addr, value)`, `phys_write_u32(addr, value)`,
 * `phys_write_u64(addr, value)`.
 *
 * The write counterpart of `RuntimePhysReadExpr` (issue #285): the address is
 * a general Int/U64 expression (a runtime value such as the framebuffer base
 * `fb_addr` discovered at boot via a multiboot2 tag or a VBE probe, possibly
 * plus a mutable pixel cursor advanced by assignment (#268)) and the value is
 * the corresponding unsigned width (mirroring `Phys<T>.write()`'s
 * literal-adaptation rule for compile-time-literal addresses). The write is a
 * trusted/opaque side effect to the verifier exactly like `Phys<T>.write()`
 * and `port_out*`: nothing to prove, no axioms constrain it.
 */
struct RuntimePhysWriteExpr
{
    // Builtin name as spelled in source (phys_write_u8/.../phys_write_u64).
    std::string_view op;
    // The address expression: a general Int/U64 expression (runtime allowed).
    std::unique_ptr<Expr> addr;
    // The value expression: the unsigned width named by the builtin (an Int
    // value is accepted for unsigned widths, mirroring Phys<T>.write()).
    std::unique_ptr<Expr> value;
};

/**
 * @brief True if `name` is a runtime-address physical memory write builtin
 * (`phys_write_u8`/`phys_write_u16`/`phys_write_u32`/`phys_write_u64`).
 *
 * Single source of truth for the runtime phys write builtin surface, shared by
 * the parser, resolver, type checker, verifier, codegen and VM emitter.
 */
[[nodiscard]] inline bool is_runtime_phys_write_builtin_name(std::string_view name)
{
    return name == "phys_write_u8" || name == "phys_write_u16" ||
           name == "phys_write_u32" || name == "phys_write_u64";
}

/**
 * @brief Address-of a Curlee-owned fixed-size array: `addr_of(arr)`.
 *
 * Returns the physical address of the storage of a freestanding-local or
 * module-level `static` fixed-size array binding (`let q: [T; N] = ...;` or
 * `static q: [T; N] = ...;`, issue #278/#287) as an `Int`. This is the DMA
 * descriptor-filling primitive for virtio_net.c: the vring descriptor's
 * 64-bit `addr` field is filled with the address of the driver's own RX/TX
 * byte buffer, and the legacy virtqueue PFN register is programmed with
 * `base >> 12` for a 4096-aligned driver-owned scratch region (over-allocate
 * + round-up, now expressible with Int address arithmetic).
 *
 * Unlike `phys_read_*`/`phys_write_*` (which dereference an address the
 * kernel does not necessarily own), `addr_of` asks the compiler for the
 * address OF memory the kernel already owns, to hand to a device for DMA. It
 * is still gated like the other unsafe primitives (`unsafe` + `cap
 * phys.mem`) and is trusted/opaque to the verifier: the value lowers to a
 * stable per-array opaque constant (same array -> same address), so it can
 * be compared/stored, but its numeric value is never assumed. General
 * pointer arithmetic / pointer types remain out of scope — this is exactly
 * "the address of ONE array I already own".
 */
struct AddrOfExpr
{
    // The target expression: a bare array binding name (a freestanding-local
    // `let` or a module-level `static`). The type checker requires it to
    // resolve to an Array-typed binding; indexed elements (`addr_of(q[0])`)
    // and scalars are rejected.
    std::unique_ptr<Expr> target;
};

/**
 * @brief True if `name` is the address-of builtin (`addr_of`).
 *
 * Single source of truth for the address-of builtin surface, shared by the
 * parser, resolver, type checker, verifier, codegen and VM emitter.
 */
[[nodiscard]] inline bool is_addr_of_builtin_name(std::string_view name)
{
    return name == "addr_of";
}

/**
 * @brief A general expression node with id, span and variant payload.
 */
struct Expr
{
    std::size_t id = 0;
    curlee::source::Span span;
    std::variant<IntExpr, BoolExpr, StringExpr, NameExpr, UnaryExpr, BinaryExpr, CallExpr,
                 MemberExpr, GroupExpr, ScopedNameExpr, StructLiteralExpr, PhysExpr, PhysReadExpr,
                 PhysWriteExpr, PortIOExpr, RuntimePhysReadExpr, RuntimePhysWriteExpr, AddrOfExpr,
                 IndexExpr, ArrayLiteralExpr>
        node;
};

/** @brief Let statement (local binding). */
struct LetStmt
{
    std::string_view name;
    TypeName type;
    std::optional<Pred> refinement;
    Expr value;
};

/**
 * @brief Ghost let statement (verification-only snapshot binding).
 *
 * `ghost let x = e;` freezes the current value of `e` into a fresh name `x`.
 * The binding is visible to verification (so later mutation of the source
 * binding does not affect the snapshot) but produces no runtime code: it is
 * erased from the emitted bytecode and freestanding C. The type annotation is
 * optional; when omitted it is inferred from the initializer expression.
 */
struct GhostLetStmt
{
    std::string_view name;
    std::optional<TypeName> type;
    Expr value;
};

/**
 * @brief Assignment statement (reassignment of an existing binding).
 *
 * `x = expr;` mutates the value of an already-declared binding `x` inside a
 * function body. The target must be an in-scope binding (a fresh declaration
 * is still a `let`); the binding's type is unchanged. The verifier rebinds
 * `x` to a fresh solver symbol so pre- and post-mutation values can be
 * reasoned about separately (loop `decreases` variants, invariant
 * preservation). This is the mechanism that makes loop-carried mutation
 * expressible (bounded sums, driver state machines).
 */
struct AssignStmt
{
    std::string_view name;
    Expr value;
};

/**
 * @brief Indexed element assignment: `arr[i] = expr;`.
 *
 * Mutates one element of a fixed-size array binding. The target name must be
 * an in-scope array `let` binding; the index is an Int expression with the
 * same bounds obligation as `IndexExpr`; the value must match the element
 * type (Int literals adapt to unsigned element widths, mirroring
 * `port_outb(0x3F8, 0x48)`).
 */
struct IndexAssignStmt
{
    std::string_view name;
    Expr index;
    Expr value;
};

/** @brief Return statement (optional return value). */
struct ReturnStmt
{
    std::optional<Expr> value;
};

/** @brief Expression statement. */
struct ExprStmt
{
    Expr expr;
};

/** Forward declaration for block nodes. */
struct Block;

/** @brief If statement with optional else block. */
struct IfStmt
{
    Expr cond;
    std::unique_ptr<Block> then_block;
    std::unique_ptr<Block> else_block;
};

/**
 * @brief While loop statement with optional loop contract clauses.
 *
 * A loop with an explicit contract block carries:
 *  - `invariants`: predicates that must hold at loop entry, be preserved by
 *    every iteration (under `invariant && cond`), and hold after the loop
 *    (alongside `!cond`).
 *  - `decreases`: an integer variant expression that must be non-negative at
 *    entry and strictly decrease each iteration, proving termination.
 *
 * Syntax:
 * @code
 * while (i < len)
 *   [ invariant 0 <= i && i <= len;
 *     decreases len - i; ]
 * { ... }
 * @endcode
 *
 * Loops without a contract block keep the legacy single-pass semantics (body
 * checked once with the condition assumed, everything after discarded).
 */
struct WhileStmt
{
    Expr cond;
    std::unique_ptr<Block> body;
    std::vector<Pred> invariants;
    std::optional<Pred> decreases;
};

/** @brief Match arm pattern: `Enum::Variant` or `Enum::Variant(payload)`. */
struct MatchArmPattern
{
    curlee::source::Span span;
    std::string_view enum_name;
    std::string_view variant_name;
    std::optional<std::string_view> payload_name;
};

/** @brief One `match` arm with a variant pattern and block body. */
struct MatchArm
{
    curlee::source::Span span;
    MatchArmPattern pattern;
    std::unique_ptr<Block> body;
};

/** @brief Match statement over an enum value. */
struct MatchStmt
{
    Expr value;
    std::vector<MatchArm> arms;
};

/** @brief Unsafe block statement (MVP semantics for capabilities). */
struct UnsafeStmt
{
    std::unique_ptr<Block> body;
};

/** @brief Block statement wrapper. */
struct BlockStmt
{
    std::unique_ptr<Block> block;
};

/**
 * @brief General statement node with span and variant payload.
 */
struct Stmt
{
    curlee::source::Span span;
    std::variant<LetStmt, GhostLetStmt, AssignStmt, ReturnStmt, ExprStmt, BlockStmt, IfStmt,
                 WhileStmt, MatchStmt, UnsafeStmt, IndexAssignStmt>
        node;
};

/** @brief A sequence of statements with a source span. */
struct Block
{
    curlee::source::Span span;
    std::vector<Stmt> stmts;
};

/**
 * @brief Top-level function definition with parameters, body and contracts.
 */
struct Function
{
    curlee::source::Span span;
    std::string_view name;
    Block body;

    // True when this is an `extern fn` declaration (no body; the implementation
    // is provided at link time by a host stub/runtime). Extern functions are a
    // trusted boundary: their contracts are assumed, not verified, and the VM
    // cannot execute them (freestanding build --link only).
    bool is_extern = false;

    // True when this is a `ghost fn` (verification-only). Ghost functions are
    // type-checked and verified like normal functions, but their bodies are
    // erased from codegen: they are never emitted to VM bytecode or freestanding
    // C, and they cannot be `main`.
    bool is_ghost = false;

    struct Param
    {
        curlee::source::Span span;
        std::string_view name;
        TypeName type;
        std::optional<Pred> refinement;
    };

    std::vector<Param> params;
    std::vector<Pred> requires_clauses;
    std::vector<Pred> ensures;

    // Contract-block ghost snapshots: `ghost let name[: Type] = expr;` clauses
    // written inside the `[...]` contract block, before the body. Each snapshot
    // freezes the pre-state value of `expr` into `name` so the function's own
    // `ensures` (and, via call-site post-state inheritance, its callers) can
    // reference it. Like all ghost code, snapshots are verification-only and
    // erased from codegen.
    std::vector<GhostLetStmt> ghost_lets;

    // Optional static fuel bound (WCET) contract clause: `fuel <pred>;` inside
    // the contract block. `<pred>` is an Int-valued expression over the
    // function's inputs that bounds the worst-case execution cost (in abstract
    // fuel units). For extern functions the bound is a *trusted* annotation of
    // the opaque body's cost. When absent, a function is not subject to a
    // static fuel obligation (backward-compatible).
    std::optional<Pred> fuel_bound;

    // Optional return type (MVP: identifier only). Present when `->` appears.
    std::optional<TypeName> return_type;
};

/** @brief Import declaration (module path and optional alias). */
struct ImportDecl
{
    curlee::source::Span span;
    std::vector<std::string_view> path;
    std::optional<std::string_view> alias;
};

/** @brief Field declaration inside a struct. */
struct StructDeclField
{
    curlee::source::Span span;
    std::string_view name;
    TypeName type;
};

/** @brief Struct declaration with fields. */
struct StructDecl
{
    curlee::source::Span span;
    std::string_view name;
    std::vector<StructDeclField> fields;
};

/** @brief Variant of an enum type, optionally carrying a payload type. */
struct EnumDeclVariant
{
    curlee::source::Span span;
    std::string_view name;
    std::optional<TypeName> payload;
};

/** @brief Enum declaration with named variants. */
struct EnumDecl
{
    curlee::source::Span span;
    std::string_view name;
    std::vector<EnumDeclVariant> variants;
};

/**
 * @brief Module-level mutable state declaration: `static name: Type = expr;`.
 *
 * Declares a file-scope mutable binding initialized exactly once, readable and
 * assignable (via #268's assignment mechanism) from any function in the module,
 * and persisting across separate top-level calls into the module (issue #287 —
 * the C `static`/global-variable equivalent). MVP types are the storable
 * scalars (Int/Bool/U8/U16/U32/U64) and fixed-size arrays `[T; N]` (#278), so
 * the joeos driver patterns (`vbe_state.c`'s four scalar framebuffer globals,
 * `net_stack.c`'s phase/seq/port scalars + 256-byte response body) are
 * expressible. The initializer must be a compile-time literal (an Int/Bool
 * literal for scalars, an `[v; N]` repeat literal for arrays), mirroring C
 * `static` initializers. Module-private: there is no cross-module extern
 * mechanism in the MVP.
 */
struct StaticVarDecl
{
    curlee::source::Span span;
    std::string_view name;
    TypeName type;
    Expr value;
};

/** @brief Full parsed program (imports, types, module state and functions). */
struct Program
{
    std::vector<ImportDecl> imports;
    std::vector<StructDecl> structs;
    std::vector<EnumDecl> enums;
    // Module-level mutable state (`static name: Type = expr;`), in declaration
    // order (initializers are emitted in this order in the freestanding C
    // output).
    std::vector<StaticVarDecl> statics;
    std::vector<Function> functions;
};

} // namespace curlee::parser
