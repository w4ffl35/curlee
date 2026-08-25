// SPDX-License-Identifier: MIT
#include <cstddef>
#include <curlee/verification/cost_model.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace curlee::verification
{
namespace
{

using curlee::diag::Diagnostic;
using curlee::parser::AssignStmt;
using curlee::parser::BinaryExpr;
using curlee::parser::Block;
using curlee::parser::BlockStmt;
using curlee::parser::BoolExpr;
using curlee::parser::CallExpr;
using curlee::parser::Expr;
using curlee::parser::ExprStmt;
using curlee::parser::Function;
using curlee::parser::GhostLetStmt;
using curlee::parser::GroupExpr;
using curlee::parser::IfStmt;
using curlee::parser::IntExpr;
using curlee::parser::LetStmt;
using curlee::parser::MatchStmt;
using curlee::parser::MemberExpr;
using curlee::parser::NameExpr;
using curlee::parser::PhysExpr;
using curlee::parser::PhysReadExpr;
using curlee::parser::PhysWriteExpr;
using curlee::parser::ReturnStmt;
using curlee::parser::ScopedNameExpr;
using curlee::parser::Stmt;
using curlee::parser::StringExpr;
using curlee::parser::StructLiteralExpr;
using curlee::parser::UnaryExpr;
using curlee::parser::UnsafeStmt;
using curlee::parser::WhileStmt;
using curlee::source::Span;

Diagnostic error_at(Span span, std::string message)
{
    Diagnostic d;
    d.severity = curlee::diag::Severity::Error;
    d.message = std::move(message);
    d.span = span;
    return d;
}

// Symbolic max: ite(a > b, a, b). Linear in Z3.
z3::expr z3_max(const z3::expr& a, const z3::expr& b)
{
    return z3::ite(a > b, a, b);
}

// Instruction cost of a leaf expression (constants, names, phys addresses).
// Non-leaf nodes are walked recursively by cost_expr so nested calls are
// charged through the call-graph fuel table; this helper only prices the
// leaf/op overhead that is not a call.
std::size_t leaf_instruction_cost(const Expr& e)
{
    return std::visit(
        [&](const auto& node) -> std::size_t
        {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, IntExpr> || std::is_same_v<Node, BoolExpr> ||
                          std::is_same_v<Node, StringExpr> || std::is_same_v<Node, NameExpr> ||
                          std::is_same_v<Node, PhysExpr> || std::is_same_v<Node, ScopedNameExpr>)
            {
                // Constant/name/phys-address load: one instruction.
                return 1;
            }
            return 0;
        },
        e.node);
}

// Number of extra instructions emitted for a binary operator (the operator
// itself plus short-circuit branch/constant sequences for &&/||).
std::size_t binary_op_overhead(curlee::lexer::TokenKind op)
{
    return (op == curlee::lexer::TokenKind::AndAnd || op == curlee::lexer::TokenKind::OrOr) ? 3 : 1;
}

} // namespace

CostResult
CostModel::cost_function(const Function& f, const LoweringContext& ctx,
                         const std::unordered_map<std::string_view, FuelEntry>& fuel_table,
                         const LoopEntryCtxMap* loop_entry_ctxs)
{
    if (f.is_extern)
    {
        // Extern bodies are trusted boundaries with a declared fuel annotation
        // (checked by the verifier); the model never costs an opaque body.
        return ctx.ctx.int_val(0);
    }
    // Push the function's own name so a call to itself (or a cycle through
    // other functions) is detected as recursion and fails closed.
    call_stack_.clear();
    call_stack_.push_back(f.name);
    auto cost = cost_block(f.body, ctx, fuel_table, loop_entry_ctxs);
    call_stack_.pop_back();
    return cost;
}

