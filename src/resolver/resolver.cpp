#include <curlee/resolver/resolver.h>
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
using curlee::parser::LetStmt;
using curlee::parser::NameExpr;
using curlee::parser::ReturnStmt;
using curlee::parser::Stmt;
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
    [[nodiscard]] ResolveResult run(const curlee::parser::Program& program)
    {
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

    void resolve_expr(const Expr& e)
    {
        std::visit([&](const auto& node) { resolve_expr_node(node, e.span); }, e.node);
    }

    void resolve_expr_node(const NameExpr& e, Span span) { use_name(e.name, span); }

    void resolve_expr_node(const curlee::parser::IntExpr&, Span) {}
    void resolve_expr_node(const curlee::parser::StringExpr&, Span) {}

    void resolve_expr_node(const curlee::parser::UnaryExpr& e, Span) { resolve_expr(*e.rhs); }

    void resolve_expr_node(const BinaryExpr& e, Span)
    {
        resolve_expr(*e.lhs);
        resolve_expr(*e.rhs);
    }

    void resolve_expr_node(const CallExpr& e, Span)
    {
        resolve_expr(*e.callee);
        for (const auto& a : e.args)
        {
            resolve_expr(a);
        }
    }

    void resolve_expr_node(const GroupExpr& e, Span) { resolve_expr(*e.inner); }

    void resolve_pred(const curlee::parser::Pred& p)
    {
        std::visit([&](const auto& node) { resolve_pred_node(node, p.span); }, p.node);
    }

    void resolve_pred_node(const curlee::parser::PredName& p, Span span) { use_name(p.name, span); }

    void resolve_pred_node(const curlee::parser::PredInt&, Span) {}

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
    return Resolver().run(program);
}

} // namespace curlee::resolver
