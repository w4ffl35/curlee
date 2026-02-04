#include <curlee/verification/predicate_lowering.h>

namespace curlee::verification
{
namespace
{

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
                    if (!left.is_literal && !right.is_literal)
                    {
                        return error_at(pred.span, "non-linear multiplication is not supported");
                    }
                    return TypedExpr{left.expr * right.expr, PredType::Int,
                                     left.is_literal && right.is_literal};

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

} // namespace curlee::verification