CostResult CostModel::cost_block(const Block& block, const LoweringContext& ctx,
                                 const std::unordered_map<std::string_view, FuelEntry>& fuel_table,
                                 const LoopEntryCtxMap* loop_entry_ctxs)
{
    z3::expr total = ctx.ctx.int_val(0);
    for (const auto& stmt : block.stmts)
    {
        auto stmt_cost = cost_stmt(stmt, ctx, fuel_table, loop_entry_ctxs);
        if (std::holds_alternative<Diagnostic>(stmt_cost))
        {
            return std::get<Diagnostic>(std::move(stmt_cost));
        }
        total = total + std::get<z3::expr>(stmt_cost);
    }
    return total;
}

CostResult CostModel::cost_stmt(const Stmt& stmt, const LoweringContext& ctx,
                                const std::unordered_map<std::string_view, FuelEntry>& fuel_table,
                                const LoopEntryCtxMap* loop_entry_ctxs)
{
    return std::visit(
        [&](const auto& node) -> CostResult
        {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, LetStmt>)
            {
                // `let x = e;` costs 1 (the StoreLocal) plus the initializer's
                // cost, which recursively charges any nested calls.
                auto init = cost_expr(node.value, ctx, fuel_table);
                if (std::holds_alternative<Diagnostic>(init))
                {
                    return std::get<Diagnostic>(std::move(init));
                }
                return ctx.ctx.int_val(1) + std::get<z3::expr>(init);
            }
            else if constexpr (std::is_same_v<Node, GhostLetStmt>)
            {
                // Ghost snapshots are verification-only and erased from codegen:
                // they cost zero at runtime.
                return ctx.ctx.int_val(0);
            }
            else if constexpr (std::is_same_v<Node, AssignStmt>)
            {
                // `x = expr;` costs 1 (the StoreLocal) plus the RHS cost, which
                // recursively charges any nested calls — mirroring the emitter's
                // instruction encoding for a reassignment.
                auto rhs = cost_expr(node.value, ctx, fuel_table);
                if (std::holds_alternative<Diagnostic>(rhs))
                {
                    return std::get<Diagnostic>(std::move(rhs));
                }
                return ctx.ctx.int_val(1) + std::get<z3::expr>(rhs);
            }
            else if constexpr (std::is_same_v<Node, ReturnStmt>)
            {
                z3::expr cost = ctx.ctx.int_val(1); // Return/Ret instruction
                if (node.value.has_value())
                {
                    auto value_cost = cost_expr(*node.value, ctx, fuel_table);
                    if (std::holds_alternative<Diagnostic>(value_cost))
                    {
                        return std::get<Diagnostic>(std::move(value_cost));
                    }
                    cost = cost + std::get<z3::expr>(value_cost);
                }
                return cost;
            }
            else if constexpr (std::is_same_v<Node, ExprStmt>)
            {
                return cost_expr(node.expr, ctx, fuel_table);
            }
            else if constexpr (std::is_same_v<Node, BlockStmt>)
            {
                return node.block != nullptr
                           ? cost_block(*node.block, ctx, fuel_table, loop_entry_ctxs)
                           : CostResult{ctx.ctx.int_val(0)};
            }
            else if constexpr (std::is_same_v<Node, IfStmt>)
            {
                // cost(c) + max(cost(A), cost(B)). The branch instructions
                // (JumpIfFalse, Jump) are charged as part of the condition cost.
                auto cond_cost = cost_expr(node.cond, ctx, fuel_table);
                if (std::holds_alternative<Diagnostic>(cond_cost))
                {
                    return std::get<Diagnostic>(std::move(cond_cost));
                }
                z3::expr cond_total =
                    ctx.ctx.int_val(2) + std::get<z3::expr>(cond_cost); // cond + 2 branch instrs
                auto then_cost = cost_block(*node.then_block, ctx, fuel_table, loop_entry_ctxs);
                if (std::holds_alternative<Diagnostic>(then_cost))
                {
                    return std::get<Diagnostic>(std::move(then_cost));
                }
                auto else_cost = ctx.ctx.int_val(0);
                if (node.else_block != nullptr)
                {
                    auto ec = cost_block(*node.else_block, ctx, fuel_table, loop_entry_ctxs);
                    if (std::holds_alternative<Diagnostic>(ec))
                    {
                        return std::get<Diagnostic>(std::move(ec));
                    }
                    else_cost = std::get<z3::expr>(ec);
                }
                return cond_total + z3_max(std::get<z3::expr>(then_cost), else_cost);
            }
            else if constexpr (std::is_same_v<Node, WhileStmt>)
            {
                // A loop's static cost is only defined when it has a provable
                // `decreases` variant (reusing #261's termination proof):
                //   (D_0 + 1) * (cost(cond) + cost(body))
                // Fail closed: without `decreases`, the fuel obligation cannot
                // be discharged (consistent with the roadmap's "no proof, no
                // run" gate).
                if (!node.decreases.has_value())
                {
                    return error_at(stmt.span,
                                    "fuel cost requires a 'decreases' variant on every loop "
                                    "(add '[ decreases <expr>; ]' to the loop contract)");
                }

                // Fail closed if the loop body shadows (rebinds via `let`) a name
                // that the variant expression references: in that case the
                // variant's termination proof and the fuel iteration bound are
                // both vacuous (the body's fresh binding is solver-unconstrained),
                // so accepting any bound would be unsound. Note that a plain
                // ASSIGNMENT in the body is the legitimate loop-carried mutation
                // this feature exists for, and is not collected by
                // collect_let_bindings.
                std::unordered_set<std::string_view> body_bindings;
                collect_let_bindings(*node.body, body_bindings);
                if (!body_bindings.empty() && pred_mentions_name(*node.decreases, body_bindings))
                {
                    return error_at(stmt.span,
                                    "decreases/fuel variant references a name shadowed in the "
                                    "loop body (the termination and fuel bounds would be "
                                    "vacuous; rename the shadowed binding or the variant)");
                }

                // D_0 is the variant's value at LOOP ENTRY. The verifier records
                // the entry lowering context at each `while` (assignment rebinds
                // loop-carried names to fresh symbols); lower the variant against
                // that snapshot when available so a genuinely bounded loop's fuel
                // cost stays tied to the pre-loop values. Falls back to the
                // passed ctx for direct unit-test callers.
                const LoweringContext& variant_ctx =
                    (loop_entry_ctxs != nullptr)
                        ? ([&]() -> const LoweringContext& {
                              // `node` is the SAME WhileStmt object the
                              // verifier visited (std::get<WhileStmt> on the
                              // shared AST), so the address matches the
                              // recorded loop-entry snapshot.
                              if (auto it = loop_entry_ctxs->find(&node);
                                  it != loop_entry_ctxs->end())
                              {
                                  return it->second;
                              }
                              return ctx;
                          }())
                        : ctx;

                auto variant = lower_predicate_int(*node.decreases, variant_ctx);
                if (std::holds_alternative<Diagnostic>(variant))
                {
                    return std::get<Diagnostic>(std::move(variant));
                }
                z3::expr d0 = std::get<z3::expr>(std::move(variant));
                // Variant must be non-negative; the verifier's termination proof
                // discharges this. Guard the formula with a max so the symbolic
                // cost stays >= 0 even if the solver is given a negative D_0.
                z3::expr d0_clamped = z3_max(d0, ctx.ctx.int_val(0));

                auto cond_cost = cost_expr(node.cond, ctx, fuel_table);
                if (std::holds_alternative<Diagnostic>(cond_cost))
                {
                    return std::get<Diagnostic>(std::move(cond_cost));
                }
                auto body_cost = cost_block(*node.body, ctx, fuel_table, loop_entry_ctxs);
                if (std::holds_alternative<Diagnostic>(body_cost))
                {
                    return std::get<Diagnostic>(std::move(body_cost));
                }
                // Per-iteration cost: condition (plus the 2 loop branch
                // instructions) + body.
                z3::expr per_iter = ctx.ctx.int_val(2) + std::get<z3::expr>(cond_cost) +
                                    std::get<z3::expr>(body_cost);
                return (d0_clamped + 1) * per_iter;
            }
            else if constexpr (std::is_same_v<Node, MatchStmt>)
            {
                // cost(value) + max over arms of cost(arm body) + 1 dispatch.
                auto value_cost = cost_expr(node.value, ctx, fuel_table);
                if (std::holds_alternative<Diagnostic>(value_cost))
                {
                    return std::get<Diagnostic>(std::move(value_cost));
                }
                z3::expr max_arm = ctx.ctx.int_val(0);
                bool any_arm = false;
                for (const auto& arm : node.arms)
                {
                    if (arm.body == nullptr)
                    {
                        continue;
                    }
                    auto arm_cost = cost_block(*arm.body, ctx, fuel_table, loop_entry_ctxs);
                    if (std::holds_alternative<Diagnostic>(arm_cost))
                    {
                        return std::get<Diagnostic>(std::move(arm_cost));
                    }
                    max_arm = any_arm ? z3_max(max_arm, std::get<z3::expr>(arm_cost))
                                      : std::get<z3::expr>(arm_cost);
                    any_arm = true;
                }
                z3::expr base = ctx.ctx.int_val(1) + std::get<z3::expr>(value_cost); // 1 dispatch
                return base + (any_arm ? max_arm : ctx.ctx.int_val(0));
            }
            else if constexpr (std::is_same_v<Node, UnsafeStmt>)
            {
                return node.body != nullptr
                           ? cost_block(*node.body, ctx, fuel_table, loop_entry_ctxs)
                           : CostResult{ctx.ctx.int_val(0)};
            }
            return CostResult{ctx.ctx.int_val(0)};
        },
        stmt.node);
}

