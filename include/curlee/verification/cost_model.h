// SPDX-License-Identifier: MIT
#pragma once

#include <curlee/parser/ast.h>
#include <curlee/verification/predicate_lowering.h>
#include <curlee/verification/solver.h>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <z3++.h>

/**
 * @file cost_model.h
 * @brief Symbolic worst-case execution cost (WCET) model for `fuel` contract clauses.
 *
 * Issue #262 (M3 of the Zero-External-Test roadmap) adds static, solver-checked
 * fuel bounds. Each statement/expression lowers to a symbolic Z3 Int cost
 * expression in terms of the function's inputs:
 *
 *   - `let x = e;`                              -> 1
 *   - `if c {A} else {B}`                       -> cost(c) + max(cost(A), cost(B))
 *   - `while c [decreases D;] {B}`              -> (D_0 + 1) * (cost(c) + cost(B))
 *   - `f(args)`                                 -> declared fuel of f (call-graph table)
 *   - `ghost let x = e;`                        -> 0 (verification-only, erased from codegen)
 *   - block statements                          -> sum of statement costs
 *   - `return e;`                               -> cost(e) + 1
 *   - expression statement                      -> cost(e)
 *
 * The cost model mirrors the emitter's instruction encoding (each instruction
 * costs one fuel unit at runtime) and is the compiler-side single source of
 * truth for static costs.
 */

namespace curlee::verification
{

/**
 * @brief A symbolic cost expression, either a Z3 Int term or a diagnostic.
 */
using CostResult = std::variant<z3::expr, curlee::diag::Diagnostic>;

/**
 * @brief Loop-entry lowering contexts, keyed by the WhileStmt's address.
 *
 * The verifier records the name bindings in scope at each `while` loop's entry
 * so the cost model can lower the loop's `decreases` variant (D_0 in the
 * `(D_0 + 1) * (cost(c) + cost(B))` fuel formula) against the PRE-loop values
 * of loop-carried variables. Assignment rebinds a name to a fresh symbol, so a
 * context evaluated after the body would use a post-mutation symbol for D_0
 * and spuriously fail a genuinely bounded loop. Keyed by the statement's
 * address because the AST is stable and shared between the verifier's body
 * check and the cost model's walk.
 */
using LoopEntryCtxMap =
    std::unordered_map<const curlee::parser::WhileStmt*, LoweringContext>;

/**
 * @brief Static WCET cost model over a parsed function body.
 *
 * The model computes a symbolic Z3 Int cost for a function body. It relies on:
 *  - the lowering context (int/bool variable bindings) to resolve names and
 *    lower expressions/conditions into Z3 terms;
 *  - a call-graph fuel table mapping callee names to their declared fuel bound
 *    (an Int Z3 expression in terms of the *caller's* bindings, already
 *    substituted at the call site);
 *  - a fail-closed policy: loops without a provable `decreases` variant and
 *    extern calls without a `fuel` annotation cannot be bounded, so the model
 *    rejects them with a diagnostic instead of guessing a cost.
 */
class CostModel
{
  public:
    /**
     * @brief Call-graph fuel table entry.
     *
     * Maps a callee's fuel cost to its declared `fuel` bound. The bound is a
     * source-level predicate over the callee's parameters; at each call site the
     * cost model lowers it with the callee's parameters substituted by the
     * caller's argument expressions (so an input-dependent bound such as
     * `fuel 3 * n + 10;` is evaluated against the actual arguments).
     *
     * A callee is only costable when it declares a `fuel` bound (user functions
     * without one are out of MVP scope and fail closed when their cost is
     * needed). Extern functions must also declare a trusted cost.
     */
    struct FuelEntry
    {
        // The declared `fuel` bound predicate (non-null when present).
        const curlee::parser::Pred* declared_bound = nullptr;
        // The callee's declaration (provides parameter names for substitution).
        const curlee::parser::Function* decl = nullptr;
        bool is_extern = false;
    };

    /**
     * @brief Expression lowering callback provided by the verifier.
     *
     * The cost model needs to lower call arguments into Z3 terms so a callee's
     * declared fuel bound (an expression over the callee's parameters) can be
     * evaluated at the call site with the caller's argument expressions
     * substituted. `lower_expr` returns the Z3 expression for the given Expr
     * (lowered against `ctx`) when lowering succeeds, or std::nullopt (with a
     * diagnostic emitted by the caller) when it fails.
     */
    using ExprLowerFn = std::function<std::optional<z3::expr>(const curlee::parser::Expr&,
                                                              const LoweringContext& ctx)>;

    explicit CostModel(z3::context& context, ExprLowerFn lower_expr = {})
        : ctx_(context), lower_expr_(std::move(lower_expr))
    {
    }

    /**
     * @brief Compute the symbolic cost of a function body.
     *
     * @param f              the function whose body is costed
     * @param ctx            lowering context with parameter bindings already mapped
     * @param fuel_table     call-graph table; callees are looked up by name
     * @param loop_entry_ctxs optional loop-entry lowering contexts recorded by
     *                        the verifier (keyed by WhileStmt address). When
     *                        present, each loop's `decreases` variant is
     *                        lowered against its PRE-loop bindings (assignment
     *                        rebinds names to fresh symbols); when null, the
     *                        passed ctx is used (unit-test fallback).
     * @param diagnostics    on failure, a fail-closed diagnostic is returned
     */
    [[nodiscard]] CostResult cost_function(
        const curlee::parser::Function& f, const LoweringContext& ctx,
        const std::unordered_map<std::string_view, FuelEntry>& fuel_table,
        const LoopEntryCtxMap* loop_entry_ctxs = nullptr);

  private:
    z3::context& ctx_;
    ExprLowerFn lower_expr_;

    // Call path currently being costed, for recursion/cycle detection. A call
    // to a callee already on the path is a recursive (unbounded) call: its
    // static fuel cost is not bounded by re-charging the declared bound, so the
    // model fails closed with a clear diagnostic instead of under-charging.
    std::vector<std::string_view> call_stack_;

    [[nodiscard]] CostResult cost_block(
        const curlee::parser::Block& block, const LoweringContext& ctx,
        const std::unordered_map<std::string_view, FuelEntry>& fuel_table,
        const LoopEntryCtxMap* loop_entry_ctxs);
    [[nodiscard]] CostResult cost_stmt(
        const curlee::parser::Stmt& stmt, const LoweringContext& ctx,
        const std::unordered_map<std::string_view, FuelEntry>& fuel_table,
        const LoopEntryCtxMap* loop_entry_ctxs);
    [[nodiscard]] CostResult
    cost_expr(const curlee::parser::Expr& expr, const LoweringContext& ctx,
              const std::unordered_map<std::string_view, FuelEntry>& fuel_table);
    [[nodiscard]] CostResult
    cost_call(const curlee::parser::CallExpr& call, const LoweringContext& ctx,
              const std::unordered_map<std::string_view, FuelEntry>& fuel_table);

    /**
     * @brief Collect the set of names a predicate (or expression subtree) binds.
     *
     * Used to detect when a loop body shadows (rebinds via `let`) a name that a
     * `decreases` variant references: the variant's termination proof and the
     * fuel iteration bound would both be vacuous in that case, so the cost model
     * rejects the loop (fail closed).
     */
    static void collect_let_bindings(const curlee::parser::Block& block,
                                     std::unordered_set<std::string_view>& out);
    [[nodiscard]] static bool pred_mentions_name(const curlee::parser::Pred& pred,
                                                 const std::unordered_set<std::string_view>& names);
};

} // namespace curlee::verification
