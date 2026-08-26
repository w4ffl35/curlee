// SPDX-License-Identifier: MIT
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
using curlee::parser::AssignStmt;
using curlee::parser::BinaryExpr;
using curlee::parser::BlockStmt;
using curlee::parser::BoolExpr;
using curlee::parser::CallExpr;
using curlee::parser::Expr;
using curlee::parser::ExprStmt;
using curlee::parser::Function;
using curlee::parser::ArrayLiteralExpr;
using curlee::parser::GhostLetStmt;
using curlee::parser::IfStmt;
using curlee::parser::IndexAssignStmt;
using curlee::parser::IndexExpr;
using curlee::parser::LetStmt;
using curlee::parser::MatchStmt;
using curlee::parser::MemberExpr;
using curlee::parser::NameExpr;
using curlee::parser::ReturnStmt;
using curlee::parser::ScopedNameExpr;
using curlee::parser::Stmt;
using curlee::parser::StructLiteralExpr;
using curlee::parser::StructDecl;
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
        "__set_insert_int", // GCOVR_EXCL_LINE
    }; // GCOVR_EXCL_LINE
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

        structs_.clear();
        for (const auto& s : program.structs)
        {
            if (structs_.contains(s.name))
            {
                diags_.push_back(error_at(s.span, "duplicate struct declaration '" +
                                                      std::string(s.name) + "'"));
                return diags_;
            }
            structs_.emplace(s.name, &s);
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

        // Extern functions have no VM implementation: their bodies are provided
        // at link time by the host boot stub/runtime (curlee build --link only).
        // Reject before emitting any bytecode so a program containing an extern
        // can never silently run it as an empty-body no-op.
        for (const auto& f : program.functions)
        {
            if (f.is_extern)
            {
                diags_.push_back(error_at(
                    f.span, "extern functions are not supported by the VM; use curlee build --link"));
                return diags_;
            }
        }

        // Ghost functions are verification-only: they are erased from codegen
        // and produce no bytecode. They cannot be `main` (the entry point must
        // be runnable), and they are simply skipped in the emit loop below.
        for (const auto& f : program.functions)
        {
            if (f.is_ghost && f.name == "main")
            {
                diags_.push_back(error_at(f.span, "ghost function cannot be 'main'"));
                return diags_;
            }
        }

        const Function* entry = find_main(program);
        if (entry == nullptr)
        {
            diags_.push_back(error_at({}, "no entry function 'main' found"));
            return diags_;
        }

        // Module-level mutable state (issue #287): allocate a global slot per
        // scalar `static` and emit its one-time initializer at program start.
        // The slots occupy the LOW region of the VM's locals array (before any
        // function's locals), so they persist across the whole run and separate
        // function calls observe each other's mutations. Fixed-size array
        // statics are freestanding-only (the VM has no array storage, exactly
        // like array `let`s).
        globals_.clear();
        std::uint16_t global_slot = 0;
        for (const auto& s : program.statics)
        {
            if (s.type.is_array())
            {
                diags_.push_back(
                    error_at(s.span, "arrays are freestanding-only and not supported in the VM"));
                return diags_;
            }
            globals_.emplace(s.name, global_slot++);
        }
        for (const auto& s : program.statics)
        {
            emit_expr(s.value);
            if (!diags_.empty())
            {
                return diags_;
            }
            chunk_.emit_local(OpCode::StoreLocal, globals_.at(s.name), s.span);
        }
        next_local_base_ = global_slot;

        // Emit main first so the VM entry point is ip=0.
        emit_function(*entry, /*is_main=*/true);
        for (const auto& f : program.functions)
        {
            if (f.name == "main")
            {
                continue;
            }
            if (f.is_ghost)
            {
                // Ghost functions produce no bytecode.
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
    // Module-level mutable state (issue #287): name -> VM local-slot. The slots
    // live in the low region of the locals array and persist across the whole
    // run (function locals start after them).
    std::unordered_map<std::string_view, std::uint16_t> globals_;
    std::uint16_t local_base_ = 0;
    std::uint16_t next_local_base_ = 0;
    bool current_is_main_ = false;

    std::unordered_map<std::string_view, const Function*> functions_;
    std::unordered_map<std::string_view, const StructDecl*> structs_;

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

    std::uint16_t emit_string_constant(std::string_view value, Span span,
                                       std::string_view context)
    {
        const auto idx = chunk_.add_constant(Value::string_v(std::string(value)));
        if (idx > std::numeric_limits<std::uint16_t>::max())
        {
            diags_.push_back(error_at(span, std::string(context) + " constant index overflow"));
            return 0;
        }
        return static_cast<std::uint16_t>(idx);
    }

    void emit_enum_constructor(std::string_view enum_name, std::string_view variant_name,
                               const Expr* payload_expr, Span span)
    {
        if (payload_expr != nullptr)
        {
            emit_expr(*payload_expr);
            if (!diags_.empty())
            {
                return;
            }
        }

        const auto enum_idx = emit_string_constant(enum_name, span, "enum name");
        if (!diags_.empty())
        {
            return;
        }
        const auto variant_idx = emit_string_constant(variant_name, span, "enum variant");
        if (!diags_.empty())
        {
            return;
        }

        chunk_.emit(OpCode::MakeEnum, span);
        chunk_.emit_u16(enum_idx, span);
        chunk_.emit_u16(variant_idx, span);
        chunk_.code.push_back(payload_expr != nullptr ? 1 : 0);
        chunk_.spans.push_back(span);
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
            if (!p.type.is_capability && p.type.name != "Int" && p.type.name != "Bool")
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
        // Fixed-size arrays are freestanding-only in the MVP (issue #278):
        // the VM has no array storage, so reject before evaluating the
        // initializer (whose ArrayLiteralExpr has no VM lowering either).
        if (stmt.type.is_array())
        {
            diags_.push_back(
                error_at(span, "arrays are freestanding-only and not supported in the VM"));
            return;
        }

        emit_expr(stmt.value);
        if (!diags_.empty())
        {
            return;
        }

        const auto slot = static_cast<std::uint16_t>(local_base_ + locals_.size());
        locals_.emplace(stmt.name, slot);
        chunk_.emit_local(OpCode::StoreLocal, slot, span);
    }

    void emit_stmt_node(const GhostLetStmt&, Span)
    {
        // Ghost snapshots are verification-only and produce no runtime code:
        // erase the statement from the bytecode (the verifier has already
        // discharged any obligations referencing it).
    }

    void emit_stmt_node(const IndexAssignStmt& stmt, Span span)
    {
        // `arr[i] = v;` mutates an array element: freestanding-only in the MVP
        // (issue #278); the VM rejects arrays outright.
        (void)stmt;
        diags_.push_back(
            error_at(span, "arrays are freestanding-only and not supported in the VM"));
    }

    void emit_stmt_node(const AssignStmt& stmt, Span span)
    {
        // `x = expr;` reassigns an existing binding: evaluate the RHS, then
        // StoreLocal into the binding's existing slot (a `let` would allocate
        // a NEW slot; an assignment reuses the slot so loop-carried mutation
        // works and the VM's StoreLocal overwrites the slot in place). A
        // module-level static (issue #287) stores into its persistent global
        // slot.
        emit_expr(stmt.value);
        if (!diags_.empty())
        {
            return;
        }

        auto it = locals_.find(stmt.name);
        if (it != locals_.end())
        {
            chunk_.emit_local(OpCode::StoreLocal, it->second, span);
            return;
        }
        auto git = globals_.find(stmt.name);
        if (git != globals_.end())
        {
            chunk_.emit_local(OpCode::StoreLocal, git->second, span);
            return;
        }
        diags_.push_back(error_at(span, "unknown name '" + std::string(stmt.name) +
                                            "' in runnable code"));
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

    void emit_stmt_node(const MatchStmt& stmt, Span)
    {
        emit_expr(stmt.value);
        if (!diags_.empty())
        {
            return;
        }

        const auto match_slot = static_cast<std::uint16_t>(local_base_ + locals_.size());
        chunk_.emit_local(OpCode::StoreLocal, match_slot, stmt.value.span);

        const auto locals_before_match = locals_;
        std::vector<std::size_t> end_patches;

        for (const auto& arm : stmt.arms)
        {
            locals_ = locals_before_match;

            const auto enum_idx =
                emit_string_constant(arm.pattern.enum_name, arm.pattern.span, "match enum");
            if (!diags_.empty())
            {
                return;
            }
            const auto variant_idx =
                emit_string_constant(arm.pattern.variant_name, arm.pattern.span, "match variant");
            if (!diags_.empty())
            {
                return;
            }

            chunk_.emit_local(OpCode::LoadLocal, match_slot, arm.pattern.span);
            chunk_.emit(OpCode::EnumIs, arm.pattern.span);
            chunk_.emit_u16(enum_idx, arm.pattern.span);
            chunk_.emit_u16(variant_idx, arm.pattern.span);

            chunk_.emit(OpCode::JumpIfFalse, arm.pattern.span);
            const auto next_arm_patch = emit_u16_placeholder(arm.pattern.span);

            if (arm.pattern.payload_name.has_value())
            {
                const auto payload_slot = static_cast<std::uint16_t>(local_base_ + locals_.size());
                const auto inserted = locals_.emplace(*arm.pattern.payload_name, payload_slot);
                if (!inserted.second)
                {
                    diags_.push_back(error_at(
                        arm.pattern.span,
                        "duplicate match payload binding '" +
                            std::string(*arm.pattern.payload_name) + "' in runnable code"));
                    return;
                }

                chunk_.emit_local(OpCode::LoadLocal, match_slot, arm.pattern.span);
                chunk_.emit(OpCode::EnumUnwrap, arm.pattern.span);
                chunk_.emit_u16(enum_idx, arm.pattern.span);
                chunk_.emit_u16(variant_idx, arm.pattern.span);
                chunk_.emit_local(OpCode::StoreLocal, payload_slot, arm.pattern.span);
            }

            if (arm.body == nullptr)
            {
                diags_.push_back(error_at(arm.span, "match arm body missing in runnable code"));
                return;
            }

            for (const auto& nested : arm.body->stmts)
            {
                emit_stmt(nested);
                if (!diags_.empty())
                {
                    return;
                }
            }

            chunk_.emit(OpCode::Jump, arm.span);
            end_patches.push_back(emit_u16_placeholder(arm.span));
            patch_u16(next_arm_patch, static_cast<std::uint16_t>(ip()));
        }

        locals_ = locals_before_match;
        for (const auto patch : end_patches)
        {
            patch_u16(patch, static_cast<std::uint16_t>(ip()));
        }
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
        if (it != locals_.end())
        {
            chunk_.emit_local(OpCode::LoadLocal, it->second, span);
            return;
        }
        // Module-level static reads load the persistent global slot (issue
        // #287); the local lookup first makes a shadowing `let` win, matching
        // the type checker's scope resolution.
        auto git = globals_.find(expr.name);
        if (git != globals_.end())
        {
            chunk_.emit_local(OpCode::LoadLocal, git->second, span);
            return;
        }
        diags_.push_back(error_at(span, "unknown name '" + std::string(expr.name) + "'"));
    }

    void emit_expr_node(const MemberExpr& expr, Span span)
    {
        if (expr.base == nullptr)
        {
            diags_.push_back(error_at(span, "invalid member access in runnable code"));
            return;
        }

        emit_expr(*expr.base);
        if (!diags_.empty())
        {
            return;
        }

        const auto field_idx = emit_string_constant(expr.member, span, "struct field");
        if (!diags_.empty())
        {
            return;
        }

        chunk_.emit(OpCode::GetField, span);
        chunk_.emit_u16(field_idx, span);
    }

    void emit_expr_node(const ScopedNameExpr& expr, Span span)
    {
        emit_enum_constructor(expr.lhs, expr.rhs, nullptr, span);
    }

    void emit_expr_node(const StructLiteralExpr& expr, Span span)
    {
        const auto struct_it = structs_.find(expr.type_name);
        if (struct_it == structs_.end())
        {
            diags_.push_back(error_at(span, "unknown struct '" + std::string(expr.type_name) +
                                                "' in runnable code"));
            return;
        }

        const auto* decl = struct_it->second;
        std::unordered_map<std::string_view, const curlee::parser::StructLiteralExprField*>
            provided_fields;
        provided_fields.reserve(expr.fields.size());
        for (const auto& field : expr.fields)
        {
            const auto [_, inserted] = provided_fields.emplace(field.name, &field);
            if (!inserted)
            {
                diags_.push_back(error_at(field.span, "duplicate field '" + std::string(field.name) +
                                                          "' in struct literal"));
                return;
            }
        }

        std::unordered_set<std::string_view> declared_fields;
        declared_fields.reserve(decl->fields.size());
        for (const auto& field : decl->fields)
        {
            declared_fields.insert(field.name);
        }

        for (const auto& field : expr.fields)
        {
            if (!declared_fields.contains(field.name))
            {
                diags_.push_back(error_at(field.span, "unknown field '" + std::string(field.name) +
                                                          "' for struct '" +
                                                      std::string(expr.type_name) + "'"));
                return;
            }
        }

        if (decl->fields.size() != expr.fields.size())
        {
            diags_.push_back(error_at(span, "struct literal for '" + std::string(expr.type_name) +
                                                "' has incorrect field count"));
            return;
        }

        if (decl->fields.size() > std::numeric_limits<std::uint16_t>::max())
        {
            diags_.push_back(
                error_at(span, "struct field count exceeds 16-bit runnable bytecode limit"));
            return;
        }

        for (const auto& declared : decl->fields)
        {
            const auto literal_field_it = provided_fields.find(declared.name);
            // Unreachable with current invariants: we already validated that every
            // literal field name is declared and the field counts are equal.
            // Keeping this diagnostic as a defensive guard for future parser/AST changes.
            // GCOVR_EXCL_START
            if (literal_field_it == provided_fields.end())
            {
                diags_.push_back(error_at(span, "missing field '" + std::string(declared.name) +
                                                    "' in struct literal for '" +
                                                std::string(expr.type_name) + "'"));
                return;
            }
            // GCOVR_EXCL_STOP

            const auto* literal_field = literal_field_it->second;
            if (literal_field->value == nullptr)
            {
                diags_.push_back(error_at(literal_field->span,
                                          "missing struct field value in runnable code"));
                return;
            }

            emit_expr(*literal_field->value);
            if (!diags_.empty())
            {
                return;
            }
        }

        const auto struct_idx = emit_string_constant(expr.type_name, span, "struct name");
        if (!diags_.empty())
        {
            return;
        }

        chunk_.emit(OpCode::MakeStruct, span);
        chunk_.emit_u16(struct_idx, span);
        chunk_.emit_u16(static_cast<std::uint16_t>(decl->fields.size()), span);
        for (const auto& declared : decl->fields)
        {
            const auto field_idx = emit_string_constant(declared.name, declared.span, "struct field");
            if (!diags_.empty())
            {
                return;
            }
            chunk_.emit_u16(field_idx, declared.span);
        }
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
        case TokenKind::Tilde:
            chunk_.emit(OpCode::BitNot, span);
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
        case TokenKind::Percent:
            chunk_.emit(OpCode::Mod, span);
            return;
        case TokenKind::Amp:
            chunk_.emit(OpCode::BitAnd, span);
            return;
        case TokenKind::Pipe:
            chunk_.emit(OpCode::BitOr, span);
            return;
        case TokenKind::Caret:
            chunk_.emit(OpCode::BitXor, span);
            return;
        case TokenKind::ShiftLeft:
            chunk_.emit(OpCode::ShiftLeft, span);
            return;
        case TokenKind::ShiftRight:
            chunk_.emit(OpCode::ShiftRight, span);
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
        if (const auto* callee_name = std::get_if<curlee::parser::NameExpr>(&expr.callee->node);
            callee_name != nullptr)
        {
            const auto emit_intrinsic = [&](OpCode op, std::size_t arity,
                                            const char* display_name) -> bool
            {
                if (expr.args.size() != arity)
                {
                    diags_.push_back(error_at(span, std::string(display_name) + " expects exactly " +
                                                        std::to_string(arity) + " argument(s)"));
                    return true;
                }
                for (const auto& arg : expr.args)
                {
                    emit_expr(arg);
                    if (!diags_.empty())
                    {
                        return true;
                    }
                }
                chunk_.emit(op, span);
                return true;
            };

            if (callee_name->name == "__read_line")
            {
                emit_intrinsic(OpCode::ReadLine, 1, "__read_line");
                return;
            }
            if (callee_name->name == "__fs_read_text")
            {
                emit_intrinsic(OpCode::FsReadText, 2, "__fs_read_text");
                return;
            }
            if (callee_name->name == "__fs_write_text")
            {
                emit_intrinsic(OpCode::FsWriteText, 3, "__fs_write_text");
                return;
            }
            if (callee_name->name == "__tty_clear")
            {
                emit_intrinsic(OpCode::TtyClear, 1, "__tty_clear");
                return;
            }
            if (callee_name->name == "__tty_write_at")
            {
                emit_intrinsic(OpCode::TtyWriteAt, 4, "__tty_write_at");
                return;
            }
            if (callee_name->name == "__tty_flush")
            {
                emit_intrinsic(OpCode::TtyFlush, 1, "__tty_flush");
                return;
            }
            if (callee_name->name == "__rng_next_int")
            {
                emit_intrinsic(OpCode::RngNextInt, 2, "__rng_next_int");
                return;
            }
            if (callee_name->name == "__vec_new_int")
            {
                emit_intrinsic(OpCode::VecNew, 1, "__vec_new_int");
                return;
            }
            if (callee_name->name == "__vec_len_int")
            {
                emit_intrinsic(OpCode::VecLen, 1, "__vec_len_int");
                return;
            }
            if (callee_name->name == "__vec_push_int")
            {
                emit_intrinsic(OpCode::VecPush, 2, "__vec_push_int");
                return;
            }
            if (callee_name->name == "__vec_get_int")
            {
                emit_intrinsic(OpCode::VecGet, 2, "__vec_get_int");
                return;
            }
            if (callee_name->name == "__vec_set_int")
            {
                emit_intrinsic(OpCode::VecSet, 3, "__vec_set_int");
                return;
            }
            if (callee_name->name == "__vec_new_bool")
            {
                emit_intrinsic(OpCode::VecNewBool, 1, "__vec_new_bool");
                return;
            }
            if (callee_name->name == "__vec_len_bool")
            {
                emit_intrinsic(OpCode::VecLenBool, 1, "__vec_len_bool");
                return;
            }
            if (callee_name->name == "__vec_push_bool")
            {
                emit_intrinsic(OpCode::VecPushBool, 2, "__vec_push_bool");
                return;
            }
            if (callee_name->name == "__vec_get_bool")
            {
                emit_intrinsic(OpCode::VecGetBool, 2, "__vec_get_bool");
                return;
            }
            if (callee_name->name == "__vec_set_bool")
            {
                emit_intrinsic(OpCode::VecSetBool, 3, "__vec_set_bool");
                return;
            }
            if (callee_name->name == "__set_new_int")
            {
                emit_intrinsic(OpCode::SetNewInt, 0, "__set_new_int");
                return;
            }
            if (callee_name->name == "__set_has_int")
            {
                emit_intrinsic(OpCode::SetHasInt, 2, "__set_has_int");
                return;
            }
            if (callee_name->name == "__set_insert_int")
            {
                emit_intrinsic(OpCode::SetInsertInt, 2, "__set_insert_int");
                return;
            }

            const auto emit_variant_predicate = [&](OpCode op,
                                                    const char* display_name) -> bool
            {
                if (expr.args.size() != 2)
                {
                    diags_.push_back(error_at(span, std::string(display_name) +
                                                        " expects exactly 2 argument(s)"));
                    return true;
                }

                const auto* variant = std::get_if<ScopedNameExpr>(&expr.args[1].node);
                if (variant == nullptr)
                {
                    diags_.push_back(error_at(
                        span,
                        std::string(display_name) +
                            " expects enum variant tag as second argument"));
                    return true;
                }

                emit_expr(expr.args[0]);
                if (!diags_.empty())
                {
                    return true;
                }

                const auto enum_idx = emit_string_constant(variant->lhs, span, display_name);
                const auto variant_idx =
                    emit_string_constant(variant->rhs, span, display_name);
                chunk_.emit(op, span);
                chunk_.emit_u16(enum_idx, span);
                chunk_.emit_u16(variant_idx, span);
                return true;
            };

            if (callee_name->name == "variant_is")
            {
                emit_variant_predicate(OpCode::EnumIs, "variant_is");
                return;
            }

            if (callee_name->name == "variant_unwrap")
            {
                emit_variant_predicate(OpCode::EnumUnwrap, "variant_unwrap");
                return;
            }
        }

        if (const auto* callee_scoped = std::get_if<ScopedNameExpr>(&expr.callee->node);
            callee_scoped != nullptr)
        {
            if (expr.args.size() > 1)
            {
                diags_.push_back(error_at(
                    span,
                    "enum constructors in runnable code accept at most one payload argument"));
                return;
            }

            const Expr* payload_expr = expr.args.empty() ? nullptr : &expr.args[0];
            emit_enum_constructor(callee_scoped->lhs, callee_scoped->rhs, payload_expr, span);
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
        if (callee_decl->is_ghost)
        {
            // Ghost functions are verification-only and erased from the bytecode:
            // a runnable function must never call one (the call would dangle).
            diags_.push_back(error_at(span, "cannot call ghost function '" +
                                                std::string(callee_fn_name) +
                                                "' from runnable code"));
            return;
        }
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

    void emit_expr_node(const curlee::parser::PhysExpr&, Span span)
    {
        diags_.push_back(error_at(span, "Phys is freestanding-only and not supported in the VM"));
    }

    void emit_expr_node(const curlee::parser::PhysReadExpr&, Span span)
    {
        diags_.push_back(error_at(span, "Phys is freestanding-only and not supported in the VM"));
    }

    void emit_expr_node(const curlee::parser::PhysWriteExpr&, Span span)
    {
        diags_.push_back(error_at(span, "Phys is freestanding-only and not supported in the VM"));
    }

    void emit_expr_node(const curlee::parser::PortIOExpr&, Span span)
    {
        diags_.push_back(
            error_at(span, "port I/O is freestanding-only and not supported in the VM"));
    }

    void emit_expr_node(const curlee::parser::RuntimePhysReadExpr&, Span span)
    {
        diags_.push_back(
            error_at(span, "physical memory reads are freestanding-only and not supported in the VM"));
    }

    void emit_expr_node(const curlee::parser::IndexExpr&, Span span)
    {
        diags_.push_back(error_at(span, "arrays are freestanding-only and not supported in the VM"));
    }

    void emit_expr_node(const curlee::parser::ArrayLiteralExpr&, Span span)
    {
        diags_.push_back(error_at(span, "arrays are freestanding-only and not supported in the VM"));
    }
};

} // namespace

EmitResult emit_bytecode(const curlee::parser::Program& program)
{
    Emitter emitter;
    return emitter.run(program);
}

} // namespace curlee::compiler
