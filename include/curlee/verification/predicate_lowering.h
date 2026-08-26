// SPDX-License-Identifier: MIT
#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/parser/ast.h>
#include <curlee/types/type.h>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
#include <z3++.h>

/**
 * @file predicate_lowering.h
 * @brief Lower parser predicates into Z3 expressions for verification.
 */

namespace curlee::verification
{

/**
 * @brief Signature of a ghost function callable from within predicates.
 *
 * Ghost functions referenced in predicates (e.g. `ensures result == old_sum(v);`)
 * are lowered to uninterpreted function applications, mirroring the phys_read
 * machinery. The signature describes the Z3-visible parameter/result kinds.
 */
struct GhostFnSig
{
    curlee::types::TypeKind result = curlee::types::TypeKind::Unit;
    std::vector<curlee::types::TypeKind> params;
};

/** @brief Table of ghost function signatures: name -> signature. */
using GhostFnTable = std::unordered_map<std::string_view, GhostFnSig>;

/**
 * @brief Context used when lowering predicates to Z3 expressions.
 *
 * Holds temporary results and symbol maps for int and bool variables.
 */
struct LoweringContext
{
    explicit LoweringContext(z3::context& context) : ctx(context) {}

    z3::context& ctx;
    std::optional<z3::expr> result_int;
    std::optional<z3::expr> result_bool;
    std::unordered_map<std::string_view, z3::expr> int_vars;
    std::unordered_map<std::string_view, z3::expr> bool_vars;

    // Fixed-size array bindings (`let q: [T; N] = ...`): name -> array-sort
    // Z3 term (`(Array Int Int)` for the MVP element kinds). Array reads lower
    // to `select`, writes to `store`, so read-over-write coherence is exact.
    // Only used by the verifier's expression lowering (predicates are
    // Int/Bool-only and cannot reference array elements).
    std::unordered_map<std::string_view, z3::expr> array_vars;

    // Ghost function signatures used to lower predicate calls to uninterpreted
    // function applications. Null when the context is not wired to a verifier
    // (predicate calls are then rejected with a diagnostic).
    const GhostFnTable* ghost_fns = nullptr;
};

/** @brief Result of lowering: either a z3::expr or a diagnostic on error. */
using LoweringResult = std::variant<z3::expr, curlee::diag::Diagnostic>;

/** @brief Lower a parsed predicate into a Z3 expression using the given context. */
[[nodiscard]] LoweringResult lower_predicate(const curlee::parser::Pred& pred,
                                             const LoweringContext& ctx);

/**
 * @brief Lower a parsed predicate into an Int Z3 expression.
 *
 * Used for integer-valued clauses such as the `decreases` variant expression
 * in loop contracts. Unlike `lower_predicate`, the predicate must resolve to
 * an Int (not Bool) term.
 */
[[nodiscard]] LoweringResult lower_predicate_int(const curlee::parser::Pred& pred,
                                                 const LoweringContext& ctx);

/**
 * @brief Lower a parsed predicate into an Int Z3 expression, naming the clause.
 *
 * Same as `lower_predicate_int` but allows the caller to supply the clause
 * name used in the "… clause must resolve to Int" diagnostic (e.g. "fuel" for
 * static WCET bounds).
 */
[[nodiscard]] LoweringResult lower_predicate_int_named(const curlee::parser::Pred& pred,
                                                       const LoweringContext& ctx,
                                                       std::string_view clause_name);

} // namespace curlee::verification
