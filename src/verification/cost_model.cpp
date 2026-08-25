// SPDX-License-Identifier: MIT
#include <cstddef>
#include <curlee/verification/cost_model.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace curlee::verification
{
namespace
{

using curlee::diag::Diagnostic;
using curlee::parser::BinaryExpr;
using curlee::parser::Block;
using curlee::parser::BlockStmt;
using curlee::parser::CallExpr;
using curlee::parser::Expr;
using curlee::parser::ExprStmt;
using curlee::parser::Function;
using curlee::parser::GhostLetStmt;
using curlee::parser::GroupExpr;
using curlee::parser::IfStmt;
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

// Cost of an expression's own instructions (excluding callee bodies, which are
// charged via the call-graph fuel table). The emitter emits one instruction per
// expression node (constants, loads, arithmetic ops, comparisons, jumps for
// short-circuit operators), so the node count is the symbolic instruction cost.
std::size_t expr_instruction_cost(const Expr& e)
{
    return std::visit(
        [&](const auto& node) -> std::size_t
        {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, curlee::parser::IntExpr> ||
                          std::is_same_v<Node, curlee::parser::BoolExpr> ||
                          std::is_same_v<Node, curlee::parser::StringExpr> ||
                          std::is_same_v<Node, NameExpr> || std::is_same_v<Node, PhysExpr>)
            {
                // Constant/name/phys-address load: one instruction.
                return 1;
            }
            else if constexpr (std::is_same_v<Node, ScopedNameExpr>)
            {
                return 1;
            }
            else if constexpr (std::is_same_v<Node, UnaryExpr>)
            {
                // Unary op: operand cost + one op instruction.
                return 1 + expr_instruction_cost(*node.rhs);
            }
            else if constexpr (std::is_same_v<Node, BinaryExpr>)
            {
                // Binary op: both operands + one op instruction. Short-circuit
                // logical ops additionally emit branch/constant instructions,
                // approximated as a flat +1.
                const std::size_t op_cost = (node.op == curlee::lexer::TokenKind::AndAnd ||
                                             node.op == curlee::lexer::TokenKind::OrOr)
                                                ? 3
                                                : 1;
                return op_cost + expr_instruction_cost(*node.lhs) +
                       expr_instruction_cost(*node.rhs);
            }
            else if constexpr (std::is_same_v<Node, MemberExpr>)
            {
                return 1 + expr_instruction_cost(*node.base);
            }
            else if constexpr (std::is_same_v<Node, CallExpr>)
            {
                // The callee's own cost is charged through the fuel table (see
                // cost_expr); the argument evaluation instructions are counted
                // here.
                std::size_t cost = 1; // the Call instruction itself
                for (const auto& arg : node.args)
                {
                    cost += expr_instruction_cost(arg);
                }
                return cost;
            }
            else if constexpr (std::is_same_v<Node, GroupExpr>)
            {
                return expr_instruction_cost(*node.inner);
            }
            else if constexpr (std::is_same_v<Node, StructLiteralExpr>)
            {
                std::size_t cost = 1;
                for (const auto& f : node.fields)
                {
                    if (f.value != nullptr)
                    {
                        cost += expr_instruction_cost(*f.value);
                    }
                }
                return cost;
            }
            else if constexpr (std::is_same_v<Node, PhysReadExpr>)
            {
                return 1 + (node.base != nullptr ? expr_instruction_cost(*node.base) : 0);
            }
            else if constexpr (std::is_same_v<Node, PhysWriteExpr>)
            {
                return 1 + (node.base != nullptr ? expr_instruction_cost(*node.base) : 0) +
                       (node.value != nullptr ? expr_instruction_cost(*node.value) : 0);
            }
            return 0;
        },
        e.node);
}

} // namespace

CostResult
CostModel::cost_function(const Function& f, const LoweringContext& ctx,
                         const std::unordered_map<std::string_view, FuelEntry>& fuel_table)
{
    if (f.is_extern)
    {
        // Extern bodies are trusted boundaries with a declared fuel annotation
        // (checked by the verifier); the model never costs an opaque body.
        return ctx.ctx.int_val(0);
    }
    return cost_block(f.body, ctx, fuel_table);
}

CostResult CostModel::cost_block(const Block& block, const LoweringContext& ctx,
                                 const std::unordered_map<std::string_view, FuelEntry>& fuel_table)
{
    z3::expr total = ctx.ctx.int_val(0);
    for (const auto& stmt : block.stmts)
    {
        auto stmt_cost = cost_stmt(stmt, ctx, fuel_table);
        if (std::holds_alternative<Diagnostic>(stmt_cost))
        {
            return std::get<Diagnostic>(std::move(stmt_cost));
        }
        total = total + std::get<z3::expr>(stmt_cost);
    }
    return total;
}

CostResult CostModel::cost_stmt(const Stmt& stmt, const LoweringContext& ctx,
                                const std::unordered_map<std::string_view, FuelEntry>& fuel_table)
{
    return std::visit(
        [&](const auto& node) -> CostResult
        {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, LetStmt>)
            {
                // `let x = e;` costs 1 (the StoreLocal) plus the initializer's
                // own cost. The initializer is routed through cost_expr so a
                // call inside the initializer propagates the callee's declared
                // fuel via the call-graph table (fail closed when unbounded).
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
                return node.block != nullptr ? cost_block(*node.block, ctx, fuel_table)
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
                auto then_cost = cost_block(*node.then_block, ctx, fuel_table);
                if (std::holds_alternative<Diagnostic>(then_cost))
                {
                    return std::get<Diagnostic>(std::move(then_cost));
                }
                auto else_cost = ctx.ctx.int_val(0);
                if (node.else_block != nullptr)
                {
                    auto ec = cost_block(*node.else_block, ctx, fuel_table);
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
                auto variant = lower_predicate_int(*node.decreases, ctx);
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
                auto body_cost = cost_block(*node.body, ctx, fuel_table);
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
                    auto arm_cost = cost_block(*arm.body, ctx, fuel_table);
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
                return node.body != nullptr ? cost_block(*node.body, ctx, fuel_table)
                                            : CostResult{ctx.ctx.int_val(0)};
            }
            return CostResult{ctx.ctx.int_val(0)};
        },
        stmt.node);
}

CostResult CostModel::cost_expr(const Expr& expr, const LoweringContext& ctx,
                                const std::unordered_map<std::string_view, FuelEntry>& fuel_table)
{
    const auto* call = std::get_if<CallExpr>(&expr.node);
    if (call != nullptr)
    {
        return cost_call(*call, ctx, fuel_table);
    }
    return CostResult{ctx.ctx.int_val(static_cast<int>(expr_instruction_cost(expr)))};
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

    auto it = fuel_table.find(callee->name);
    if (it == fuel_table.end())
    {
        return error_at(call.callee->span,
                        "fuel cost: no fuel cost computable for callee '" +
                            std::string(callee->name) +
                            "' (declare a '[ fuel <bound>; ]' contract on the callee)");
    }

    std::size_t arg_cost = 1; // Call instruction
    for (const auto& arg : call.args)
    {
        arg_cost += expr_instruction_cost(arg);
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
            auto arg = lower_expr_(call.args[i]);
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

    return ctx.ctx.int_val(static_cast<int>(arg_cost)) + std::get<z3::expr>(bound_res);
}

} // namespace curlee::verification
