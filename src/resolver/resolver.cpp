#include <cassert>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/resolver/module_loader.h>
#include <curlee/resolver/resolver.h>
#include <curlee/source/source_file.h>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

namespace curlee::resolver
{
namespace
{

using curlee::diag::Diagnostic;
using curlee::diag::Related;
using curlee::diag::Severity;
using namespace curlee::parser;
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

        // First pass: declare top-level functions.
        for (const auto& f : program.functions)
        {
            declare(f.name, f.span, "duplicate function");
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
    int unsafe_depth_ = 0;
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

    static bool is_python_ffi_call(const Expr& callee)
    {
        const auto* member = std::get_if<MemberExpr>(&callee.node);
        if (member == nullptr)
        {
            return false;
        }
        if (member->base == nullptr) // GCOVR_EXCL_LINE
        {
            return false; // GCOVR_EXCL_LINE
        }

        const auto* base_name = std::get_if<NameExpr>(&member->base->node);
        if (base_name == nullptr)
        {
            return false;
        }

        return base_name->name == "python_ffi" && member->member == "call";
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
                // Preserve the "import not found: 'foo'" message format if needed,
                // or just trust module_loader.
                // The module_loader already returns a diagnostic with "import not found: '...'"
                // but let's make sure the span is correct.
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
            const Related previous_note{.message = "previous definition is here",
                                        .span = it->second.span};
            d.notes.push_back(previous_note); // GCOVR_EXCL_LINE
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
        ++unsafe_depth_;
        push_scope();
        for (const auto& stmt : s.body->stmts)
        {
            resolve_stmt(stmt);
        }
        pop_scope();
        --unsafe_depth_;
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

        push_scope();
        for (const auto& stmt : s.body->stmts)
        {
            resolve_stmt(stmt);
        }
        pop_scope();
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
        if (is_python_ffi_call(*e.callee) && unsafe_depth_ == 0)
        {
            Diagnostic d;
            d.severity = Severity::Error;
            d.message = "python_ffi.call requires an unsafe context";
            d.span = e.callee->span;
            diagnostics_.push_back(std::move(d));
        }

        resolve_expr(*e.callee);
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
            // Builtin module name used for interop. Do not require declaration.
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

} // namespace curlee::resolver
