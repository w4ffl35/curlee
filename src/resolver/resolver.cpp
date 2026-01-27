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
using curlee::parser::Stmt;
using curlee::parser::UnsafeStmt;
using curlee::parser::WhileStmt;
using curlee::source::Span;

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
             std::optional<std::filesystem::path> entry_dir)
        : base_path_(std::move(base_path)), entry_dir_(std::move(entry_dir))
    {
    }

    [[nodiscard]] ResolveResult run(const curlee::parser::Program& program)
    {
        resolve_imports(program);

        // First pass: declare top-level functions.
        push_scope();
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
    int unsafe_depth_ = 0;

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

    void resolve_imports(const curlee::parser::Program& program)
    {
        for (const auto& imp : program.imports)
        {
            if (!base_path_.has_value())
            {
                Diagnostic d;
                d.severity = Severity::Error;
                d.message = "imports require a source file path";
                d.span = imp.span;
                diagnostics_.push_back(std::move(d));
                continue;
            }

            std::string import_name;
            for (std::size_t i = 0; i < imp.path.size(); ++i)
            {
                import_name += std::string(imp.path[i]);
                if (i + 1 < imp.path.size())
                {
                    import_name += ".";
                }
            }

            std::vector<std::filesystem::path> roots;
            roots.push_back(*base_path_);
            if (entry_dir_.has_value() && *entry_dir_ != *base_path_)
            {
                roots.push_back(*entry_dir_);
            }

            bool found = false;
            std::filesystem::path first_expected = *base_path_;
            for (const auto& part : imp.path)
            {
                first_expected /= std::string(part);
            }
            first_expected += ".curlee";

            for (const auto& root : roots)
            {
                std::filesystem::path module_path = root;
                for (const auto& part : imp.path)
                {
                    module_path /= std::string(part);
                }
                module_path += ".curlee";

                const auto loaded = source::load_source_file(module_path.string());
                if (std::holds_alternative<source::SourceFile>(loaded))
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                Diagnostic d;
                d.severity = Severity::Error;
                d.message = "import not found: '" + import_name + "'";
                d.span = imp.span;
                d.notes.push_back(
                    Related{.message = "expected module at " + first_expected.string(),
                            .span = std::nullopt});
                diagnostics_.push_back(std::move(d));
                continue;
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
        if (scopes_.empty())
        {
            push_scope();
        }

        auto& scope = scopes_.back();
        if (auto it = scope.defs.find(name); it != scope.defs.end())
        {
            Diagnostic d;
            d.severity = Severity::Error;
            d.message = std::string(kind) + ": '" + std::string(name) + "'";
            d.span = span;
            d.notes.push_back(
                Related{.message = "previous definition is here", .span = it->second.span});
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

    void resolve_expr_node(const MemberExpr& e, Span)
    {
        if (e.base == nullptr)
        {
            return;
        }

        if (const auto* base_name = std::get_if<NameExpr>(&e.base->node);
            base_name != nullptr && base_name->name == "python_ffi")
        {
            // Builtin module name used for interop. Do not require declaration.
            return;
        }

        resolve_expr(*e.base);
    }

    void resolve_expr_node(const GroupExpr& e, Span) { resolve_expr(*e.inner); }

    void resolve_pred(const curlee::parser::Pred& p)
    {
        std::visit([&](const auto& node) { resolve_pred_node(node, p.span); }, p.node);
    }

    void resolve_pred_node(const curlee::parser::PredName& p, Span span) { use_name(p.name, span); }

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
                      std::optional<std::filesystem::path> entry_dir)
{
    std::optional<std::filesystem::path> base;
    if (!source.path.empty())
    {
        base = std::filesystem::path(source.path).parent_path();
    }
    Resolver r(std::move(base), std::move(entry_dir));
    return r.run(program);
}

} // namespace curlee::resolver