// The single recursive expression cost walker. Every non-leaf node recurses
// into its children via cost_expr so that a CallExpr ANYWHERE in the tree is
// charged its declared callee fuel (DEFECT 1 fix). Leaf nodes price a flat
// instruction count.
CostResult CostModel::cost_expr(const Expr& expr, const LoweringContext& ctx,
                                const std::unordered_map<std::string_view, FuelEntry>& fuel_table)
{
    return std::visit(
        [&](const auto& node) -> CostResult
        {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, CallExpr>)
            {
                return cost_call(node, ctx, fuel_table);
            }
            else if constexpr (std::is_same_v<Node, UnaryExpr>)
            {
                // Unary op: operand cost + one op instruction.
                auto rhs = cost_expr(*node.rhs, ctx, fuel_table);
                if (std::holds_alternative<Diagnostic>(rhs))
                {
                    return std::get<Diagnostic>(std::move(rhs));
                }
                return ctx.ctx.int_val(1) + std::get<z3::expr>(rhs);
            }
            else if constexpr (std::is_same_v<Node, BinaryExpr>)
            {
                // Binary op: both operands + op overhead. Operands are walked
                // recursively so nested calls in either operand are charged.
                auto lhs = cost_expr(*node.lhs, ctx, fuel_table);
                if (std::holds_alternative<Diagnostic>(lhs))
                {
                    return std::get<Diagnostic>(std::move(lhs));
                }
                auto rhs = cost_expr(*node.rhs, ctx, fuel_table);
                if (std::holds_alternative<Diagnostic>(rhs))
                {
                    return std::get<Diagnostic>(std::move(rhs));
                }
                z3::expr total = ctx.ctx.int_val(static_cast<int>(binary_op_overhead(node.op)));
                return total + std::get<z3::expr>(lhs) + std::get<z3::expr>(rhs);
            }
            else if constexpr (std::is_same_v<Node, MemberExpr>)
            {
                auto base = cost_expr(*node.base, ctx, fuel_table);
                if (std::holds_alternative<Diagnostic>(base))
                {
                    return std::get<Diagnostic>(std::move(base));
                }
                return ctx.ctx.int_val(1) + std::get<z3::expr>(base);
            }
            else if constexpr (std::is_same_v<Node, GroupExpr>)
            {
                return cost_expr(*node.inner, ctx, fuel_table);
            }
            else if constexpr (std::is_same_v<Node, StructLiteralExpr>)
            {
                z3::expr total = ctx.ctx.int_val(1);
                for (const auto& field : node.fields)
                {
                    if (field.value == nullptr)
                    {
                        continue;
                    }
                    auto fcost = cost_expr(*field.value, ctx, fuel_table);
                    if (std::holds_alternative<Diagnostic>(fcost))
                    {
                        return std::get<Diagnostic>(std::move(fcost));
                    }
                    total = total + std::get<z3::expr>(fcost);
                }
                return total;
            }
            else if constexpr (std::is_same_v<Node, PhysReadExpr>)
            {
                auto base = cost_expr(*node.base, ctx, fuel_table);
                if (std::holds_alternative<Diagnostic>(base))
                {
                    return std::get<Diagnostic>(std::move(base));
                }
                return ctx.ctx.int_val(1) + std::get<z3::expr>(base);
            }
            else if constexpr (std::is_same_v<Node, PhysWriteExpr>)
            {
                auto base = cost_expr(*node.base, ctx, fuel_table);
                if (std::holds_alternative<Diagnostic>(base))
                {
                    return std::get<Diagnostic>(std::move(base));
                }
                auto value = cost_expr(*node.value, ctx, fuel_table);
                if (std::holds_alternative<Diagnostic>(value))
                {
                    return std::get<Diagnostic>(std::move(value));
                }
                return ctx.ctx.int_val(1) + std::get<z3::expr>(base) + std::get<z3::expr>(value);
            }
            // Leaf nodes: flat instruction cost.
            return CostResult{ctx.ctx.int_val(static_cast<int>(leaf_instruction_cost(expr)))};
        },
        expr.node);
}

