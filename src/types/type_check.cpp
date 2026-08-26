// SPDX-License-Identifier: MIT
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
using curlee::parser::AssignStmt;
using curlee::parser::BinaryExpr;
using curlee::parser::BlockStmt;
using curlee::parser::CallExpr;
using curlee::parser::Expr;
using curlee::parser::ExprStmt;
using curlee::parser::Function;
using curlee::parser::GroupExpr;
using curlee::parser::GhostLetStmt;
using curlee::parser::IfStmt;
using curlee::parser::LetStmt;
using curlee::parser::MatchStmt;
using curlee::parser::MemberExpr;
using curlee::parser::NameExpr;
using curlee::parser::ReturnStmt;
using curlee::parser::ScopedNameExpr;
using curlee::parser::Stmt;
using curlee::parser::StructLiteralExpr;
using curlee::parser::UnsafeStmt;
using curlee::parser::WhileStmt;
using curlee::source::Span;

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

struct Scope
{
    std::unordered_map<std::string_view, Type> vars;

    // Names in this scope that are not assignable: parameters and ghost
    // snapshot bindings. Parameters cannot be reassigned because call-site
    // ensures inheritance assumes a callee's parameters keep their entry
    // values (post_ctx binds callee param names to the caller's argument
    // expressions); ghost snapshots freeze a pre-state value and must never
    // be mutated. Assignment targets are therefore limited to local `let`
    // bindings (MVP semantics, documented in docs/loop-contracts.md).
    std::unordered_set<std::string_view> read_only;
};

class Checker
{
  public:
    [[nodiscard]] TypeCheckResult run(const curlee::parser::Program& program)
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
            if (is_reserved_builtin_name(f.name))
            {
                error_at(f.span, "cannot declare builtin function '" + std::string(f.name) +
                                     "'");
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
    std::unordered_set<std::string> imported_module_keys_;
    std::unordered_map<std::string_view, std::string> imported_module_aliases_;
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
        std::vector<std::string_view> variant_order;
    };

    std::unordered_map<std::string_view, StructInfo> structs_;
    std::unordered_map<std::string_view, EnumInfo> enums_;

