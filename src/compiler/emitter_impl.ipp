#include <curlee/compiler/emitter.h>
#include <curlee/lexer/token.h>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

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
using curlee::parser::ScopedNameExpr;
using curlee::parser::Stmt;
using curlee::parser::StructLiteralExpr;
using curlee::parser::UnsafeStmt;
using curlee::parser::WhileStmt;
using curlee::source::Span;
using curlee::vm::Chunk;
using curlee::vm::OpCode;
using curlee::vm::Value;

static std::string join_path(const std::vector<std::string_view>& parts)
{
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        out += std::string(parts[i]);
        if (i + 1 < parts.size())
        {
            out += '.';
        }
    }
    return out;
} // GCOVR_EXCL_LINE

static bool is_reserved_builtin_name(std::string_view name)
{
    static const std::unordered_set<std::string_view> reserved = {
        "print",           "__read_line",      "__tty_clear",      "__fs_read_text",
        "__fs_write_text", "__tty_write_at",   "__tty_flush",      "__rng_next_int",
        "__vec_new_int",   "__vec_len_int",    "__vec_push_int",   "__vec_get_int",
        "__vec_set_int",   "__vec_new_bool",   "__vec_len_bool",   "__vec_push_bool",
        "__vec_get_bool",  "__vec_set_bool",   "__set_new_int",    "__set_has_int",
        "__set_insert_int",
    };
    return reserved.contains(name);
}

