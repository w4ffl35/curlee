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
 * @brief A general expression node with id, span and variant payload.
 */
struct Expr
{
    std::size_t id = 0;
    curlee::source::Span span;
    std::variant<IntExpr, BoolExpr, StringExpr, NameExpr, UnaryExpr, BinaryExpr, CallExpr,
                 MemberExpr, GroupExpr, ScopedNameExpr, StructLiteralExpr, PhysExpr, PhysReadExpr,
                 PhysWriteExpr>
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
    std::variant<LetStmt, GhostLetStmt, ReturnStmt, ExprStmt, BlockStmt, IfStmt, WhileStmt,
                 MatchStmt, UnsafeStmt>
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

/** @brief Full parsed program (imports, types and functions). */
struct Program
{
    std::vector<ImportDecl> imports;
    std::vector<StructDecl> structs;
    std::vector<EnumDecl> enums;
    std::vector<Function> functions;
};

} // namespace curlee::parser
