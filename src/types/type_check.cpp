#include <cassert>
#include <curlee/parser/ast.h>
#include <curlee/types/type_check.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
using curlee::parser::MemberExpr;
using curlee::parser::NameExpr;
using curlee::parser::ReturnStmt;
using curlee::parser::ScopedNameExpr;
using curlee::parser::Stmt;
using curlee::parser::StructLiteralExpr;
using curlee::parser::UnsafeStmt;
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
        collect_structs_and_enums(program);

        // Builtins (compiler/runtime-provided).
        {
            FunctionType print_sig;
            // Minimal MVP: print supports the core scalar types.
            // (We model this as a pseudo-overload in the checker; the emitter enforces arity.)
            // Note: verification currently ignores builtins and does not attempt to lower them.
            print_sig.result = Type{.kind = TypeKind::Unit};
            // Placeholder; per-call validation happens in CallExpr checking.
            print_sig.params.push_back(Type{.kind = TypeKind::Unit});
            functions_.emplace("print", print_sig);
        }

        // Collect function signatures first.
        for (const auto& f : program.functions)
        {
            if (f.name == "print")
            {
                error_at(f.span, "cannot declare builtin function 'print'");
                continue;
            }
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
    int unsafe_depth_ = 0;

    struct StructInfo
    {
        std::unordered_map<std::string_view, Type> fields;
    };

    struct EnumInfo
    {
        struct VariantInfo
        {
            std::optional<Type> payload;
        };
        std::unordered_map<std::string_view, VariantInfo> variants;
    };

    std::unordered_map<std::string_view, StructInfo> structs_;
    std::unordered_map<std::string_view, EnumInfo> enums_;

    void collect_structs_and_enums(const curlee::parser::Program& program)
    {
        // First pass: collect names to allow forward references.
        for (const auto& s : program.structs)
        {
            if (structs_.contains(s.name) || enums_.contains(s.name))
            {
                error_at(s.span, "duplicate type name '" + std::string(s.name) + "'");
                continue;
            }
            structs_.emplace(s.name, StructInfo{});
        }

        for (const auto& e : program.enums)
        {
            if (structs_.contains(e.name) || enums_.contains(e.name))
            {
                error_at(e.span, "duplicate type name '" + std::string(e.name) + "'");
                continue;
            }
            enums_.emplace(e.name, EnumInfo{});
        }

        // Second pass: resolve field/variant types.
        for (const auto& s : program.structs)
        {
            auto it = structs_.find(s.name);
            if (it == structs_.end())
            {
                continue;
            }

            StructInfo info;
            for (const auto& field : s.fields)
            {
                auto ft = type_from_ast(field.type);
                if (!ft.has_value())
                {
                    continue;
                }
                info.fields.emplace(field.name, *ft);
            }
            it->second = std::move(info);
        }

        for (const auto& e : program.enums)
        {
            auto it = enums_.find(e.name);
            if (it == enums_.end())
            {
                continue;
            }

            EnumInfo info;
            for (const auto& v : e.variants)
            {
                EnumInfo::VariantInfo vi;
                if (v.payload.has_value())
                {
                    auto pt = type_from_ast(*v.payload);
                    if (pt.has_value())
                    {
                        vi.payload = *pt;
                    }
                }
                info.variants.emplace(v.name, std::move(vi));
            }
            it->second = std::move(info);
        }
    }

    static bool is_python_ffi_call(const Expr& callee)
    {
        const auto* member = std::get_if<MemberExpr>(&callee.node);
        if (member == nullptr || member->base == nullptr)
        {
            return false;
        }
        const auto* base_name = std::get_if<NameExpr>(&member->base->node);
        if (base_name == nullptr)
        {
            return false;
        }
        return base_name->name == "python_ffi" && member->member == "call";
    }

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
            if (structs_.contains(name.name))
            {
                return Type{.kind = TypeKind::Struct, .name = name.name};
            }
            if (enums_.contains(name.name))
            {
                return Type{.kind = TypeKind::Enum, .name = name.name};
            }

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

    void check_stmt_node(const UnsafeStmt& s, Span, Type expected_return)
    {
        ++unsafe_depth_;
        push_scope();
        for (const auto& stmt : s.body->stmts)
        {
            check_stmt(stmt, expected_return);
        }
        pop_scope();
        --unsafe_depth_;
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

    [[nodiscard]] std::optional<Type> check_expr_node(const curlee::parser::BoolExpr&, Span)
    {
        return Type{.kind = TypeKind::Bool};
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
            if (lhs->kind == TypeKind::String && rhs->kind == TypeKind::String)
            {
                return Type{.kind = TypeKind::String};
            }
            if (lhs->kind != TypeKind::Int || rhs->kind != TypeKind::Int)
            {
                error_at(span, "'+' expects Int+Int or String+String");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Int};

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

    [[nodiscard]] std::optional<Type> check_expr_node(const MemberExpr& e, Span span)
    {
        if (e.base == nullptr)
        {
            error_at(span, "invalid member access");
            return std::nullopt;
        }

        const auto base_t = check_expr(*e.base);
        if (!base_t.has_value())
        {
            return std::nullopt;
        }

        if (base_t->kind != TypeKind::Struct)
        {
            error_at(span, "cannot access field '" + std::string(e.member) +
                               "' on non-struct type " + std::string(to_string(*base_t)));
            return std::nullopt;
        }

        const auto it = structs_.find(base_t->name);
        if (it == structs_.end())
        {
            error_at(span, "unknown struct type '" + std::string(base_t->name) + "'");
            return std::nullopt;
        }

        const auto fit = it->second.fields.find(e.member);
        if (fit == it->second.fields.end())
        {
            error_at(span, "unknown field '" + std::string(e.member) + "' on struct '" +
                               std::string(base_t->name) + "'");
            return std::nullopt;
        }

        return fit->second;
    }

    [[nodiscard]] std::optional<Type> check_expr_node(const CallExpr& e, Span span)
    {
        if (is_python_ffi_call(*e.callee))
        {
            if (unsafe_depth_ == 0)
            {
                error_at(span, "python_ffi.call requires an unsafe context");
                return std::nullopt;
            }

            if (!e.args.empty())
            {
                error_at(span, "python_ffi.call is stubbed and currently takes 0 arguments");
                return std::nullopt;
            }

            return Type{.kind = TypeKind::Unit};
        }

        if (const auto* callee_scoped = std::get_if<ScopedNameExpr>(&e.callee->node);
            callee_scoped != nullptr)
        {
            const auto enum_it = enums_.find(callee_scoped->lhs);
            if (enum_it == enums_.end())
            {
                error_at(span, "unknown enum type '" + std::string(callee_scoped->lhs) + "'");
                return std::nullopt;
            }

            const auto var_it = enum_it->second.variants.find(callee_scoped->rhs);
            if (var_it == enum_it->second.variants.end())
            {
                error_at(span, "unknown variant '" + std::string(callee_scoped->rhs) +
                                   "' for enum '" + std::string(callee_scoped->lhs) + "'");
                return std::nullopt;
            }

            if (!var_it->second.payload.has_value())
            {
                if (!e.args.empty())
                {
                    error_at(span, "enum variant '" + std::string(callee_scoped->lhs) +
                                       "::" + std::string(callee_scoped->rhs) +
                                       "' does not take a payload");
                }
                return Type{.kind = TypeKind::Enum, .name = callee_scoped->lhs};
            }

            if (e.args.size() != 1)
            {
                error_at(span, "enum variant '" + std::string(callee_scoped->lhs) +
                                   "::" + std::string(callee_scoped->rhs) +
                                   "' expects exactly 1 payload argument");
                return Type{.kind = TypeKind::Enum, .name = callee_scoped->lhs};
            }

            const auto arg_t = check_expr(e.args[0]);
            if (arg_t.has_value() && *arg_t != *var_it->second.payload)
            {
                error_at(span, "enum payload type mismatch for '" +
                                   std::string(callee_scoped->lhs) +
                                   "::" + std::string(callee_scoped->rhs) + "': expected " +
                                   std::string(to_string(*var_it->second.payload)) + ", got " +
                                   std::string(to_string(*arg_t)));
            }

            return Type{.kind = TypeKind::Enum, .name = callee_scoped->lhs};
        }

        const auto* callee_name = std::get_if<NameExpr>(&e.callee->node);
        if (callee_name == nullptr)
        {
            error_at(span, "only direct calls are supported (callee must be a name)");
            return std::nullopt;
        }

        if (callee_name->name == "print")
        {
            if (e.args.size() != 1)
            {
                error_at(span, "print expects exactly 1 argument");
                return std::nullopt;
            }
            const auto arg_t = check_expr(e.args[0]);
            if (!arg_t.has_value())
            {
                return std::nullopt;
            }
            if (arg_t->kind != TypeKind::Int && arg_t->kind != TypeKind::Bool &&
                arg_t->kind != TypeKind::String)
            {
                error_at(span, "print only supports Int, Bool, or String");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Unit};
        }

        const auto it = functions_.find(callee_name->name);
        if (it == functions_.end())
        {
            error_at(span, "unknown function '" + std::string(callee_name->name) + "'");
            return std::nullopt;
        }

        const auto& sig = it->second;
        // Builtin placeholder signatures may not match; only user functions are checked here.
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

    [[nodiscard]] std::optional<Type> check_expr_node(const StructLiteralExpr& e, Span span)
    {
        const auto it = structs_.find(e.type_name);
        if (it == structs_.end())
        {
            error_at(span, "unknown struct type '" + std::string(e.type_name) + "'");
            return std::nullopt;
        }

        std::unordered_set<std::string_view> seen;
        for (const auto& field : e.fields)
        {
            seen.insert(field.name);

            const auto f_it = it->second.fields.find(field.name);
            if (f_it == it->second.fields.end())
            {
                error_at(field.span, "unknown field '" + std::string(field.name) +
                                         "' for struct '" + std::string(e.type_name) + "'");
                (void)check_expr(*field.value);
                continue;
            }

            const auto init_t = check_expr(*field.value);
            if (!init_t.has_value())
            {
                continue;
            }

            if (*init_t != f_it->second)
            {
                error_at(field.span, "field '" + std::string(field.name) +
                                         "' type mismatch: expected " +
                                         std::string(to_string(f_it->second)) + ", got " +
                                         std::string(to_string(*init_t)));
            }
        }

        std::vector<std::string_view> missing;
        missing.reserve(it->second.fields.size());
        for (const auto& [field_name, _] : it->second.fields)
        {
            if (!seen.contains(field_name))
            {
                missing.push_back(field_name);
            }
        }

        if (!missing.empty())
        {
            std::string msg =
                "struct literal for '" + std::string(e.type_name) + "' is missing required field";
            msg += (missing.size() == 1) ? " '" : "s: ";
            for (std::size_t i = 0; i < missing.size(); ++i)
            {
                if (missing.size() == 1)
                {
                    msg += std::string(missing[i]) + "'";
                }
                else
                {
                    if (i != 0)
                    {
                        msg += ", ";
                    }
                    msg += "'" + std::string(missing[i]) + "'";
                }
            }
            error_at(span, std::move(msg));
        }

        return Type{.kind = TypeKind::Struct, .name = e.type_name};
    }

    [[nodiscard]] std::optional<Type> check_expr_node(const ScopedNameExpr& e, Span span)
    {
        const auto enum_it = enums_.find(e.lhs);
        if (enum_it == enums_.end())
        {
            error_at(span, "unknown enum type '" + std::string(e.lhs) + "'");
            return std::nullopt;
        }

        const auto var_it = enum_it->second.variants.find(e.rhs);
        if (var_it == enum_it->second.variants.end())
        {
            error_at(span, "unknown variant '" + std::string(e.rhs) + "' for enum '" +
                               std::string(e.lhs) + "'");
            return std::nullopt;
        }

        if (var_it->second.payload.has_value())
        {
            error_at(span, "enum variant '" + std::string(e.lhs) + "::" + std::string(e.rhs) +
                               "' requires a payload; use " + std::string(e.lhs) +
                               "::" + std::string(e.rhs) + "(expr)");
            return std::nullopt;
        }

        return Type{.kind = TypeKind::Enum, .name = e.lhs};
    }
};

} // namespace

TypeCheckResult type_check(const curlee::parser::Program& program)
{
    return Checker().run(program);
}

} // namespace curlee::types