CostResult CostModel::cost_call(const CallExpr& call, const LoweringContext& ctx,
                                const std::unordered_map<std::string_view, FuelEntry>& fuel_table)
{
    // `f(args)` costs `declared_fuel_of(f)` from the call-graph fuel table, plus
    // the call-site overhead (argument evaluation + the Call instruction).
    const auto* callee = std::get_if<NameExpr>(&call.callee->node);
    if (callee == nullptr)
    {
        return error_at(call.callee->span, "fuel cost: calls must name a function directly");
    }

    // Recursion / call-graph cycle detection: if the callee is already on the
    // cost path, the call is (mutually) recursive. Re-charging the declared
    // bound per nesting site is NOT a sound bound — the recursion is unbounded —
    // so fail closed with a clear diagnostic instead of the misleading
    // "fuel bound may be exceeded".
    for (const auto& on_path : call_stack_)
    {
        if (on_path == callee->name)
        {
            return error_at(call.callee->span,
                            "recursive call: fuel cost is not bounded; declare a "
                            "'decreases'-based termination argument or an explicit "
                            "recursion bound");
        }
    }

    auto it = fuel_table.find(callee->name);
    if (it == fuel_table.end())
    {
        return error_at(call.callee->span,
                        "fuel cost: no fuel cost computable for callee '" +
                            std::string(callee->name) +
                            "' (declare a '[ fuel <bound>; ]' contract on the callee)");
    }

    // Argument evaluation cost: 1 per argument for the load/instruction plus the
    // recursively-walked cost of each argument (so nested calls in arguments are
    // charged too).
    z3::expr arg_cost = ctx.ctx.int_val(1); // the Call instruction itself
    for (const auto& arg : call.args)
    {
        auto acost = cost_expr(arg, ctx, fuel_table);
        if (std::holds_alternative<Diagnostic>(acost))
        {
            return std::get<Diagnostic>(std::move(acost));
        }
        arg_cost = arg_cost + std::get<z3::expr>(acost);
    }

    const FuelEntry& entry = it->second;
    if (entry.declared_bound == nullptr)
    {
        return error_at(call.callee->span,
                        "fuel cost: callee '" + std::string(callee->name) +
                            "' has no declared fuel bound (add '[ fuel <bound>; ]')");
    }

    // Lower the callee's declared bound at the call site, substituting the
    // callee's parameters with the caller's argument expressions.
    LoweringContext sub_ctx(ctx.ctx);
    sub_ctx.ghost_fns = ctx.ghost_fns;
    sub_ctx.result_int = ctx.result_int;
    sub_ctx.result_bool = ctx.result_bool;

    if (entry.decl != nullptr)
    {
        if (call.args.size() != entry.decl->params.size())
        {
            return error_at(call.callee->span, "fuel cost: argument count mismatch for callee '" +
                                                   std::string(callee->name) + "'");
        }
        // Start from the caller's variable bindings so the bound can reference
        // caller-scope names, then override the callee's parameters with the
        // caller's argument expressions.
        sub_ctx.int_vars = ctx.int_vars;
        sub_ctx.bool_vars = ctx.bool_vars;
        for (std::size_t i = 0; i < entry.decl->params.size(); ++i)
        {
            const auto& param = entry.decl->params[i];
            if (!lower_expr_)
            {
                return error_at(call.callee->span,
                                "fuel cost: cannot evaluate argument for callee '" +
                                    std::string(callee->name) + "'");
            }
            auto arg = lower_expr_(call.args[i], sub_ctx);
            if (!arg.has_value())
            {
                return error_at(call.callee->span,
                                "fuel cost: cannot lower call argument for callee '" +
                                    std::string(callee->name) + "'");
            }
            sub_ctx.int_vars.insert_or_assign(param.name, *arg);
        }
    }

    auto bound_res = lower_predicate_int_named(*entry.declared_bound, sub_ctx, "fuel");
    if (std::holds_alternative<Diagnostic>(bound_res))
    {
        return std::get<Diagnostic>(std::move(bound_res));
    }

    return arg_cost + std::get<z3::expr>(bound_res);
}

