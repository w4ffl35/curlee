#include <curlee/compiler/emitter.h>
#include <curlee/lexer/token.h>
#include <limits>
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
using curlee::parser::MemberExpr;
using curlee::parser::NameExpr;
using curlee::parser::ReturnStmt;
using curlee::parser::Stmt;
using curlee::parser::UnsafeStmt;
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
        const Function* entry = find_main(program);
        if (entry == nullptr)
        {
            diags_.push_back(error_at({}, "no entry function 'main' found"));
            return diags_;
        }

        // Emit main first so the VM entry point is ip=0.
        emit_function(*entry, /*is_main=*/true);
        for (const auto& f : program.functions)
        {
            if (f.name == "main")
            {
                continue;
            }
            emit_function(f, /*is_main=*/false);
        }

        if (!diags_.empty())
        {
            return diags_;
        }

        for (const auto& [name, patches] : pending_calls_)
        {
            auto it = function_addrs_.find(name);
            if (it == function_addrs_.end())
            {
                diags_.push_back(error_at({}, "unknown function '" + std::string(name) + "'"));
                continue;
            }
            const auto addr = it->second;
            if (addr > std::numeric_limits<std::uint16_t>::max())
            {
                diags_.push_back(error_at({}, "function too large for 16-bit address '" +
                                                  std::string(name) + "'"));
                continue;
            }
            for (const auto pos : patches)
            {
                patch_u16(pos, static_cast<std::uint16_t>(addr));
            }
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
    std::uint16_t local_base_ = 0;
    std::uint16_t next_local_base_ = 0;
    bool current_is_main_ = false;

    std::unordered_map<std::string_view, std::size_t> function_addrs_;
    std::unordered_map<std::string_view, std::vector<std::size_t>> pending_calls_;

    static const Function* find_main(const curlee::parser::Program& program)
    {
        for (const auto& f : program.functions)
        {
            if (f.name == "main")
            {
                return &f;
            }
        }
        return nullptr;
    }

    std::size_t ip() const { return chunk_.code.size(); }

    std::size_t emit_u16_placeholder(Span span)
    {
        const auto pos = chunk_.code.size();
        chunk_.emit_u16(0, span);
        return pos;
    }

    void patch_u16(std::size_t pos, std::uint16_t value)
    {
        chunk_.code[pos] = static_cast<std::uint8_t>(value & 0xFF);
        chunk_.code[pos + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    }

    void emit_function(const Function& fn, bool is_main)
    {
        current_is_main_ = is_main;

        // Track function start address for calls.
        function_addrs_.emplace(fn.name, ip());

        // Allocate locals in a disjoint slot range per function to avoid clobbering
        // across calls without requiring VM local snapshots.
        locals_.clear();
        local_base_ = next_local_base_;

        if (!fn.params.empty())
        {
            diags_.push_back(error_at(fn.span, "function parameters not supported in emitter yet"));
            return;
        }

        for (const auto& stmt : fn.body.stmts)
        {
            emit_stmt(stmt);
        }

        // Conservative implicit return (reachable if user omitted an explicit return).
        if (!diags_.empty())
        {
            return;
        }
        chunk_.emit_constant(Value::unit_v(), fn.span);
        chunk_.emit(is_main ? OpCode::Return : OpCode::Ret, fn.span);

        // Reserve this function's locals slot range.
        next_local_base_ = static_cast<std::uint16_t>(local_base_ + locals_.size());
    }

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

        const auto slot = static_cast<std::uint16_t>(local_base_ + locals_.size());
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
        chunk_.emit(current_is_main_ ? OpCode::Return : OpCode::Ret, span);
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

    void emit_stmt_node(const UnsafeStmt& stmt, Span)
    {
        for (const auto& nested : stmt.body->stmts)
        {
            emit_stmt(nested);
        }
    }

    void emit_stmt_node(const IfStmt& stmt, Span)
    {
        emit_expr(stmt.cond);
        if (!diags_.empty())
        {
            return;
        }

        chunk_.emit(OpCode::JumpIfFalse, stmt.cond.span);
        const auto else_patch = emit_u16_placeholder(stmt.cond.span);

        for (const auto& s : stmt.then_block->stmts)
        {
            emit_stmt(s);
        }
        if (!diags_.empty())
        {
            return;
        }

        if (stmt.else_block != nullptr)
        {
            chunk_.emit(OpCode::Jump, stmt.cond.span);
            const auto end_patch = emit_u16_placeholder(stmt.cond.span);

            patch_u16(else_patch, static_cast<std::uint16_t>(ip()));
            for (const auto& s : stmt.else_block->stmts)
            {
                emit_stmt(s);
            }
            if (!diags_.empty())
            {
                return;
            }
            patch_u16(end_patch, static_cast<std::uint16_t>(ip()));
        }
        else
        {
            patch_u16(else_patch, static_cast<std::uint16_t>(ip()));
        }
    }

    void emit_stmt_node(const WhileStmt& stmt, Span)
    {
        const auto loop_start = ip();

        emit_expr(stmt.cond);
        if (!diags_.empty())
        {
            return;
        }

        chunk_.emit(OpCode::JumpIfFalse, stmt.cond.span);
        const auto exit_patch = emit_u16_placeholder(stmt.cond.span);

        for (const auto& s : stmt.body->stmts)
        {
            emit_stmt(s);
        }
        if (!diags_.empty())
        {
            return;
        }

        chunk_.emit(OpCode::Jump, stmt.cond.span);
        chunk_.emit_u16(static_cast<std::uint16_t>(loop_start), stmt.cond.span);
        patch_u16(exit_patch, static_cast<std::uint16_t>(ip()));
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

    void emit_expr_node(const MemberExpr&, Span span)
    {
        diags_.push_back(error_at(span, "member access not supported in emitter yet"));
    }

    void emit_expr_node(const curlee::parser::UnaryExpr& expr, Span span)
    {
        using curlee::lexer::TokenKind;

        emit_expr(*expr.rhs);
        if (!diags_.empty())
        {
            return;
        }

        switch (expr.op)
        {
        case TokenKind::Bang:
            chunk_.emit(OpCode::Not, span);
            return;
        case TokenKind::Minus:
            chunk_.emit(OpCode::Neg, span);
            return;
        default:
            diags_.push_back(error_at(span, "unsupported unary operator in emitter"));
            return;
        }
    }

    void emit_expr_node(const BinaryExpr& expr, Span span)
    {
        using curlee::lexer::TokenKind;

        if (expr.op == TokenKind::AndAnd)
        {
            // Short-circuit: if lhs is false, result is false without evaluating rhs.
            emit_expr(*expr.lhs);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::JumpIfFalse, span);
            const auto false_patch = emit_u16_placeholder(span);

            emit_expr(*expr.rhs);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::Jump, span);
            const auto end_patch = emit_u16_placeholder(span);

            patch_u16(false_patch, static_cast<std::uint16_t>(ip()));
            chunk_.emit_constant(Value::bool_v(false), span);
            patch_u16(end_patch, static_cast<std::uint16_t>(ip()));
            return;
        }

        if (expr.op == TokenKind::OrOr)
        {
            // Short-circuit: if lhs is true, result is true without evaluating rhs.
            emit_expr(*expr.lhs);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::Not, span);
            chunk_.emit(OpCode::JumpIfFalse, span);
            const auto true_patch = emit_u16_placeholder(span);

            emit_expr(*expr.rhs);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::Jump, span);
            const auto end_patch = emit_u16_placeholder(span);

            patch_u16(true_patch, static_cast<std::uint16_t>(ip()));
            chunk_.emit_constant(Value::bool_v(true), span);
            patch_u16(end_patch, static_cast<std::uint16_t>(ip()));
            return;
        }

        emit_expr(*expr.lhs);
        emit_expr(*expr.rhs);
        if (!diags_.empty())
        {
            return;
        }

        switch (expr.op)
        {
        case TokenKind::Plus:
            chunk_.emit(OpCode::Add, span);
            return;
        case TokenKind::Minus:
            chunk_.emit(OpCode::Sub, span);
            return;
        case TokenKind::Star:
            chunk_.emit(OpCode::Mul, span);
            return;
        case TokenKind::Slash:
            chunk_.emit(OpCode::Div, span);
            return;
        case TokenKind::EqualEqual:
            chunk_.emit(OpCode::Equal, span);
            return;
        case TokenKind::BangEqual:
            chunk_.emit(OpCode::NotEqual, span);
            return;
        case TokenKind::Less:
            chunk_.emit(OpCode::Less, span);
            return;
        case TokenKind::LessEqual:
            chunk_.emit(OpCode::LessEqual, span);
            return;
        case TokenKind::Greater:
            chunk_.emit(OpCode::Greater, span);
            return;
        case TokenKind::GreaterEqual:
            chunk_.emit(OpCode::GreaterEqual, span);
            return;
        default:
            diags_.push_back(error_at(span, "unsupported binary operator in emitter"));
            return;
        }
    }

    void emit_call(std::string_view callee, Span span)
    {
        chunk_.emit(OpCode::Call, span);
        const auto pos = emit_u16_placeholder(span);
        pending_calls_[callee].push_back(pos);
    }

    void emit_expr_node(const CallExpr& expr, Span span)
    {
        if (!expr.args.empty())
        {
            diags_.push_back(error_at(span, "call arguments not supported in emitter yet"));
            return;
        }

        if (const auto* callee_member = std::get_if<MemberExpr>(&expr.callee->node);
            callee_member != nullptr && callee_member->base != nullptr)
        {
            const auto* base_name =
                std::get_if<curlee::parser::NameExpr>(&callee_member->base->node);
            if (base_name != nullptr && base_name->name == "python_ffi" &&
                callee_member->member == "call")
            {
                chunk_.emit(OpCode::PythonCall, span);
                return;
            }

            diags_.push_back(error_at(span, "only name calls are supported in emitter yet"));
            return;
        }

        const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
        if (callee_name == nullptr)
        {
            diags_.push_back(error_at(span, "only name calls are supported in emitter yet"));
            return;
        }

        emit_call(callee_name->name, span);
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
