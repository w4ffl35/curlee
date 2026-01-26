#include <curlee/compiler/emitter.h>
#include <curlee/lexer/token.h>
#include <string>
#include <unordered_map>

namespace curlee::compiler
{
namespace
{

using curlee::diag::Diagnostic;
using curlee::diag::Severity;
using curlee::parser::BinaryExpr;
using curlee::parser::BlockStmt;
using curlee::parser::BoolExpr;
using curlee::parser::CallExpr;
using curlee::parser::Expr;
using curlee::parser::ExprStmt;
using curlee::parser::Function;
using curlee::parser::IfStmt;
using curlee::parser::LetStmt;
using curlee::parser::NameExpr;
using curlee::parser::ReturnStmt;
using curlee::parser::Stmt;
using curlee::parser::WhileStmt;
using curlee::source::Span;
using curlee::vm::Chunk;
using curlee::vm::OpCode;
using curlee::vm::Value;

Diagnostic error_at(Span span, std::string message)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.message = std::move(message);
    d.span = span;
    return d;
}

class Emitter
{
  public:
    EmitResult run(const curlee::parser::Program& program)
    {
        const Function* entry = nullptr;
        for (const auto& f : program.functions)
        {
            if (f.name == "main")
            {
                entry = &f;
                break;
            }
        }

        if (entry == nullptr)
        {
            diags_.push_back(error_at({}, "no entry function 'main' found"));
            return diags_;
        }

        for (const auto& stmt : entry->body.stmts)
        {
            emit_stmt(stmt);
        }

        if (!diags_.empty())
        {
            return diags_;
        }
        return chunk_;
    }

  private:
    Chunk chunk_;
    std::vector<Diagnostic> diags_;
    std::unordered_map<std::string_view, std::uint16_t> locals_;

    void emit_stmt(const Stmt& stmt)
    {
        std::visit([&](const auto& node) { emit_stmt_node(node, stmt.span); }, stmt.node);
    }

    void emit_stmt_node(const LetStmt& stmt, Span span)
    {
        emit_expr(stmt.value);
        if (!diags_.empty())
        {
            return;
        }

        const auto slot = static_cast<std::uint16_t>(locals_.size());
        locals_.emplace(stmt.name, slot);
        chunk_.emit_local(OpCode::StoreLocal, slot, span);
    }

    void emit_stmt_node(const ReturnStmt& stmt, Span span)
    {
        if (stmt.value.has_value())
        {
            emit_expr(*stmt.value);
        }
        else
        {
            chunk_.emit_constant(Value::unit_v(), span);
        }
        if (!diags_.empty())
        {
            return;
        }
        chunk_.emit(OpCode::Return, span);
    }

    void emit_stmt_node(const ExprStmt& stmt, Span span)
    {
        emit_expr(stmt.expr);
        if (!diags_.empty())
        {
            return;
        }
        chunk_.emit(OpCode::Pop, span);
    }

    void emit_stmt_node(const BlockStmt& stmt, Span)
    {
        for (const auto& nested : stmt.block->stmts)
        {
            emit_stmt(nested);
        }
    }

    void emit_stmt_node(const IfStmt& stmt, Span)
    {
        diags_.push_back(error_at(stmt.cond.span, "if/else not supported in emitter yet"));
    }

    void emit_stmt_node(const WhileStmt& stmt, Span)
    {
        diags_.push_back(error_at(stmt.cond.span, "while not supported in emitter yet"));
    }

    void emit_expr(const Expr& expr)
    {
        std::visit([&](const auto& node) { emit_expr_node(node, expr.span); }, expr.node);
    }

    void emit_expr_node(const curlee::parser::IntExpr& expr, Span span)
    {
        const std::string literal(expr.lexeme);
        const long long value = std::stoll(literal);
        chunk_.emit_constant(Value::int_v(value), span);
    }

    void emit_expr_node(const BoolExpr& expr, Span span)
    {
        chunk_.emit_constant(Value::bool_v(expr.value), span);
    }

    void emit_expr_node(const curlee::parser::StringExpr&, Span span)
    {
        diags_.push_back(error_at(span, "string literals not supported in emitter yet"));
    }

    void emit_expr_node(const NameExpr& expr, Span span)
    {
        auto it = locals_.find(expr.name);
        if (it == locals_.end())
        {
            diags_.push_back(error_at(span, "unknown name '" + std::string(expr.name) + "'"));
            return;
        }
        chunk_.emit_local(OpCode::LoadLocal, it->second, span);
    }

    void emit_expr_node(const curlee::parser::UnaryExpr&, Span span)
    {
        diags_.push_back(error_at(span, "unary operators not supported in emitter yet"));
    }

    void emit_expr_node(const BinaryExpr& expr, Span span)
    {
        using curlee::lexer::TokenKind;
        if (expr.op != TokenKind::Plus)
        {
            diags_.push_back(error_at(span, "only '+' is supported in emitter yet"));
            return;
        }
        emit_expr(*expr.lhs);
        emit_expr(*expr.rhs);
        if (!diags_.empty())
        {
            return;
        }
        chunk_.emit(OpCode::Add, span);
    }

    void emit_expr_node(const CallExpr&, Span span)
    {
        diags_.push_back(error_at(span, "calls not supported in emitter yet"));
    }

    void emit_expr_node(const curlee::parser::GroupExpr& expr, Span) { emit_expr(*expr.inner); }
};

} // namespace

EmitResult emit_bytecode(const curlee::parser::Program& program)
{
    Emitter emitter;
    return emitter.run(program);
}

} // namespace curlee::compiler
