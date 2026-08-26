// SPDX-License-Identifier: MIT
#include <curlee/verification/predicate_lowering.h>

namespace curlee::verification
{
namespace
{

z3::sort pred_sort_for_kind(z3::context& ctx, curlee::types::TypeKind kind)
{
    if (kind == curlee::types::TypeKind::Bool)
    {
        return ctx.bool_sort();
    }
    return ctx.int_sort();
}

// True when the Z3 expression is a top-level division or modulo term. Such
// terms are linear in Z3's Int theory (div/mod elimination is handled by the
// arithmetic solver), so a product involving one is not treated as
// non-linear (issue #270).
bool is_div_mod_term(const z3::expr& e)
{
    // Every expression reaching this helper is produced by lower_expr /
    // lower_predicate, which always construct Z3 apps (numerals, constants,
    // function applications); quantifier-bound (non-app) terms never occur.
    if (!e.is_app()) // GCOVR_EXCL_LINE
    {
        return false; // GCOVR_EXCL_LINE
    }
    switch (e.decl().decl_kind())
    {
    case Z3_OP_DIV:
    case Z3_OP_IDIV:
    case Z3_OP_MOD:
    case Z3_OP_REM:
        return true;
    default:
        return false;
    }
}

enum class PredType
{
    Int,
    Bool,
};

struct TypedExpr
{
    z3::expr expr;
    PredType type;
    bool is_literal = false;
};

curlee::diag::Diagnostic error_at(curlee::source::Span span, std::string message)
{
    curlee::diag::Diagnostic d;
    d.severity = curlee::diag::Severity::Error;
    d.message = std::move(message);
    d.span = span;
    return d;
}

using TypedResult = std::variant<TypedExpr, curlee::diag::Diagnostic>;

TypedResult lower_node(const curlee::parser::Pred& pred, const LoweringContext& ctx)
{
    return std::visit(
        [&](const auto& node) -> TypedResult
        {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, curlee::parser::PredInt>)
            {
                const std::string literal(node.lexeme);
                return TypedExpr{ctx.ctx.int_val(literal.c_str()), PredType::Int, true};
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredBool>)
            {
                return TypedExpr{ctx.ctx.bool_val(node.value), PredType::Bool, true};
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredName>)
            {
                if (node.name == "result")
                {
                    if (ctx.result_int.has_value())
                    {
                        return TypedExpr{*ctx.result_int, PredType::Int, false};
                    }
                    if (ctx.result_bool.has_value())
                    {
                        return TypedExpr{*ctx.result_bool, PredType::Bool, false};
                    }
                }
                if (auto it = ctx.int_vars.find(node.name); it != ctx.int_vars.end())
                {
                    return TypedExpr{it->second, PredType::Int, false};
                }
                if (auto it = ctx.bool_vars.find(node.name); it != ctx.bool_vars.end())
                {
                    return TypedExpr{it->second, PredType::Bool, false};
                }
                return error_at(pred.span,
                                "unknown predicate name '" + std::string(node.name) + "'");
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredCall>)
            {
                // Calls in predicates are restricted to ghost functions, which
                // lower to uninterpreted function applications (mirroring the
                // phys_read machinery).
                if (ctx.ghost_fns == nullptr)
                {
                    return error_at(pred.span,
                                    "calls are not supported in verification expressions");
                }
                auto sig_it = ctx.ghost_fns->find(node.callee);
                if (sig_it == ctx.ghost_fns->end())
                {
                    return error_at(pred.span,
                                    "unknown ghost function '" + std::string(node.callee) + "'");
                }
                const GhostFnSig& sig = sig_it->second;
                if (node.args.size() != sig.params.size())
                {
                    return error_at(pred.span,
                                    "ghost function '" + std::string(node.callee) + "' expects " +
                                        std::to_string(sig.params.size()) + " argument(s), got " +
                                        std::to_string(node.args.size()));
                }

                std::vector<z3::expr> arg_exprs;
                arg_exprs.reserve(node.args.size());
                for (std::size_t i = 0; i < node.args.size(); ++i)
                {
                    auto arg_res = lower_node(node.args[i], ctx);
                    if (std::holds_alternative<curlee::diag::Diagnostic>(arg_res))
                    {
                        return std::get<curlee::diag::Diagnostic>(arg_res);
                    }
                    auto arg = std::get<TypedExpr>(arg_res);
                    const bool want_bool = (sig.params[i] == curlee::types::TypeKind::Bool);
                    const bool got_bool = (arg.type == PredType::Bool);
                    if (want_bool != got_bool)
                    {
                        return error_at(node.args[i].span, "ghost function argument type mismatch");
                    }
                    arg_exprs.push_back(arg.expr);
                }

                if (sig.result != curlee::types::TypeKind::Int &&
                    sig.result != curlee::types::TypeKind::Bool)
                {
                    return error_at(pred.span, "ghost function '" + std::string(node.callee) +
                                                   "' must return Int or Bool in predicates");
                }

                z3::sort_vector arg_sorts(ctx.ctx);
                for (const auto k : sig.params)
                {
                    arg_sorts.push_back(pred_sort_for_kind(ctx.ctx, k));
                }
                const z3::sort res_sort = pred_sort_for_kind(ctx.ctx, sig.result);
                const std::string fname = "ghost_" + std::string(node.callee);
                auto decl = ctx.ctx.function(fname.c_str(), arg_sorts, res_sort);

                z3::expr app = decl(static_cast<unsigned>(arg_exprs.size()), arg_exprs.data());
                return TypedExpr{app,
                                 sig.result == curlee::types::TypeKind::Bool ? PredType::Bool
                                                                             : PredType::Int,
                                 false};
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredUnary>)
            {
                auto rhs = lower_node(*node.rhs, ctx);
                if (std::holds_alternative<curlee::diag::Diagnostic>(rhs))
                {
                    return std::get<curlee::diag::Diagnostic>(rhs);
                }
                auto typed = std::get<TypedExpr>(rhs);

                using curlee::lexer::TokenKind;
                if (node.op == TokenKind::Bang)
                {
                    if (typed.type != PredType::Bool)
                    {
                        return error_at(pred.span, "'!' expects Bool predicate");
                    }
                    return TypedExpr{!typed.expr, PredType::Bool, false};
                }
                if (node.op == TokenKind::Minus)
                {
                    if (typed.type != PredType::Int)
                    {
                        return error_at(pred.span, "unary '-' expects Int predicate");
                    }
                    return TypedExpr{-typed.expr, PredType::Int, typed.is_literal};
                }
                // Bitwise complement: exact two's-complement identity ~x == -x - 1
                // over the 64-bit Int model (issue #270).
                if (node.op == TokenKind::Tilde)
                {
                    if (typed.type != PredType::Int)
                    {
                        return error_at(pred.span, "unary '~' expects Int predicate");
                    }
                    return TypedExpr{(-typed.expr) - 1, PredType::Int, typed.is_literal};
                }

                return error_at(pred.span, "unsupported unary operator in predicate");
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredBinary>)
            {
                auto lhs = lower_node(*node.lhs, ctx);
                if (std::holds_alternative<curlee::diag::Diagnostic>(lhs))
                {
                    return std::get<curlee::diag::Diagnostic>(lhs);
                }
                auto rhs = lower_node(*node.rhs, ctx);
                if (std::holds_alternative<curlee::diag::Diagnostic>(rhs))
                {
                    return std::get<curlee::diag::Diagnostic>(rhs);
                }
                auto left = std::get<TypedExpr>(lhs);
                auto right = std::get<TypedExpr>(rhs);

                using curlee::lexer::TokenKind;
                switch (node.op)
                {
                case TokenKind::AndAnd:
                case TokenKind::OrOr:
                    if (left.type != PredType::Bool || right.type != PredType::Bool)
                    {
                        return error_at(pred.span, "boolean operators expect Bool predicates");
                    }
                    return TypedExpr{node.op == TokenKind::AndAnd ? (left.expr && right.expr)
                                                                  : (left.expr || right.expr),
                                     PredType::Bool, false};

                case TokenKind::EqualEqual:
                case TokenKind::BangEqual:
                    if (left.type != right.type)
                    {
                        return error_at(pred.span, "equality expects matching predicate types");
                    }
                    return TypedExpr{node.op == TokenKind::EqualEqual ? (left.expr == right.expr)
                                                                      : (left.expr != right.expr),
                                     PredType::Bool, false};

                case TokenKind::Less:
                case TokenKind::LessEqual:
                case TokenKind::Greater:
                case TokenKind::GreaterEqual:
                    if (left.type != PredType::Int || right.type != PredType::Int)
                    {
                        return error_at(pred.span, "comparison operators expect Int predicates");
                    }
                    if (node.op == TokenKind::Less)
                    {
                        return TypedExpr{left.expr < right.expr, PredType::Bool, false};
                    }
                    if (node.op == TokenKind::LessEqual)
                    {
                        return TypedExpr{left.expr <= right.expr, PredType::Bool, false};
                    }
                    if (node.op == TokenKind::Greater)
                    {
                        return TypedExpr{left.expr > right.expr, PredType::Bool, false};
                    }
                    return TypedExpr{left.expr >= right.expr, PredType::Bool, false};

                case TokenKind::Plus:
                case TokenKind::Minus:
                    if (left.type != PredType::Int || right.type != PredType::Int)
                    {
                        return error_at(pred.span, "arithmetic operators expect Int predicates");
                    }
                    return TypedExpr{node.op == TokenKind::Plus ? (left.expr + right.expr)
                                                                : (left.expr - right.expr),
                                     PredType::Int, left.is_literal && right.is_literal};

                case TokenKind::Star:
                    if (left.type != PredType::Int || right.type != PredType::Int)
                    {
                        return error_at(pred.span, "'*' expects Int predicates");
                    }
                    // Products with a literal factor are linear. A product with a
                    // div/mod factor is also linear in Z3's Int theory (the
                    // division-remainder identity a % b == a - (a / b) * b is an
                    // axiom, issue #270). Arbitrary products of two non-literal,
                    // non-div/mod factors remain unsupported.
                    if (!left.is_literal && !right.is_literal && !is_div_mod_term(left.expr) &&
                        !is_div_mod_term(right.expr))
                    {
                        return error_at(pred.span, "non-linear multiplication is not supported");
                    }
                    return TypedExpr{left.expr * right.expr, PredType::Int,
                                     left.is_literal && right.is_literal};

                // Division: Z3 Euclidean integer division (matches C truncating
                // division for non-negative operands, issue #270).
                case TokenKind::Slash:
                    if (left.type != PredType::Int || right.type != PredType::Int)
                    {
                        return error_at(pred.span, "'/' expects Int predicates");
                    }
                    return TypedExpr{left.expr / right.expr, PredType::Int,
                                     left.is_literal && right.is_literal};

                // Modulo: Z3 Euclidean modulo; satisfies the division-remainder
                // identity a % b == a - (a / b) * b for non-negative operands
                // (issue #270).
                case TokenKind::Percent:
                    if (left.type != PredType::Int || right.type != PredType::Int)
                    {
                        return error_at(pred.span, "'%' expects Int predicates");
                    }
                    return TypedExpr{z3::mod(left.expr, right.expr), PredType::Int,
                                     left.is_literal && right.is_literal};

                // Bitwise operators (issue #270): bit-precise 64-bit model via
                // Z3's bit-vector theory (see checker.cpp lower_expr for the
                // full rationale). Right shift is arithmetic (bvashr); shift
                // amounts are taken mod 64.
                case TokenKind::Amp:
                case TokenKind::Pipe:
                case TokenKind::Caret:
                case TokenKind::ShiftLeft:
                case TokenKind::ShiftRight:
                {
                    if (left.type != PredType::Int || right.type != PredType::Int)
                    {
                        return error_at(pred.span, "bitwise operators expect Int predicates");
                    }
                    constexpr unsigned kWidth = 64;
                    const z3::expr left_bv = z3::int2bv(kWidth, left.expr);
                    const z3::expr right_bv = z3::int2bv(kWidth, right.expr);
                    z3::expr result_bv = left_bv & right_bv;
                    if (node.op == TokenKind::Pipe)
                    {
                        result_bv = left_bv | right_bv;
                    }
                    else if (node.op == TokenKind::Caret)
                    {
                        result_bv = left_bv ^ right_bv;
                    }
                    else if (node.op == TokenKind::ShiftLeft)
                    {
                        result_bv = z3::shl(left_bv, right_bv);
                    }
                    else if (node.op == TokenKind::ShiftRight)
                    {
                        result_bv = z3::ashr(left_bv, right_bv);
                    }
                    return TypedExpr{z3::bv2int(result_bv, true), PredType::Int,
                                     left.is_literal && right.is_literal};
                }

                default:
                    break;
                }

                return error_at(pred.span, "unsupported binary operator in predicate");
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredGroup>)
            {
                return lower_node(*node.inner, ctx);
            }

            return error_at(pred.span, "unsupported predicate node");
        },
        pred.node);
}

} // namespace

LoweringResult lower_predicate(const curlee::parser::Pred& pred, const LoweringContext& ctx)
{
    auto lowered = lower_node(pred, ctx);
    if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
    {
        return std::get<curlee::diag::Diagnostic>(lowered);
    }

    auto typed = std::get<TypedExpr>(lowered);
    if (typed.type != PredType::Bool)
    {
        return error_at(pred.span, "predicate must resolve to Bool");
    }

    return typed.expr;
}

LoweringResult lower_predicate_int(const curlee::parser::Pred& pred, const LoweringContext& ctx)
{
    return lower_predicate_int_named(pred, ctx, "decreases");
}

LoweringResult lower_predicate_int_named(const curlee::parser::Pred& pred,
                                         const LoweringContext& ctx, std::string_view clause_name)
{
    auto lowered = lower_node(pred, ctx);
    if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
    {
        return std::get<curlee::diag::Diagnostic>(lowered);
    }

    auto typed = std::get<TypedExpr>(lowered);
    if (typed.type != PredType::Int)
    {
        return error_at(pred.span, std::string(clause_name) + " clause must resolve to Int");
    }

    return typed.expr;
}

} // namespace curlee::verification