static bool collect_member_chain(const Expr& expr, std::vector<std::string_view>& out)
{
    if (const auto* name = std::get_if<NameExpr>(&expr.node))
    {
        out.push_back(name->name);
        return true;
    }

    if (const auto* mem = std::get_if<MemberExpr>(&expr.node))
    {
        if (mem->base == nullptr)
        {
            return false;
        }
        if (!collect_member_chain(*mem->base, out))
        {
            return false;
        }
        out.push_back(mem->member);
        return true;
    }

    return false;
}

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
        imported_module_keys_.clear();
        imported_module_aliases_.clear();
        for (const auto& imp : program.imports)
        {
            const std::string key = join_path(imp.path);
            imported_module_keys_.insert(key);
            if (imp.alias.has_value())
            {
                imported_module_aliases_.emplace(*imp.alias, key);
            }
        }

        functions_.clear();
        for (const auto& f : program.functions)
        {
            if (functions_.contains(f.name))
            {
                diags_.push_back(error_at(f.span, "duplicate function declaration '" +
                                                      std::string(f.name) + "'"));
                return diags_;
            }
            functions_.emplace(f.name, &f);
        }

        for (const auto& f : program.functions)
        {
            if (is_reserved_builtin_name(f.name))
            {
                diags_.push_back(error_at(f.span, "cannot declare builtin function '" +
                                                      std::string(f.name) + "'"));
                return diags_;
            }
        }

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
            // GCOVR_EXCL_START
            if (it == function_addrs_.end())
            {
                diags_.push_back(error_at({}, "unknown function '" + std::string(name) + "'"));
                continue;
            }
            // GCOVR_EXCL_STOP
            const auto addr = it->second;
            // GCOVR_EXCL_START
            if (addr > std::numeric_limits<std::uint16_t>::max())
            {
                diags_.push_back(error_at({}, "function too large for 16-bit address '" +
                                                  std::string(name) + "'"));
                continue;
            }
            // GCOVR_EXCL_STOP
            for (const auto pos : patches)
            {
                patch_u16(pos, static_cast<std::uint16_t>(addr));
            }
        }

        if (!diags_.empty()) // GCOVR_EXCL_LINE
        {
            return diags_; // GCOVR_EXCL_LINE
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

    std::unordered_map<std::string_view, const Function*> functions_;

    std::unordered_set<std::string> imported_module_keys_;
    std::unordered_map<std::string_view, std::string> imported_module_aliases_;

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

    void emit_u8(std::uint8_t value, Span span)
    {
        chunk_.code.push_back(value);
        chunk_.spans.push_back(span);
    }

    void emit_string_operand(std::string_view text, Span span)
    {
        const auto idx = chunk_.add_constant(Value::string_v(std::string(text)));
        // Defensive guard: the chunk format uses 16-bit constant indices.
        // Reaching this would require >65k constants in one chunk, which is out-of-scope for
        // MVP runnable programs and impractical in tests.
        // GCOVR_EXCL_START
        if (idx > std::numeric_limits<std::uint16_t>::max())
        {
            diags_.push_back(error_at(span, "too many constants for 16-bit constant pool"));
            return;
        }
        // GCOVR_EXCL_STOP
        chunk_.emit_u16(static_cast<std::uint16_t>(idx), span);
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

        if (is_main)
        {
            for (const auto& p : fn.params)
            {
                if (!p.type.is_capability)
                {
                    diags_.push_back(error_at(
                        p.type.span,
                        "main parameters are only supported for capability types (cap ...)"));
                    return;
                }
            }
        }

        // Parameters live in the first slots of this function's locals range.
        for (std::size_t i = 0; i < fn.params.size(); ++i)
        {
            const auto& p = fn.params[i];
            if (!p.type.is_capability && p.type.name != "Int" && p.type.name != "Bool" &&
                p.type.name != "Vec")
            {
                diags_.push_back(
                    error_at(p.type.span, "parameter type not supported in runnable code: '" +
                                              std::string(p.type.name) + "'"));
                return;
            }

            const auto slot = static_cast<std::uint16_t>(local_base_ + i);
            locals_.emplace(p.name, slot);
        }

        if (is_main && !fn.params.empty())
        {
            // Entry point has no caller, so synthesize default capability values.
            // Capabilities are represented as Unit at runtime; authority is gated by the host
            // capability set passed to VM::run.
            for (std::size_t i = 0; i < fn.params.size(); ++i)
            {
                const auto& p = fn.params[i];
                const auto slot = static_cast<std::uint16_t>(local_base_ + i);
                chunk_.emit_constant(Value::unit_v(), p.span);
                chunk_.emit_local(OpCode::StoreLocal, slot, p.span);
            }
        }
        else
        {
            // Callee prologue: pop call arguments into parameter locals.
            // Call sites evaluate args left-to-right, so we pop into params right-to-left.
            for (std::size_t i = fn.params.size(); i > 0; --i)
            {
                const auto& p = fn.params[i - 1];
                const auto slot = static_cast<std::uint16_t>(local_base_ + (i - 1));
                chunk_.emit_local(OpCode::StoreLocal, slot, p.span);
            }
        }

        for (const auto& stmt : fn.body.stmts)
        {
            emit_stmt(stmt);
            if (!diags_.empty())
            {
                return;
            }
        }

        // Conservative implicit return (reachable if user omitted an explicit return).
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
            if (!diags_.empty())
            {
                return;
            }
        }
    }

    void emit_stmt_node(const UnsafeStmt& stmt, Span)
    {
        for (const auto& nested : stmt.body->stmts)
        {
            emit_stmt(nested);
            if (!diags_.empty())
            {
                return;
            }
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
            if (!diags_.empty())
            {
                return;
            }
        }

        if (stmt.else_block != nullptr)
        {
            chunk_.emit(OpCode::Jump, stmt.cond.span);
            const auto end_patch = emit_u16_placeholder(stmt.cond.span);

            patch_u16(else_patch, static_cast<std::uint16_t>(ip()));
            for (const auto& s : stmt.else_block->stmts)
            {
                emit_stmt(s);
                if (!diags_.empty())
                {
                    return;
                }
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
            if (!diags_.empty())
            {
                return;
            }
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

    [[nodiscard]] static std::optional<std::string> decode_string_literal(std::string_view lexeme,
                                                                          std::string& error)
    {
        if (lexeme.size() < 2 || lexeme.front() != '"' || lexeme.back() != '"')
        {
            error = "malformed string literal";
            return std::nullopt;
        }

        std::string out;
        out.reserve(lexeme.size() - 2);
        for (std::size_t i = 1; i + 1 < lexeme.size(); ++i)
        {
            const char c = lexeme[i];
            if (c != '\\')
            {
                out.push_back(c);
                continue;
            }

            if (i + 1 >= lexeme.size() - 1)
            {
                error = "unterminated escape in string literal";
                return std::nullopt;
            }

            const char esc = lexeme[++i];
            switch (esc)
            {
            case 'n':
                out.push_back('\n');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case '"':
                out.push_back('"');
                break;
            default:
                error = std::string("unsupported escape \\") + esc + "'";
                return std::nullopt;
            }
        }

        return out;
    }

    void emit_expr_node(const curlee::parser::StringExpr& expr, Span span)
    {
        std::string err;
        auto decoded = decode_string_literal(expr.lexeme, err);
        if (!decoded.has_value())
        {
            diags_.push_back(error_at(span, err));
            return;
        }
        chunk_.emit_constant(Value::string_v(std::move(*decoded)), span);
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

    void emit_expr_node(const ScopedNameExpr&, Span span)
    {
        diags_.push_back(error_at(span, "scoped names (::) not supported in emitter yet"));
    }

    void emit_expr_node(const StructLiteralExpr&, Span span)
    {
        diags_.push_back(error_at(span, "struct literals not supported in emitter yet"));
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
        if (const auto* callee_scoped = std::get_if<ScopedNameExpr>(&expr.callee->node);
            callee_scoped != nullptr)
        {
            if (expr.args.size() > 1)
            {
                diags_.push_back(error_at(span,
                                          "enum variant constructor expects at most 1 argument"));
                return;
            }

            if (!expr.args.empty()) // GCOVR_EXCL_LINE
            {
                emit_expr(expr.args[0]);
                if (!diags_.empty())
                {
                    return;
                }
            }

            chunk_.emit(OpCode::MakeEnum, span);
            emit_string_operand(callee_scoped->lhs, span);
            // Only reachable when emit_string_operand hits 16-bit constant overflow.
            // GCOVR_EXCL_START
            if (!diags_.empty())
            {
                return;
            }
            emit_string_operand(callee_scoped->rhs, span);
            if (!diags_.empty())
            {
                return;
            }
            // GCOVR_EXCL_STOP
            emit_u8(expr.args.empty() ? 0 : 1, span); // GCOVR_EXCL_LINE
            return;
        }

        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node); // GCOVR_EXCL_LINE
            callee_name != nullptr &&
            (callee_name->name == "variant_is" || callee_name->name == "variant_unwrap"))
        {
            if (expr.args.size() != 2)
            {
                diags_.push_back(
                    error_at(span, std::string(callee_name->name) + " expects exactly 2 arguments"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            const auto* variant_ref = std::get_if<ScopedNameExpr>(&expr.args[1].node);
            if (variant_ref == nullptr)
            {
                diags_.push_back(error_at(
                    span, std::string(callee_name->name) + " expects second argument Enum::Variant"));
                return;
            }

            chunk_.emit(callee_name->name == "variant_is" ? OpCode::EnumIs : OpCode::EnumUnwrap,
                        span);
            emit_string_operand(variant_ref->lhs, span);
            // Only reachable when emit_string_operand hits 16-bit constant overflow.
            // GCOVR_EXCL_START
            if (!diags_.empty())
            {
                return;
            }
            // GCOVR_EXCL_STOP
            emit_string_operand(variant_ref->rhs, span);
            return;
        }

        // Builtin: print(<expr>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "print")
        {
            if (expr.args.size() != 1)
            {
                diags_.push_back(error_at(span, "print expects exactly 1 argument"));
                return;
            }
            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }
            chunk_.emit(OpCode::Print, span);
            return;
        }

        // Builtin: __read_line(<cap io.stdin>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__read_line")
        {
            if (expr.args.size() != 1)
            {
                diags_.push_back(error_at(span, "__read_line expects exactly 1 argument"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::ReadLine, span);
            return;
        }

        // Builtin: __tty_clear(<cap io.tty>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__tty_clear")
        {
            if (expr.args.size() != 1)
            {
                diags_.push_back(error_at(span, "__tty_clear expects exactly 1 argument"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::TtyClear, span);
            return;
        }

        // Builtin: __fs_read_text(<path>, <cap fs.read>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__fs_read_text")
        {
            if (expr.args.size() != 2)
            {
                diags_.push_back(error_at(span, "__fs_read_text expects exactly 2 arguments"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            emit_expr(expr.args[1]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::FsReadText, span);
            return;
        }

        // Builtin: __fs_write_text(<path>, <content>, <cap fs.write>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__fs_write_text")
        {
            if (expr.args.size() != 3)
            {
                diags_.push_back(error_at(span, "__fs_write_text expects exactly 3 arguments"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            emit_expr(expr.args[1]);
            if (!diags_.empty())
            {
                return;
            }

            emit_expr(expr.args[2]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::FsWriteText, span);
            return;
        }

        // Builtin: __tty_write_at(<row>, <col>, <text>, <cap io.tty>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__tty_write_at")
        {
            if (expr.args.size() != 4)
            {
                diags_.push_back(error_at(span, "__tty_write_at expects exactly 4 arguments"));
                return;
            }

            for (const auto& arg : expr.args)
            {
                emit_expr(arg);
                if (!diags_.empty())
                {
                    return;
                }
            }

            chunk_.emit(OpCode::TtyWriteAt, span);
            return;
        }

        // Builtin: __tty_flush(<cap io.tty>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__tty_flush")
        {
            if (expr.args.size() != 1)
            {
                diags_.push_back(error_at(span, "__tty_flush expects exactly 1 argument"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::TtyFlush, span);
            return;
        }

        // Builtin: __rng_next_int(<max_exclusive>, <cap rng.seeded>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__rng_next_int")
        {
            if (expr.args.size() != 2)
            {
                diags_.push_back(error_at(span, "__rng_next_int expects exactly 2 arguments"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            emit_expr(expr.args[1]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::RngNextInt, span);
            return;
        }

        // Builtin: __vec_new_int(<max_len>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__vec_new_int")
        {
            if (expr.args.size() != 1)
            {
                diags_.push_back(error_at(span, "__vec_new_int expects exactly 1 argument"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::VecNew, span);
            return;
        }

        // Builtin: __vec_len_int(<vec>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__vec_len_int")
        {
            if (expr.args.size() != 1)
            {
                diags_.push_back(error_at(span, "__vec_len_int expects exactly 1 argument"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::VecLen, span);
            return;
        }

        // Builtin: __vec_push_int(<vec>, <value>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__vec_push_int")
        {
            if (expr.args.size() != 2)
            {
                diags_.push_back(error_at(span, "__vec_push_int expects exactly 2 arguments"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            emit_expr(expr.args[1]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::VecPush, span);
            return;
        }

        // Builtin: __vec_get_int(<vec>, <index>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__vec_get_int")
        {
            if (expr.args.size() != 2)
            {
                diags_.push_back(error_at(span, "__vec_get_int expects exactly 2 arguments"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            emit_expr(expr.args[1]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::VecGet, span);
            return;
        }

        // Builtin: __vec_set_int(<vec>, <index>, <value>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__vec_set_int")
        {
            if (expr.args.size() != 3)
            {
                diags_.push_back(error_at(span, "__vec_set_int expects exactly 3 arguments"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            emit_expr(expr.args[1]);
            if (!diags_.empty())
            {
                return;
            }

            emit_expr(expr.args[2]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::VecSet, span);
            return;
        }

        // Builtin: __vec_new_bool(<max_len>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__vec_new_bool")
        {
            if (expr.args.size() != 1)
            {
                diags_.push_back(error_at(span, "__vec_new_bool expects exactly 1 argument"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::VecNewBool, span);
            return;
        }

        // Builtin: __vec_len_bool(<vec>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__vec_len_bool")
        {
            if (expr.args.size() != 1)
            {
                diags_.push_back(error_at(span, "__vec_len_bool expects exactly 1 argument"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::VecLenBool, span);
            return;
        }

        // Builtin: __vec_push_bool(<vec>, <value>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__vec_push_bool")
        {
            if (expr.args.size() != 2)
            {
                diags_.push_back(error_at(span, "__vec_push_bool expects exactly 2 arguments"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            emit_expr(expr.args[1]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::VecPushBool, span);
            return;
        }

        // Builtin: __vec_get_bool(<vec>, <index>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__vec_get_bool")
        {
            if (expr.args.size() != 2)
            {
                diags_.push_back(error_at(span, "__vec_get_bool expects exactly 2 arguments"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            emit_expr(expr.args[1]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::VecGetBool, span);
            return;
        }

        // Builtin: __vec_set_bool(<vec>, <index>, <value>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__vec_set_bool")
        {
            if (expr.args.size() != 3)
            {
                diags_.push_back(error_at(span, "__vec_set_bool expects exactly 3 arguments"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            emit_expr(expr.args[1]);
            if (!diags_.empty())
            {
                return;
            }

            emit_expr(expr.args[2]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::VecSetBool, span);
            return;
        }

        // Builtin: __set_new_int()
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__set_new_int")
        {
            if (expr.args.size() != 0)
            {
                diags_.push_back(error_at(span, "__set_new_int expects exactly 0 arguments"));
                return;
            }

            chunk_.emit(OpCode::SetNewInt, span);
            return;
        }

        // Builtin: __set_has_int(<set>, <value>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__set_has_int")
        {
            if (expr.args.size() != 2)
            {
                diags_.push_back(error_at(span, "__set_has_int expects exactly 2 arguments"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            emit_expr(expr.args[1]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::SetHasInt, span);
            return;
        }

        // Builtin: __set_insert_int(<set>, <value>)
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "__set_insert_int")
        {
            if (expr.args.size() != 2)
            {
                diags_.push_back(error_at(span, "__set_insert_int expects exactly 2 arguments"));
                return;
            }

            emit_expr(expr.args[0]);
            if (!diags_.empty())
            {
                return;
            }

            emit_expr(expr.args[1]);
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit(OpCode::SetInsertInt, span);
            return;
        }

        std::string_view callee_fn_name;
        if (const auto* callee_member = std::get_if<MemberExpr>(&expr.callee->node);
            callee_member != nullptr)
        {
            if (callee_member->base == nullptr)
            {
                diags_.push_back(error_at(span, "only name calls and module-qualified calls are "
                                                "supported in runnable code"));
                return;
            }

            const auto* base_name =
                std::get_if<curlee::parser::NameExpr>(&callee_member->base->node);
            if (base_name != nullptr && base_name->name == "python_ffi" &&
                callee_member->member == "call")
            {
                chunk_.emit(OpCode::PythonCall, span);
                return;
            }

            // Module-qualified call: either `alias.fn(...)` or `foo.bar.fn(...)`.
            std::vector<std::string_view> parts;
            if (!collect_member_chain(*expr.callee, parts))
            {
                diags_.push_back(error_at(span, "only name calls and module-qualified calls are "
                                                "supported in runnable code"));
                return;
            }

            const std::string_view member = parts.back();
            const std::vector<std::string_view> qualifier(parts.begin(), parts.end() - 1);

            bool qualifier_ok = false;
            if (qualifier.size() == 1)
            {
                if (imported_module_aliases_.contains(qualifier[0]))
                {
                    qualifier_ok = true;
                }
            }
            if (!qualifier_ok)
            {
                const std::string key = join_path(qualifier);
                if (imported_module_keys_.contains(key))
                {
                    qualifier_ok = true;
                }
            }

            if (!qualifier_ok)
            {
                diags_.push_back(error_at(span, "unknown module qualifier in runnable call: '" +
                                                    join_path(qualifier) + "'"));
                return;
            }

            callee_fn_name = member;
        }
        else
        {
            const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            if (callee_name == nullptr)
            {
                diags_.push_back(error_at(span, "only name calls and module-qualified calls are "
                                                "supported in runnable code"));
                return;
            }
            callee_fn_name = callee_name->name;
        }

        auto fn_it = functions_.find(callee_fn_name);
        if (fn_it == functions_.end())
        {
            diags_.push_back(
                error_at(span, "unknown function '" + std::string(callee_fn_name) + "'"));
            return;
        }

        const auto* callee_decl = fn_it->second;
        if (expr.args.size() != callee_decl->params.size())
        {
            diags_.push_back(
                error_at(span, "call to '" + std::string(callee_fn_name) + "' expects " +
                                   std::to_string(callee_decl->params.size()) + " argument(s)"));
            return;
        }

        for (const auto& arg : expr.args)
        {
            emit_expr(arg);
            if (!diags_.empty())
            {
                return;
            }
        }

        emit_call(callee_fn_name, span);
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
