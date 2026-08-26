// SPDX-License-Identifier: MIT
#include <cassert>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/resolver/module_loader.h>
#include <curlee/resolver/resolver.h>
#include <curlee/source/source_file.h>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace curlee::resolver
{
namespace
{

using curlee::diag::Diagnostic;
using curlee::diag::Related;
using curlee::diag::Severity;
using curlee::parser::ArrayLiteralExpr;
using curlee::parser::AssignStmt;
using curlee::parser::BinaryExpr;
using curlee::parser::BlockStmt;
using curlee::parser::CallExpr;
using curlee::parser::Expr;
using curlee::parser::ExprStmt;
using curlee::parser::Function;
using curlee::parser::GhostLetStmt;
using curlee::parser::GroupExpr;
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

static bool collect_member_chain(const Expr& expr, std::vector<std::string_view>& out)
{
    if (const auto* name = std::get_if<NameExpr>(&expr.node))
    {
        out.push_back(name->name);
        return true;
    }

    if (const auto* mem = std::get_if<MemberExpr>(&expr.node))
    {
        if (mem->base == nullptr) // GCOVR_EXCL_LINE
        {
            return false; // GCOVR_EXCL_LINE
        }
        if (!collect_member_chain(*mem->base, out)) // GCOVR_EXCL_LINE
        {
            return false; // GCOVR_EXCL_LINE
        }
        out.push_back(mem->member);
        return true;
    }

    return false;
}

struct Def
{
    SymbolId id;
    Span span;
};

struct Scope
{
    std::unordered_map<std::string_view, Def> defs;
};

class Resolver
{
  public:
    Resolver(std::optional<std::filesystem::path> base_path,
             std::optional<std::filesystem::path> entry_dir,
             std::vector<std::filesystem::path> stdlib_roots = {})
        : base_path_(std::move(base_path)), entry_dir_(std::move(entry_dir)),
          stdlib_roots_(std::move(stdlib_roots))
    {
    }

    [[nodiscard]] ResolveResult run(const curlee::parser::Program& program)
    {
        // Root scope: imports, functions, and all top-level declarations live here.
        push_scope();
        resolve_imports(program);

        // First pass: declare module-level mutable state, then top-level
        // functions (a static and a function sharing a name is a duplicate in
        // the root scope, reported by declare).
        for (const auto& s : program.statics)
        {
            declare(s.name, s.span, "duplicate static variable");
        }
        for (const auto& f : program.functions)
        {
            declare(f.name, f.span, "duplicate function");
        }

        // Resolve static initializers (literal-only by type-checker rule, but
        // resolve like any expression so uses are recorded).
        for (const auto& s : program.statics)
        {
            resolve_expr(s.value);
        }

        // Second pass: resolve bodies.
        for (const auto& f : program.functions)
        {
            resolve_function(f);
        }

        if (!diagnostics_.empty())
        {
            return diagnostics_;
        }
        return resolution_;
    }

  private:
    std::vector<Scope> scopes_;
    Resolution resolution_;
    std::vector<Diagnostic> diagnostics_;
    std::optional<std::filesystem::path> base_path_;
    std::optional<std::filesystem::path> entry_dir_;
    std::vector<std::filesystem::path> stdlib_roots_;
    bool resolving_ensures_ = false;

    struct ModuleInfo
    {
        // Exported symbol names defined in the imported module.
        std::unordered_map<std::string_view, Def> exports;
    };

    // Own imported module contents so parsed AST string_views remain valid.
    std::vector<curlee::source::SourceFile> imported_files_;
    // module path (e.g. "foo.bar") -> module info
    std::unordered_map<std::string, ModuleInfo> modules_by_path_;
    // alias name (e.g. "baz") -> module path key in modules_by_path_
    std::unordered_map<std::string_view, std::string> module_aliases_;

    static constexpr std::string_view kPythonFfiUnsupportedMessage =
        "python_ffi is not part of the Curlee v1 surface (see Python-Interop-(Future))";

    // Delegate to the public curlee::resolver::is_builtin_call_name so there is
    // a single source of truth for the builtin call surface (the freestanding
    // codegen backend reuses the same predicate).
    static bool is_builtin_call_name(std::string_view name)
    {
        return curlee::resolver::is_builtin_call_name(name);
    }

    void resolve_imports(const curlee::parser::Program& program)
    {
        for (const auto& imp : program.imports)
        {
            const std::string import_name = join_path(imp.path);

            if (!base_path_.has_value())
            {
                Diagnostic d;
                d.severity = Severity::Error;
                d.message = "imports require a source file path";
                d.span = imp.span;
                diagnostics_.push_back(std::move(d));
                continue;
            }

            ModuleSearchOptions opts;
            opts.importing_file_dir = *base_path_;
            if (entry_dir_.has_value())
            {
                opts.entry_dir = *entry_dir_;
            }
            opts.stdlib_roots = stdlib_roots_;
            opts.debug_trace = std::getenv("CURLEE_DEBUG_IMPORTS") != nullptr;

            auto found = resolve_module_path(imp.path, opts);
            if (!found)
            {
                Diagnostic d = std::move(found.error());
                d.span = imp.span;
                diagnostics_.push_back(std::move(d));
                continue;
            }

            const auto& mod_path = found->path;
            const auto loaded = source::load_source_file(mod_path.string());
            if (std::holds_alternative<source::LoadError>(loaded))
            {
                Diagnostic d;
                d.severity = Severity::Error;
                d.message = "failed to load imported module: " +
                            std::get<source::LoadError>(loaded).message;
                d.span = imp.span;
                diagnostics_.push_back(d);
                continue;
            }

            source::SourceFile loaded_file = std::get<source::SourceFile>(loaded);

            // Record the module and parse its exports.
            imported_files_.push_back(std::move(loaded_file));
            const auto& src = imported_files_.back().contents;

            const auto lexed = curlee::lexer::lex(src);
            if (!std::holds_alternative<std::vector<curlee::lexer::Token>>(lexed))
            {
                Diagnostic d;
                d.severity = Severity::Error;
                d.message = "failed to lex imported module: '" + import_name + "'";
                d.span = imp.span;
                diagnostics_.push_back(std::move(d));
                continue;
            }

            const auto& toks = std::get<std::vector<curlee::lexer::Token>>(lexed);
            auto parsed = curlee::parser::parse(toks);
            if (!std::holds_alternative<curlee::parser::Program>(parsed))
            {
                Diagnostic d;
                d.severity = Severity::Error;
                d.message = "failed to parse imported module: '" + import_name + "'";
                d.span = imp.span;
                diagnostics_.push_back(std::move(d));
                continue;
            }

            auto mod_prog = std::get<curlee::parser::Program>(std::move(parsed));

            ModuleInfo info;
            auto export_symbol = [&](std::string_view name, Span span)
            {
                const SymbolId id{.value = static_cast<std::uint32_t>(resolution_.symbols.size())};
                resolution_.symbols.push_back(Symbol{.id = id, .name = name, .span = span});
                info.exports.emplace(name, Def{.id = id, .span = span});
            };

            for (const auto& f : mod_prog.functions)
            {
                export_symbol(f.name, f.span);
            }
            for (const auto& s : mod_prog.structs)
            {
                export_symbol(s.name, s.span);
            }
            for (const auto& e : mod_prog.enums)
            {
                export_symbol(e.name, e.span);
            }

            modules_by_path_.emplace(import_name, std::move(info));

            // Optional alias introduces a top-level module name.
            if (imp.alias.has_value())
            {
                declare(*imp.alias, imp.span, "duplicate import alias");
                module_aliases_.emplace(*imp.alias, import_name);
            }
        }
    }

    void push_scope() { scopes_.push_back(Scope{}); }
    void pop_scope() { scopes_.pop_back(); }

    [[nodiscard]] std::optional<Def> lookup(std::string_view name) const
    {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
        {
            auto found = it->defs.find(name);
            if (found != it->defs.end())
            {
                return found->second;
            }
        }
        return std::nullopt;
    }

    void declare(std::string_view name, Span span, std::string_view kind)
    {
        assert(!scopes_.empty()); // GCOVR_EXCL_LINE

        auto& scope = scopes_.back();
        if (auto it = scope.defs.find(name); it != scope.defs.end())
        {
            Diagnostic d;
            d.severity = Severity::Error;
            d.message = std::string(kind) + ": '" + std::string(name) + "'";
            d.span = span;
            d.notes.push_back(Related{.message = "previous definition is here", // GCOVR_EXCL_LINE
                                      .span = it->second.span});                // GCOVR_EXCL_LINE
            diagnostics_.push_back(std::move(d));
            return;
        }

        const SymbolId id{.value = static_cast<std::uint32_t>(resolution_.symbols.size())};
        scope.defs.emplace(name, Def{.id = id, .span = span});
        resolution_.symbols.push_back(Symbol{.id = id, .name = name, .span = span});
    }

    void use_name(std::string_view name, Span span)
    {
        const auto def = lookup(name);
        if (!def.has_value())
        {
            Diagnostic d;
            d.severity = Severity::Error;
            d.message = "unknown name '" + std::string(name) + "'";
            d.span = span;
            diagnostics_.push_back(std::move(d));
            return;
        }

        resolution_.uses.push_back(NameUse{.target = def->id, .span = span});
    }

    void resolve_function(const Function& f)
    {
        push_scope();

        // Params are in the function scope.
        for (const auto& p : f.params)
        {
            declare(p.name, p.span, "duplicate parameter");
        }

        for (const auto& p : f.params)
        {
            if (p.refinement.has_value())
            {
                resolve_pred(*p.refinement);
            }
        }

        // Contract-block ghost snapshots are declared before requires/ensures so
        // both can reference the snapshot names (the pre-state snapshot pattern
        // from the roadmap: `[ ghost let src_before = src; ensures ...; ]`).
        for (const auto& g : f.ghost_lets)
        {
            resolve_ghost_let(g, f.span);
        }

        for (const auto& req : f.requires_clauses)
        {
            resolve_pred(req);
        }

        const bool prev_resolving_ensures = resolving_ensures_;
        resolving_ensures_ = true;
        for (const auto& ens : f.ensures)
        {
            resolve_pred(ens);
        }
        resolving_ensures_ = prev_resolving_ensures;

        // The static fuel (WCET) bound is an Int-valued expression over the
        // function's inputs; resolve names like any other contract clause.
        if (f.fuel_bound.has_value())
        {
            resolve_pred(*f.fuel_bound);
        }

        for (const auto& s : f.body.stmts)
        {
            resolve_stmt(s);
        }

        pop_scope();
    }

    void resolve_stmt(const Stmt& s)
    {
        std::visit([&](const auto& node) { resolve_stmt_node(node, s.span); }, s.node);
    }

    void resolve_stmt_node(const LetStmt& s, Span stmt_span)
    {
        // Declare before resolving value so `let x = x;` resolves to the new binding.
        declare(s.name, stmt_span, "duplicate definition");
        resolve_expr(s.value);

        if (s.refinement.has_value())
        {
            resolve_pred(*s.refinement);
        }
    }

    void resolve_ghost_let(const GhostLetStmt& s, Span stmt_span)
    {
        // A ghost snapshot is a normal name binding from the resolver's
        // perspective: it declares the snapshot name in the current scope and
        // resolves its initializer expression like any other binding. The
        // verifier later treats the name as a fresh constant so later mutation
        // of the source binding cannot affect it.
        declare(s.name, stmt_span, "duplicate definition");
        resolve_expr(s.value);
    }

    void resolve_stmt_node(const GhostLetStmt& s, Span stmt_span)
    {
        resolve_ghost_let(s, stmt_span);
    }

    void resolve_stmt_node(const AssignStmt& s, Span span)
    {
        // Assignment does not declare: the target must resolve to an existing
        // binding (the resolver reports `unknown name` otherwise). The
        // read-only restriction on parameters/ghost snapshots is enforced by
        // the type checker, which has the type-level scope information.
        use_name(s.name, span);
        resolve_expr(s.value);
    }

    void resolve_stmt_node(const IndexAssignStmt& s, Span span)
    {
        // `arr[i] = v;` mutates an element of an existing array binding: the
        // array name resolves like any other use; the index and value are
        // ordinary expressions.
        use_name(s.name, span);
        resolve_expr(s.index);
        resolve_expr(s.value);
    }

    void resolve_stmt_node(const ReturnStmt& s, Span)
    {
        if (s.value.has_value())
        {
            resolve_expr(*s.value);
        }
    }
    void resolve_stmt_node(const ExprStmt& s, Span) { resolve_expr(s.expr); }

    void resolve_stmt_node(const BlockStmt& s, Span)
    {
        push_scope();
        for (const auto& stmt : s.block->stmts)
        {
            resolve_stmt(stmt);
        }
        pop_scope();
    }

    void resolve_stmt_node(const UnsafeStmt& s, Span)
    {
        push_scope();
        for (const auto& stmt : s.body->stmts)
        {
            resolve_stmt(stmt);
        }
        pop_scope();
    }

    void resolve_stmt_node(const IfStmt& s, Span)
    {
        resolve_expr(s.cond);

        push_scope();
        for (const auto& stmt : s.then_block->stmts)
        {
            resolve_stmt(stmt);
        }
        pop_scope();

        if (s.else_block != nullptr)
        {
            push_scope();
            for (const auto& stmt : s.else_block->stmts)
            {
                resolve_stmt(stmt);
            }
            pop_scope();
        }
    }

    void resolve_stmt_node(const WhileStmt& s, Span)
    {
        resolve_expr(s.cond);

        // Loop contract clauses are resolved against the scope visible at the
        // loop header (loop-carried variables, parameters, ghost snapshots),
        // before the body's own bindings shadow anything.
        for (const auto& inv : s.invariants)
        {
            resolve_pred(inv);
        }
        if (s.decreases.has_value())
        {
            resolve_pred(*s.decreases);
        }

        push_scope();
        for (const auto& stmt : s.body->stmts)
        {
            resolve_stmt(stmt);
        }
        pop_scope();
    }

    void resolve_stmt_node(const MatchStmt& s, Span)
    {
        resolve_expr(s.value);

        for (const auto& arm : s.arms)
        {
            push_scope();
            if (arm.pattern.payload_name.has_value())
            {
                declare(*arm.pattern.payload_name, arm.pattern.span,
                        "duplicate match payload binding");
            }
            if (arm.body != nullptr)
            {
                for (const auto& stmt : arm.body->stmts)
                {
                    resolve_stmt(stmt);
                }
            }
            pop_scope();
        }
    }

    void resolve_expr(const Expr& e)
    {
        std::visit([&](const auto& node) { resolve_expr_node(node, e.span); }, e.node);
    }

    void resolve_expr_node(const NameExpr& e, Span span) { use_name(e.name, span); }

    void resolve_expr_node(const curlee::parser::IntExpr&, Span) {}
    void resolve_expr_node(const curlee::parser::BoolExpr&, Span) {}
    void resolve_expr_node(const curlee::parser::StringExpr&, Span) {}

    void resolve_expr_node(const curlee::parser::UnaryExpr& e, Span) { resolve_expr(*e.rhs); }

    void resolve_expr_node(const BinaryExpr& e, Span)
    {
        resolve_expr(*e.lhs);
        resolve_expr(*e.rhs);
    }

    void resolve_expr_node(const CallExpr& e, Span)
    {
        bool resolve_callee = true;
        if (const auto* callee_name = std::get_if<NameExpr>(&e.callee->node);
            callee_name != nullptr)
        {
            if (is_builtin_call_name(callee_name->name))
            {
                resolve_callee = false;
            }
        }

        if (resolve_callee)
        {
            resolve_expr(*e.callee);
        }

        for (const auto& a : e.args)
        {
            resolve_expr(a);
        }
    }

    void resolve_expr_node(const MemberExpr& e, Span span)
    {
        if (e.base == nullptr) // GCOVR_EXCL_LINE
        {
            return;
        }

        if (const auto* base_name = std::get_if<NameExpr>(&e.base->node);
            base_name != nullptr && base_name->name == "python_ffi")
        {
            Diagnostic d;
            d.severity = Severity::Error;
            d.message = std::string(kPythonFfiUnsupportedMessage);
            d.span = span;
            diagnostics_.push_back(std::move(d));
            return;
        }

        // Module-qualified reference: either `alias.member` or `foo.bar.member`.
        std::vector<std::string_view> parts;
        if (collect_member_chain(*e.base, parts))
        {
            parts.push_back(e.member);
            assert(parts.size() >= 2); // GCOVR_EXCL_LINE

            const std::string_view member = parts.back();
            const std::vector<std::string_view> qualifier(parts.begin(), parts.end() - 1);

            std::optional<std::string> module_key;
            if (qualifier.size() == 1)
            {
                if (const auto it = module_aliases_.find(qualifier[0]); it != module_aliases_.end())
                {
                    module_key = it->second;
                }
            }
            if (!module_key.has_value())
            {
                const std::string key = join_path(qualifier);
                if (modules_by_path_.find(key) != modules_by_path_.end())
                {
                    module_key = key;
                }
            }

            if (module_key.has_value())
            {
                const auto it_mod = modules_by_path_.find(*module_key);
                assert(it_mod != modules_by_path_.end()); // GCOVR_EXCL_LINE

                const auto it_export = it_mod->second.exports.find(member);
                if (it_export == it_mod->second.exports.end())
                {
                    Diagnostic d;
                    d.severity = Severity::Error;
                    d.message =
                        "unknown qualified name '" + *module_key + "." + std::string(member) + "'";
                    d.span = e.base->span;
                    diagnostics_.push_back(std::move(d));
                    return;
                }

                resolution_.uses.push_back(NameUse{.target = it_export->second.id, .span = span});
                return;
            }
        }

        resolve_expr(*e.base);
    }

    void resolve_expr_node(const GroupExpr& e, Span) { resolve_expr(*e.inner); }

    void resolve_expr_node(const IndexExpr& e, Span)
    {
        // `arr[i]`: resolve the base (the array binding) and the index.
        if (e.base != nullptr)
        {
            resolve_expr(*e.base);
        }
        if (e.index != nullptr)
        {
            resolve_expr(*e.index);
        }
    }

    void resolve_expr_node(const ArrayLiteralExpr& e, Span)
    {
        // `[v; N]`: the repeat value is an ordinary expression; the count is a
        // compile-time literal with no references.
        if (e.value != nullptr)
        {
            resolve_expr(*e.value);
        }
    }

    void resolve_expr_node(const curlee::parser::PhysExpr&, Span)
    {
        // `phys<U>(literal)` contains no variable references.
    }

    void resolve_expr_node(const curlee::parser::PhysReadExpr& e, Span)
    {
        if (e.base != nullptr)
        {
            resolve_expr(*e.base);
        }
    }

    void resolve_expr_node(const curlee::parser::PhysWriteExpr& e, Span)
    {
        if (e.base != nullptr)
        {
            resolve_expr(*e.base);
        }
        if (e.value != nullptr)
        {
            resolve_expr(*e.value);
        }
    }

    void resolve_expr_node(const curlee::parser::PortIOExpr& e, Span)
    {
        // The port is a general Int expression (issue #276) and may reference
        // variables (a let-bound base); the out* value is also a general
        // expression.
        if (e.port != nullptr)
        {
            resolve_expr(*e.port);
        }
        if (e.value != nullptr)
        {
            resolve_expr(*e.value);
        }
    }

    void resolve_expr_node(const curlee::parser::RuntimePhysReadExpr& e, Span)
    {
        // The address is a general Int/U64 expression (issue #279) and may
        // reference variables (the runtime multiboot2 info base, a mutable
        // byte cursor advanced by assignment).
        if (e.addr != nullptr)
        {
            resolve_expr(*e.addr);
        }
    }

    void resolve_expr_node(const ScopedNameExpr&, Span)
    {
        // `Enum::Variant` is not a variable reference; do not call use_name().
    }

    void resolve_expr_node(const StructLiteralExpr& e, Span)
    {
        // `Struct{ field: expr, ... }`: type/field names are not variable references.
        for (const auto& f : e.fields)
        {
            resolve_expr(*f.value);
        }
    }

    void resolve_pred(const curlee::parser::Pred& p)
    {
        std::visit([&](const auto& node) { resolve_pred_node(node, p.span); }, p.node);
    }

    void resolve_pred_node(const curlee::parser::PredName& p, Span span)
    {
        if (resolving_ensures_ && p.name == "result")
        {
            return;
        }
        use_name(p.name, span);
    }

    void resolve_pred_node(const curlee::parser::PredCall& p, Span span)
    {
        // The callee is a function name (ghost fn); resolve it like any other
        // name. Argument predicates resolve as usual.
        use_name(p.callee, span);
        for (const auto& arg : p.args)
        {
            resolve_pred(arg);
        }
    }

    void resolve_pred_node(const curlee::parser::PredInt&, Span) {}
    void resolve_pred_node(const curlee::parser::PredBool&, Span) {}

    void resolve_pred_node(const curlee::parser::PredGroup& p, Span) { resolve_pred(*p.inner); }

    void resolve_pred_node(const curlee::parser::PredUnary& p, Span) { resolve_pred(*p.rhs); }

    void resolve_pred_node(const curlee::parser::PredBinary& p, Span)
    {
        resolve_pred(*p.lhs);
        resolve_pred(*p.rhs);
    }
};

} // namespace

ResolveResult resolve(const curlee::parser::Program& program)
{
    Resolver r(std::nullopt, std::nullopt);
    return r.run(program);
}

ResolveResult resolve(const curlee::parser::Program& program,
                      const curlee::source::SourceFile& source)
{
    std::optional<std::filesystem::path> base;
    if (!source.path.empty())
    {
        base = std::filesystem::path(source.path).parent_path();
    }
    Resolver r(std::move(base), std::nullopt);
    return r.run(program);
}

ResolveResult resolve(const curlee::parser::Program& program,
                      const curlee::source::SourceFile& source,
                      std::optional<std::filesystem::path> entry_dir,
                      std::vector<std::filesystem::path> stdlib_roots)
{
    std::optional<std::filesystem::path> base;
    if (!source.path.empty())
    {
        base = std::filesystem::path(source.path).parent_path();
    }
    Resolver r(std::move(base), std::move(entry_dir), std::move(stdlib_roots));
    return r.run(program);
}

bool is_builtin_call_name(std::string_view name)
{
    static const std::unordered_set<std::string_view> builtins = {
        "print",           "__read_line",     "__tty_clear",
        "__fs_read_text",  "__fs_write_text", "__tty_write_at",
        "__tty_flush",     "__rng_next_int",  "__vec_new_int",
        "__vec_len_int",   "__vec_push_int",  "__vec_get_int",
        "__vec_set_int",   "__vec_new_bool",  "__vec_len_bool",
        "__vec_push_bool", "__vec_get_bool",  "__vec_set_bool",
        "__set_new_int",   "__set_has_int",   "__set_insert_int",
        "variant_is",      "variant_unwrap",
        "port_inb",        "port_outb",       "port_inw",
        "port_outw",       "port_inl",        "port_outl",
        "phys_read_u8",    "phys_read_u16",   "phys_read_u32",
        "phys_read_u64",
    };
    return builtins.contains(name);
}

} // namespace curlee::resolver
