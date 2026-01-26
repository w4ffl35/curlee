#include <cassert>
#include <curlee/parser/ast.h>
#include <curlee/types/type_check.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace curlee::types
{
namespace
{

using curlee::diag::Diagnostic;
using curlee::diag::Severity;
using curlee::parser::BinaryExpr;
using curlee::parser::BlockStmt;
using curlee::parser::CallExpr;
using curlee::parser::Expr;
using curlee::parser::ExprStmt;
using curlee::parser::Function;
using curlee::parser::GroupExpr;
using curlee::parser::IfStmt;
using curlee::parser::LetStmt;
using curlee::parser::NameExpr;
using curlee::parser::ReturnStmt;
using curlee::parser::Stmt;
using curlee::parser::WhileStmt;
using curlee::source::Span;

struct Scope
{
    std::unordered_map<std::string_view, Type> vars;
};

class Checker
{
  public:
    [[nodiscard]] TypeCheckResult run(const curlee::parser::Program& program)
    {
        // Collect function signatures first.
        for (const auto& f : program.functions)
        {
            auto sig = function_signature(f);
            if (!sig.has_value())
            {
                continue;
            }
            functions_.emplace(f.name, *sig);
        }

        for (const auto& f : program.functions)
        {
            check_function(f);
        }

        if (!diags_.empty())
        {
            return diags_;
        }
        return info_;
    }

  private:
    std::unordered_map<std::string_view, FunctionType> functions_;
    std::vector<Scope> scopes_;
    std::vector<Diagnostic> diags_;
    TypeInfo info_;

    void push_scope() { scopes_.push_back(Scope{}); }
    void pop_scope() { scopes_.pop_back(); }

    [[nodiscard]] std::optional<Type> lookup_var(std::string_view name) const
    {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
        {
            if (auto found = it->vars.find(name); found != it->vars.end())
            {
                return found->second;
            }
        }
        return std::nullopt;
    }

    void declare_var(std::string_view name, Type t)
    {
        if (scopes_.empty())
        {
            push_scope();
        }
        scopes_.back().vars[name] = t;
    }

    void error_at(Span span, std::string message)
    {
        Diagnostic d;
        d.severity = Severity::Error;
        d.message = std::move(message);
        d.span = span;
        diags_.push_back(std::move(d));
    }

    [[nodiscard]] std::optional<Type> type_from_ast(const curlee::parser::TypeName& name)
    {
        const auto t = core_type_from_name(name.name);
        if (!t.has_value())
        {
            error_at(name.span, "unknown type '" + std::string(name.name) + "'");
            return std::nullopt;
        }
        return *t;
    }

    [[nodiscard]] std::optional<FunctionType> function_signature(const Function& f)
    {
        if (!f.return_type.has_value())
        {
            error_at(f.span,
                     "missing return type annotation for function '" + std::string(f.name) + "'");
            return std::nullopt;
        }

        auto ret = type_from_ast(*f.return_type);
        if (!ret.has_value())
        {
            return std::nullopt;
        }

        FunctionType sig;
        sig.result = *ret;

        for (const auto& p : f.params)
        {
            auto pt = type_from_ast(p.type);
            if (!pt.has_value())
            {
                return std::nullopt;
            }
            sig.params.push_back(*pt);
        }

        return sig;
    }

    void check_function(const Function& f)
    {
        auto it = functions_.find(f.name);
        if (it == functions_.end())
        {
            return;
        }

        push_scope();
        for (std::size_t i = 0; i < f.params.size(); ++i)
        {
            declare_var(f.params[i].name, it->second.params[i]);
        }

        for (const auto& s : f.body.stmts)
        {
            check_stmt(s, it->second.result);
        }

        pop_scope();
    }

    void check_stmt(const Stmt& s, Type expected_return)
    {
        std::visit([&](const auto& node) { check_stmt_node(node, s.span, expected_return); },
                   s.node);
    }

    void check_stmt_node(const LetStmt& s, Span stmt_span, Type)
    {
        auto declared = type_from_ast(s.type);
        if (!declared.has_value())
        {
            return;
        }

        // Mirror resolver semantics: declare before checking initializer.
        declare_var(s.name, *declared);

        auto init = check_expr(s.value);
        if (!init.has_value())
        {
            return;
        }

        if (*init != *declared)
        {
            error_at(stmt_span, "type mismatch in let: expected " +
                                    std::string(to_string(*declared)) + ", got " +
                                    std::string(to_string(*init)));
        }
    }

    void check_stmt_node(const ReturnStmt& s, Span stmt_span, Type expected_return)
    {
        if (!s.value.has_value())
        {
            if (expected_return.kind != TypeKind::Unit)
            {
                error_at(stmt_span, "return; used in non-Unit function");
            }
            return;
        }

        auto value_t = check_expr(*s.value);
        if (!value_t.has_value())
        {
            return;
        }

        if (*value_t != expected_return)
        {
            error_at(stmt_span, "return type mismatch: expected " +
                                    std::string(to_string(expected_return)) + ", got " +
                                    std::string(to_string(*value_t)));
        }
    }

    void check_stmt_node(const ExprStmt& s, Span, Type) { (void)check_expr(s.expr); }

    void check_stmt_node(const BlockStmt& s, Span, Type expected_return)
    {
        push_scope();
        for (const auto& stmt : s.block->stmts)
        {
            check_stmt(stmt, expected_return);
        }
        pop_scope();
    }

    void check_stmt_node(const IfStmt& s, Span, Type expected_return)
    {
        const auto cond_t = check_expr(s.cond);
        if (cond_t.has_value() && cond_t->kind != TypeKind::Bool)
        {
            error_at(s.cond.span, "if condition type mismatch: expected Bool, got " +
                                      std::string(to_string(*cond_t)));
        }

        push_scope();
        for (const auto& stmt : s.then_block->stmts)
        {
            check_stmt(stmt, expected_return);
        }
        pop_scope();

        if (s.else_block != nullptr)
        {
            push_scope();
            for (const auto& stmt : s.else_block->stmts)
            {
                check_stmt(stmt, expected_return);
            }
            pop_scope();
        }
    }

    void check_stmt_node(const WhileStmt& s, Span, Type expected_return)
    {
        const auto cond_t = check_expr(s.cond);
        if (cond_t.has_value() && cond_t->kind != TypeKind::Bool)
        {
            error_at(s.cond.span, "while condition type mismatch: expected Bool, got " +
                                      std::string(to_string(*cond_t)));
        }

        push_scope();
        for (const auto& stmt : s.body->stmts)
        {
            check_stmt(stmt, expected_return);
        }
        pop_scope();
    }

    [[nodiscard]] std::optional<Type> check_expr(const Expr& e)
    {
        auto t =
            std::visit([&](const auto& node) { return check_expr_node(node, e.span); }, e.node);
        if (t.has_value())
        {
            info_.expr_types.emplace(e.id, *t);
        }
        return t;
    }

    [[nodiscard]] std::optional<Type> check_expr_node(const curlee::parser::IntExpr&, Span)
    {
        return Type{.kind = TypeKind::Int};
    }

    [[nodiscard]] std::optional<Type> check_expr_node(const curlee::parser::StringExpr&, Span)
    {
        return Type{.kind = TypeKind::String};
    }

    [[nodiscard]] std::optional<Type> check_expr_node(const NameExpr& e, Span span)
    {
        if (const auto v = lookup_var(e.name); v.has_value())
        {
            return *v;
        }

        // Allow referring to functions as values only for calls.
        if (functions_.find(e.name) != functions_.end())
        {
            error_at(span, "function name '" + std::string(e.name) + "' is not a value");
            return std::nullopt;
        }

        error_at(span, "unknown name '" + std::string(e.name) + "'");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Type> check_expr_node(const curlee::parser::UnaryExpr& e, Span span)
    {
        const auto rhs = check_expr(*e.rhs);
        if (!rhs.has_value())
        {
            return std::nullopt;
        }

        using curlee::lexer::TokenKind;
        if (e.op == TokenKind::Minus)
        {
            if (rhs->kind != TypeKind::Int)
            {
                error_at(span, "unary '-' expects Int");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Int};
        }

        if (e.op == TokenKind::Bang)
        {
            if (rhs->kind != TypeKind::Bool)
            {
                error_at(span, "unary '!' expects Bool");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Bool};
        }

        error_at(span, "unsupported unary operator");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Type> check_expr_node(const BinaryExpr& e, Span span)
    {
        const auto lhs = check_expr(*e.lhs);
        const auto rhs = check_expr(*e.rhs);
        if (!lhs.has_value() || !rhs.has_value())
        {
            return std::nullopt;
        }

        using curlee::lexer::TokenKind;
        switch (e.op)
        {
        case TokenKind::Plus:
        case TokenKind::Minus:
        case TokenKind::Star:
        case TokenKind::Slash:
            if (lhs->kind != TypeKind::Int || rhs->kind != TypeKind::Int)
            {
                error_at(span, "arithmetic operators expect Int operands");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Int};

        case TokenKind::EqualEqual:
        case TokenKind::BangEqual:
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            if (lhs->kind != TypeKind::Int || rhs->kind != TypeKind::Int)
            {
                error_at(span, "comparison operators expect Int operands");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Bool};

        case TokenKind::AndAnd:
        case TokenKind::OrOr:
            if (lhs->kind != TypeKind::Bool || rhs->kind != TypeKind::Bool)
            {
                error_at(span, "boolean operators expect Bool operands");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Bool};

        default:
            break;
        }

        error_at(span, "unsupported binary operator");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Type> check_expr_node(const CallExpr& e, Span span)
    {
        const auto* callee_name = std::get_if<NameExpr>(&e.callee->node);
        if (callee_name == nullptr)
        {
            error_at(span, "only direct calls are supported (callee must be a name)");
            return std::nullopt;
        }

        const auto it = functions_.find(callee_name->name);
        if (it == functions_.end())
        {
            error_at(span, "unknown function '" + std::string(callee_name->name) + "'");
            return std::nullopt;
        }

        const auto& sig = it->second;
        if (e.args.size() != sig.params.size())
        {
            error_at(span, "wrong number of arguments for call to '" +
                               std::string(callee_name->name) + "'");
            return std::nullopt;
        }

        for (std::size_t i = 0; i < e.args.size(); ++i)
        {
            const auto arg_t = check_expr(e.args[i]);
            if (!arg_t.has_value())
            {
                continue;
            }
            if (*arg_t != sig.params[i])
            {
                error_at(span, "argument type mismatch for call to '" +
                                   std::string(callee_name->name) + "'");
            }
        }

        return sig.result;
    }

    [[nodiscard]] std::optional<Type> check_expr_node(const GroupExpr& e, Span)
    {
        return check_expr(*e.inner);
    }
};

} // namespace

TypeCheckResult type_check(const curlee::parser::Program& program)
{
    return Checker().run(program);
}

} // namespace curlee::types
