// SPDX-License-Identifier: MIT
#include <cstddef>
#include <curlee/lexer/token.h>
#include <curlee/types/type.h>
#include <curlee/verification/checker.h>
#include <curlee/verification/predicate_lowering.h>
#include <curlee/verification/solver.h>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace curlee::verification
{
namespace
{

using curlee::diag::Diagnostic;
using curlee::diag::Related;
using curlee::diag::Severity;
using curlee::parser::Block;
using curlee::parser::BlockStmt;
using curlee::parser::CallExpr;
using curlee::parser::Expr;
using curlee::parser::ExprStmt;
using curlee::parser::Function;
using curlee::parser::GhostLetStmt;
using curlee::parser::IfStmt;
using curlee::parser::LetStmt;
using curlee::parser::MatchStmt;
using curlee::parser::MemberExpr;
using curlee::parser::NameExpr;
using curlee::parser::ReturnStmt;
using curlee::parser::Stmt;
using curlee::parser::UnsafeStmt;
using curlee::parser::WhileStmt;
using curlee::source::Span;
using curlee::types::TypeKind;

std::string token_to_string(curlee::lexer::TokenKind kind)
{
    using curlee::lexer::TokenKind;
    switch (kind)
    {
    case TokenKind::Plus:
        return "+";
    case TokenKind::Minus:
        return "-";
    case TokenKind::Star:
        return "*";
    case TokenKind::Slash:
        return "/";
    case TokenKind::EqualEqual:
        return "==";
    case TokenKind::BangEqual:
        return "!=";
    case TokenKind::Less:
        return "<";
    case TokenKind::LessEqual:
        return "<=";
    case TokenKind::Greater:
        return ">";
    case TokenKind::GreaterEqual:
        return ">=";
    case TokenKind::AndAnd:
        return "&&";
    case TokenKind::OrOr:
        return "||";
    case TokenKind::Bang:
        return "!";
    default:
        break;
    }
    return "<op>";
}

std::string pred_to_string(const curlee::parser::Pred& pred)
{
    return std::visit(
        [&](const auto& node) -> std::string
        {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, curlee::parser::PredInt>)
            {
                return std::string(node.lexeme);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredBool>)
            {
                return node.value ? "true" : "false";
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredName>)
            {
                return std::string(node.name);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredCall>)
            {
                std::string out = std::string(node.callee) + "(";
                for (std::size_t i = 0; i < node.args.size(); ++i)
                {
                    if (i > 0)
                    {
                        out += ", ";
                    }
                    out += pred_to_string(node.args[i]);
                }
                out += ")";
                return out;
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredUnary>)
            {
                return token_to_string(node.op) + pred_to_string(*node.rhs);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredBinary>)
            {
                return "(" + pred_to_string(*node.lhs) + " " + token_to_string(node.op) + " " +
                       pred_to_string(*node.rhs) + ")";
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredGroup>)
            {
                return "(" + pred_to_string(*node.inner) + ")";
            }

            return "<pred>";
        },
        pred.node);
}

void collect_pred_names(const curlee::parser::Pred& pred,
                        std::unordered_set<std::string_view>& names)
{
    std::visit(
        [&](const auto& node)
        {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, curlee::parser::PredName>)
            {
                names.insert(node.name);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredCall>)
            {
                for (const auto& arg : node.args)
                {
                    collect_pred_names(arg, names);
                }
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredUnary>)
            {
                collect_pred_names(*node.rhs, names);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredBinary>)
            {
                collect_pred_names(*node.lhs, names);
                collect_pred_names(*node.rhs, names);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PredGroup>)
            {
                collect_pred_names(*node.inner, names);
            }
        },
        pred.node);
}

std::vector<z3::expr> model_vars_for_pred(const curlee::parser::Pred& pred,
                                          const LoweringContext& ctx)
{
    std::unordered_set<std::string_view> names;
    collect_pred_names(pred, names);

    std::vector<z3::expr> vars;
    for (const auto& name : names)
    {
        if (name == "result")
        {
            if (ctx.result_int.has_value())
            {
                vars.push_back(*ctx.result_int);
            }
            else if (ctx.result_bool.has_value())
            {
                vars.push_back(*ctx.result_bool);
            }
            continue;
        }

        if (auto it = ctx.int_vars.find(name); it != ctx.int_vars.end())
        {
            vars.push_back(it->second);
        }
        else if (auto it2 = ctx.bool_vars.find(name); it2 != ctx.bool_vars.end())
        {
            vars.push_back(it2->second);
        }
    }

    return vars;
}

struct ExprValue
{
    z3::expr expr;
    TypeKind kind;
    bool is_literal = false;
};

using ExprLowerResult = std::variant<ExprValue, Diagnostic>;

Diagnostic error_at(Span span, std::string message)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.message = std::move(message);
    d.span = span;
    return d;
}

Diagnostic note_at(Span span, std::string message)
{
    Diagnostic d;
    d.severity = Severity::Note;
    d.message = std::move(message);
    d.span = span;
    return d;
}

/**
 * @brief True if the phys address lexeme is a plain constant literal.
 *
 * Accepts decimal integers and hex literals (0x...) with underscore separators.
 * This is the verifier's second-line enforcement: the front-end already rejects
 * non-literals, but the verifier must not trust the AST (defense in depth).
 */