    void collect_structs_and_enums(const curlee::parser::Program& program)
    {
        // First pass: collect names to allow forward references.
        for (const auto& s : program.structs)
        {
            // Note: enums_ is empty during this first structs-only pass.
            if (structs_.contains(s.name))
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
            enums_.emplace(e.name, EnumInfo{}); // GCOVR_EXCL_LINE
        }

        // Second pass: resolve field/variant types.
        for (const auto& s : program.structs)
        {
            auto it = structs_.find(s.name);
            if (it == structs_.end()) // GCOVR_EXCL_LINE
            {
                continue; // GCOVR_EXCL_LINE
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
                info.variant_order.push_back(v.name);
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

    void declare_var(std::string_view name, Type t, bool read_only = false)
    {
        if (scopes_.empty()) // GCOVR_EXCL_LINE
        {
            push_scope(); // GCOVR_EXCL_LINE
        }
        scopes_.back().vars[name] = t;
        if (read_only)
        {
            scopes_.back().read_only.insert(name);
        }
    }

    // Resolve the type of a name for an assignment target, along with whether
    // the binding is assignable. Returns std::nullopt when the name is not a
    // variable (or is a function name).
    [[nodiscard]] std::optional<std::pair<Type, bool>>
    lookup_assignable_var(std::string_view name) const
    {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
        {
            if (auto found = it->vars.find(name); found != it->vars.end())
            {
                return std::make_pair(found->second, !it->read_only.contains(name));
            }
        }
        return std::nullopt;
    }

    void error_at(Span span, std::string message)
    {
        Diagnostic d;
        d.severity = Severity::Error;
        d.message = std::move(message);
        d.span = span;
        diags_.push_back(std::move(d));
    }

    [[nodiscard]] bool has_capability_value(std::string_view cap_name) const
    {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
        {
            for (const auto& [var_name, t] : it->vars)
            {
                (void)var_name;
                if (t.kind == TypeKind::Capability && t.name == cap_name)
                {
                    return true;
                }
            }
        }
        return false;
    }

    void require_capability(std::string_view cap_name, Span use_span)
    {
        info_.required_capabilities.push_back(
            curlee::types::TypeInfo::RequiredCapability{.name = cap_name, .span = use_span});

        if (!has_capability_value(cap_name))
        {
            error_at(use_span, "missing capability value '" + std::string(cap_name) +
                                   "' (add a parameter of type cap " + std::string(cap_name) + ")");
        }
    }

    [[nodiscard]] std::optional<Type> type_from_ast(const curlee::parser::TypeName& name)
    {
        // MVP: capability types are introduced by the explicit `cap <name>` syntax.
        if (name.is_capability)
        {
            return Type{.kind = TypeKind::Capability, .name = name.name};
        }

        // Physical memory pointer type: `Phys<U8|U16|U32|U64>`.
        if (name.name == "Phys")
        {
            if (!name.type_arg.has_value())
            {
                error_at(name.span, "Phys requires an element type argument (Phys<U8|U16|U32|U64>)");
                return std::nullopt;
            }
            const std::string_view elem = *name.type_arg;
            const auto elem_kind = phys_element_kind_from_name(elem);
            if (!elem_kind.has_value())
            {
                error_at(name.span, "unsupported Phys element kind '" + std::string(elem) +
                                        "' (expected U8, U16, U32 or U64)");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Phys, .name = "Phys", .element_kind = *elem_kind,
                        .element_name = elem};
        }

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
            // Parameters are read-only (not assignable): call-site ensures
            // inheritance assumes callee parameters keep their entry values.
            declare_var(f.params[i].name, it->second.params[i], /*read_only=*/true);
        }

        // Contract-block ghost snapshots are declared (and their initializers
        // type-checked) before the body so that `ensures` clauses referencing
        // the snapshot names type-check against them. The snapshot bindings are
        // verification-only and never reach codegen.
        for (const auto& g : f.ghost_lets)
        {
            check_stmt_node(g, f.span, it->second.result);
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

    void check_stmt_node(const GhostLetStmt& s, Span stmt_span, Type)
    {
        // Ghost snapshots are type-checked like ordinary lets: the snapshot name
        // is declared with the annotated type (or inferred from the initializer
        // when omitted) and the initializer is checked for a matching type. The
        // binding is verification-only (the emitter rejects it in emitted
        // bodies), but type errors in ghost bindings must still be caught here.
        auto init = check_expr(s.value);
        if (!init.has_value())
        {
            return;
        }

        if (s.type.has_value())
        {
            auto declared = type_from_ast(*s.type);
            if (!declared.has_value())
            {
                return;
            }
            if (*init != *declared)
            {
                error_at(stmt_span, "type mismatch in ghost let: expected " +
                                        std::string(to_string(*declared)) + ", got " +
                                        std::string(to_string(*init)));
            }
            // Ghost snapshots freeze a pre-state value; they are read-only.
            declare_var(s.name, *declared, /*read_only=*/true);
        }
        else
        {
            // Infer the snapshot type from the initializer.
            declare_var(s.name, *init, /*read_only=*/true);
        }
    }

    void check_stmt_node(const AssignStmt& s, Span stmt_span, Type)
    {
        // Assignment target must be an existing, assignable local binding.
        const auto target = lookup_assignable_var(s.name);
        if (!target.has_value())
        {
            if (functions_.find(s.name) != functions_.end())
            {
                error_at(stmt_span,
                         "cannot assign to function name '" + std::string(s.name) + "'");
            }
            else
            {
                error_at(stmt_span,
                         "cannot assign to unknown name '" + std::string(s.name) + "'");
            }
            return;
        }

        if (!target->second)
        {
            error_at(stmt_span,
                     "cannot assign to '" + std::string(s.name) +
                         "': parameters and ghost snapshots are read-only "
                         "(assignment targets must be local 'let' bindings)");
            return;
        }

        auto value_t = check_expr(s.value);
        if (!value_t.has_value())
        {
            return;
        }

        if (*value_t != target->first)
        {
            error_at(stmt_span, "type mismatch in assignment: expected " +
                                    std::string(to_string(target->first)) + ", got " +
                                    std::string(to_string(*value_t)));
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

    void check_stmt_node(const MatchStmt& s, Span stmt_span, Type expected_return)
    {
        const auto value_t = check_expr(s.value);
        if (!value_t.has_value())
        {
            return;
        }
        if (value_t->kind != TypeKind::Enum)
        {
            error_at(stmt_span,
                     "match expects an enum value, got " + std::string(to_string(*value_t)));
            for (const auto& arm : s.arms)
            {
                push_scope();
                if (arm.body != nullptr)
                {
                    for (const auto& nested : arm.body->stmts)
                    {
                        check_stmt(nested, expected_return);
                    }
                }
                pop_scope();
            }
            return;
        }

        const auto enum_it = enums_.find(value_t->name);
        if (enum_it == enums_.end()) // GCOVR_EXCL_LINE
        {
            error_at(stmt_span,                                                 // GCOVR_EXCL_LINE
                     "unknown enum type '" + std::string(value_t->name) + "'"); // GCOVR_EXCL_LINE
            return;                                                             // GCOVR_EXCL_LINE
        }

        std::unordered_map<std::string_view, Span> seen_variants;

        for (const auto& arm : s.arms)
        {
            const auto& pattern = arm.pattern;

            if (pattern.enum_name != value_t->name)
            {
                error_at(pattern.span, "match arm uses enum '" + std::string(pattern.enum_name) +
                                           "' but matched value is '" + std::string(value_t->name) +
                                           "'");
            }

            const auto variant_it = enum_it->second.variants.find(pattern.variant_name);
            if (variant_it == enum_it->second.variants.end())
            {
                error_at(pattern.span, "unknown variant '" + std::string(pattern.variant_name) +
                                           "' for enum '" + std::string(value_t->name) + "'");
                push_scope();
                if (arm.body != nullptr)
                {
                    for (const auto& nested : arm.body->stmts)
                    {
                        check_stmt(nested, expected_return);
                    }
                }
                pop_scope();
                continue;
            }

            if (const auto seen_it = seen_variants.find(pattern.variant_name);
                seen_it != seen_variants.end())
            {
                Diagnostic d;
                d.severity = Severity::Error;
                d.message = "duplicate match arm for variant '" + std::string(value_t->name) +
                            "::" + std::string(pattern.variant_name) + "'";
                d.span = pattern.span;
                d.notes.push_back(
                    curlee::diag::Related{.message = "previous arm is here", // GCOVR_EXCL_LINE
                                          .span = seen_it->second});
                diags_.push_back(std::move(d));
            }
            else
            {
                seen_variants.emplace(pattern.variant_name, pattern.span);
            }

            push_scope();
            if (variant_it->second.payload.has_value())
            {
                if (!pattern.payload_name.has_value())
                {
                    error_at(pattern.span, "match arm for '" + std::string(value_t->name) +
                                               "::" + std::string(pattern.variant_name) +
                                               "' must bind payload with (name)");
                }
                else
                {
                    declare_var(*pattern.payload_name, *variant_it->second.payload);
                }
            }
            else if (pattern.payload_name.has_value())
            {
                error_at(pattern.span, "match arm for '" + std::string(value_t->name) +
                                           "::" + std::string(pattern.variant_name) +
                                           "' cannot bind payload (variant has no payload)");
            }

            if (arm.body != nullptr)
            {
                for (const auto& nested : arm.body->stmts)
                {
                    check_stmt(nested, expected_return);
                }
            }
            pop_scope();
        }

        std::vector<std::string_view> missing;
        for (const auto& variant_name : enum_it->second.variant_order)
        {
            if (!seen_variants.contains(variant_name))
            {
                missing.push_back(variant_name);
            }
        }
        if (!missing.empty())
        {
            std::string msg =
                "non-exhaustive match on enum '" + std::string(value_t->name) + "': missing arm";
            msg += (missing.size() == 1) ? " " : "s ";
            for (std::size_t i = 0; i < missing.size(); ++i)
            {
                if (i != 0)
                {
                    msg += ", ";
                }
                msg += std::string(value_t->name) + "::" + std::string(missing[i]);
            }
            error_at(stmt_span, std::move(msg));
        }
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

        if (e.op == TokenKind::Bang) // GCOVR_EXCL_LINE
        {
            if (rhs->kind != TypeKind::Bool)
            {
                error_at(span, "unary '!' expects Bool");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Bool};
        }

        // Bitwise complement: Int or any unsigned fixed-width integer type.
        if (e.op == TokenKind::Tilde)
        {
            if (rhs->kind != TypeKind::Int && !is_phys_element_kind(rhs->kind))
            {
                error_at(span, "unary '~' expects Int or an unsigned integer type (U8/U16/U32/U64)");
                return std::nullopt;
            }
            return *rhs;
        }

        error_at(span, "unsupported unary operator"); // GCOVR_EXCL_LINE
        return std::nullopt;                          // GCOVR_EXCL_LINE
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

        // Modulo: non-negative Integer semantics are defined by the identity
        // a % b == a - (a / b) * b (issue #270). Type-wise it behaves like the
        // other arithmetic operators.
        case TokenKind::Percent:
            if (lhs->kind != TypeKind::Int || rhs->kind != TypeKind::Int)
            {
                error_at(span, "'%' expects Int operands");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Int};

        // Bitwise operators: Int or unsigned fixed-width integer types
        // (U8/U16/U32/U64). Both operands must have the same type; the result
        // keeps that type (issue #270).
        case TokenKind::Amp:
        case TokenKind::Pipe:
        case TokenKind::Caret:
        case TokenKind::ShiftLeft:
        case TokenKind::ShiftRight:
        {
            const bool lhs_ok = lhs->kind == TypeKind::Int || is_phys_element_kind(lhs->kind);
            const bool rhs_ok = rhs->kind == TypeKind::Int || is_phys_element_kind(rhs->kind);
            if (!lhs_ok || !rhs_ok)
            {
                error_at(span, "bitwise operators expect Int or unsigned integer "
                               "operands (U8/U16/U32/U64)");
                return std::nullopt;
            }
            if (lhs->kind != rhs->kind)
            {
                error_at(span, "bitwise operators expect matching operand types");
                return std::nullopt;
            }
            return *lhs;
        }

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

        error_at(span, "unsupported binary operator"); // GCOVR_EXCL_LINE
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
        // This is an internal consistency guard: a Struct type should always resolve
        // to a known struct definition in `structs_`.
        //
        // Keep it for robustness, but exclude it from coverage since it should be
        // unreachable for all well-formed programs that pass earlier phases.
        // GCOVR_EXCL_START
        if (it == structs_.end())
        {
            error_at(span, "unknown struct type '" + std::string(base_t->name) + "'");
            return std::nullopt;
        }
        // GCOVR_EXCL_STOP

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

            require_capability("python.ffi", span);

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

        std::string_view callee_name;
        if (const auto* callee_direct = std::get_if<NameExpr>(&e.callee->node);
            callee_direct != nullptr)
        {
            callee_name = callee_direct->name;
        }
        else if (const auto* callee_member = std::get_if<MemberExpr>(&e.callee->node);
                 callee_member != nullptr)
        {
            // Module-qualified call: either `alias.fn(...)` or `foo.bar.fn(...)`.
            std::vector<std::string_view> parts;
            if (!collect_member_chain(*e.callee, parts))
            {
                error_at(span, "only direct calls are supported (callee must be a name)");
                return std::nullopt;
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
                error_at(span, "unknown module qualifier in call: '" + join_path(qualifier) + "'");
                return std::nullopt;
            }

            callee_name = member;
        }
        else
        {
            error_at(span, "only direct calls are supported (callee must be a name)");
            return std::nullopt;
        }

        if (callee_name == "print")
        {
            if (e.args.size() != 1)
            {
                error_at(span, "print expects exactly 1 argument");
                return std::nullopt;
            }

            require_capability("io.stdout", span);

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

        const auto check_builtin_arity = [&](std::size_t arity,
                                             std::string_view name) -> bool
        {
            if (e.args.size() != arity)
            {
                error_at(span, std::string(name) + " expects exactly " + std::to_string(arity) +
                                   " argument(s)");
                return false;
            }
            return true;
        };

        if (callee_name == "__read_line")
        {
            if (!check_builtin_arity(1, callee_name))
            {
                return std::nullopt;
            }
            require_capability("io.stdin", span);
            (void)check_expr(e.args[0]);
            return Type{.kind = TypeKind::String};
        }

        if (callee_name == "__fs_read_text")
        {
            if (!check_builtin_arity(2, callee_name))
            {
                return std::nullopt;
            }
            require_capability("fs.read", span);
            const auto path_t = check_expr(e.args[0]);
            (void)check_expr(e.args[1]);
            if (path_t.has_value() && path_t->kind != TypeKind::String)
            {
                error_at(span, "fs path must be String");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::String};
        }

        if (callee_name == "__fs_write_text")
        {
            if (!check_builtin_arity(3, callee_name))
            {
                return std::nullopt;
            }
            require_capability("fs.write", span);
            const auto path_t = check_expr(e.args[0]);
            const auto content_t = check_expr(e.args[1]);
            (void)check_expr(e.args[2]);
            if (path_t.has_value() && path_t->kind != TypeKind::String)
            {
                error_at(span, "fs path must be String");
                return std::nullopt;
            }
            if (content_t.has_value() && content_t->kind != TypeKind::String)
            {
                error_at(span, "fs content must be String");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Unit};
        }

        if (callee_name == "__tty_clear")
        {
            if (!check_builtin_arity(1, callee_name))
            {
                return std::nullopt;
            }
            require_capability("io.tty", span);
            (void)check_expr(e.args[0]);
            return Type{.kind = TypeKind::Unit};
        }

        if (callee_name == "__tty_write_at")
        {
            if (!check_builtin_arity(4, callee_name))
            {
                return std::nullopt;
            }
            require_capability("io.tty", span);
            const auto row_t = check_expr(e.args[0]);
            const auto col_t = check_expr(e.args[1]);
            const auto text_t = check_expr(e.args[2]);
            (void)check_expr(e.args[3]);
            if (row_t.has_value() && row_t->kind != TypeKind::Int)
            {
                error_at(span, "tty row must be Int");
                return std::nullopt;
            }
            if (col_t.has_value() && col_t->kind != TypeKind::Int)
            {
                error_at(span, "tty col must be Int");
                return std::nullopt;
            }
            if (text_t.has_value() && text_t->kind != TypeKind::String)
            {
                error_at(span, "tty text must be String");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Unit};
        }

        if (callee_name == "__tty_flush")
        {
            if (!check_builtin_arity(1, callee_name))
            {
                return std::nullopt;
            }
            require_capability("io.tty", span);
            (void)check_expr(e.args[0]);
            return Type{.kind = TypeKind::Unit};
        }

        if (callee_name == "__rng_next_int")
        {
            if (!check_builtin_arity(2, callee_name))
            {
                return std::nullopt;
            }
            require_capability("rng.seeded", span);
            const auto max_t = check_expr(e.args[0]);
            (void)check_expr(e.args[1]);
            if (max_t.has_value() && max_t->kind != TypeKind::Int)
            {
                error_at(span, "rng max must be Int");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Int};
        }

        if (callee_name == "__vec_new_int")
        {
            if (!check_builtin_arity(1, callee_name))
            {
                return std::nullopt;
            }
            const auto max_t = check_expr(e.args[0]);
            if (max_t.has_value() && max_t->kind != TypeKind::Int)
            {
                error_at(span, "vec max length must be Int");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Vec, .element_kind = TypeKind::Int};
        }

        if (callee_name == "__vec_len_int")
        {
            if (!check_builtin_arity(1, callee_name))
            {
                return std::nullopt;
            }
            (void)check_expr(e.args[0]);
            return Type{.kind = TypeKind::Int};
        }

        if (callee_name == "__vec_push_int")
        {
            if (!check_builtin_arity(2, callee_name))
            {
                return std::nullopt;
            }
            (void)check_expr(e.args[0]);
            const auto value_t = check_expr(e.args[1]);
            if (value_t.has_value() && value_t->kind != TypeKind::Int)
            {
                error_at(span, "vec value must be Int");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Unit};
        }

        if (callee_name == "__vec_get_int")
        {
            if (!check_builtin_arity(2, callee_name))
            {
                return std::nullopt;
            }
            (void)check_expr(e.args[0]);
            const auto index_t = check_expr(e.args[1]);
            if (index_t.has_value() && index_t->kind != TypeKind::Int)
            {
                error_at(span, "vec index must be Int");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Int};
        }

        if (callee_name == "__vec_set_int")
        {
            if (!check_builtin_arity(3, callee_name))
            {
                return std::nullopt;
            }
            (void)check_expr(e.args[0]);
            const auto index_t = check_expr(e.args[1]);
            const auto value_t = check_expr(e.args[2]);
            if (index_t.has_value() && index_t->kind != TypeKind::Int)
            {
                error_at(span, "vec index must be Int");
                return std::nullopt;
            }
            if (value_t.has_value() && value_t->kind != TypeKind::Int)
            {
                error_at(span, "vec value must be Int");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Unit};
        }

        if (callee_name == "__vec_new_bool")
        {
            if (!check_builtin_arity(1, callee_name))
            {
                return std::nullopt;
            }
            const auto max_t = check_expr(e.args[0]);
            if (max_t.has_value() && max_t->kind != TypeKind::Int)
            {
                error_at(span, "vec max length must be Int");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Vec, .element_kind = TypeKind::Bool};
        }

        if (callee_name == "__vec_len_bool")
        {
            if (!check_builtin_arity(1, callee_name))
            {
                return std::nullopt;
            }
            (void)check_expr(e.args[0]);
            return Type{.kind = TypeKind::Int};
        }

        if (callee_name == "__vec_push_bool")
        {
            if (!check_builtin_arity(2, callee_name))
            {
                return std::nullopt;
            }
            (void)check_expr(e.args[0]);
            const auto value_t = check_expr(e.args[1]);
            if (value_t.has_value() && value_t->kind != TypeKind::Bool)
            {
                error_at(span, "vec value must be Bool");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Unit};
        }

        if (callee_name == "__vec_get_bool")
        {
            if (!check_builtin_arity(2, callee_name))
            {
                return std::nullopt;
            }
            (void)check_expr(e.args[0]);
            const auto index_t = check_expr(e.args[1]);
            if (index_t.has_value() && index_t->kind != TypeKind::Int)
            {
                error_at(span, "vec index must be Int");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Bool};
        }

        if (callee_name == "__vec_set_bool")
        {
            if (!check_builtin_arity(3, callee_name))
            {
                return std::nullopt;
            }
            (void)check_expr(e.args[0]);
            const auto index_t = check_expr(e.args[1]);
            const auto value_t = check_expr(e.args[2]);
            if (index_t.has_value() && index_t->kind != TypeKind::Int)
            {
                error_at(span, "vec index must be Int");
                return std::nullopt;
            }
            if (value_t.has_value() && value_t->kind != TypeKind::Bool)
            {
                error_at(span, "vec value must be Bool");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Unit};
        }

        if (callee_name == "__set_new_int")
        {
            if (!check_builtin_arity(0, callee_name))
            {
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Set, .element_kind = TypeKind::Int};
        }

        if (callee_name == "__set_has_int")
        {
            if (!check_builtin_arity(2, callee_name))
            {
                return std::nullopt;
            }
            (void)check_expr(e.args[0]);
            const auto value_t = check_expr(e.args[1]);
            if (value_t.has_value() && value_t->kind != TypeKind::Int)
            {
                error_at(span, "set value must be Int");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Bool};
        }

        if (callee_name == "__set_insert_int")
        {
            if (!check_builtin_arity(2, callee_name))
            {
                return std::nullopt;
            }
            (void)check_expr(e.args[0]);
            const auto value_t = check_expr(e.args[1]);
            if (value_t.has_value() && value_t->kind != TypeKind::Int)
            {
                error_at(span, "set value must be Int");
                return std::nullopt;
            }
            return Type{.kind = TypeKind::Unit};
        }

        if (callee_name == "variant_is")
        {
            if (!check_builtin_arity(2, callee_name))
            {
                return std::nullopt;
            }

            const auto value_t = check_expr(e.args[0]);
            if (!value_t.has_value() || value_t->kind != TypeKind::Enum)
            {
                error_at(span, "variant_is expects enum value as first argument");
                return std::nullopt;
            }

            const auto* variant = std::get_if<ScopedNameExpr>(&e.args[1].node);
            if (variant == nullptr)
            {
                error_at(span, "variant_is expects enum variant tag as second argument");
                return std::nullopt;
            }

            if (variant->lhs != value_t->name)
            {
                error_at(span, "variant_is enum mismatch between value and variant tag");
                return std::nullopt;
            }

            const auto enum_it = enums_.find(variant->lhs);
            if (enum_it == enums_.end() || // GCOVR_EXCL_LINE
                !enum_it->second.variants.contains(variant->rhs))
            {
                error_at(span, "unknown variant '" + std::string(variant->rhs) +
                                   "' for enum '" + std::string(variant->lhs) + "'");
                return std::nullopt;
            }

            return Type{.kind = TypeKind::Bool};
        }

        if (callee_name == "variant_unwrap")
        {
            if (!check_builtin_arity(2, callee_name))
            {
                return std::nullopt;
            }

            const auto value_t = check_expr(e.args[0]);
            if (!value_t.has_value() || value_t->kind != TypeKind::Enum)
            {
                error_at(span, "variant_unwrap expects enum value as first argument");
                return std::nullopt;
            }

            const auto* variant = std::get_if<ScopedNameExpr>(&e.args[1].node);
            if (variant == nullptr)
            {
                error_at(span, "variant_unwrap expects enum variant tag as second argument");
                return std::nullopt;
            }

            if (variant->lhs != value_t->name)
            {
                error_at(span, "variant_unwrap enum mismatch between value and variant tag");
                return std::nullopt;
            }

            const auto enum_it = enums_.find(variant->lhs);
            // GCOVR_EXCL_START
            if (enum_it == enums_.end())
            {
                error_at(span, "unknown enum type '" + std::string(variant->lhs) + "'");
                return std::nullopt;
            }
            // GCOVR_EXCL_STOP

            const auto var_it = enum_it->second.variants.find(variant->rhs);
            if (var_it == enum_it->second.variants.end())
            {
                error_at(span, "unknown variant '" + std::string(variant->rhs) +
                                   "' for enum '" + std::string(variant->lhs) + "'");
                return std::nullopt;
            }

            if (!var_it->second.payload.has_value())
            {
                error_at(span, "variant_unwrap expects a payload-carrying variant");
                return std::nullopt;
            }

            return *var_it->second.payload;
        }

        const auto it = functions_.find(callee_name);
        if (it == functions_.end())
        {
            error_at(span, "unknown function '" + std::string(callee_name) + "'");
            return std::nullopt;
        }

        const auto& sig = it->second;
        // Builtin placeholder signatures may not match; only user functions are checked here.
        if (e.args.size() != sig.params.size())
        {
            error_at(span,
                     "wrong number of arguments for call to '" + std::string(callee_name) + "'");
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
                error_at(span,
                         "argument type mismatch for call to '" + std::string(callee_name) + "'");
            }
        }

        return sig.result;
    }

    [[nodiscard]] std::optional<Type> check_expr_node(const curlee::parser::PhysExpr& e, Span span)
    {
        // Rule (1): the address must be a compile-time integer literal (decimal or hex).
        const std::string_view lexeme = e.lexeme;
        bool is_literal = !lexeme.empty();
        if (is_literal)
        {
            for (std::size_t i = 0; i < lexeme.size(); ++i)
            {
                const char c = lexeme[i];
                const bool is_digit = (c >= '0' && c <= '9');
                const bool is_hex_digit = (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                const bool is_hex_marker =
                    (c == 'x' || c == 'X') && i == 1 && lexeme[0] == '0';
                if (!is_digit && c != '_' && !is_hex_digit && !is_hex_marker)
                {
                    is_literal = false;
                    break;
                }
            }
        }
        if (!is_literal)
        {
            error_at(span, "physical address must be a constant literal");
            return std::nullopt;
        }

        const auto elem_kind = phys_element_kind_from_name(e.element_kind);
        if (!elem_kind.has_value())
        {
            error_at(span, "unsupported Phys element kind '" + std::string(e.element_kind) +
                               "' (expected U8, U16, U32 or U64)");
            return std::nullopt;
        }

        return Type{.kind = TypeKind::Phys,
                    .name = "Phys",
                    .element_kind = *elem_kind,
                    .element_name = e.element_kind};
    }

    [[nodiscard]] std::optional<Type> check_expr_node(const curlee::parser::PhysReadExpr& e,
                                                      Span span)
    {
        // Rules (2)+(3): read() only inside `unsafe` and requires `cap phys.mem`.
        if (unsafe_depth_ == 0)
        {
            error_at(span, "physical memory access requires an unsafe block");
            return std::nullopt;
        }
        require_capability("phys.mem", span);

        if (e.base == nullptr) // GCOVR_EXCL_LINE
        {
            return std::nullopt; // GCOVR_EXCL_LINE
        }
        const auto base_t = check_expr(*e.base);
        if (!base_t.has_value())
        {
            return std::nullopt;
        }
        if (base_t->kind != TypeKind::Phys)
        {
            error_at(span, "read() can only be called on a Phys<T> value");
            return std::nullopt;
        }
        if (!base_t->element_kind.has_value()) // GCOVR_EXCL_LINE
        {
            return std::nullopt; // GCOVR_EXCL_LINE
        }
        // read() returns the element kind T.
        return Type{.kind = *base_t->element_kind, .name = base_t->element_name};
    }

    [[nodiscard]] std::optional<Type> check_expr_node(const curlee::parser::PhysWriteExpr& e,
                                                      Span span)
    {
        // Rules (2)+(3): write(v) only inside `unsafe` and requires `cap phys.mem`.
        if (unsafe_depth_ == 0)
        {
            error_at(span, "physical memory access requires an unsafe block");
            return std::nullopt;
        }
        require_capability("phys.mem", span);

        if (e.base == nullptr || e.value == nullptr) // GCOVR_EXCL_LINE
        {
            return std::nullopt; // GCOVR_EXCL_LINE
        }
        const auto base_t = check_expr(*e.base);
        if (!base_t.has_value())
        {
            return std::nullopt;
        }
        if (base_t->kind != TypeKind::Phys)
        {
            error_at(span, "write() can only be called on a Phys<T> value");
            return std::nullopt;
        }
        if (!base_t->element_kind.has_value()) // GCOVR_EXCL_LINE
        {
            return std::nullopt; // GCOVR_EXCL_LINE
        }
        const auto value_t = check_expr(*e.value);
        if (!value_t.has_value())
        {
            return std::nullopt;
        }
        // Accept Int literals when writing to unsigned element kinds (e.g. `fb.write(0xFF8800)`
        // on Phys<U32>): the physical address space is little-endian byte-oriented and the
        // compiler range-checks at the freestanding target. Exact unsigned types must still match.
        const Type elem_type{.kind = *base_t->element_kind, .name = base_t->element_name};
        const bool int_literal_for_unsigned =
            (*value_t).kind == TypeKind::Int && is_phys_element_kind(*base_t->element_kind);
        if (*value_t != elem_type && !int_literal_for_unsigned)
        {
            error_at(span, "write() value type mismatch: expected " +
                               std::string(base_t->element_name) + ", got " +
                               std::string(to_string(*value_t)));
        }
        return Type{.kind = TypeKind::Unit};
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
