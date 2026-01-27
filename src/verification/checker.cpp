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
using curlee::parser::IfStmt;
using curlee::parser::LetStmt;
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
            else if constexpr (std::is_same_v<Node, curlee::parser::PredName>)
            {
                return std::string(node.name);
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

struct FunctionSig
{
    const Function* decl = nullptr;
    std::vector<TypeKind> params;
    TypeKind result = TypeKind::Unit;
};

struct ScopeState
{
    std::unordered_map<std::string_view, z3::expr> int_vars;
    std::unordered_map<std::string_view, z3::expr> bool_vars;
    std::size_t facts_size = 0;
};

class Verifier
{
  public:
    explicit Verifier(const curlee::types::TypeInfo& type_info)
        : type_info_(type_info), solver_(), lower_ctx_(solver_.context())
    {
    }

    VerificationResult run(const curlee::parser::Program& program)
    {
        collect_signatures(program);

        for (const auto& f : program.functions)
        {
            check_function(f);
        }

        if (!diags_.empty())
        {
            return diags_;
        }
        return Verified{};
    }

  private:
    const curlee::types::TypeInfo& type_info_;
    Solver solver_;
    LoweringContext lower_ctx_;
    std::vector<Diagnostic> diags_;
    std::vector<z3::expr> facts_;
    std::vector<ScopeState> scopes_;
    std::unordered_map<std::string_view, FunctionSig> functions_;

    void push_scope()
    {
        scopes_.push_back(ScopeState{.int_vars = lower_ctx_.int_vars,
                                     .bool_vars = lower_ctx_.bool_vars,
                                     .facts_size = facts_.size()});
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
        if (facts_.size() > state.facts_size)
        {
            facts_.erase(facts_.begin() + static_cast<std::ptrdiff_t>(state.facts_size),
                         facts_.end());
        }
    }

    std::optional<TypeKind> supported_type(const curlee::parser::TypeName& name)
    {
        auto t = curlee::types::core_type_from_name(name.name);
        if (!t.has_value())
        {
            diags_.push_back(error_at(name.span, "unknown type '" + std::string(name.name) + "'"));
            return std::nullopt;
        }

        if (t->kind == TypeKind::Int || t->kind == TypeKind::Bool)
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
                functions_.emplace(f.name, std::move(sig));
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

    void add_fact(const curlee::parser::Pred& pred)
    {
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
                        if (lhs.kind != TypeKind::Int || rhs.kind != TypeKind::Int)
                        {
                            return error_at(e.span, "comparison expects Int expressions");
                        }
                        switch (node.op)
                        {
                        case TokenKind::Less:
                            return ExprValue{lhs.expr < rhs.expr, TypeKind::Bool, false};
                        case TokenKind::LessEqual:
                            return ExprValue{lhs.expr <= rhs.expr, TypeKind::Bool, false};
                        case TokenKind::Greater:
                            return ExprValue{lhs.expr > rhs.expr, TypeKind::Bool, false};
                        case TokenKind::GreaterEqual:
                            return ExprValue{lhs.expr >= rhs.expr, TypeKind::Bool, false};
                        default:
                            break;
                        }
                        break;

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

    void check_call(const CallExpr& call)
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
            else if (sig.params[i] == TypeKind::Bool)
            {
                auto sym = solver_.context().bool_const(sym_name.c_str());
                call_ctx.bool_vars.emplace(param.name, sym);
                call_facts.push_back(sym == arg_exprs[i]);
            }
        }

        for (const auto& req : sig.decl->requires_clauses)
        {
            auto lowered = lower_predicate(req, call_ctx);
            if (std::holds_alternative<Diagnostic>(lowered))
            {
                diags_.push_back(std::get<Diagnostic>(std::move(lowered)));
                continue;
            }

            check_obligation(req, call_ctx, std::get<z3::expr>(lowered), req.span, call_facts,
                             "requires clause not satisfied");
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

    void check_expr_for_calls(const Expr& e)
    {
        std::visit(
            [&](const auto& node)
            {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, CallExpr>)
                {
                    if (!is_python_ffi_call(node))
                    {
                        check_call(node);
                    }
                    for (const auto& arg : node.args)
                    {
                        check_expr_for_calls(arg);
                    }
                }
                else if constexpr (std::is_same_v<Node, MemberExpr>)
                {
                    if (node.base)
                    {
                        check_expr_for_calls(*node.base);
                    }
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::UnaryExpr>)
                {
                    check_expr_for_calls(*node.rhs);
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::BinaryExpr>)
                {
                    check_expr_for_calls(*node.lhs);
                    check_expr_for_calls(*node.rhs);
                }
                else if constexpr (std::is_same_v<Node, curlee::parser::GroupExpr>)
                {
                    check_expr_for_calls(*node.inner);
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
        if (value.kind != expected_return)
        {
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
        else if (expected_return == TypeKind::Bool)
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

    void check_stmt_node(const LetStmt& s, Span, TypeKind)
    {
        auto var_type = supported_type(s.type);
        if (!var_type.has_value())
        {
            return;
        }

        declare_var(s.name, *var_type);

        if (s.refinement.has_value())
        {
            add_fact(*s.refinement);
        }

        check_expr_for_calls(s.value);
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

    void check_stmt_node(const WhileStmt& s, Span, TypeKind expected_return)
    {
        check_expr_for_calls(s.cond);

        push_scope();
        for (const auto& stmt : s.body->stmts)
        {
            check_stmt(stmt, expected_return);
        }
        pop_scope();
    }

    void check_function(const Function& f)
    {
        auto sig_it = functions_.find(f.name);
        if (sig_it == functions_.end())
        {
            return;
        }

        current_function_ = sig_it->second;
        lower_ctx_.result_int.reset();
        lower_ctx_.result_bool.reset();
        lower_ctx_.int_vars.clear();
        lower_ctx_.bool_vars.clear();
        facts_.clear();
        scopes_.clear();

        push_scope();
        for (std::size_t i = 0; i < f.params.size(); ++i)
        {
            const auto& param = f.params[i];
            const auto param_kind = sig_it->second.params[i];
            declare_var(param.name, param_kind);

            if (param.refinement.has_value())
            {
                add_fact(*param.refinement);
            }
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
};

} // namespace

VerificationResult verify(const curlee::parser::Program& program,
                          const curlee::types::TypeInfo& type_info)
{
    Verifier verifier(type_info);
    return verifier.run(program);
}

} // namespace curlee::verification
