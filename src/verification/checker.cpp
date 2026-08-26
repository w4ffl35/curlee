// SPDX-License-Identifier: MIT
#include <charconv>
#include <cstddef>
#include <curlee/lexer/token.h>
#include <curlee/types/type.h>
#include <curlee/verification/checker.h>
#include <curlee/verification/cost_model.h>
#include <curlee/verification/predicate_lowering.h>
#include <curlee/verification/solver.h>
#include <functional>
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
using curlee::parser::AssignStmt;
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
using curlee::parser::PortIOExpr;
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
    case TokenKind::Percent:
        return "%";
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
    case TokenKind::Amp:
        return "&";
    case TokenKind::Pipe:
        return "|";
    case TokenKind::Caret:
        return "^";
    case TokenKind::Tilde:
        return "~";
    case TokenKind::ShiftLeft:
        return "<<";
    case TokenKind::ShiftRight:
        return ">>";
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

// True for the unsigned fixed-width integer types the verifier lowers to their
// Int value: U8/U16/U32 (issue #274) and, since issue #277, U64. A U64 binding
// is constructed from an Int (widening) or a literal in the MVP — its value IS
// the Int value (no wraparound, no overflow cases in scope) — so the same
// Int-sort lowering applies. (The front-end's operator rules still keep U64
// out of arithmetic/mixed comparisons; this only affects solver bindings.)
static bool is_widening_unsigned(TypeKind kind)
{
    return kind == TypeKind::U8 || kind == TypeKind::U16 || kind == TypeKind::U32 ||
           kind == TypeKind::U64;
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

/**
 * @brief True if the lexeme is a plain decimal numeral (digits only).
 *
 * Z3's numeral parser accepts such lexemes directly at arbitrary precision.
 * Hex (0x...) and underscore-separated lexemes (PhysAddrLiteral forms) need
 * conversion to a plain decimal numeral string before int_val(), so this
 * classifier decides which path the IntExpr lowering takes (issue #276).
 */
bool is_plain_decimal_literal(std::string_view lexeme)
{
    if (lexeme.empty())
    {
        return false;
    }
    for (const char c : lexeme)
    {
        if (c < '0' || c > '9')
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
    // Names bound from a port_in* read result (opaque; distinct provenance for
    // the port-specific opaque diagnostic).
    std::unordered_set<std::string_view> opaque_port_read_vars;
    std::size_t facts_size = 0;
};

class Verifier
{
  public:
    explicit Verifier(const curlee::types::TypeInfo& type_info)
        : type_info_(type_info), solver_(), lower_ctx_(solver_.context()),
          cost_model_(solver_.context(),
                      [this](const curlee::parser::Expr& e, const LoweringContext&)
                      {
                          auto lowered = lower_expr(e);
                          if (std::holds_alternative<ExprValue>(lowered))
                          {
                              return std::optional<z3::expr>{
                                  std::get<ExprValue>(std::move(lowered)).expr};
                          }
                          return std::optional<z3::expr>{};
                      })
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
    CostModel cost_model_;
    std::vector<Diagnostic> diags_;
    std::vector<Diagnostic> notes_;
    std::vector<z3::expr> facts_;
    std::vector<ScopeState> scopes_;
    std::unordered_map<std::string_view, FunctionSig> functions_;

    // Call-graph fuel table (name -> declared fuel bound). Populated during
    // collect_signatures. A callee is costable only when it declares a `fuel`
    // bound; a call to a callee without one fails closed when the caller's
    // static fuel cost is computed.
    std::unordered_map<std::string_view, CostModel::FuelEntry> fuel_table_;

    // Ghost function signatures used to lower predicate calls to uninterpreted
    // function applications. Populated during collect_signatures.
    GhostFnTable ghost_fns_;

    // Phys<T> pointer variables (name -> element kind name), scoped like int/bool vars.
    std::unordered_map<std::string_view, std::string_view> phys_vars_;
    // Names bound from a Phys read() result; contracts referencing these cannot be proven.
    std::unordered_set<std::string_view> opaque_read_vars_;
    // Names bound from a port_in* read result; contracts referencing these get the
    // port-specific opaque diagnostic.
    std::unordered_set<std::string_view> opaque_port_read_vars_;

    // Per-function counter for fresh solver symbol names (see fresh_symbol).
    // Reset in check_function so symbol names are short and stable in model
    // output; uniqueness within a function is all that is required because
    // facts and bindings are scoped per function.
    std::size_t fresh_counter_ = 0;

    // Loop-entry lowering contexts recorded at each `while` with a contract, so
    // the static fuel cost model can lower the `decreases` variant against the
    // PRE-loop bindings. Assignments inside the loop body rebind names to fresh
    // symbols; without this snapshot, a fuel obligation evaluated after the
    // body would use a post-mutation symbol for D_0 (e.g. a top-level
    // assignment after the loop) and spuriously fail a bounded loop. Keyed by
    // the WhileStmt's address: the AST is stable and shared with the cost
    // model's walk, and the cost model visits the SAME WhileStmt object stored
    // inside the parent Stmt's variant (std::get<WhileStmt>(stmt.node)), so
    // `&s` here and `&node` in cost_stmt's WhileStmt branch are identical.
    std::unordered_map<const WhileStmt*, LoweringContext> loop_entry_ctxs_;

    void push_scope()
    {
        scopes_.emplace_back();
        auto& state = scopes_.back();
        state.int_vars = lower_ctx_.int_vars;
        state.bool_vars = lower_ctx_.bool_vars;
        state.phys_vars = phys_vars_;
        state.opaque_read_vars = opaque_read_vars_;
        state.opaque_port_read_vars = opaque_port_read_vars_;
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
        opaque_port_read_vars_ = state.opaque_port_read_vars;
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

            // Populate the call-graph fuel table: a function is costable when it
            // declares a `fuel` bound. Extern functions must declare a trusted
            // cost (their bodies are opaque). Functions without a declared bound
            // are simply absent from the table; a call to them fails closed when
            // the caller's static fuel cost is computed.
            if (f.fuel_bound.has_value())
            {
                CostModel::FuelEntry entry;
                entry.declared_bound = &*f.fuel_bound;
                entry.decl = &f;
                entry.is_extern = f.is_extern;
                fuel_table_.emplace(f.name, std::move(entry));
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

    // A FRESH Z3 symbol per source-level binding. The solver identifies
    // constants by name within a context, so two `int_const("i")` calls are the
    // SAME logical constant: reusing a name for a reassigned or shadowed
    // binding would conflate pre- and post-mutation values and make loop
    // `decreases`/invariant reasoning vacuous (the documented name-based
    // constant-reuse gap in docs/loop-contracts.md). Each declaration and each
    // assignment therefore gets a unique symbol name (`name@<counter>`), while
    // the lower_ctx_ maps keep the source name as the lookup key. `@` cannot
    // appear in a source identifier, so model output stays unambiguous.
    z3::expr fresh_symbol(std::string_view name, TypeKind kind)
    {
        const std::string sym_name =
            std::string(name) + "@" + std::to_string(fresh_counter_++);
        return kind == TypeKind::Int ? solver_.context().int_const(sym_name.c_str())
                                     : solver_.context().bool_const(sym_name.c_str());
    }

    void declare_var(std::string_view name, TypeKind kind)
    {
        if (kind == TypeKind::Int)
        {
            lower_ctx_.int_vars.insert_or_assign(name, fresh_symbol(name, TypeKind::Int));
            return;
        }
        // U8/U16/U32/U64 values lower to their Int value in the solver
        // (issues #274/#277): the read stays opaque (uninterpreted), but its
        // *type* is usable — a port read can be bit-tested, compared, and
        // arithmetically widened, and a U64 binding (constructed from an Int
        // or literal) is a normal Int-valued binding.
        if (is_widening_unsigned(kind))
        {
            lower_ctx_.int_vars.insert_or_assign(name, fresh_symbol(name, TypeKind::Int));
            return;
        }
        if (kind == TypeKind::Bool)
        {
            lower_ctx_.bool_vars.insert_or_assign(name, fresh_symbol(name, TypeKind::Bool));
            return;
        }
    }

    // Reassign an existing binding: bind the name to a FRESH symbol constrained
    // to the RHS value. The old symbol stays in the solver's history (facts
    // referencing it describe the pre-mutation state), so `D_before` and
    // `D_after` of a loop variant remain distinct and the decrease proof is
    // non-vacuous. Only Int/Bool bindings are modeled in the solver fragment;
    // non-scalar bindings are ignored here (the type checker gates assignment
    // targets to local let bindings, and non-scalar lets are not contractable).
    void assign_var(std::string_view name, TypeKind kind, const z3::expr& value)
    {
        if (kind == TypeKind::Int)
        {
            auto fresh = fresh_symbol(name, TypeKind::Int);
            lower_ctx_.int_vars.insert_or_assign(name, fresh);
            facts_.push_back(fresh == value);
            return;
        }
        if (kind == TypeKind::Bool)
        {
            auto fresh = fresh_symbol(name, TypeKind::Bool);
            lower_ctx_.bool_vars.insert_or_assign(name, fresh);
            facts_.push_back(fresh == value);
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
        auto [inserted_it, _] =
            phys_sorts_.emplace(element_kind, solver_.context().uninterpreted_sort(name.c_str()));
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

    // Width tag ("b"/"w"/"l") for a port I/O builtin name; empty for non-port names.
    static std::string_view port_width_for_op(std::string_view op)
    {
        if (op == "port_inb" || op == "port_outb")
        {
            return "b";
        }
        if (op == "port_inw" || op == "port_outw")
        {
            return "w";
        }
        if (op == "port_inl" || op == "port_outl")
        {
            return "l";
        }
        return {};
    }

    // Parse a constant port literal (decimal or 0x-hex, with optional '_'
    // separators) into its numeric value. Z3's rational parser accepts decimal
    // numerals only — not '0x' hex prefixes or '_' separators — so the port
    // must be converted to a plain decimal value before int_val(). Ports are
    // 16-bit, so uint64_t is ample.
    static std::optional<std::uint64_t> parse_port_literal(std::string_view lexeme)
    {
        std::string cleaned;
        cleaned.reserve(lexeme.size());
        for (const char ch : lexeme)
        {
            if (ch != '_')
            {
                cleaned.push_back(ch);
            }
        }
        std::uint64_t value = 0;
        const char* begin = cleaned.data();
        const char* end = cleaned.data() + cleaned.size();
        int base = 10;
        if (cleaned.size() > 2 && cleaned[0] == '0' && (cleaned[1] == 'x' || cleaned[1] == 'X'))
        {
            base = 16;
            begin += 2;
        }
        const auto res = std::from_chars(begin, end, value, base);
        if (res.ec != std::errc{} || res.ptr != end)
        {
            return std::nullopt;
        }
        return value;
    }

    // One fresh uninterpreted read function per width: port_in_<b|w|l> : Int -> Int.
    // No axioms constrain them, so the returned value is opaque to the solver.
    std::unordered_map<std::string_view, z3::func_decl> port_in_fns_;

    const z3::func_decl& port_in_fn(std::string_view width)
    {
        auto it = port_in_fns_.find(width);
        if (it != port_in_fns_.end())
        {
            return it->second;
        }
        auto& ctx = solver_.context();
        const std::string name = "port_in_" + std::string(width);
        auto [inserted_it, _] =
            port_in_fns_.emplace(width, ctx.function(name.c_str(), ctx.int_sort(), ctx.int_sort()));
        return inserted_it->second;
    }

    // Uninterpreted write function per width: port_out_<b|w|l> : Int x Int -> Unit.
    // No axioms relate writes to reads, so writes are opaque too.
    std::unordered_map<std::string_view, z3::func_decl> port_out_fns_;

    const z3::func_decl& port_out_fn(std::string_view width)
    {
        auto it = port_out_fns_.find(width);
        if (it != port_out_fns_.end())
        {
            return it->second;
        }
        auto& ctx = solver_.context();
        const std::string name = "port_out_" + std::string(width);
        auto [inserted_it, _] = port_out_fns_.emplace(
            width, ctx.function(name.c_str(), ctx.int_sort(), ctx.int_sort(), ctx.int_sort()));
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

    // Like pred_mentions_opaque_read, but only for port_in* provenance, so the
    // opaque diagnostic can name the right access kind.
    bool pred_mentions_port_opaque_read(const curlee::parser::Pred& pred) const
    {
        bool found = false;
        std::visit(
            [&](const auto& node)
            {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, curlee::parser::PredName>)
                {
                    if (opaque_port_read_vars_.count(node.name) != 0)
                    {
                        found = true;
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PredCall>)
                {
                    for (const auto& arg : node.args)
                    {
                        if (pred_mentions_port_opaque_read(arg))
                        {
                            found = true;
                        }
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PredUnary>)
                {
                    if (node.rhs != nullptr && pred_mentions_port_opaque_read(*node.rhs))
                    {
                        found = true;
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PredBinary>)
                {
                    if (node.lhs != nullptr && pred_mentions_port_opaque_read(*node.lhs))
                    {
                        found = true;
                    }
                    if (node.rhs != nullptr && pred_mentions_port_opaque_read(*node.rhs))
                    {
                        found = true;
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::PredGroup>)
                {
                    if (node.inner != nullptr && pred_mentions_port_opaque_read(*node.inner))
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

    // True if an expression derives from an opaque read (MMIO or port I/O),
    // transitively. A value is opaque if it is a PhysReadExpr, a port_in*
    // PortIOExpr, or a NameExpr bound to an opaque read (directly or via
    // aliases), or a compound expression containing one.
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
                else if constexpr (std::is_same_v<Node, PortIOExpr>)
                {
                    return node.op.rfind("port_in", 0) == 0;
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

    // Like expr_is_opaque_read but restricted to port_in* provenance, so the
    // opaque diagnostic can name the right access kind.
    bool expr_is_port_opaque_read(const Expr& e) const
    {
        return std::visit(
            [&](const auto& node) -> bool
            {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, PortIOExpr>)
                {
                    return node.op.rfind("port_in", 0) == 0;
                }
                else if constexpr (std::is_same_v<Node, NameExpr>)
                {
                    return opaque_port_read_vars_.count(node.name) != 0;
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::GroupExpr>)
                {
                    return node.inner != nullptr && expr_is_port_opaque_read(*node.inner);
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::UnaryExpr>)
                {
                    return node.rhs != nullptr && expr_is_port_opaque_read(*node.rhs);
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::BinaryExpr>)
                {
                    if (node.lhs != nullptr && expr_is_port_opaque_read(*node.lhs))
                    {
                        return true;
                    }
                    return node.rhs != nullptr && expr_is_port_opaque_read(*node.rhs);
                }
                return false;
            },
            e.node);
    }

    void add_opaque_read_note(Diagnostic& d, bool is_port)
    {
        Related note;
        note.message = is_port ? "port I/O read is opaque; cannot prove this contract"
                               : "MMIO read is opaque; cannot prove this contract";
        note.span = std::nullopt;
        d.notes.push_back(std::move(note));
    }

    // Hard diagnostic for a contract that mentions an opaque read value
    // (MMIO or port I/O). The build must fail rather than silently accept an
    // unprovable contract.
    void reject_opaque_read_contract(Span span, std::string_view message, bool is_port = false)
    {
        auto d = error_at(span, std::string(message));
        add_opaque_read_note(d, is_port);
        diags_.push_back(std::move(d));
    }

    void add_fact(const curlee::parser::Pred& pred)
    {
        // Opaque read values can never satisfy a contract: reject unconditionally
        // before attempting to lower, so no opaque-derived contract can silently pass.
        if (pred_mentions_opaque_read(pred))
        {
            const bool is_port = pred_mentions_port_opaque_read(pred);
            reject_opaque_read_contract(pred.span,
                                        is_port ? "cannot prove contract on opaque port I/O read value"
                                                : "cannot prove contract on opaque MMIO read value",
                                        is_port);
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
                    // Plain decimal Int literals are accepted directly by Z3's
                    // numeral parser (arbitrary precision): keep them on that
                    // path so literals in [2^63, 2^64-1] stay positive (review
                    // finding, issue #276 — routing them through int64_t
                    // corrupted them to negative). Hex/underscore lexemes
                    // (PhysAddrLiteral forms, e.g. `0x0E` inside a runtime port
                    // offset or a write() value) are not accepted by Z3's
                    // numeral parser, so convert them to a plain decimal
                    // numeral STRING first (issue #276: a runtime port like
                    // `io_base + 0x0E` lowers through here). The decimal
                    // string keeps Z3's arbitrary-precision path; converting
                    // through int64_t would corrupt values in [2^63, 2^64-1].
                    if (!is_plain_decimal_literal(node.lexeme))
                    {
                        const auto decimal = parse_port_literal(node.lexeme);
                        if (decimal.has_value())
                        {
                            char buf[32];
                            const auto [chars_end, chars_ec] =
                                std::to_chars(buf, buf + sizeof(buf), *decimal);
                            if (chars_ec == std::errc{})
                            {
                                const std::string decimal_str(buf, chars_end);
                                return ExprValue{solver_.context().int_val(decimal_str.c_str()),
                                                 TypeKind::Int, true};
                            }
                        }
                    }
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
                    // Bitwise complement: exact two's-complement identity
                    // ~x == -x - 1 over the 64-bit Int model (issue #270).
                    if (node.op == TokenKind::Tilde)
                    {
                        if (rhs.kind != TypeKind::Int &&
                            !curlee::types::is_phys_element_kind(rhs.kind))
                        {
                            return error_at(e.span, "unary '~' expects Int expression");
                        }
                        return ExprValue{(-rhs.expr) - 1, TypeKind::Int, rhs.is_literal};
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

                    // Division: Z3 Euclidean integer division. Matches C
                    // truncating division for non-negative operands (the
                    // supported subset, issue #270).
                    case TokenKind::Slash:
                        if (lhs.kind != TypeKind::Int || rhs.kind != TypeKind::Int)
                        {
                            return error_at(e.span, "'/' expects Int expressions");
                        }
                        return ExprValue{lhs.expr / rhs.expr, TypeKind::Int,
                                         lhs.is_literal && rhs.is_literal};

                    // Modulo: Z3 Euclidean modulo. For non-negative operands it
                    // satisfies the division-remainder identity
                    // a % b == a - (a / b) * b (issue #270), which is an axiom
                    // of the Z3 Int theory and directly provable.
                    case TokenKind::Percent:
                        if (lhs.kind != TypeKind::Int || rhs.kind != TypeKind::Int)
                        {
                            return error_at(e.span, "'%' expects Int expressions");
                        }
                        return ExprValue{z3::mod(lhs.expr, rhs.expr), TypeKind::Int,
                                         lhs.is_literal && rhs.is_literal};

                    // Bitwise operators (issue #270): bit-precise 64-bit model.
                    // Int (and unsigned element kinds, which the verifier treats
                    // as Int-sort values) are lowered through Z3's bit-vector
                    // theory: int2bv(64, x) takes the 64-bit two's-complement
                    // pattern, the bitwise op runs in that domain, and
                    // bv2int(_, signed) interprets the result back as a signed
                    // Int. Right shift is ARITHMETIC (bvashr), per the documented
                    // Int semantics. Shift amounts are taken mod 64 by Z3's bv
                    // semantics; protocol code uses constant small shifts.
                    case TokenKind::Amp:
                    case TokenKind::Pipe:
                    case TokenKind::Caret:
                    case TokenKind::ShiftLeft:
                    case TokenKind::ShiftRight:
                    {
                        const bool lhs_ok =
                            lhs.kind == TypeKind::Int ||
                            curlee::types::is_phys_element_kind(lhs.kind);
                        const bool rhs_ok =
                            rhs.kind == TypeKind::Int ||
                            curlee::types::is_phys_element_kind(rhs.kind);
                        if (!lhs_ok || !rhs_ok)
                        {
                            return error_at(e.span, "bitwise operators expect Int expressions");
                        }
                        constexpr unsigned kWidth = 64;
                        const z3::expr lhs_bv = z3::int2bv(kWidth, lhs.expr);
                        const z3::expr rhs_bv = z3::int2bv(kWidth, rhs.expr);
                        z3::expr result_bv = lhs_bv & rhs_bv;
                        if (node.op == TokenKind::Pipe)
                        {
                            result_bv = lhs_bv | rhs_bv;
                        }
                        else if (node.op == TokenKind::Caret)
                        {
                            result_bv = lhs_bv ^ rhs_bv;
                        }
                        else if (node.op == TokenKind::ShiftLeft)
                        {
                            result_bv = z3::shl(lhs_bv, rhs_bv);
                        }
                        else if (node.op == TokenKind::ShiftRight)
                        {
                            result_bv = z3::ashr(lhs_bv, rhs_bv);
                        }
                        return ExprValue{z3::bv2int(result_bv, true), TypeKind::Int,
                                         lhs.is_literal && rhs.is_literal};
                    }

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
                    const std::string const_name =
                        "phys_" + std::string(node.element_kind) + "_" + std::string(node.lexeme);
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
                        return error_at(e.span,
                                        "missing Phys base or value in write()"); // GCOVR_EXCL_LINE
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
                else if constexpr (std::is_same_v<Node, PortIOExpr>)
                {
                    const std::string_view width = port_width_for_op(node.op);
                    if (width.empty())
                    {
                        return error_at(e.span,
                                        "unknown port I/O builtin '" + std::string(node.op) + "'");
                    }

                    // The port is an extra argument to the uninterpreted
                    // read/write functions (issue #276). Constant literal ports
                    // keep today's behavior: they lower to their integer value
                    // (hex/underscore forms are converted to a decimal numeral
                    // because Z3's rational parser does not accept them).
                    // Runtime ports (a let-bound base plus a constant offset,
                    // the virtio_net.c pattern) lower to an opaque term; reads
                    // stay opaque because no axioms constrain port_in_<b|w|l>.
                    if (node.port == nullptr) // GCOVR_EXCL_LINE
                    {
                        return error_at(e.span, "missing port I/O address"); // GCOVR_EXCL_LINE
                    }
                    z3::expr port_term = solver_.context().int_val(0);
                    if (const auto* lit = std::get_if<curlee::parser::IntExpr>(&node.port->node);
                        lit != nullptr && is_phys_address_literal(lit->lexeme))
                    {
                        const auto port_value = parse_port_literal(lit->lexeme);
                        if (!port_value.has_value())
                        {
                            return error_at(e.span, "port I/O address must be a constant literal");
                        }
                        // int_val(uint64_t) is exact for the full 64-bit range;
                        // narrowing through a signed int would truncate literals
                        // at or above 2^31 (same defect class as the general
                        // IntExpr lowering fix, issue #276 review finding).
                        port_term = solver_.context().int_val(*port_value);
                    }
                    else
                    {
                        auto port_res = lower_expr(*node.port);
                        if (std::holds_alternative<Diagnostic>(port_res))
                        {
                            return std::get<Diagnostic>(std::move(port_res));
                        }
                        auto port_val = std::get<ExprValue>(std::move(port_res));
                        if (port_val.kind != TypeKind::Int)
                        {
                            return error_at(e.span, "port I/O address must be an Int expression");
                        }
                        port_term = port_val.expr;
                    }

                    if (node.op.rfind("port_out", 0) == 0)
                    {
                        // Writes lower to the uninterpreted function
                        // port_out_<b|w|l> : Int x Int -> Unit. No axioms relate
                        // writes to reads, so writes are opaque too.
                        if (node.value == nullptr) // GCOVR_EXCL_LINE
                        {
                            return error_at(e.span, "missing port I/O write value"); // GCOVR_EXCL_LINE
                        }
                        auto value_res = lower_expr(*node.value);
                        if (std::holds_alternative<Diagnostic>(value_res))
                        {
                            return std::get<Diagnostic>(value_res);
                        }
                        auto value = std::get<ExprValue>(value_res);
                        if (value.kind != TypeKind::Int && !is_phys_element_kind(value.kind))
                        {
                            return error_at(e.span,
                                            "port I/O write value must be an integer expression");
                        }
                        // Unit result: represent with a dummy Bool true (Unit is not a Z3 sort).
                        (void)port_out_fn(width)(port_term, value.expr);
                        return ExprValue{solver_.context().bool_val(true), TypeKind::Unit, false};
                    }

                    // Reads lower to the uninterpreted function port_in_<b|w|l> : Int -> Int.
                    // No axioms constrain them, so the returned value is opaque to the solver.
                    return ExprValue{port_in_fn(width)(port_term), TypeKind::Int, false};
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

        // An opaque read value (MMIO or port I/O) must never flow into a contract silently.
        // Check argument provenance before the MVP gate so that a call like consume(reg.read())
        // with a requires-guarded unsigned parameter is a hard error, not a silent skip.
        bool arg_is_opaque = false;
        bool arg_is_port_opaque = false;
        for (const auto& arg : call.args)
        {
            if (expr_is_opaque_read(arg))
            {
                arg_is_opaque = true;
            }
            if (expr_is_port_opaque_read(arg))
            {
                arg_is_port_opaque = true;
            }
        }

        // MVP: only reason about calls whose arguments are entirely Int/Bool.
        for (const auto k : sig.params)
        {
            if (k != TypeKind::Int && k != TypeKind::Bool)
            {
                // The callee has non-scalar parameters (e.g. unsigned element kinds). If an
                // argument derives from an opaque read, any contract on that parameter
                // cannot be proven - fail loudly instead of silently skipping.
                if (arg_is_opaque)
                {
                    // arg_is_opaque is only set when an argument was scanned, so args is
                    // guaranteed non-empty here (the ternary fallback was dead code).
                    reject_opaque_read_contract(
                        call.args[0].span,
                        arg_is_port_opaque
                            ? "cannot prove call contract on opaque port I/O read argument"
                            : "cannot prove call contract on opaque MMIO read argument",
                        arg_is_port_opaque);
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
            // A requires clause mentioning an opaque read value can never be proven;
            // reject unconditionally before lowering.
            if (pred_mentions_opaque_read(req))
            {
                const bool is_port = pred_mentions_port_opaque_read(req);
                reject_opaque_read_contract(
                    req.span,
                    is_port ? "cannot prove requires on opaque port I/O read value"
                            : "cannot prove requires on opaque MMIO read value",
                    is_port);
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
                    const bool is_port = pred_mentions_port_opaque_read(ens);
                    reject_opaque_read_contract(
                        ens.span,
                        is_port ? "cannot prove ensures on opaque port I/O read value"
                                : "cannot prove ensures on opaque MMIO read value",
                        is_port);
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
                else if constexpr (std::is_same_v<Node, PortIOExpr>)
                {
                    // Port I/O is not a function call; scan the port expression
                    // (issue #276: it is a general Int expression) and the out*
                    // value for calls.
                    if (node.port)
                    {
                        check_expr_for_calls(*node.port, nullptr);
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
        const bool opaque_unsigned_return = is_phys_element_kind(expected_return) &&
                                            value.kind == TypeKind::Int &&
                                            expr_is_opaque_read(*s.value);
        if (value.kind != expected_return && !opaque_unsigned_return)
        {
            return;
        }

        // A function returning an opaque read value (MMIO or port I/O) has an
        // unprovable `result`: any ensures clause mentioning it must be rejected
        // with the opaque note, never silently satisfied.
        const bool result_is_opaque = expr_is_opaque_read(*s.value);
        if (result_is_opaque)
        {
            const bool result_is_port = expr_is_port_opaque_read(*s.value);
            for (const auto& ens : func->ensures)
            {
                if (pred_mentions_result(ens))
                {
                    reject_opaque_read_contract(
                        ens.span,
                        result_is_port ? "cannot prove ensures on opaque port I/O read result"
                                       : "cannot prove ensures on opaque MMIO read result",
                        result_is_port);
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

        // Fresh symbol per snapshot SITE, keyed by the source span so the same
        // `ghost let` re-lowered in a different context (e.g. the fuel bound's
        // restricted ctx) yields the SAME solver symbol: two `ghost let x = ...`
        // in different scopes must not share a constant, while the fuel bound
        // and the body's facts must agree on the snapshot's identity. Span
        // start is unique per binding site and stable across re-lowerings.
        const std::string snap_key =
            "ghost_" + std::string(s.name) + "@" + std::to_string(s.value.span.start);
        if (value.kind == TypeKind::Int)
        {
            auto snap = solver_.context().int_const(snap_key.c_str());
            ctx.int_vars.insert_or_assign(s.name, snap);
            facts_.push_back(snap == value.expr);
            return snap;
        }
        if (value.kind == TypeKind::Bool)
        {
            auto snap = solver_.context().bool_const(snap_key.c_str());
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
            // Opaque provenance is transitive: a binding whose initializer derives from a
            // Phys read() or port_in* read (directly or via an alias chain) is itself
            // opaque and must not be contractable.
            if (std::holds_alternative<curlee::parser::PhysReadExpr>(s.value.node) ||
                expr_is_opaque_read(s.value))
            {
                opaque_read_vars_.insert(s.name);
            }
            if (expr_is_port_opaque_read(s.value))
            {
                opaque_port_read_vars_.insert(s.name);
            }
            // A refinement on an opaque read value cannot be proven; surface it as a hard
            // diagnostic with the opaque-value note rather than silently passing.
            if (s.refinement.has_value() && pred_mentions_opaque_read(*s.refinement))
            {
                const bool is_port = pred_mentions_port_opaque_read(*s.refinement);
                reject_opaque_read_contract(
                    s.refinement->span,
                    is_port ? "cannot prove refinement on opaque port I/O read value"
                            : "cannot prove refinement on opaque MMIO read value",
                    is_port);
            }

            // U8/U16/U32/U64 values lower to their Int value in the solver
            // (issues #274/#277): declare an Int-sort symbol and bind it to the
            // initializer, so the read can be bit-tested, compared, and
            // arithmetically widened while its VALUE stays uninterpreted, and a
            // U64 binding (constructed from an Int or literal) is a normal
            // Int-valued binding.
            if (is_widening_unsigned(core_t->kind))
            {
                declare_var(s.name, TypeKind::Int);

                // Unsigned lets never receive a call result (check_call only models
                // Int/Bool callee results, and an unsigned-typed callee would be a
                // front-end type mismatch); the branch is kept for symmetry with the
                // Int let path below.
                std::optional<z3::expr> call_result;
                check_expr_for_calls(s.value, &call_result);
                if (call_result.has_value()) // GCOVR_EXCL_LINE
                {
                    auto sym = fresh_symbol(s.name, TypeKind::Int);
                    lower_ctx_.int_vars.insert_or_assign(s.name, sym);
                    facts_.push_back(sym == *call_result);
                }
                else
                {
                    auto lowered = lower_expr(s.value);
                    if (std::holds_alternative<ExprValue>(lowered))
                    {
                        auto value = std::get<ExprValue>(std::move(lowered));
                        // The opaque read lowers to an Int-sort term; bind the let
                        // to it so later expressions can use the read's *type* (the
                        // value itself stays opaque — contracts still cannot mention
                        // it).
                        if (value.kind == TypeKind::Int) // GCOVR_EXCL_LINE
                        {
                            if (auto it = lower_ctx_.int_vars.find(s.name);
                                it != lower_ctx_.int_vars.end())
                            {
                                facts_.push_back(it->second == value.expr);
                            }
                        }
                    }
                }
            }
            else
            {
                check_expr_for_calls(s.value);
            }
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
            // Fresh symbol per binding (the call result is itself fresh), so a
            // later reassignment of `y` cannot alias the inherited facts.
            if (core_t->kind == TypeKind::Int)
            {
                auto sym = fresh_symbol(s.name, TypeKind::Int);
                lower_ctx_.int_vars.insert_or_assign(s.name, sym);
                facts_.push_back(sym == *call_result);
            }
            else if (core_t->kind == TypeKind::Bool)
            {
                auto sym = fresh_symbol(s.name, TypeKind::Bool);
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
                    if (auto it = lower_ctx_.int_vars.find(s.name); it != lower_ctx_.int_vars.end())
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

    void check_stmt_node(const AssignStmt& s, Span span, TypeKind)
    {
        // `x = expr;` reassigns an existing local binding. The type checker
        // gates targets to assignable local `let` bindings (parameters and
        // ghost snapshots are read-only), so here we only need to model the
        // mutation soundly: lower the RHS (discharging any call requires/
        // ensures obligations), then rebind `x` to a fresh symbol constrained
        // to the RHS value. The OLD symbol remains in the solver's history, so
        // facts referencing it (e.g. a loop `decreases` variant lowered before
        // the assignment) stay valid and the post-mutation invariant/variant
        // re-check is non-vacuous.
        std::optional<z3::expr> rhs_call_result;
        check_expr_for_calls(s.value, &rhs_call_result);

        // Non-scalar bindings (structs/Phys/Unit) are not modeled in the
        // solver fragment; nothing to do beyond the call scan above.
        if (auto it = lower_ctx_.int_vars.find(s.name); it != lower_ctx_.int_vars.end())
        {
            if (rhs_call_result.has_value())
            {
                assign_var(s.name, TypeKind::Int, *rhs_call_result);
            }
            else
            {
                auto lowered = lower_expr(s.value);
                if (std::holds_alternative<ExprValue>(lowered))
                {
                    assign_var(s.name, TypeKind::Int,
                               std::get<ExprValue>(std::move(lowered)).expr);
                }
            }
            return;
        }
        if (auto itb = lower_ctx_.bool_vars.find(s.name); itb != lower_ctx_.bool_vars.end())
        {
            if (rhs_call_result.has_value())
            {
                assign_var(s.name, TypeKind::Bool, *rhs_call_result);
            }
            else
            {
                auto lowered = lower_expr(s.value);
                if (std::holds_alternative<ExprValue>(lowered))
                {
                    assign_var(s.name, TypeKind::Bool,
                               std::get<ExprValue>(std::move(lowered)).expr);
                }
            }
            return;
        }
        (void)span;
    }

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

        // Pre-branch bindings: names the branches reassign are JOINED into the
        // continuation below, so snapshot the entry bindings to fall back to
        // the pre-branch value for a branch that leaves a name untouched (and
        // for the if's not-taken path).
        const auto pre_int = lower_ctx_.int_vars;
        const auto pre_bool = lower_ctx_.bool_vars;

        // Names reassigned in either branch (transitively through nested
        // blocks/ifs/whiles/matches/unsafe) and names SHADOWED by a `let` in a
        // branch. A shadowed name's assignments target the branch-local
        // shadow, so it must NOT be joined: the outer binding is restored by
        // pop_scope and stays unchanged.
        std::unordered_set<std::string_view> assigned_names;
        std::unordered_set<std::string_view> shadowed_names;
        collect_assigned_names(*s.then_block, assigned_names);
        collect_declared_names(*s.then_block, shadowed_names);
        if (s.else_block != nullptr)
        {
            collect_assigned_names(*s.else_block, assigned_names);
            collect_declared_names(*s.else_block, shadowed_names);
        }

        // Post-branch bindings and the facts each branch contributes to the
        // continuation (everything the branch learned EXCEPT its condition
        // assumption, which must not escape: the continuation cannot assume
        // the branch was taken).
        std::unordered_map<std::string_view, z3::expr> post_then_int;
        std::unordered_map<std::string_view, z3::expr> post_then_bool;
        std::unordered_map<std::string_view, z3::expr> post_else_int;
        std::unordered_map<std::string_view, z3::expr> post_else_bool;
        std::vector<z3::expr> retained_facts;

        {
            push_scope();
            const std::size_t branch_facts_start = facts_.size();
            if (cond_fact.has_value())
            {
                facts_.push_back(*cond_fact);
            }
            for (const auto& stmt : s.then_block->stmts)
            {
                check_stmt(stmt, expected_return);
            }
            post_then_int = lower_ctx_.int_vars;
            post_then_bool = lower_ctx_.bool_vars;
            for (std::size_t i = branch_facts_start; i < facts_.size(); ++i)
            {
                if (!cond_fact.has_value() || !z3::eq(facts_[i], *cond_fact))
                {
                    retained_facts.push_back(facts_[i]);
                }
            }
            pop_scope();
        }

        if (s.else_block != nullptr)
        {
            push_scope();
            const std::size_t branch_facts_start = facts_.size();
            const std::optional<z3::expr> neg_cond =
                cond_fact.has_value() ? std::optional<z3::expr>(!*cond_fact) : std::nullopt;
            if (neg_cond.has_value())
            {
                facts_.push_back(*neg_cond);
            }
            for (const auto& stmt : s.else_block->stmts)
            {
                check_stmt(stmt, expected_return);
            }
            post_else_int = lower_ctx_.int_vars;
            post_else_bool = lower_ctx_.bool_vars;
            for (std::size_t i = branch_facts_start; i < facts_.size(); ++i)
            {
                if (!neg_cond.has_value() || !z3::eq(facts_[i], *neg_cond))
                {
                    retained_facts.push_back(facts_[i]);
                }
            }
            pop_scope();
        }

        // Join: re-assert the retained branch facts first (they pin the
        // branch-end symbols to their real values), then rebind each assigned
        // name to a fresh join symbol. The empty post_else maps model the if's
        // not-taken path (the value stays at pre), so
        // `if (c) { x = 1; }` joins to `x' == x_then || x' == x_pre`.
        for (const auto& fact : retained_facts)
        {
            facts_.push_back(fact);
        }
        join_branch_states(pre_int, pre_bool, {post_then_int, post_else_int},
                           {post_then_bool, post_else_bool}, assigned_names, shadowed_names);
    }

    // Collect the set of names a loop body ASSIGNS (reassignment targets),
    // recursively through nested blocks, if/while/match bodies, and unsafe
    // blocks. These are exactly the loop-carried variables: names the body
    // mutates, whose pre-body value the preservation pass must abstract (the
    // standard induction hypothesis) rather than pin to the entry `let` facts.
    static void collect_assigned_names(const curlee::parser::Block& block,
                                       std::unordered_set<std::string_view>& out)
    {
        for (const auto& stmt : block.stmts)
        {
            std::visit(
                [&](const auto& node)
                {
                    using Node = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<Node, AssignStmt>)
                    {
                        out.insert(node.name);
                    }
                    else if constexpr (std::is_same_v<Node, BlockStmt>)
                    {
                        if (node.block != nullptr)
                        {
                            collect_assigned_names(*node.block, out);
                        }
                    }
                    else if constexpr (std::is_same_v<Node, IfStmt>)
                    {
                        collect_assigned_names(*node.then_block, out);
                        if (node.else_block != nullptr)
                        {
                            collect_assigned_names(*node.else_block, out);
                        }
                    }
                    else if constexpr (std::is_same_v<Node, WhileStmt>)
                    {
                        collect_assigned_names(*node.body, out);
                    }
                    else if constexpr (std::is_same_v<Node, MatchStmt>)
                    {
                        for (const auto& arm : node.arms)
                        {
                            if (arm.body != nullptr)
                            {
                                collect_assigned_names(*arm.body, out);
                            }
                        }
                    }
                    else if constexpr (std::is_same_v<Node, UnsafeStmt>)
                    {
                        if (node.body != nullptr)
                        {
                            collect_assigned_names(*node.body, out);
                        }
                    }
                },
                stmt.node);
        }
    }

    // Collect the set of names DECLARED (via `let` or `ghost let`) inside a
    // block, transitively through nested blocks/if/while/match/unsafe bodies.
    // A declaration SHADOWS an outer binding of the same name, so an
    // assignment to that name inside the block targets the branch/loop-local
    // shadow, not the outer binding. The branch/loop joins below skip such
    // names: the outer binding is restored by pop_scope and must not be
    // conflated with the (scoped) shadow's value.
    static void collect_declared_names(const curlee::parser::Block& block,
                                       std::unordered_set<std::string_view>& out)
    {
        for (const auto& stmt : block.stmts)
        {
            std::visit(
                [&](const auto& node)
                {
                    using Node = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<Node, LetStmt>)
                    {
                        out.insert(node.name);
                    }
                    else if constexpr (std::is_same_v<Node, GhostLetStmt>)
                    {
                        out.insert(node.name);
                    }
                    else if constexpr (std::is_same_v<Node, BlockStmt>)
                    {
                        if (node.block != nullptr)
                        {
                            collect_declared_names(*node.block, out);
                        }
                    }
                    else if constexpr (std::is_same_v<Node, IfStmt>)
                    {
                        collect_declared_names(*node.then_block, out);
                        if (node.else_block != nullptr)
                        {
                            collect_declared_names(*node.else_block, out);
                        }
                    }
                    else if constexpr (std::is_same_v<Node, WhileStmt>)
                    {
                        collect_declared_names(*node.body, out);
                    }
                    else if constexpr (std::is_same_v<Node, MatchStmt>)
                    {
                        for (const auto& arm : node.arms)
                        {
                            if (arm.body != nullptr)
                            {
                                collect_declared_names(*arm.body, out);
                            }
                        }
                    }
                    else if constexpr (std::is_same_v<Node, UnsafeStmt>)
                    {
                        if (node.body != nullptr)
                        {
                            collect_declared_names(*node.body, out);
                        }
                    }
                },
                stmt.node);
        }
    }

    // Join the post-branch states of a multi-way branch (if/else with an
    // optional not-taken path, match arms) into the continuation. Each name
    // the branches reassign is rebound to a fresh symbol constrained to be one
    // of the branch-end values, falling back to the PRE-branch value for any
    // branch that leaves the name untouched (an empty `post_int`/`post_bool`
    // entry models the if's not-taken path). Names shadowed by a `let` inside
    // a branch are skipped: their assignments target the shadow, and the outer
    // binding is restored by the scope pop (conflating them would leak the
    // branch-local shadow into the continuation).
    //
    // `post_int`/`post_bool` are the per-branch END bindings (one entry per
    // branch; an empty map means the branch never rebinds any scalar). The
    // retained branch facts (re-asserted by the caller before this runs) pin
    // the branch-end symbols to their real values, so the continuation can
    // prove disjunctive facts (`ensures result == 0 || result == 1`) but
    // cannot assume a specific branch value. This closes the #268
    // branch-mutation gap where the continuation saw the PRE-branch binding
    // (pinned by its `let` facts) and both accepted false postconditions and
    // failed true ones.
    void join_branch_states(const std::unordered_map<std::string_view, z3::expr>& pre_int,
                            const std::unordered_map<std::string_view, z3::expr>& pre_bool,
                            const std::vector<std::unordered_map<std::string_view, z3::expr>>& post_int,
                            const std::vector<std::unordered_map<std::string_view, z3::expr>>& post_bool,
                            const std::unordered_set<std::string_view>& assigned_names,
                            const std::unordered_set<std::string_view>& shadowed_names)
    {
        for (const auto& name : assigned_names)
        {
            if (shadowed_names.count(name) != 0)
            {
                continue;
            }
            if (auto pre = pre_int.find(name); pre != pre_int.end())
            {
                auto joined = fresh_symbol(name, TypeKind::Int);
                z3::expr disj = solver_.context().bool_val(false);
                bool any_new = false;
                for (const auto& post : post_int)
                {
                    auto it = post.find(name);
                    const z3::expr& sym = it != post.end() ? it->second : pre->second;
                    if (!z3::eq(sym, pre->second))
                    {
                        any_new = true;
                    }
                    disj = disj || (joined == sym);
                }
                if (!any_new)
                {
                    continue;
                }
                lower_ctx_.int_vars.insert_or_assign(name, joined);
                facts_.push_back(disj);
            }
            else if (auto preb = pre_bool.find(name); preb != pre_bool.end())
            {
                auto joined = fresh_symbol(name, TypeKind::Bool);
                z3::expr disj = solver_.context().bool_val(false);
                bool any_new = false;
                for (const auto& post : post_bool)
                {
                    auto it = post.find(name);
                    const z3::expr& sym = it != post.end() ? it->second : preb->second;
                    if (!z3::eq(sym, preb->second))
                    {
                        any_new = true;
                    }
                    disj = disj || (joined == sym);
                }
                if (!any_new)
                {
                    continue;
                }
                lower_ctx_.bool_vars.insert_or_assign(name, joined);
                facts_.push_back(disj);
            }
        }
    }

    // Lower a loop contract clause into a Z3 term with the opaque-read
    // rejection applied first (an opaque read value can never satisfy a
    // contract). `int_clause` selects the Int-valued lowering used by
    // `decreases` variant expressions.
    std::optional<z3::expr> lower_loop_clause(const curlee::parser::Pred& pred,
                                              bool int_clause = false)
    {
        if (pred_mentions_opaque_read(pred))
        {
            const bool is_port = pred_mentions_port_opaque_read(pred);
            reject_opaque_read_contract(pred.span,
                                        is_port ? "cannot prove contract on opaque port I/O read value"
                                                : "cannot prove contract on opaque MMIO read value",
                                        is_port);
            return std::nullopt;
        }
        auto lowered =
            int_clause ? lower_predicate_int(pred, lower_ctx_) : lower_predicate(pred, lower_ctx_);
        if (std::holds_alternative<Diagnostic>(lowered))
        {
            diags_.push_back(std::get<Diagnostic>(std::move(lowered)));
            return std::nullopt;
        }
        return std::get<z3::expr>(std::move(lowered));
    }

    void check_stmt_node(const WhileStmt& s, Span, TypeKind expected_return)
    {
        check_expr_for_calls(s.cond);

        // Record the loop-entry lowering context so the static fuel cost model
        // can lower the `decreases` variant (D_0) against the PRE-loop
        // bindings. Assignments inside the body rebind loop-carried names to
        // fresh symbols; without this snapshot the fuel obligation would use a
        // post-mutation symbol for D_0 and spuriously fail a genuinely bounded
        // loop (docs/fuel-contracts.md's `(D_0 + 1) * (cost(c) + cost(B))`
        // formula). Keyed by this statement's address, which is stable and
        // shared with the cost model's AST walk.
        // emplace (not insert_or_assign): LoweringContext has a reference
        // member, so it is not copy-assignable; each WhileStmt is recorded
        // exactly once per function check.
        loop_entry_ctxs_.emplace(&s, lower_ctx_);

        const bool has_contract = !s.invariants.empty() || s.decreases.has_value();

        // Loop-carried names: names the body ASSIGNS (reassignment targets,
        // transitively through nested blocks/ifs/whiles/matches/unsafe) and
        // names SHADOWED by a `let` in the body. The post-loop continuation
        // carries the POST-body bindings of the former forward; the latter's
        // assignments target the loop-local shadow, so the outer binding is
        // restored by pop_scope and must not be conflated with the shadow.
        std::unordered_set<std::string_view> assigned_names;
        std::unordered_set<std::string_view> shadowed_names;
        collect_assigned_names(*s.body, assigned_names);
        collect_declared_names(*s.body, shadowed_names);

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

        // Loop without an explicit contract block: legacy single-pass mode.
        // The body is checked exactly once with the loop condition assumed as
        // a fact, then everything inside is discarded. The pre-loop bindings
        // remain EXCEPT for names the body assigns: their post-loop values are
        // unknown (there is no invariant to constrain them), so each is
        // rebound to a fresh unconstrained symbol. Otherwise
        // `let x = 0; while (c) { x = x + 1; }` would leave `x` pinned to its
        // entry `let` fact in the continuation and verify false postconditions
        // vacuously. This matches the historical behavior for loops that do
        // not assign (e.g. kernel infinite loops) and keeps them verifying
        // unchanged. The single-pass fallback is documented in
        // docs/loop-contracts.md.
        if (!has_contract)
        {
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
            for (const auto& name : assigned_names)
            {
                if (shadowed_names.count(name) != 0)
                {
                    continue;
                }
                if (auto it = lower_ctx_.int_vars.find(name);
                    it != lower_ctx_.int_vars.end())
                {
                    it->second = fresh_symbol(name, TypeKind::Int);
                }
                else if (auto itb = lower_ctx_.bool_vars.find(name);
                         itb != lower_ctx_.bool_vars.end())
                {
                    itb->second = fresh_symbol(name, TypeKind::Bool);
                }
            }
            return;
        }

        // ----- Loop with a contract block: sound loop-invariant reasoning -----
        //
        // Issue #268 (assignment) makes loop-carried mutation expressible, so
        // the preservation pass is the real workhorse here. Per loop:
        //
        //  1. ENTRY: each invariant must hold at loop entry (proved from the
        //     pre-loop facts, not assumed).
        //  2. PRESERVATION: for ARBITRARY state satisfying invariant && cond,
        //     one body pass must re-establish each invariant. The body's
        //     ASSIGNMENTS (loop-carried mutation) are modeled via fresh
        //     symbols, and the loop-carried variables are ABSTRACTED before
        //     the body runs: each name the body assigns is rebound to a fresh
        //     unconstrained symbol, so the proof covers every reachable
        //     iteration, not just the first (the entry `let` facts would
        //     otherwise pin the pre-body value and limit the proof to the
        //     first-iteration path). The invariant/cond facts assumed for the
        //     body are lowered against these abstracted symbols.
        //  3. VARIANT: `decreases(D)` must be non-negative at entry (D >= 0,
        //     checked against the real entry state) and each iteration must
        //     strictly decrease it: D_after < D_before where D_before is the
        //     variant in the abstracted pre-iteration state.
        //  4. POST-STATE: after the loop, invariant && !cond become facts for
        //     the continuation (partial-correctness post-state; total
        //     correctness is established by the variant obligation).

        // Entry invariants: prove against pre-loop facts (checked, not assumed).
        for (const auto& inv : s.invariants)
        {
            auto lowered_inv = lower_loop_clause(inv);
            if (lowered_inv.has_value())
            {
                check_obligation(inv, lower_ctx_, *lowered_inv, inv.span, {},
                                 "loop invariant does not hold at loop entry");
            }
        }

        // Variant well-foundedness at entry: D >= 0 (real entry state).
        if (s.decreases.has_value())
        {
            auto lowered_v = lower_loop_clause(*s.decreases, /*int_clause=*/true);
            if (lowered_v.has_value())
            {
                const auto& d = *s.decreases;
                check_obligation(d, lower_ctx_, *lowered_v >= 0, d.span, {},
                                 "decreases expression must be non-negative at loop entry");
            }
        }

        // Preservation pass: abstract the loop-carried variables, assume
        // invariant && cond, run the body once, then obligation-check each
        // invariant and the variant decrease at the end. The push_scope
        // isolates the body's bindings and facts; the pop below restores the
        // pre-loop state (post-state facts are re-added after).
        //
        // `post_body_int`/`post_body_bool` capture the POST-body bindings of
        // the loop-carried names so the continuation can carry them forward
        // (the pop restores the pre-loop bindings).
        std::unordered_map<std::string_view, z3::expr> post_body_int;
        std::unordered_map<std::string_view, z3::expr> post_body_bool;
        {
            push_scope();

            // Abstract the loop-carried variables: names the body ASSIGNS are
            // rebound to fresh symbols constrained only by the invariant/cond
            // facts assumed below. This makes the preservation proof quantify
            // over every reachable iteration state (the standard partial-
            // correctness induction), instead of the specific entry values the
            // `let` facts would pin. Names the body never assigns are
            // loop-invariant and keep their facts.
            for (const auto& name : assigned_names)
            {
                // insert_or_assign-style: z3::expr is not default-constructible,
                // so rebind through the found iterator rather than operator[].
                if (auto it = lower_ctx_.int_vars.find(name);
                    it != lower_ctx_.int_vars.end())
                {
                    it->second = fresh_symbol(name, TypeKind::Int);
                }
                else if (auto itb = lower_ctx_.bool_vars.find(name);
                         itb != lower_ctx_.bool_vars.end())
                {
                    itb->second = fresh_symbol(name, TypeKind::Bool);
                }
            }

            // Assume the invariants (facts) for the body, lowered against the
            // abstracted loop-carried symbols.
            for (const auto& inv : s.invariants)
            {
                auto lowered_inv = lower_loop_clause(inv);
                if (lowered_inv.has_value())
                {
                    facts_.push_back(*lowered_inv);
                }
            }
            // The loop condition must constrain the ABSTRACTED symbols too:
            // cond_fact was lowered at loop entry against the real bindings,
            // so re-lower it here (the post-state still uses the entry-level
            // cond_fact below, which is correct there).
            {
                auto lowered_cond = lower_expr(s.cond);
                if (std::holds_alternative<ExprValue>(lowered_cond))
                {
                    auto cond_value = std::get<ExprValue>(std::move(lowered_cond));
                    if (cond_value.kind == TypeKind::Bool)
                    {
                        facts_.push_back(cond_value.expr);
                    }
                }
            }

            // D_before for the decrease obligation: the variant evaluated in
            // the abstracted PRE-iteration state.
            std::optional<z3::expr> variant_before;
            if (s.decreases.has_value())
            {
                auto lowered_v = lower_loop_clause(*s.decreases, /*int_clause=*/true);
                if (lowered_v.has_value())
                {
                    variant_before = *lowered_v;
                }
            }

            for (const auto& stmt : s.body->stmts)
            {
                check_stmt(stmt, expected_return);
            }

            // Preservation obligations: the invariant must hold again at the
            // end of one body pass.
            for (const auto& inv : s.invariants)
            {
                auto lowered_inv = lower_loop_clause(inv);
                if (lowered_inv.has_value())
                {
                    check_obligation(inv, lower_ctx_, *lowered_inv, inv.span, {},
                                     "loop invariant is not preserved by an iteration");
                }
            }

            // Variant decrease obligation: D_after < D_before.
            if (s.decreases.has_value() && variant_before.has_value())
            {
                auto lowered_v = lower_loop_clause(*s.decreases, /*int_clause=*/true);
                if (lowered_v.has_value())
                {
                    const auto& d = *s.decreases;
                    check_obligation(
                        d, lower_ctx_, *lowered_v < *variant_before, d.span, {},
                        "decreases expression does not strictly decrease each iteration");
                }
            }

            // Capture the POST-body bindings of the loop-carried names so the
            // continuation below can carry them forward (the pop restores the
            // pre-loop bindings). These symbols are unconstrained after the pop
            // (the body's assignment facts are scoped), so the invariant/!cond
            // facts re-added below are the only constraints on them — exactly
            // the partial-correctness post-state of the loop.
            for (const auto& name : assigned_names)
            {
                if (auto it = lower_ctx_.int_vars.find(name);
                    it != lower_ctx_.int_vars.end())
                {
                    post_body_int.insert_or_assign(name, it->second);
                }
                else if (auto itb = lower_ctx_.bool_vars.find(name);
                         itb != lower_ctx_.bool_vars.end())
                {
                    post_body_bool.insert_or_assign(name, itb->second);
                }
            }

            pop_scope();
        }

        // Post-state: the loop exits with invariant && !cond as facts in the
        // continuation, and the loop-carried names rebound to their POST-BODY
        // symbols (the values after the final iteration). pop_scope above
        // restored the PRE-loop bindings, so carry the post-body bindings of
        // the loop-carried names forward, then lower the invariant/!cond facts
        // against those post-loop symbols. Without this the continuation would
        // see the pre-loop value (e.g. `total` still pinned to its entry `let`
        // fact) and BOTH accept false postconditions about loop-carried values
        // and fail true ones — the finding that reopened #268. Names shadowed
        // by a `let` inside the body are skipped: their assignments target the
        // shadow, and the outer binding is unchanged by the loop.
        for (const auto& name : assigned_names)
        {
            if (shadowed_names.count(name) != 0)
            {
                continue;
            }
            if (auto it = post_body_int.find(name); it != post_body_int.end())
            {
                lower_ctx_.int_vars.insert_or_assign(name, it->second);
            }
            else if (auto itb = post_body_bool.find(name); itb != post_body_bool.end())
            {
                lower_ctx_.bool_vars.insert_or_assign(name, itb->second);
            }
        }

        for (const auto& inv : s.invariants)
        {
            auto lowered_inv = lower_loop_clause(inv);
            if (lowered_inv.has_value())
            {
                facts_.push_back(*lowered_inv);
            }
        }
        // The negated condition must constrain the POST-LOOP symbols too: the
        // entry-level cond_fact references the pre-loop bindings and would
        // leave the post-loop variant value unconstrained (e.g. `i` staying
        // below `n`, making `ensures result == n` unprovable).
        {
            auto lowered_cond = lower_expr(s.cond);
            if (std::holds_alternative<ExprValue>(lowered_cond))
            {
                auto cond_value = std::get<ExprValue>(std::move(lowered_cond));
                if (cond_value.kind == TypeKind::Bool)
                {
                    facts_.push_back(!cond_value.expr);
                }
            }
        }
    }

    void check_stmt_node(const MatchStmt& s, Span, TypeKind expected_return)
    {
        check_expr_for_calls(s.value);

        // Match arms are branches: names they reassign are JOINED into the
        // continuation (same as if/else). Snapshot the pre-match bindings and
        // collect the names reassigned / shadowed across any arm.
        const auto pre_int = lower_ctx_.int_vars;
        const auto pre_bool = lower_ctx_.bool_vars;
        std::unordered_set<std::string_view> assigned_names;
        std::unordered_set<std::string_view> shadowed_names;
        for (const auto& arm : s.arms)
        {
            if (arm.body != nullptr)
            {
                collect_assigned_names(*arm.body, assigned_names);
                collect_declared_names(*arm.body, shadowed_names);
            }
        }

        // Per-arm post bindings and the facts each arm contributes to the
        // continuation (every fact the arm learned: match arms carry no
        // condition assumption to filter).
        std::vector<std::unordered_map<std::string_view, z3::expr>> post_int(s.arms.size());
        std::vector<std::unordered_map<std::string_view, z3::expr>> post_bool(s.arms.size());
        std::vector<z3::expr> retained_facts;

        for (std::size_t arm_idx = 0; arm_idx < s.arms.size(); ++arm_idx)
        {
            const auto& arm = s.arms[arm_idx];
            push_scope();
            const std::size_t arm_facts_start = facts_.size();
            if (arm.body != nullptr)
            {
                for (const auto& stmt : arm.body->stmts)
                {
                    check_stmt(stmt, expected_return);
                }
            }
            post_int[arm_idx] = lower_ctx_.int_vars;
            post_bool[arm_idx] = lower_ctx_.bool_vars;
            for (std::size_t i = arm_facts_start; i < facts_.size(); ++i)
            {
                retained_facts.push_back(facts_[i]);
            }
            pop_scope();
        }

        // Join the arm post-states: re-assert the retained arm facts, then
        // rebind each assigned name to a fresh symbol constrained to be one of
        // the arm-end values (arms that leave the name untouched contribute
        // the pre-match value).
        for (const auto& fact : retained_facts)
        {
            facts_.push_back(fact);
        }
        join_branch_states(pre_int, pre_bool, post_int, post_bool, assigned_names,
                           shadowed_names);
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
                d.notes.push_back(Related{.message =
                                              "extern declaration carries contracts that are "
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
        loop_entry_ctxs_.clear();
        fresh_counter_ = 0;

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

        // Static fuel (WCET) obligation: if the function declares a `fuel`
        // bound, the symbolic cost of the body must not exceed it. The cost
        // model lowers the body into a Z3 Int cost expression (calls to
        // fuel-annotated callees propagate their declared costs via the
        // call-graph table); loops must carry a provable `decreases` variant
        // (fail closed otherwise). Runs while the parameter bindings are still
        // in scope so the bound and body expressions resolve.
        //
        // Extern functions are excluded: their `fuel` bound is a *trusted* cost
        // annotation for the opaque body (used at call sites), not a proof
        // obligation against a body that does not exist.
        if (f.fuel_bound.has_value() && !f.is_extern)
        {
            check_fuel_obligation(f, sig_it->second);
        }

        pop_scope();
        current_function_.reset();
    }

    // Check a function's static fuel (WCET) obligation: symbolic cost of the
    // body must be <= the declared `fuel` bound. Uses the standard solver
    // obligation path (facts + !obligation); SAT yields a WCET-violating input
    // assignment with a model note. Fail-closed diagnostics (loop without
    // `decreases`, callee without a declared bound) are reported by the cost
    // model and surfaced as errors.
    void check_fuel_obligation(const Function& f, const FunctionSig& sig)
    {
        const auto& bound_pred = *f.fuel_bound;

        // Build a RESTRICTED lowering context for the DECLARED FUEL BOUND only:
        // parameters + contract-block ghost snapshots are in scope; local `let`
        // bindings from the body are NOT. This prevents a fuel clause from
        // silently depending on a local binding whose value the solver can shift
        // to weaken the obligation.
        LoweringContext fuel_ctx(solver_.context());
        fuel_ctx.ghost_fns = &ghost_fns_;
        for (std::size_t i = 0; i < sig.params.size() && i < f.params.size(); ++i)
        {
            const auto& param = f.params[i];
            const auto kind = sig.params[i];
            // Reuse the FRESH symbols the body check bound for each parameter
            // (lower_ctx_ is still live: the fuel check runs inside the
            // function scope). Creating a plain-named constant here would give
            // the bound a solver symbol UNRELATED to the one the cost/facts
            // reference, making every input-dependent bound unconstrained and
            // the fuel obligation spuriously fail.
            if (kind == TypeKind::Int)
            {
                auto it = lower_ctx_.int_vars.find(param.name);
                if (it != lower_ctx_.int_vars.end())
                {
                    fuel_ctx.int_vars.emplace(param.name, it->second);
                }
                else // GCOVR_EXCL_LINE
                {
                    auto sym = solver_.context().int_const(std::string(param.name).c_str());
                    fuel_ctx.int_vars.emplace(param.name, sym);
                }
            }
            else if (kind == TypeKind::Bool)
            {
                auto it = lower_ctx_.bool_vars.find(param.name);
                if (it != lower_ctx_.bool_vars.end())
                {
                    fuel_ctx.bool_vars.emplace(param.name, it->second);
                }
                else // GCOVR_EXCL_LINE
                {
                    auto sym = solver_.context().bool_const(std::string(param.name).c_str());
                    fuel_ctx.bool_vars.emplace(param.name, sym);
                }
            }
        }
        for (const auto& g : f.ghost_lets)
        {
            auto saved_int = std::move(lower_ctx_.int_vars);
            auto saved_bool = std::move(lower_ctx_.bool_vars);
            lower_ctx_.int_vars = fuel_ctx.int_vars;
            lower_ctx_.bool_vars = fuel_ctx.bool_vars;
            lower_ghost_let(g, fuel_ctx);
            lower_ctx_.int_vars = std::move(saved_int);
            lower_ctx_.bool_vars = std::move(saved_bool);
        }

        // Lower the declared bound against the restricted scope.
        auto lowered_bound = lower_predicate_int_named(bound_pred, fuel_ctx, "fuel");
        if (std::holds_alternative<Diagnostic>(lowered_bound))
        {
            diags_.push_back(std::get<Diagnostic>(std::move(lowered_bound)));
            return;
        }

        // Compute the symbolic cost of the body against the FULL lowering
        // context: the cost walker needs the loop-entry bindings (local lets in
        // scope at each WhileStmt) to lower the `decreases` variant soundly. The
        // shadowing rejection in the cost model guards against a vacuous variant;
        // call arguments legitimately reference local lets. The recorded
        // loop-entry contexts let each `while`'s variant lower against its
        // PRE-loop bindings (assignment inside the body rebinds names to fresh
        // symbols; see loop_entry_ctxs_).
        auto cost_res = cost_model_.cost_function(f, lower_ctx_, fuel_table_,
                                                  &loop_entry_ctxs_);
        if (std::holds_alternative<Diagnostic>(cost_res))
        {
            diags_.push_back(std::get<Diagnostic>(std::move(cost_res)));
            return;
        }
        const z3::expr total_cost = std::get<z3::expr>(cost_res);

        // Obligation: total_cost <= bound. Fail when a model satisfies
        // total_cost > bound (i.e. the bound is too low).
        solver_.push();
        for (const auto& fact : facts_)
        {
            solver_.add(fact);
        }
        solver_.add(total_cost > std::get<z3::expr>(lowered_bound));
        const auto res = solver_.check();

        if (res == CheckResult::Sat)
        {
            Diagnostic d =
                error_at(f.span, "fuel bound may be exceeded: worst-case cost exceeds the "
                                 "declared fuel '" +
                                     pred_to_string(bound_pred) + "'");
            Related goal;
            goal.message = "goal: total_cost <= " + pred_to_string(bound_pred);
            goal.span = std::nullopt;
            d.notes.push_back(std::move(goal));

            // Model note over the input variables mentioned in the bound/body.
            std::vector<z3::expr> model_vars;
            std::unordered_set<std::string_view> names;
            collect_pred_names(bound_pred, names);
            for (const auto& name : names)
            {
                if (auto it = lower_ctx_.int_vars.find(name); it != lower_ctx_.int_vars.end())
                {
                    model_vars.push_back(it->second);
                }
                else if (auto it2 = lower_ctx_.bool_vars.find(name);
                         it2 != lower_ctx_.bool_vars.end())
                {
                    model_vars.push_back(it2->second);
                }
            }
            add_model_note(d, model_vars);

            Related hint;
            hint.message = "hint: raise the fuel bound or reduce the worst-case cost (e.g. add a "
                           "'decreases' variant to loops, or a lower declared bound on callees)";
            hint.span = std::nullopt;
            d.notes.push_back(std::move(hint));
            diags_.push_back(std::move(d));
        }
        else if (res == CheckResult::Unknown)
        {
            Diagnostic d =
                error_at(f.span, "fuel bound could not be checked (solver returned unknown)");
            Related hint;
            hint.message =
                "hint: simplify the cost expression to keep it in the decidable fragment";
            hint.span = std::nullopt;
            d.notes.push_back(std::move(hint));
            diags_.push_back(std::move(d));
        }

        solver_.pop();
        (void)sig;
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