void CostModel::collect_let_bindings(const Block& block, std::unordered_set<std::string_view>& out)
{
    for (const auto& stmt : block.stmts)
    {
        std::visit(
            [&](const auto& node)
            {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, LetStmt>)
                {
                    out.insert(node.name);
                }
                else if constexpr (std::is_same_v<Node, GhostLetStmt>)
                {
                    out.insert(node.name);
                }
                else if constexpr (std::is_same_v<Node, BlockStmt>)
                {
                    if (node.block != nullptr)
                    {
                        collect_let_bindings(*node.block, out);
                    }
                }
                else if constexpr (std::is_same_v<Node, IfStmt>)
                {
                    collect_let_bindings(*node.then_block, out);
                    if (node.else_block != nullptr)
                    {
                        collect_let_bindings(*node.else_block, out);
                    }
                }
                else if constexpr (std::is_same_v<Node, WhileStmt>)
                {
                    collect_let_bindings(*node.body, out);
                }
                else if constexpr (std::is_same_v<Node, MatchStmt>)
                {
                    for (const auto& arm : node.arms)
                    {
                        if (arm.body != nullptr)
                        {
                            collect_let_bindings(*arm.body, out);
                        }
                    }
                }
                else if constexpr (std::is_same_v<Node, UnsafeStmt>)
                {
                    if (node.body != nullptr)
                    {
                        collect_let_bindings(*node.body, out);
                    }
                }
            },
            stmt.node);
    }
}

bool CostModel::pred_mentions_name(const curlee::parser::Pred& pred,
                                   const std::unordered_set<std::string_view>& names)
{
    bool found = false;
    std::visit(
        [&](const auto& node)
        {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, curlee::parser::PredName>)
            {
                if (names.count(node.name) != 0)
                {
                    found = true;
                }
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredCall>)
            {
                for (const auto& arg : node.args)
                {
                    if (pred_mentions_name(arg, names))
                    {
                        found = true;
                    }
                }
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredUnary>)
            {
                if (pred_mentions_name(*node.rhs, names))
                {
                    found = true;
                }
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredBinary>)
            {
                if (pred_mentions_name(*node.lhs, names) || pred_mentions_name(*node.rhs, names))
                {
                    found = true;
                }
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredGroup>)
            {
                if (pred_mentions_name(*node.inner, names))
                {
                    found = true;
                }
            }
        },
        pred.node);
    return found;
}

} // namespace curlee::verification