bool is_phys_address_literal(std::string_view lexeme)
{
    if (lexeme.empty())
    {
        return false;
    }
    for (std::size_t i = 0; i < lexeme.size(); ++i)
    {
        const char c = lexeme[i];
        const bool is_digit = (c >= '0' && c <= '9');
        const bool is_hex_digit = (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        const bool is_hex_marker = (c == 'x' || c == 'X') && i == 1 && lexeme[0] == '0';
        if (!is_digit && c != '_' && !is_hex_digit && !is_hex_marker)
        {
            return false;
        }
    }
    return true;
}

struct FunctionSig
{
    const Function* decl = nullptr;
    std::vector<TypeKind> params;
    TypeKind result = TypeKind::Unit;

    // Pointers to the callee's contract-block ghost snapshots (name + source
    // expression), used at call sites to re-lower each snapshot's pre-state
    // value in the caller's context so inherited ensures can reference it.
    // Non-owning: the GhostLetStmt nodes live in the Program, which outlives
    // the verifier.
    std::vector<const curlee::parser::GhostLetStmt*> ghost_lets;
};

struct ScopeState
{
    std::unordered_map<std::string_view, z3::expr> int_vars;
    std::unordered_map<std::string_view, z3::expr> bool_vars;
    // Phys<T> pointer variables, mapped to their element kind name (e.g. "U32").
    std::unordered_map<std::string_view, std::string_view> phys_vars;
    // Names bound from a Phys read() result (opaque values that cannot be contracted).
    std::unordered_set<std::string_view> opaque_read_vars;
    std::size_t facts_size = 0;
};

class Verifier
{
  public:
    explicit Verifier(const curlee::types::TypeInfo& type_info)
        : type_info_(type_info), solver_(), lower_ctx_(solver_.context())
    {
        // Wire the ghost function table into the shared lowering context so
        // predicate calls in requires/ensures lower to uninterpreted functions.
        lower_ctx_.ghost_fns = &ghost_fns_;
    }

    VerificationResult run(const curlee::parser::Program& program)
    {
        collect_signatures(program);

        for (const auto& f : program.functions)
        {
            check_function(f);
        }

        // Note-severity diagnostics are non-fatal: they document trust transfers
        // (e.g. extern boundaries) without failing verification. Errors and
        // warnings fail as before.
        std::vector<Diagnostic> errors;
        notes_.clear();
        for (const auto& d : diags_)
        {
            if (d.severity == Severity::Note)
            {
                notes_.push_back(d);
            }
            else
            {
                errors.push_back(d);
            }
        }
        if (!errors.empty())
        {
            return errors;
        }
        return Verified{};
    }

    [[nodiscard]] const std::vector<Diagnostic>& notes() const { return notes_; }

  private:
    const curlee::types::TypeInfo& type_info_;
    Solver solver_;
    LoweringContext lower_ctx_;
    std::vector<Diagnostic> diags_;
    std::vector<Diagnostic> notes_;
    std::vector<z3::expr> facts_;
    std::vector<ScopeState> scopes_;
    std::unordered_map<std::string_view, FunctionSig> functions_;

    // Ghost function signatures used to lower predicate calls to uninterpreted
    // function applications. Populated during collect_signatures.
    GhostFnTable ghost_fns_;

    // Phys<T> pointer variables (name -> element kind name), scoped like int/bool vars.
    std::unordered_map<std::string_view, std::string_view> phys_vars_;
    // Names bound from a Phys read() result; contracts referencing these cannot be proven.
    std::unordered_set<std::string_view> opaque_read_vars_;

    void push_scope()
    {
        scopes_.emplace_back();
        auto& state = scopes_.back();
        state.int_vars = lower_ctx_.int_vars;
        state.bool_vars = lower_ctx_.bool_vars;
        state.phys_vars = phys_vars_;
        state.opaque_read_vars = opaque_read_vars_;
        state.facts_size = facts_.size();
    }

    void pop_scope()
    {
        if (scopes_.empty())
        {
            return;
        }
        const auto state = scopes_.back();
        scopes_.pop_back();
        lower_ctx_.int_vars = state.int_vars;
        lower_ctx_.bool_vars = state.bool_vars;
        phys_vars_ = state.phys_vars;
        opaque_read_vars_ = state.opaque_read_vars;
        if (facts_.size() > state.facts_size)
        {
            facts_.erase(facts_.begin() + static_cast<std::ptrdiff_t>(state.facts_size),
                         facts_.end());
        }
    }

    std::optional<TypeKind> supported_type(const curlee::parser::TypeName& name)
    {
        // Capability types (e.g. `cap io.stdout`) are not part of the verification logic
        // fragment. They are treated as uninterpreted values and must not block verification
        // unless used inside predicates (which are Int/Bool-only in the MVP scope).
        if (name.is_capability)
        {
            return TypeKind::Unit;
        }

        // Physical memory pointers are freestanding-only. Verification models them as opaque
        // Z3 sorts (one per element kind) with uninterpreted read/write functions, so they
        // are a distinct TypeKind here (not Unit like capabilities).
        if (name.name == "Phys")
        {
            return TypeKind::Phys;
        }

        auto t = curlee::types::core_type_from_name(name.name);
        if (!t.has_value())
        {
            diags_.push_back(error_at(name.span, "unknown type '" + std::string(name.name) + "'"));
            return std::nullopt;
        }

        if (t->kind == TypeKind::Int || t->kind == TypeKind::Bool || t->kind == TypeKind::Unit ||
            is_phys_element_kind(t->kind))
        {
            return t->kind;
        }

        diags_.push_back(error_at(name.span, "verification does not support type '" +
                                                 std::string(curlee::types::to_string(*t)) + "'"));
        return std::nullopt;
    }

    void collect_signatures(const curlee::parser::Program& program)
    {
        for (const auto& f : program.functions)
        {
            if (!f.return_type.has_value())
            {
                continue;
            }

            auto result = supported_type(*f.return_type);
            if (!result.has_value())
            {
                continue;
            }

            FunctionSig sig;
            sig.decl = &f;
            sig.result = *result;

            bool ok = true;
            for (const auto& p : f.params)
            {
                auto param_t = supported_type(p.type);
                if (!param_t.has_value())
                {
                    ok = false;
                    break;
                }
                sig.params.push_back(*param_t);
            }

            if (ok)
            {
                for (const auto& g : f.ghost_lets)
                {
                    sig.ghost_lets.push_back(&g);
                }
                functions_.emplace(f.name, std::move(sig));
            }

            // Ghost functions are additionally exposed to predicate lowering as
            // uninterpreted functions so they can be called from requires/ensures
            // clauses (issue #260, M1 part A).
            if (f.is_ghost)
            {
                GhostFnSig ghost_sig;
                ghost_sig.result = *result;
                for (const auto& p : f.params)
                {
                    auto param_t = supported_type(p.type);
                    if (!param_t.has_value())
                    {
                        ok = false;
                        break;
                    }
                    ghost_sig.params.push_back(*param_t);
                }
                if (ok)
                {
                    ghost_fns_.emplace(f.name, std::move(ghost_sig));
                }
            }
        }
    }

    std::optional<ExprValue> lookup_var(std::string_view name)
    {
        if (auto it = lower_ctx_.int_vars.find(name); it != lower_ctx_.int_vars.end())
        {
            return ExprValue{it->second, TypeKind::Int, false};
        }
        if (auto it = lower_ctx_.bool_vars.find(name); it != lower_ctx_.bool_vars.end())
        {
            return ExprValue{it->second, TypeKind::Bool, false};
        }
        // Phys<T> pointers are opaque values; a bare name reference lowers to a constant of
        // the opaque sort (used as the base of read()/write()).
        if (auto it = phys_vars_.find(name); it != phys_vars_.end())
        {
            const std::string const_name = std::string("phys_var_") + std::string(name);
            const z3::sort& sort = phys_sort(it->second);
            return ExprValue{solver_.context().constant(const_name.c_str(), sort), TypeKind::Phys,
                             false};
        }
        return std::nullopt;
    }

    void declare_var(std::string_view name, TypeKind kind)
    {
        const std::string name_str(name);
        if (kind == TypeKind::Int)
        {
            auto expr = solver_.context().int_const(name_str.c_str());
            lower_ctx_.int_vars.insert_or_assign(name, expr);
            return;
        }
        if (kind == TypeKind::Bool)
        {
            auto expr = solver_.context().bool_const(name_str.c_str());
            lower_ctx_.bool_vars.insert_or_assign(name, expr);
            return;
        }
    }

    // One fresh opaque Z3 sort per Phys element kind (e.g. "PhysU32").
    std::unordered_map<std::string_view, z3::sort> phys_sorts_;
    // Uninterpreted read function per element kind: phys_read : Phys<T> -> T.
    std::unordered_map<std::string_view, z3::func_decl> phys_read_fns_;
    // Uninterpreted write function per element kind: phys_write : Phys<T> x T -> Unit.
    std::unordered_map<std::string_view, z3::func_decl> phys_write_fns_;

    const z3::sort& phys_sort(std::string_view element_kind)
    {
        auto it = phys_sorts_.find(element_kind);
        if (it != phys_sorts_.end())
        {
            return it->second;
        }
        const std::string name = "Phys" + std::string(element_kind);
        auto [inserted_it, _] = phys_sorts_.emplace(
            element_kind, solver_.context().uninterpreted_sort(name.c_str()));
        return inserted_it->second;
    }

    const z3::func_decl& phys_read_fn(std::string_view element_kind)
    {
        auto it = phys_read_fns_.find(element_kind);
        if (it != phys_read_fns_.end())
        {
            return it->second;
        }
        auto& ctx = solver_.context();
        const std::string name = "phys_read_" + std::string(element_kind);
        // phys_read : Phys<T> -> Int. Values are opaque; no axioms constrain them.
        const z3::sort& sort = phys_sort(element_kind);
        auto [inserted_it, _] =
            phys_read_fns_.emplace(element_kind, ctx.function(name.c_str(), sort, ctx.int_sort()));
        return inserted_it->second;
    }

    const z3::func_decl& phys_write_fn(std::string_view element_kind)
    {
        auto it = phys_write_fns_.find(element_kind);
        if (it != phys_write_fns_.end())
        {
            return it->second;
        }
        auto& ctx = solver_.context();
        const std::string name = "phys_write_" + std::string(element_kind);
        // phys_write : Phys<T> x Int -> Unit. No axioms relate it to read().
        const z3::sort& sort = phys_sort(element_kind);
        auto [inserted_it, _] = phys_write_fns_.emplace(
            element_kind, ctx.function(name.c_str(), sort, ctx.int_sort(), ctx.int_sort()));
        return inserted_it->second;
    }

    std::optional<std::string_view> lookup_phys_var(std::string_view name)
    {
        if (auto it = phys_vars_.find(name); it != phys_vars_.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    bool pred_mentions_opaque_read(const curlee::parser::Pred& pred) const
    {
        bool found = false;
        std::visit(
            [&](const auto& node)
            {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, curlee::parser::PredName>)
                {
                    if (opaque_read_vars_.count(node.name) != 0)
                    {
                        found = true;
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PredCall>)
                {
                    for (const auto& arg : node.args)
                    {
                        if (pred_mentions_opaque_read(arg))
                        {
                            found = true;
                        }
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PredUnary>)
                {
                    if (node.rhs != nullptr && pred_mentions_opaque_read(*node.rhs))
                    {
                        found = true;
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PredBinary>)
                {
                    if (node.lhs != nullptr && pred_mentions_opaque_read(*node.lhs))
                    {
                        found = true;
                    }
                    if (node.rhs != nullptr && pred_mentions_opaque_read(*node.rhs))
                    {
                        found = true;
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PredGroup>)
                {
                    if (node.inner != nullptr && pred_mentions_opaque_read(*node.inner))
                    {
                        found = true;
                    }
                }
            },
            pred.node);
        return found;
    }

    // True if a predicate mentions the special `result` symbol.
    bool pred_mentions_result(const curlee::parser::Pred& pred) const
    {
        bool found = false;
        std::visit(
            [&](const auto& node)
            {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, curlee::parser::PredName>)
                {
                    if (node.name == "result")
                    {
                        found = true;
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PredCall>)
                {
                    for (const auto& arg : node.args)
                    {
                        if (pred_mentions_result(arg))
                        {
                            found = true;
                        }
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PredUnary>)
                {
                    if (node.rhs != nullptr && pred_mentions_result(*node.rhs))
                    {
                        found = true;
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PredBinary>)
                {
                    if (node.lhs != nullptr && pred_mentions_result(*node.lhs))
                    {
                        found = true;
                    }
                    if (node.rhs != nullptr && pred_mentions_result(*node.rhs))
                    {
                        found = true;
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PredGroup>)
                {
                    if (node.inner != nullptr && pred_mentions_result(*node.inner))
                    {
                        found = true;
                    }
                }
            },
            pred.node);
        return found;
    }

    // Resolve the element kind name of a Phys base expression: either a NameExpr bound to a
    // Phys<T> variable, or a direct PhysExpr literal.
    std::string_view base_phys_element_kind(const Expr& base)
    {
        if (const auto* name = std::get_if<NameExpr>(&base.node))
        {
            if (auto found = lookup_phys_var(name->name); found.has_value())
            {
                return *found;
            }
            return {};
        }
        if (const auto* phys = std::get_if<curlee::parser::PhysExpr>(&base.node))
        {
            return phys->element_kind;
        }
        return {};
    }

    // True if an expression derives from an opaque MMIO read, transitively. A value is opaque
    // if it is a PhysReadExpr, or a NameExpr bound to an opaque read (directly or via aliases),
    // or a compound expression containing one.
    bool expr_is_opaque_read(const Expr& e) const
    {
        return std::visit(
            [&](const auto& node) -> bool
            {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, curlee::parser::PhysReadExpr>)
                {
                    return true;
                }
                else if constexpr (std::is_same_v<Node, NameExpr>)
                {
                    return opaque_read_vars_.count(node.name) != 0;
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::GroupExpr>)
                {
                    return node.inner != nullptr && expr_is_opaque_read(*node.inner);
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::UnaryExpr>)
                {
                    return node.rhs != nullptr && expr_is_opaque_read(*node.rhs);
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::BinaryExpr>)
                {
                    if (node.lhs != nullptr && expr_is_opaque_read(*node.lhs))
                    {
                        return true;
                    }
                    return node.rhs != nullptr && expr_is_opaque_read(*node.rhs);
                }
                return false;
            },
            e.node);
    }

    void add_opaque_read_note(Diagnostic& d)
    {
        Related note;
        note.message = "MMIO read is opaque; cannot prove this contract";
        note.span = std::nullopt;
        d.notes.push_back(std::move(note));
    }

    // Hard diagnostic for a contract that mentions an opaque MMIO read value. The build must
    // fail rather than silently accept an unprovable contract.
    void reject_opaque_read_contract(Span span, std::string_view message)
    {
        auto d = error_at(span, std::string(message));
        add_opaque_read_note(d);
        diags_.push_back(std::move(d));
    }

    void add_fact(const curlee::parser::Pred& pred)
    {
        // Opaque MMIO read values can never satisfy a contract: reject unconditionally
        // before attempting to lower, so no opaque-derived contract can silently pass.
        if (pred_mentions_opaque_read(pred))
        {
            reject_opaque_read_contract(pred.span, "cannot prove contract on opaque MMIO read value");
            return;
        }
        auto lowered = lower_predicate(pred, lower_ctx_);
        if (std::holds_alternative<Diagnostic>(lowered))
        {
            diags_.push_back(std::get<Diagnostic>(std::move(lowered)));
            return;
        }
        facts_.push_back(std::get<z3::expr>(lowered));
    }

    ExprLowerResult lower_expr(const Expr& e)
    {
        return std::visit(
            [&](const auto& node) -> ExprLowerResult
            {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, curlee::parser::IntExpr>)
                {
                    const std::string literal(node.lexeme);
                    return ExprValue{solver_.context().int_val(literal.c_str()), TypeKind::Int,
                                     true};
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::BoolExpr>)
                {
                    return ExprValue{solver_.context().bool_val(node.value), TypeKind::Bool, true};
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::StringExpr>)
                {
                    return error_at(e.span, "verification does not support String expressions");
                }
                else if constexpr (std::is_same_v<Node, NameExpr>)
                {
                    if (auto found = lookup_var(node.name); found.has_value())
                    {
                        return *found;
                    }
                    return error_at(e.span, "unknown name '" + std::string(node.name) + "'");
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::UnaryExpr>)
                {
                    auto rhs_res = lower_expr(*node.rhs);
                    if (std::holds_alternative<Diagnostic>(rhs_res))
                    {
                        return std::get<Diagnostic>(rhs_res);
                    }
                    auto rhs = std::get<ExprValue>(rhs_res);

                    using curlee::lexer::TokenKind;
                    if (node.op == TokenKind::Minus)
                    {
                        if (rhs.kind != TypeKind::Int)
                        {
                            return error_at(e.span, "unary '-' expects Int expression");
                        }
                        return ExprValue{-rhs.expr, TypeKind::Int, rhs.is_literal};
                    }
                    if (node.op == TokenKind::Bang)
                    {
                        if (rhs.kind != TypeKind::Bool)
                        {
                            return error_at(e.span, "unary '!' expects Bool expression");
                        }
                        return ExprValue{!rhs.expr, TypeKind::Bool, false};
                    }

                    return error_at(e.span, "unsupported unary operator in expression");
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::BinaryExpr>)
                {
                    auto lhs_res = lower_expr(*node.lhs);
                    if (std::holds_alternative<Diagnostic>(lhs_res))
                    {
                        return std::get<Diagnostic>(lhs_res);
                    }
                    auto rhs_res = lower_expr(*node.rhs);
                    if (std::holds_alternative<Diagnostic>(rhs_res))
                    {
                        return std::get<Diagnostic>(rhs_res);
                    }
                    auto lhs = std::get<ExprValue>(lhs_res);
                    auto rhs = std::get<ExprValue>(rhs_res);

                    using curlee::lexer::TokenKind;
                    switch (node.op)
                    {
                    case TokenKind::Plus:
                    case TokenKind::Minus:
                        if (lhs.kind != TypeKind::Int || rhs.kind != TypeKind::Int)
                        {
                            return error_at(e.span, "arithmetic expects Int expressions");
                        }
                        return ExprValue{node.op == TokenKind::Plus ? (lhs.expr + rhs.expr)
                                                                    : (lhs.expr - rhs.expr),
                                         TypeKind::Int, lhs.is_literal && rhs.is_literal};

                    case TokenKind::Star:
                        if (lhs.kind != TypeKind::Int || rhs.kind != TypeKind::Int)
                        {
                            return error_at(e.span, "'*' expects Int expressions");
                        }
                        if (!lhs.is_literal && !rhs.is_literal)
                        {
                            return error_at(e.span, "non-linear multiplication is not supported");
                        }
                        return ExprValue{lhs.expr * rhs.expr, TypeKind::Int,
                                         lhs.is_literal && rhs.is_literal};

                    case TokenKind::EqualEqual:
                    case TokenKind::BangEqual:
                        if (lhs.kind != rhs.kind)
                        {
                            return error_at(e.span, "equality expects matching expression types");
                        }
                        return ExprValue{node.op == TokenKind::EqualEqual ? (lhs.expr == rhs.expr)
                                                                          : (lhs.expr != rhs.expr),
                                         TypeKind::Bool, false};

                    case TokenKind::Less:
                    case TokenKind::LessEqual:
                    case TokenKind::Greater:
                    case TokenKind::GreaterEqual:
                    {
                        if (lhs.kind != TypeKind::Int || rhs.kind != TypeKind::Int)
                        {
                            return error_at(e.span, "comparison expects Int expressions");
                        }
                        z3::expr cmp = lhs.expr < rhs.expr;
                        if (node.op == TokenKind::LessEqual)
                        {
                            cmp = lhs.expr <= rhs.expr;
                        }
                        if (node.op == TokenKind::Greater)
                        {
                            cmp = lhs.expr > rhs.expr;
                        }
                        if (node.op == TokenKind::GreaterEqual)
                        {
                            cmp = lhs.expr >= rhs.expr;
                        }
                        return ExprValue{cmp, TypeKind::Bool, false};
                    }

                    case TokenKind::AndAnd:
                    case TokenKind::OrOr:
                        if (lhs.kind != TypeKind::Bool || rhs.kind != TypeKind::Bool)
                        {
                            return error_at(e.span, "boolean operators expect Bool expressions");
                        }
                        return ExprValue{node.op == TokenKind::AndAnd ? (lhs.expr && rhs.expr)
                                                                      : (lhs.expr || rhs.expr),
                                         TypeKind::Bool, false};

                    default:
                        break;
                    }

                    return error_at(e.span, "unsupported binary operator in expression");
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PhysExpr>)
                {
                    // Second-line enforcement: the address must be a constant literal.
                    // The front-end guarantees this, but the verifier does not trust the AST.
                    if (!is_phys_address_literal(node.lexeme))
                    {
                        return error_at(e.span, "physical address must be a constant literal");
                    }
                    const auto elem_kind =
                        curlee::types::phys_element_kind_from_name(node.element_kind);
                    if (!elem_kind.has_value())
                    {
                        return error_at(e.span, "unsupported Phys element kind '" +
                                                    std::string(node.element_kind) + "'");
                    }
                    // Lower to a constant of the opaque sort. The constant name embeds the
                    // element kind and the address lexeme so distinct addresses stay distinct.
                    const std::string const_name = "phys_" + std::string(node.element_kind) + "_" +
                                                   std::string(node.lexeme);
                    const z3::sort& sort = phys_sort(node.element_kind);
                    return ExprValue{solver_.context().constant(const_name.c_str(), sort),
                                     TypeKind::Phys, true};
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PhysReadExpr>)
                {
                    // read() lowers to the uninterpreted function phys_read : Phys<T> -> T.
                    // No axioms constrain it, so the returned value is opaque to the solver.
                    if (node.base == nullptr) // GCOVR_EXCL_LINE
                    {
                        return error_at(e.span, "missing Phys base in read()"); // GCOVR_EXCL_LINE
                    }
                    auto base_res = lower_expr(*node.base);
                    if (std::holds_alternative<Diagnostic>(base_res))
                    {
                        return std::get<Diagnostic>(base_res);
                    }
                    auto base = std::get<ExprValue>(base_res);
                    if (base.kind != TypeKind::Phys)
                    {
                        return error_at(e.span, "read() can only be called on a Phys<T> value");
                    }
                    const std::string_view elem = base_phys_element_kind(*node.base);
                    if (elem.empty())
                    {
                        return error_at(e.span, "cannot determine Phys element kind");
                    }
                    return ExprValue{phys_read_fn(elem)(base.expr), TypeKind::Int, false};
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PhysWriteExpr>)
                {
                    // write(v) lowers to the uninterpreted function phys_write : Phys<T> x T ->
                    // Unit. No axioms relate it to read(), so writes are opaque too.
                    if (node.base == nullptr || node.value == nullptr) // GCOVR_EXCL_LINE
                    {
                        return error_at(e.span, "missing Phys base or value in write()"); // GCOVR_EXCL_LINE
                    }
                    auto base_res = lower_expr(*node.base);
                    if (std::holds_alternative<Diagnostic>(base_res))
                    {
                        return std::get<Diagnostic>(base_res);
                    }
                    auto base = std::get<ExprValue>(base_res);
                    if (base.kind != TypeKind::Phys)
                    {
                        return error_at(e.span, "write() can only be called on a Phys<T> value");
                    }
                    auto value_res = lower_expr(*node.value);
                    if (std::holds_alternative<Diagnostic>(value_res))
                    {
                        return std::get<Diagnostic>(value_res);
                    }
                    auto value = std::get<ExprValue>(value_res);
                    const std::string_view elem = base_phys_element_kind(*node.base);
                    if (elem.empty())
                    {
                        return error_at(e.span, "cannot determine Phys element kind");
                    }
                    // Unit result: represent with a dummy Bool true (Unit is not a Z3 sort).
                    (void)phys_write_fn(elem)(base.expr, value.expr);
                    return ExprValue{solver_.context().bool_val(true), TypeKind::Unit, false};
                }
                else if constexpr (std::is_same_v<Node, CallExpr>)
                {
                    return error_at(e.span, "calls are not supported in verification expressions");
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::GroupExpr>)
                {
                    return lower_expr(*node.inner);
                }

                return error_at(e.span, "unsupported expression in verification");
            },
            e.node);
    }

    void add_goal_note(Diagnostic& d, const curlee::parser::Pred& pred)
    {
        Related note;
        note.message = "goal: " + pred_to_string(pred);
        note.span = std::nullopt;
        d.notes.push_back(std::move(note));
    }

    void add_hint_note(Diagnostic& d)
    {
        Related note;
        note.message = "hint: add or strengthen preconditions/refinements to satisfy this contract";
        note.span = std::nullopt;
        d.notes.push_back(std::move(note));
    }

    void add_model_note(Diagnostic& d, const std::vector<z3::expr>& vars)
    {
        auto model = solver_.model_for(vars);
        if (!model.has_value() || model->entries.empty())
        {
            return;
        }
        Related note;
        note.message = "model:\n" + Solver::format_model(*model);
        note.span = std::nullopt;
        d.notes.push_back(std::move(note));
    }

    void check_obligation(const curlee::parser::Pred& pred, const LoweringContext& ctx,
                          const z3::expr& obligation, Span span,
                          const std::vector<z3::expr>& extra_facts, std::string_view message)
    {
        solver_.push();
        for (const auto& fact : facts_)
        {
            solver_.add(fact);
        }
        for (const auto& fact : extra_facts)
        {
            solver_.add(fact);
        }
        solver_.add(!obligation);
        const auto res = solver_.check();

        if (res == CheckResult::Sat)
        {
            Diagnostic d = error_at(span, std::string(message));
            add_goal_note(d, pred);
            add_model_note(d, model_vars_for_pred(pred, ctx));
            add_hint_note(d);
            diags_.push_back(std::move(d));
        }
        else if (res == CheckResult::Unknown)
        {
            Diagnostic d = error_at(span, std::string(message) + " (solver returned unknown)");
            add_goal_note(d, pred);
            add_hint_note(d);
            diags_.push_back(std::move(d));
        }

        solver_.pop();
    }

    // Re-lower a callee's contract-block snapshots into a call-site lowering
    // context (`ctx`, which already maps the callee's params to the caller's
    // argument expressions). Each snapshot name is bound to a fresh constant
    // equal to its pre-state value; `insert_or_assign` guarantees the callee's
    // snapshot WINS over any caller binding with the same name, so inherited
    // requires/ensures resolve the snapshot to the callee's pre-state, never a
    // colliding caller value (soundness).
    void lower_callee_snapshots(const FunctionSig& sig, LoweringContext& ctx)
    {
        for (const auto* g : sig.ghost_lets)
        {
            if (g == nullptr)
            {
                continue;
            }
            // lower_expr reads the member lower_ctx_; temporarily substitute
            // ctx's bindings so the snapshot's source expression (which may
            // reference the callee's params) resolves to the caller's argument
            // expressions.
            auto saved_int = std::move(lower_ctx_.int_vars);
            auto saved_bool = std::move(lower_ctx_.bool_vars);
            lower_ctx_.int_vars = ctx.int_vars;
            lower_ctx_.bool_vars = ctx.bool_vars;
            auto snap = lower_expr(g->value);
            lower_ctx_.int_vars = std::move(saved_int);
            lower_ctx_.bool_vars = std::move(saved_bool);

            if (std::holds_alternative<ExprValue>(snap))
            {
                auto value = std::get<ExprValue>(std::move(snap));
                // The constant name must be unique PER CALL SITE (and distinct
                // from the callee's own snapshot constant and any caller
                // binding with the same name), otherwise two distinct snapshots
                // would alias to the same Z3 constant and conflate values —
                // producing a false proof. The caller-binding map key collision
                // is handled by insert_or_assign; the constant-name collision
                // is handled by this unique suffix.
                const std::string snap_name = "ghost_" + std::string(g->name) + "_callsite_" +
                                               std::to_string(snapshot_counter_++);
                if (value.kind == TypeKind::Int)
                {
                    auto c = solver_.context().int_const(snap_name.c_str());
                    ctx.int_vars.insert_or_assign(g->name, c);
                    facts_.push_back(c == value.expr);
                }
                else if (value.kind == TypeKind::Bool)
                {
                    auto c = solver_.context().bool_const(snap_name.c_str());
                    ctx.bool_vars.insert_or_assign(g->name, c);
                    facts_.push_back(c == value.expr);
                }
            }
        }
    }

    // Returns (via out param) the fresh call-result symbol when the callee has
    // a scalar result, so a `let y = f(x);` binding can alias the result and
    // inherit the callee's ensures facts.
    void check_call(const CallExpr& call, std::optional<z3::expr>* out_result = nullptr)
    {
        const auto* callee_name = std::get_if<NameExpr>(&call.callee->node);
        if (callee_name == nullptr)
        {
            return;
        }

        auto sig_it = functions_.find(callee_name->name);
        if (sig_it == functions_.end())
        {
            return;
        }
        const FunctionSig& sig = sig_it->second;
        if (sig.decl == nullptr)
        {
            return;
        }

        // An opaque MMIO read value must never flow into a contract silently. Check argument
        // provenance before the MVP gate so that a call like consume(reg.read()) with a
        // requires-guarded unsigned parameter is a hard error, not a silent skip.
        bool arg_is_opaque = false;
        for (const auto& arg : call.args)
        {
            if (expr_is_opaque_read(arg))
            {
                arg_is_opaque = true;
                break;
            }
        }

        // MVP: only reason about calls whose arguments are entirely Int/Bool.
        for (const auto k : sig.params)
        {
            if (k != TypeKind::Int && k != TypeKind::Bool)
            {
                // The callee has non-scalar parameters (e.g. unsigned element kinds). If an
                // argument derives from an opaque MMIO read, any contract on that parameter
                // cannot be proven - fail loudly instead of silently skipping.
                if (arg_is_opaque)
                {
                    // arg_is_opaque is only set when an argument was scanned, so args is
                    // guaranteed non-empty here (the ternary fallback was dead code).
                    reject_opaque_read_contract(call.args[0].span,
                                                "cannot prove call contract on opaque MMIO read argument");
                }
                return;
            }
        }

        if (call.args.size() != sig.params.size())
        {
            return;
        }

        std::vector<z3::expr> arg_exprs;
        arg_exprs.reserve(call.args.size());
        for (const auto& arg : call.args)
        {
            auto lowered = lower_expr(arg);
            if (std::holds_alternative<Diagnostic>(lowered))
            {
                diags_.push_back(std::get<Diagnostic>(std::move(lowered)));
                return;
            }
            arg_exprs.push_back(std::get<ExprValue>(lowered).expr);
        }

        const std::string callee_name_str(callee_name->name);

        std::vector<z3::expr> call_facts;
        LoweringContext call_ctx(solver_.context());
        call_ctx.ghost_fns = &ghost_fns_;

        for (std::size_t i = 0; i < sig.decl->params.size(); ++i)
        {
            const auto& param = sig.decl->params[i];
            const auto param_name = std::string(param.name);
            const auto sym_name = callee_name_str + "::" + param_name;

            if (sig.params[i] == TypeKind::Int)
            {
                auto sym = solver_.context().int_const(sym_name.c_str());
                call_ctx.int_vars.emplace(param.name, sym);
                call_facts.push_back(sym == arg_exprs[i]);
            }
            if (sig.params[i] == TypeKind::Bool)
            {
                auto sym = solver_.context().bool_const(sym_name.c_str());
                call_ctx.bool_vars.emplace(param.name, sym);
                call_facts.push_back(sym == arg_exprs[i]);
            }
        }

        // The callee's contract-block snapshots must be visible to the
        // call-site `requires` lowering (the resolver permits `requires` to
        // reference snapshot names), so re-lower them into call_ctx before the
        // requires obligations are checked.
        lower_callee_snapshots(sig, call_ctx);

        for (const auto& req : sig.decl->requires_clauses)
        {
            // A requires clause mentioning an opaque MMIO read value can never be proven;
            // reject unconditionally before lowering.
            if (pred_mentions_opaque_read(req))
            {
                reject_opaque_read_contract(req.span,
                                            "cannot prove requires on opaque MMIO read value");
                continue;
            }
            auto lowered = lower_predicate(req, call_ctx);
            if (std::holds_alternative<Diagnostic>(lowered))
            {
                diags_.push_back(std::get<Diagnostic>(std::move(lowered)));
                continue;
            }

            check_obligation(req, call_ctx, std::get<z3::expr>(lowered), req.span, call_facts,
                             "requires clause not satisfied");
        }

        // Post-state inheritance: once the call's requires are satisfied, the
        // callee's `ensures` clauses become facts in the caller's scope. The
        // callee's `result` symbol is bound to the fresh call-result symbol.
        // This is what makes contracts compose at call sites (issue #260, M1
        // part C).
        //
        // IMPORTANT: the ensures facts must reference the CALLER's argument
        // expressions directly (not the fresh callee-param symbols from
        // call_ctx), otherwise the pushed facts would mention unconstrained
        // symbols and the caller could not discharge its own obligations. So a
        // separate post_ctx maps each callee param name to the corresponding
        // caller argument expression, plus the caller's visible snapshots.
        //
        // An opaque MMIO read value can never satisfy an ensures clause; reject
        // before lowering (mirrors the requires path above).
        if (!sig.decl->ensures.empty())
        {
            LoweringContext post_ctx(solver_.context());
            post_ctx.ghost_fns = &ghost_fns_;
            for (std::size_t i = 0; i < sig.decl->params.size() && i < arg_exprs.size(); ++i)
            {
                const auto& param = sig.decl->params[i];
                if (sig.params[i] == TypeKind::Int)
                {
                    post_ctx.int_vars.emplace(param.name, arg_exprs[i]);
                }
                else if (sig.params[i] == TypeKind::Bool)
                {
                    post_ctx.bool_vars.emplace(param.name, arg_exprs[i]);
                }
            }
            // Ghost snapshots visible in the caller scope must also resolve in
            // the post context so ensures can reference them.
            post_ctx.int_vars.insert(lower_ctx_.int_vars.begin(), lower_ctx_.int_vars.end());
            post_ctx.bool_vars.insert(lower_ctx_.bool_vars.begin(), lower_ctx_.bool_vars.end());

            // The callee's contract-block snapshots are re-lowered into the
            // post context so inherited ensures referencing them resolve to the
            // callee's pre-state (bound from the caller's argument), never a
            // caller binding with the same name (see lower_callee_snapshots).
            lower_callee_snapshots(sig, post_ctx);

            // Bind the callee's `result` symbol to a fresh call-result constant.
            // The name is fresh per call so multiple calls in one scope do not
            // alias each other's results.
            const std::string call_result_name =
                callee_name_str + "::result_" + std::to_string(call_result_counter_++);
            if (sig.result == TypeKind::Int)
            {
                auto result = solver_.context().int_const(call_result_name.c_str());
                post_ctx.result_int = result;
                if (out_result != nullptr)
                {
                    *out_result = result;
                }
            }
            else if (sig.result == TypeKind::Bool)
            {
                auto result = solver_.context().bool_const(call_result_name.c_str());
                post_ctx.result_bool = result;
                if (out_result != nullptr)
                {
                    *out_result = result;
                }
            }

            for (const auto& ens : sig.decl->ensures)
            {
                if (pred_mentions_opaque_read(ens))
                {
                    reject_opaque_read_contract(ens.span,
                                                "cannot prove ensures on opaque MMIO read value");
                    continue;
                }
                auto lowered_ens = lower_predicate(ens, post_ctx);
                if (std::holds_alternative<Diagnostic>(lowered_ens))
                {
                    diags_.push_back(std::get<Diagnostic>(std::move(lowered_ens)));
                    continue;
                }
                // Push the ensures clause as a fact in the caller's scope so later
                // obligations (refinements, requires of subsequent calls, ensures
                // of the enclosing function) can use it.
                facts_.push_back(std::get<z3::expr>(lowered_ens));
            }
        }
    }

    static bool is_python_ffi_call(const CallExpr& call)
    {
        if (call.callee == nullptr)
        {
            return false;
        }

        const auto* member = std::get_if<MemberExpr>(&call.callee->node);
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

    // Check a call-heavy expression for requires/ensures obligations. When the
    // expression is (or wraps) a plain function call, the outermost call's
    // fresh result symbol is written to `out_result` (when non-null) so the
    // caller can bind a let to it and inherit the callee's ensures facts.
    void check_expr_for_calls(const Expr& e, std::optional<z3::expr>* out_result = nullptr)
    {
        std::visit(
            [&](const auto& node)
            {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, CallExpr>)
                {
                    std::optional<z3::expr> call_result;
                    if (!is_python_ffi_call(node))
                    {
                        check_call(node, &call_result);
                    }
                    for (const auto& arg : node.args)
                    {
                        check_expr_for_calls(arg, nullptr);
                    }
                    // The outermost call's result wins; nested call results are
                    // discarded (they cannot be bound to this expression).
                    if (out_result != nullptr && call_result.has_value())
                    {
                        *out_result = call_result;
                    }
                }
                else if constexpr (std::is_same_v<Node, MemberExpr>)
                {
                    if (node.base)
                    {
                        check_expr_for_calls(*node.base, nullptr);
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::UnaryExpr>)
                {
                    check_expr_for_calls(*node.rhs, nullptr);
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::BinaryExpr>)
                {
                    check_expr_for_calls(*node.lhs, nullptr);
                    check_expr_for_calls(*node.rhs, nullptr);
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::GroupExpr>)
                {
                    check_expr_for_calls(*node.inner, out_result);
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PhysReadExpr>)
                {
                    if (node.base)
                    {
                        check_expr_for_calls(*node.base, nullptr);
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PhysWriteExpr>)
                {
                    if (node.base)
                    {
                        check_expr_for_calls(*node.base, nullptr);
                    }
                    if (node.value)
                    {
                        check_expr_for_calls(*node.value, nullptr);
                    }
                }
                else
                {
                    (void)node;
                }
            },
            e.node);
    }

    void check_return(const ReturnStmt& s, TypeKind expected_return)
    {
        if (!s.value.has_value())
        {
            return;
        }

        if (!current_function_.has_value())
        {
            return;
        }

        const auto* func = current_function_->decl;
        if (func == nullptr || func->ensures.empty())
        {
            return;
        }

        auto lowered = lower_expr(*s.value);
        if (std::holds_alternative<Diagnostic>(lowered))
        {
            diags_.push_back(std::get<Diagnostic>(std::move(lowered)));
            return;
        }

        auto value = std::get<ExprValue>(lowered);
        // An opaque MMIO read lowers to an Int-typed Z3 term; accept it for unsigned element
        // kind returns (U8/U16/U32/U64) so the ensures check can run below.
        const bool opaque_unsigned_return =
            is_phys_element_kind(expected_return) && value.kind == TypeKind::Int &&
            expr_is_opaque_read(*s.value);
        if (value.kind != expected_return && !opaque_unsigned_return)
        {
            return;
        }

        // A function returning an opaque MMIO read value has an unprovable `result`: any
        // ensures clause mentioning it must be rejected with the opaque note, never silently
        // satisfied.
        const bool result_is_opaque = expr_is_opaque_read(*s.value);
        if (result_is_opaque)
        {
            for (const auto& ens : func->ensures)
            {
                if (pred_mentions_result(ens))
                {
                    reject_opaque_read_contract(ens.span,
                                                "cannot prove ensures on opaque MMIO read result");
                }
            }
            return;
        }

        const std::string result_symbol = "result";
        std::vector<z3::expr> ensure_facts;
        LoweringContext ensure_ctx = lower_ctx_;

        if (expected_return == TypeKind::Int)
        {
            auto result = solver_.context().int_const(result_symbol.c_str());
            ensure_ctx.result_int = result;
            ensure_facts.push_back(result == value.expr);
        }
        if (expected_return == TypeKind::Bool)
        {
            auto result = solver_.context().bool_const(result_symbol.c_str());
            ensure_ctx.result_bool = result;
            ensure_facts.push_back(result == value.expr);
        }

        for (const auto& ens : func->ensures)
        {
            auto lowered_pred = lower_predicate(ens, ensure_ctx);
            if (std::holds_alternative<Diagnostic>(lowered_pred))
            {
                diags_.push_back(std::get<Diagnostic>(std::move(lowered_pred)));
                continue;
            }

            check_obligation(ens, ensure_ctx, std::get<z3::expr>(lowered_pred), ens.span,
                             ensure_facts, "ensures clause not satisfied");
        }
    }

    void check_stmt(const Stmt& s, TypeKind expected_return)
    {
        std::visit([&](const auto& node) { check_stmt_node(node, s.span, expected_return); },
                   s.node);
    }

    // Lower a ghost snapshot `ghost let x = e;` into a fresh solver constant
    // bound to the name `x`, declared in `ctx` with an equality fact appended
    // to `facts`. Used for both body statements and contract-block snapshot
    // clauses. Returns the lowered snapshot expression, or std::nullopt if the
    // snapshot is non-scalar (cannot be modeled in the solver fragment).
    std::optional<z3::expr> lower_ghost_let(const GhostLetStmt& s, LoweringContext& ctx)
    {
        auto lowered = lower_expr(s.value);
        if (std::holds_alternative<Diagnostic>(lowered))
        {
            diags_.push_back(std::get<Diagnostic>(std::move(lowered)));
            return std::nullopt;
        }
        auto value = std::get<ExprValue>(std::move(lowered));

        if (value.kind == TypeKind::Int)
        {
            const std::string snap_name = "ghost_" + std::string(s.name);
            auto snap = solver_.context().int_const(snap_name.c_str());
            ctx.int_vars.insert_or_assign(s.name, snap);
            facts_.push_back(snap == value.expr);
            return snap;
        }
        if (value.kind == TypeKind::Bool)
        {
            const std::string snap_name = "ghost_" + std::string(s.name);
            auto snap = solver_.context().bool_const(snap_name.c_str());
            ctx.bool_vars.insert_or_assign(s.name, snap);
            facts_.push_back(snap == value.expr);
            return snap;
        }
        // Non-scalar snapshots (structs/enums/Phys) cannot be modeled in the
        // solver fragment; they are allowed as long as no contract references
        // them. Do not declare a solver variable.
        return std::nullopt;
    }

    void check_stmt_node(const GhostLetStmt& s, Span, TypeKind)
    {
        // Ghost code is verification-only: this statement never appears in
        // emitted bodies (the emitter/codegen reject it as an internal error),
        // so no runtime semantics are needed here.
        (void)lower_ghost_let(s, lower_ctx_);
        check_expr_for_calls(s.value);
    }

    void check_stmt_node(const LetStmt& s, Span, TypeKind)
    {
        // Verification scope only reasons about Int/Bool values.
        // Non-scalar bindings (e.g. structs/enums) are allowed as long as we don't
        // attach refinements to them, since we can't lower them into the solver.

        // A Phys<T> binding names an opaque pointer value. Record its element kind so that
        // read()/write() can be lowered as uninterpreted functions.
        if (s.type.name == "Phys")
        {
            if (s.type.type_arg.has_value())
            {
                phys_vars_.insert_or_assign(s.name, *s.type.type_arg);
            }
            // Re-validate the address is a constant literal inside the verifier (defense in
            // depth: the front-end enforces this, but the verifier must not trust the AST).
            if (const auto* phys = std::get_if<curlee::parser::PhysExpr>(&s.value.node))
            {
                if (!is_phys_address_literal(phys->lexeme))
                {
                    diags_.push_back(
                        error_at(s.value.span, "physical address must be a constant literal"));
                }
            }
            check_expr_for_calls(s.value);
            return;
        }

        const auto core_t = curlee::types::core_type_from_name(s.type.name);
        if (!core_t.has_value())
        {
            if (s.refinement.has_value())
            {
                diags_.push_back(
                    error_at(s.refinement->span,
                             "verification does not support refinements on non-scalar type '" +
                                 std::string(s.type.name) + "'"));
            }
            check_expr_for_calls(s.value);
            return;
        }

        // Unsigned element kinds (U8/U16/U32/U64) are freestanding-only and treated as
        // uninterpreted by verification, mirroring Phys<T> itself.
        if (core_t->kind != TypeKind::Int && core_t->kind != TypeKind::Bool &&
            !is_phys_element_kind(core_t->kind))
        {
            diags_.push_back(
                error_at(s.type.span, "verification does not support type '" +
                                          std::string(curlee::types::to_string(*core_t)) + "'"));
            check_expr_for_calls(s.value);
            return;
        }

        if (is_phys_element_kind(core_t->kind))
        {
            // Uninterpreted: do not declare a solver variable, but still scan for calls.
            // Opaque provenance is transitive: a binding whose initializer derives from a
            // Phys read() (directly or via an alias chain) is itself opaque and must not be
            // contractable.
            if (std::holds_alternative<curlee::parser::PhysReadExpr>(s.value.node) ||
                expr_is_opaque_read(s.value))
            {
                opaque_read_vars_.insert(s.name);
            }
            // A refinement on an opaque read value cannot be proven; surface it as a hard
            // diagnostic with the opaque-value note rather than silently passing.
            if (s.refinement.has_value() && pred_mentions_opaque_read(*s.refinement))
            {
                reject_opaque_read_contract(s.refinement->span,
                                            "cannot prove refinement on opaque MMIO read value");
            }
            check_expr_for_calls(s.value);
            return;
        }

        declare_var(s.name, core_t->kind);

        if (s.refinement.has_value())
        {
            add_fact(*s.refinement);
        }

        // For call-valued lets, `check_expr_for_calls` performs the requires
        // check, pushes the callee's ensures facts, AND returns the fresh call
        // result so the let can alias it (post-state inheritance, issue #260).
        std::optional<z3::expr> call_result;
        check_expr_for_calls(s.value, &call_result);

        if (call_result.has_value() && core_t->kind != TypeKind::Unit)
        {
            // Bind the let to the call result so `let y = f(x);` makes `y`
            // alias the call result and the inherited ensures facts apply to it.
            if (core_t->kind == TypeKind::Int)
            {
                auto sym = solver_.context().int_const(std::string(s.name).c_str());
                lower_ctx_.int_vars.insert_or_assign(s.name, sym);
                facts_.push_back(sym == *call_result);
            }
            else if (core_t->kind == TypeKind::Bool)
            {
                auto sym = solver_.context().bool_const(std::string(s.name).c_str());
                lower_ctx_.bool_vars.insert_or_assign(s.name, sym);
                facts_.push_back(sym == *call_result);
            }
        }

        // Non-call initializers: record an equality fact so later predicates can
        // reason about the binding's value (e.g. `let x = 5;` makes `x == 5` a
        // fact, and ghost snapshots of x freeze 5).
        if (!call_result.has_value())
        {
            auto lowered = lower_expr(s.value);
            if (std::holds_alternative<ExprValue>(lowered))
            {
                auto value = std::get<ExprValue>(std::move(lowered));
                if (value.kind == core_t->kind)
                {
                    if (auto it = lower_ctx_.int_vars.find(s.name);
                        it != lower_ctx_.int_vars.end())
                    {
                        facts_.push_back(it->second == value.expr);
                    }
                    else if (auto itb = lower_ctx_.bool_vars.find(s.name);
                             itb != lower_ctx_.bool_vars.end())
                    {
                        facts_.push_back(itb->second == value.expr);
                    }
                }
            }
        }
    }

    void check_stmt_node(const ReturnStmt& s, Span, TypeKind expected_return)
    {
        if (s.value.has_value())
        {
            check_expr_for_calls(*s.value);
        }
        check_return(s, expected_return);
    }

    void check_stmt_node(const ExprStmt& s, Span, TypeKind) { check_expr_for_calls(s.expr); }

    void check_stmt_node(const BlockStmt& s, Span, TypeKind expected_return)
    {
        push_scope();
        for (const auto& stmt : s.block->stmts)
        {
            check_stmt(stmt, expected_return);
        }
        pop_scope();
    }

    void check_stmt_node(const UnsafeStmt& s, Span, TypeKind expected_return)
    {
        push_scope();
        for (const auto& stmt : s.body->stmts)
        {
            check_stmt(stmt, expected_return);
        }
        pop_scope();
    }

    void check_stmt_node(const IfStmt& s, Span, TypeKind expected_return)
    {
        check_expr_for_calls(s.cond);

        std::optional<z3::expr> cond_fact;
        {
            auto lowered = lower_expr(s.cond);
            if (std::holds_alternative<ExprValue>(lowered))
            {
                auto value = std::get<ExprValue>(std::move(lowered));
                if (value.kind == TypeKind::Bool)
                {
                    cond_fact = std::move(value.expr);
                }
            }
        }

        push_scope();
        if (cond_fact.has_value())
        {
            facts_.push_back(*cond_fact);
        }
        for (const auto& stmt : s.then_block->stmts)
        {
            check_stmt(stmt, expected_return);
        }
        pop_scope();

        if (s.else_block != nullptr)
        {
            push_scope();
            if (cond_fact.has_value())
            {
                facts_.push_back(!*cond_fact);
            }
            for (const auto& stmt : s.else_block->stmts)
            {
                check_stmt(stmt, expected_return);
            }
            pop_scope();
        }
    }

    void check_stmt_node(const WhileStmt& s, Span, TypeKind expected_return)
    {
        check_expr_for_calls(s.cond);

        std::optional<z3::expr> cond_fact;
        {
            auto lowered = lower_expr(s.cond);
            if (std::holds_alternative<ExprValue>(lowered))
            {
                auto value = std::get<ExprValue>(std::move(lowered));
                if (value.kind == TypeKind::Bool)
                {
                    cond_fact = std::move(value.expr);
                }
            }
        }

        push_scope();
        if (cond_fact.has_value())
        {
            facts_.push_back(*cond_fact);
        }
        for (const auto& stmt : s.body->stmts)
        {
            check_stmt(stmt, expected_return);
        }
        pop_scope();
    }

    void check_stmt_node(const MatchStmt& s, Span, TypeKind expected_return)
    {
        check_expr_for_calls(s.value);

        for (const auto& arm : s.arms)
        {
            push_scope();
            if (arm.body != nullptr)
            {
                for (const auto& stmt : arm.body->stmts)
                {
                    check_stmt(stmt, expected_return);
                }
            }
            pop_scope();
        }
    }

    void check_function(const Function& f)
    {
        auto sig_it = functions_.find(f.name);
        if (sig_it == functions_.end())
        {
            return;
        }

        // Extern functions are a trusted boundary: the implementation is
        // provided at link time by the host stub/runtime, so their contracts
        // are ASSUMED, not verified. Emit a Note so the trust transfer is
        // explicit ("never silently drop a contract"), and do not attempt to
        // prove requires/ensures against a body that does not exist.
        if (f.is_extern)
        {
            Diagnostic d = note_at(f.span, "extern boundary: contract assumed, not verified");
            if (!f.requires_clauses.empty() || !f.ensures.empty())
            {
                d.notes.push_back(
                    Related{.message = "extern declaration carries contracts that are "
                                       "assumed without proof",
                            .span = f.span});
            }
            diags_.push_back(std::move(d));
            return;
        }

        current_function_ = sig_it->second;
        lower_ctx_.result_int.reset();
        lower_ctx_.result_bool.reset();
        lower_ctx_.int_vars.clear();
        lower_ctx_.bool_vars.clear();
        phys_vars_.clear();
        opaque_read_vars_.clear();
        facts_.clear();
        scopes_.clear();

        push_scope();
        for (std::size_t i = 0; i < f.params.size(); ++i)
        {
            const auto& param = f.params[i];
            const auto param_kind = sig_it->second.params[i];

            // Phys<T> parameters are opaque pointers; record their element kind so that
            // read()/write() on the parameter can be lowered.
            if (param_kind == TypeKind::Phys && param.type.type_arg.has_value())
            {
                phys_vars_.insert_or_assign(param.name, *param.type.type_arg);
            }

            // Unsigned element-kind parameters are opaque: a refinement/requires on them
            // cannot be proven, so mark them opaque to route any such contract to the hard
            // opaque-read diagnostic.
            if (is_phys_element_kind(param_kind))
            {
                opaque_read_vars_.insert(param.name);
            }

            declare_var(param.name, param_kind);

            if (param.refinement.has_value())
            {
                add_fact(*param.refinement);
            }
        }

        // Contract-block ghost snapshots are lowered into the function's solver
        // context BEFORE requires/ensures are added, so the function's own
        // postcondition can reference them (the roadmap's pre-state snapshot
        // pattern). The snapshot bindings are also copied into call-site
        // post_ctx (via the callee's ghost_lets) so inherited ensures resolve
        // them at the call site.
        for (const auto& g : f.ghost_lets)
        {
            lower_ghost_let(g, lower_ctx_);
        }

        for (const auto& req : f.requires_clauses)
        {
            add_fact(req);
        }

        for (const auto& stmt : f.body.stmts)
        {
            check_stmt(stmt, sig_it->second.result);
        }

        pop_scope();
        current_function_.reset();
    }

    std::optional<FunctionSig> current_function_;

    // Fresh-name counter for call-result symbols created during post-state
    // inheritance. Kept per-Verifier so names never collide across calls.
    std::size_t call_result_counter_ = 0;

    // Fresh-name counter for call-site snapshot constants. Kept per-Verifier so
    // distinct call-site snapshot re-lowerings never alias the same Z3 constant.
    std::size_t snapshot_counter_ = 0;
};

} // namespace

VerificationResult verify(const curlee::parser::Program& program,
                          const curlee::types::TypeInfo& type_info,
                          std::vector<curlee::diag::Diagnostic>* out_notes)
{
    Verifier verifier(type_info);
    auto result = verifier.run(program);
    // Surface the (non-fatal) Note diagnostics to the caller so the CLI can
    // render them while still exiting 0.
    if (out_notes != nullptr)
    {
        *out_notes = std::move(verifier.notes());
    }
    return result;
}

} // namespace curlee::verification
