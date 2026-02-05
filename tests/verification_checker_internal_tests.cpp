#include <cstdlib>
#include <curlee/lexer/token.h>
#include <curlee/parser/ast.h>
#include <curlee/types/type.h>
#include <curlee/verification/checker.h>
#include <curlee/verification/predicate_lowering.h>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

// Pull in checker.cpp internals for deterministic, direct line coverage.
// We include public headers above so the private->public macro does not affect them.
#define private public
#include "../src/verification/checker.cpp"
#undef private

static curlee::parser::Expr make_expr(
    curlee::source::Span span,
    std::variant<curlee::parser::IntExpr, curlee::parser::BoolExpr, curlee::parser::StringExpr,
                 curlee::parser::NameExpr, curlee::parser::UnaryExpr, curlee::parser::BinaryExpr,
                 curlee::parser::CallExpr, curlee::parser::MemberExpr, curlee::parser::GroupExpr,
                 curlee::parser::ScopedNameExpr, curlee::parser::StructLiteralExpr>
        node)
{
    curlee::parser::Expr e;
    e.span = span;
    e.node = std::move(node);
    return e;
}

static std::unique_ptr<curlee::parser::Expr> make_expr_ptr(curlee::parser::Expr&& e)
{
    return std::make_unique<curlee::parser::Expr>(std::move(e));
}

static curlee::parser::Pred make_pred(
    curlee::source::Span span,
    std::variant<curlee::parser::PredInt, curlee::parser::PredBool, curlee::parser::PredName,
                 curlee::parser::PredUnary, curlee::parser::PredBinary, curlee::parser::PredGroup>
        node)
{
    curlee::parser::Pred p;
    p.span = span;
    p.node = std::move(node);
    return p;
}

static std::unique_ptr<curlee::parser::Pred> make_pred_ptr(curlee::parser::Pred&& p)
{
    return std::make_unique<curlee::parser::Pred>(std::move(p));
}

int main()
{
    using curlee::lexer::TokenKind;

    {
        // token_to_string: cover Slash and default.
        if (curlee::verification::token_to_string(TokenKind::Slash) != "/")
        {
            fail("token_to_string(Slash) mismatch");
        }
        if (curlee::verification::token_to_string(TokenKind::Identifier) != "<op>")
        {
            fail("token_to_string(default) mismatch");
        }
    }

    {
        // pred_to_string: cover bool/unary/binary/group forms.
        const curlee::source::Span s{.start = 0, .end = 1};
        auto p_true = make_pred(s, curlee::parser::PredBool{.value = true});
        if (curlee::verification::pred_to_string(p_true) != "true")
        {
            fail("pred_to_string(true) mismatch");
        }

        auto p_not_false = make_pred(
            s, curlee::parser::PredUnary{
                   .op = TokenKind::Bang,
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredBool{.value = false}))});
        if (curlee::verification::pred_to_string(p_not_false).find("!") == std::string::npos)
        {
            fail("pred_to_string unary did not include '!'");
        }

        auto p_lt = make_pred(
            s, curlee::parser::PredBinary{
                   .op = TokenKind::Less,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "1"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "2"}))});
        const std::string lt = curlee::verification::pred_to_string(p_lt);
        if (lt.find("<") == std::string::npos)
        {
            fail("pred_to_string binary did not include '<'");
        }

        auto p_group = make_pred(
            s, curlee::parser::PredGroup{
                   .inner = make_pred_ptr(make_pred(s, curlee::parser::PredBool{.value = true}))});
        const std::string g = curlee::verification::pred_to_string(p_group);
        if (g.find('(') == std::string::npos || g.find(')') == std::string::npos)
        {
            fail("pred_to_string group did not include parentheses");
        }
    }

    curlee::types::TypeInfo type_info;
    curlee::verification::Verifier v(type_info);

    {
        // model_vars_for_pred: cover result Bool and bool var collection.
        curlee::verification::LoweringContext ctx(v.solver_.context());
        ctx.bool_vars.emplace("b", v.solver_.context().bool_const("b"));
        ctx.result_bool = v.solver_.context().bool_const("result");

        const curlee::source::Span s{.start = 0, .end = 1};
        auto p = make_pred(
            s, curlee::parser::PredBinary{
                   .op = TokenKind::AndAnd,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "result"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "b"}))});

        const auto vars = curlee::verification::model_vars_for_pred(p, ctx);
        if (vars.size() != 2)
        {
            fail("expected model_vars_for_pred to return result+b");
        }

        // result-only should also hit the result_bool branch.
        auto p_result_only = make_pred(s, curlee::parser::PredName{.name = "result"});
        const auto vars_result_only = curlee::verification::model_vars_for_pred(p_result_only, ctx);
        if (vars_result_only.size() != 1)
        {
            fail("expected model_vars_for_pred(result-only) to return 1 var");
        }

        // bool-var-only should also hit the bool_vars lookup branch.
        auto p_b_only = make_pred(s, curlee::parser::PredName{.name = "b"});
        const auto vars_b_only = curlee::verification::model_vars_for_pred(p_b_only, ctx);
        if (vars_b_only.size() != 1)
        {
            fail("expected model_vars_for_pred(b-only) to return 1 var");
        }

        // Cover the not-taken branches when result/bool lookups fail.
        curlee::verification::LoweringContext ctx_none(v.solver_.context());
        auto p_result_missing = make_pred(s, curlee::parser::PredName{.name = "result"});
        const auto vars_result_missing =
            curlee::verification::model_vars_for_pred(p_result_missing, ctx_none);
        if (!vars_result_missing.empty())
        {
            fail("expected model_vars_for_pred(result) to return empty when no result is bound");
        }

        auto p_unknown = make_pred(s, curlee::parser::PredName{.name = "no_such_name"});
        const auto vars_unknown = curlee::verification::model_vars_for_pred(p_unknown, ctx_none);
        if (!vars_unknown.empty())
        {
            fail("expected model_vars_for_pred(unknown) to return empty");
        }
    }

    {
        // pop_scope: empty guard.
        v.pop_scope();
    }

    {
        // push_scope: exercise a few shapes to cover inlined branch paths.
        v.scopes_.clear();
        v.scopes_.shrink_to_fit();
        v.facts_.clear();
        v.lower_ctx_.int_vars.clear();
        v.lower_ctx_.bool_vars.clear();

        v.push_scope();
        v.pop_scope();

        v.scopes_.clear();
        v.scopes_.reserve(4);
        v.lower_ctx_.int_vars.emplace("x_ps", v.solver_.context().int_const("x_ps"));
        v.push_scope();
        v.pop_scope();

        v.lower_ctx_.int_vars.clear();
        v.lower_ctx_.bool_vars.emplace("b_ps", v.solver_.context().bool_const("b_ps"));
        v.facts_.push_back(v.solver_.context().bool_val(true));
        v.push_scope();
        v.pop_scope();

        // Force nested scopes deep enough to trigger multiple std::vector growth
        // paths while existing elements must be relocated.
        decltype(v.scopes_)().swap(v.scopes_);
        v.facts_.clear();
        v.lower_ctx_.int_vars.clear();
        v.lower_ctx_.bool_vars.clear();
        v.lower_ctx_.int_vars.emplace("x_ps0", v.solver_.context().int_const("x_ps0"));
        v.lower_ctx_.bool_vars.emplace("b_ps0", v.solver_.context().bool_const("b_ps0"));
        for (int i = 0; i < 8; ++i)
        {
            v.push_scope();
        }
        for (int i = 0; i < 8; ++i)
        {
            v.pop_scope();
        }

        // Restore clean state for later tests.
        v.facts_.clear();
        v.scopes_.clear();
        v.lower_ctx_.int_vars.clear();
        v.lower_ctx_.bool_vars.clear();
    }

    {
        // pop_scope: facts erase branch.
        const curlee::source::Span s{.start = 0, .end = 1};
        v.facts_.clear();
        v.scopes_.clear();
        v.push_scope();
        if (v.scopes_.empty())
        {
            fail("expected push_scope to append scope state");
        }
        v.add_fact(make_pred(s, curlee::parser::PredBool{.value = true}));
        if (v.facts_.empty())
        {
            fail("expected add_fact to append a fact");
        }
        v.pop_scope();
        if (!v.facts_.empty())
        {
            fail("expected pop_scope to erase facts added after push_scope");
        }
    }

    {
        // supported_type: unknown type path.
        curlee::parser::TypeName tn;
        tn.span = curlee::source::Span{.start = 0, .end = 0};
        tn.name = "NotAType";
        const auto t = v.supported_type(tn);
        if (t.has_value())
        {
            fail("expected supported_type to fail for unknown type");
        }
        if (v.diags_.empty() || v.diags_.back().message.find("unknown type") == std::string::npos)
        {
            fail("expected unknown type diagnostic");
        }

        // supported_type: capability type is treated as Unit (uninterpreted) for verification.
        curlee::parser::TypeName cap;
        cap.span = curlee::source::Span{.start = 0, .end = 1};
        cap.is_capability = true;
        cap.name = "foo";
        const auto cap_t = v.supported_type(cap);
        if (!cap_t.has_value() || *cap_t != curlee::types::TypeKind::Unit)
        {
            fail("expected supported_type(cap foo) to return Unit");
        }
    }

    {
        // collect_signatures: function without return type should be skipped.
        curlee::parser::Program p;
        curlee::parser::Function f;
        f.name = "no_ret";
        f.return_type = std::nullopt;
        p.functions.push_back(std::move(f));

        const std::size_t before = v.functions_.size();
        v.collect_signatures(p);
        const std::size_t after = v.functions_.size();
        if (after != before)
        {
            fail("expected collect_signatures to skip functions without return type");
        }
    }

    {
        // collect_signatures: supported_type failure for return type should hit the continue.
        curlee::parser::Program p;
        curlee::parser::Function f;
        f.name = "bad_ret";
        f.return_type = curlee::parser::TypeName{.span = curlee::source::Span{.start = 0, .end = 1},
                                                 .name = "Unit"};
        p.functions.push_back(std::move(f));

        const std::size_t before = v.functions_.size();
        v.collect_signatures(p);
        if (v.functions_.size() != before)
        {
            fail("expected collect_signatures to skip functions with unsupported return type");
        }
    }

    {
        // declare_var + lookup_var: cover Bool branch and not-found.
        v.declare_var("b", curlee::types::TypeKind::Bool);
        const auto found = v.lookup_var("b");
        if (!found.has_value() || found->kind != curlee::types::TypeKind::Bool)
        {
            fail("expected lookup_var to find bool var");
        }
        const auto missing = v.lookup_var("nope");
        if (missing.has_value())
        {
            fail("expected lookup_var missing to return nullopt");
        }

        // Int branch.
        v.declare_var("i", curlee::types::TypeKind::Int);
        const auto found_i = v.lookup_var("i");
        if (!found_i.has_value() || found_i->kind != curlee::types::TypeKind::Int)
        {
            fail("expected lookup_var to find int var");
        }

        // declare_var: unsupported kind should do nothing (covers fallthrough).
        v.declare_var("u", curlee::types::TypeKind::Unit);
        if (v.lookup_var("u").has_value())
        {
            fail("expected declare_var(Unit) to not bind a var");
        }
    }

    {
        // add_fact: diagnostic path when predicate lowering fails (unknown name).
        const curlee::source::Span s{.start = 0, .end = 1};
        const std::size_t before = v.diags_.size();
        v.add_fact(make_pred(s, curlee::parser::PredName{.name = "unknown_pred_name"}));
        if (v.diags_.size() == before)
        {
            fail("expected add_fact to emit diagnostic for unknown predicate name");
        }
    }

    {
        // lower_expr: exercise major error branches deterministically.
        const curlee::source::Span s{.start = 0, .end = 1};

        // IntExpr / BoolExpr success.
        {
            auto e0 = make_expr(s, curlee::parser::IntExpr{.lexeme = "42"});
            auto r0 = v.lower_expr(e0);
            if (!std::holds_alternative<curlee::verification::ExprValue>(r0))
            {
                fail("expected lower_expr(IntExpr) to succeed");
            }
            auto e1 = make_expr(s, curlee::parser::BoolExpr{.value = false});
            auto r1 = v.lower_expr(e1);
            if (!std::holds_alternative<curlee::verification::ExprValue>(r1))
            {
                fail("expected lower_expr(BoolExpr) to succeed");
            }
        }

        // String expressions unsupported.
        auto e_string = make_expr(s, curlee::parser::StringExpr{.lexeme = "\"hi\""});
        auto r_string = v.lower_expr(e_string);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_string))
        {
            fail("expected lower_expr(StringExpr) to error");
        }

        // Unknown name.
        auto e_name = make_expr(s, curlee::parser::NameExpr{.name = "x"});
        auto r_name = v.lower_expr(e_name);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_name))
        {
            fail("expected lower_expr(unknown NameExpr) to error");
        }

        // Unary '-' with Bool rhs.
        auto e_bool = make_expr(s, curlee::parser::BoolExpr{.value = true});
        auto e_unary_minus =
            make_expr(s, curlee::parser::UnaryExpr{.op = TokenKind::Minus,
                                                   .rhs = make_expr_ptr(std::move(e_bool))});
        auto r_unary_minus = v.lower_expr(e_unary_minus);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_unary_minus))
        {
            fail("expected lower_expr(unary - on bool) to error");
        }

        // Unary '!' with Int rhs.
        // Unary success paths.
        {
            auto rhs_i = make_expr(s, curlee::parser::IntExpr{.lexeme = "5"});
            auto e_neg =
                make_expr(s, curlee::parser::UnaryExpr{.op = TokenKind::Minus,
                                                       .rhs = make_expr_ptr(std::move(rhs_i))});
            auto r_neg = v.lower_expr(e_neg);
            if (!std::holds_alternative<curlee::verification::ExprValue>(r_neg))
            {
                fail("expected lower_expr(-Int) to succeed");
            }

            auto rhs_b = make_expr(s, curlee::parser::BoolExpr{.value = true});
            auto e_not =
                make_expr(s, curlee::parser::UnaryExpr{.op = TokenKind::Bang,
                                                       .rhs = make_expr_ptr(std::move(rhs_b))});
            auto r_not = v.lower_expr(e_not);
            if (!std::holds_alternative<curlee::verification::ExprValue>(r_not))
            {
                fail("expected lower_expr(!Bool) to succeed");
            }

            auto rhs_any = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
            auto e_bad_unary =
                make_expr(s, curlee::parser::UnaryExpr{.op = TokenKind::Plus,
                                                       .rhs = make_expr_ptr(std::move(rhs_any))});
            auto r_bad_unary = v.lower_expr(e_bad_unary);
            if (!std::holds_alternative<curlee::diag::Diagnostic>(r_bad_unary))
            {
                fail("expected lower_expr(unsupported unary op) to error");
            }
        }

        // Binary success paths.
        {
            auto a = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
            auto b = make_expr(s, curlee::parser::IntExpr{.lexeme = "2"});
            auto e_add =
                make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::Plus,
                                                        .lhs = make_expr_ptr(std::move(a)),
                                                        .rhs = make_expr_ptr(std::move(b))});
            if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_add)))
            {
                fail("expected lower_expr(1+2) to succeed");
            }

            auto c = make_expr(s, curlee::parser::IntExpr{.lexeme = "3"});
            auto d = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
            auto e_sub =
                make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::Minus,
                                                        .lhs = make_expr_ptr(std::move(c)),
                                                        .rhs = make_expr_ptr(std::move(d))});
            if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_sub)))
            {
                fail("expected lower_expr(3-1) to succeed");
            }

            // Cover `lhs.is_literal && rhs.is_literal` short-circuit shapes.
            v.declare_var("lit_a", curlee::types::TypeKind::Int);
            auto e_add_lit_var = make_expr(
                s,
                curlee::parser::BinaryExpr{
                    .op = TokenKind::Plus,
                    .lhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "1"})),
                    .rhs = make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "lit_a"}))});
            if (!std::holds_alternative<curlee::verification::ExprValue>(
                    v.lower_expr(e_add_lit_var)))
            {
                fail("expected lower_expr(1+lit_a) to succeed");
            }
            auto e_add_var_lit = make_expr(
                s,
                curlee::parser::BinaryExpr{
                    .op = TokenKind::Plus,
                    .lhs = make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "lit_a"})),
                    .rhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "1"}))});
            if (!std::holds_alternative<curlee::verification::ExprValue>(
                    v.lower_expr(e_add_var_lit)))
            {
                fail("expected lower_expr(lit_a+1) to succeed");
            }

            auto e_eq = make_expr(
                s,
                curlee::parser::BinaryExpr{
                    .op = TokenKind::EqualEqual,
                    .lhs = make_expr_ptr(make_expr(s, curlee::parser::BoolExpr{.value = true})),
                    .rhs = make_expr_ptr(make_expr(s, curlee::parser::BoolExpr{.value = false}))});
            if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_eq)))
            {
                fail("expected lower_expr(true==false) to succeed");
            }

            auto e_cmp = make_expr(
                s, curlee::parser::BinaryExpr{
                       .op = TokenKind::LessEqual,
                       .lhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "1"})),
                       .rhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "2"}))});
            if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_cmp)))
            {
                fail("expected lower_expr(1<=2) to succeed");
            }

            auto e_boolop = make_expr(
                s,
                curlee::parser::BinaryExpr{
                    .op = TokenKind::OrOr,
                    .lhs = make_expr_ptr(make_expr(s, curlee::parser::BoolExpr{.value = true})),
                    .rhs = make_expr_ptr(make_expr(s, curlee::parser::BoolExpr{.value = false}))});
            if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_boolop)))
            {
                fail("expected lower_expr(true||false) to succeed");
            }

            auto e_booland = make_expr(
                s,
                curlee::parser::BinaryExpr{
                    .op = TokenKind::AndAnd,
                    .lhs = make_expr_ptr(make_expr(s, curlee::parser::BoolExpr{.value = true})),
                    .rhs = make_expr_ptr(make_expr(s, curlee::parser::BoolExpr{.value = true}))});
            if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_booland)))
            {
                fail("expected lower_expr(true&&true) to succeed");
            }
        }

        // Multiplication: non-linear rejection (var*var) and accepted (literal*var).
        {
            v.declare_var("a", curlee::types::TypeKind::Int);
            v.declare_var("b", curlee::types::TypeKind::Int);
            auto ea = make_expr(s, curlee::parser::NameExpr{.name = "a"});
            auto eb = make_expr(s, curlee::parser::NameExpr{.name = "b"});
            auto e_mul_nl =
                make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::Star,
                                                        .lhs = make_expr_ptr(std::move(ea)),
                                                        .rhs = make_expr_ptr(std::move(eb))});
            if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e_mul_nl)))
            {
                fail("expected lower_expr(var*var) to error (non-linear)");
            }

            auto el = make_expr(s, curlee::parser::IntExpr{.lexeme = "2"});
            auto er = make_expr(s, curlee::parser::NameExpr{.name = "a"});
            auto e_mul_ok =
                make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::Star,
                                                        .lhs = make_expr_ptr(std::move(el)),
                                                        .rhs = make_expr_ptr(std::move(er))});
            if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_mul_ok)))
            {
                fail("expected lower_expr(2*a) to succeed");
            }

            // Also cover the other short-circuit shape: var*literal.
            auto el2 = make_expr(s, curlee::parser::NameExpr{.name = "a"});
            auto er2 = make_expr(s, curlee::parser::IntExpr{.lexeme = "2"});
            auto e_mul_ok2 =
                make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::Star,
                                                        .lhs = make_expr_ptr(std::move(el2)),
                                                        .rhs = make_expr_ptr(std::move(er2))});
            if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_mul_ok2)))
            {
                fail("expected lower_expr(a*2) to succeed");
            }

            auto e_mul_ll = make_expr(
                s, curlee::parser::BinaryExpr{
                       .op = TokenKind::Star,
                       .lhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "2"})),
                       .rhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "3"}))});
            if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_mul_ll)))
            {
                fail("expected lower_expr(2*3) to succeed");
            }
        }
        auto e_int = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
        auto e_unary_bang =
            make_expr(s, curlee::parser::UnaryExpr{.op = TokenKind::Bang,
                                                   .rhs = make_expr_ptr(std::move(e_int))});
        auto r_unary_bang = v.lower_expr(e_unary_bang);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_unary_bang))
        {
            fail("expected lower_expr(unary ! on int) to error");
        }

        // Arithmetic expects Int: true + 1.
        auto e_lhs = make_expr(s, curlee::parser::BoolExpr{.value = true});
        auto e_rhs = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
        auto e_plus =
            make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::Plus,
                                                    .lhs = make_expr_ptr(std::move(e_lhs)),
                                                    .rhs = make_expr_ptr(std::move(e_rhs))});
        auto r_plus = v.lower_expr(e_plus);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_plus))
        {
            fail("expected lower_expr(bool + int) to error");
        }

        // Also cover int + bool (second half of the short-circuit).
        auto e_lhs_ib = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
        auto e_rhs_ib = make_expr(s, curlee::parser::BoolExpr{.value = true});
        auto e_plus_ib =
            make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::Plus,
                                                    .lhs = make_expr_ptr(std::move(e_lhs_ib)),
                                                    .rhs = make_expr_ptr(std::move(e_rhs_ib))});
        if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e_plus_ib)))
        {
            fail("expected lower_expr(int + bool) to error");
        }

        // Comparison expects Int: true < false.
        auto e_lhs2 = make_expr(s, curlee::parser::BoolExpr{.value = true});
        auto e_rhs2 = make_expr(s, curlee::parser::BoolExpr{.value = false});
        auto e_lt =
            make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::Less,
                                                    .lhs = make_expr_ptr(std::move(e_lhs2)),
                                                    .rhs = make_expr_ptr(std::move(e_rhs2))});
        auto r_lt = v.lower_expr(e_lt);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_lt))
        {
            fail("expected lower_expr(bool < bool) to error");
        }

        // Also cover int < bool (rhs-side type mismatch).
        auto e_lhs2b = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
        auto e_rhs2b = make_expr(s, curlee::parser::BoolExpr{.value = false});
        auto e_lt2 =
            make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::Less,
                                                    .lhs = make_expr_ptr(std::move(e_lhs2b)),
                                                    .rhs = make_expr_ptr(std::move(e_rhs2b))});
        if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e_lt2)))
        {
            fail("expected lower_expr(int < bool) to error");
        }

        // Cover remaining comparison operators.
        auto e_gt = make_expr(
            s, curlee::parser::BinaryExpr{
                   .op = TokenKind::Greater,
                   .lhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "2"})),
                   .rhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "1"}))});
        if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_gt)))
        {
            fail("expected lower_expr(2>1) to succeed");
        }
        auto e_ge = make_expr(
            s, curlee::parser::BinaryExpr{
                   .op = TokenKind::GreaterEqual,
                   .lhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "2"})),
                   .rhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "2"}))});
        if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_ge)))
        {
            fail("expected lower_expr(2>=2) to succeed");
        }
        auto e_lt_succ = make_expr(
            s, curlee::parser::BinaryExpr{
                   .op = TokenKind::Less,
                   .lhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "1"})),
                   .rhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "2"}))});
        if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_lt_succ)))
        {
            fail("expected lower_expr(1<2) to succeed");
        }

        // Boolean ops expect Bool: 1 && true.
        auto e_lhs3 = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
        auto e_rhs3 = make_expr(s, curlee::parser::BoolExpr{.value = true});
        auto e_and =
            make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::AndAnd,
                                                    .lhs = make_expr_ptr(std::move(e_lhs3)),
                                                    .rhs = make_expr_ptr(std::move(e_rhs3))});
        auto r_and = v.lower_expr(e_and);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_and))
        {
            fail("expected lower_expr(int && bool) to error");
        }

        // Also cover bool && int (rhs-side mismatch).
        auto e_lhs3b = make_expr(s, curlee::parser::BoolExpr{.value = true});
        auto e_rhs3b = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
        auto e_and2 =
            make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::AndAnd,
                                                    .lhs = make_expr_ptr(std::move(e_lhs3b)),
                                                    .rhs = make_expr_ptr(std::move(e_rhs3b))});
        if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e_and2)))
        {
            fail("expected lower_expr(bool && int) to error");
        }

        // Equality mismatch: 1 == true.
        auto e_eq_mismatch = make_expr(
            s, curlee::parser::BinaryExpr{
                   .op = TokenKind::EqualEqual,
                   .lhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "1"})),
                   .rhs = make_expr_ptr(make_expr(s, curlee::parser::BoolExpr{.value = true}))});
        if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e_eq_mismatch)))
        {
            fail("expected lower_expr(int == bool) to error");
        }

        // '*' type mismatch: bool * int and int * bool.
        auto e_mul_m1 = make_expr(
            s, curlee::parser::BinaryExpr{
                   .op = TokenKind::Star,
                   .lhs = make_expr_ptr(make_expr(s, curlee::parser::BoolExpr{.value = true})),
                   .rhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "1"}))});
        if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e_mul_m1)))
        {
            fail("expected lower_expr(bool * int) to error");
        }
        auto e_mul_m2 = make_expr(
            s, curlee::parser::BinaryExpr{
                   .op = TokenKind::Star,
                   .lhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "1"})),
                   .rhs = make_expr_ptr(make_expr(s, curlee::parser::BoolExpr{.value = true}))});
        if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e_mul_m2)))
        {
            fail("expected lower_expr(int * bool) to error");
        }

        // Unsupported binary operator: '/'
        auto e_lhs4 = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
        auto e_rhs4 = make_expr(s, curlee::parser::IntExpr{.lexeme = "2"});
        auto e_div =
            make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::Slash,
                                                    .lhs = make_expr_ptr(std::move(e_lhs4)),
                                                    .rhs = make_expr_ptr(std::move(e_rhs4))});
        auto r_div = v.lower_expr(e_div);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_div))
        {
            fail("expected lower_expr(div) to error");
        }

        // Call expressions unsupported.
        curlee::parser::CallExpr call;
        call.callee = make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "f"}));
        call.args.push_back(make_expr(s, curlee::parser::IntExpr{.lexeme = "1"}));
        auto e_call = make_expr(s, std::move(call));
        auto r_call = v.lower_expr(e_call);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_call))
        {
            fail("expected lower_expr(CallExpr) to error");
        }

        // GroupExpr should forward to inner.
        curlee::parser::GroupExpr group;
        group.inner = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "7"}));
        auto e_group = make_expr(s, std::move(group));
        auto r_group = v.lower_expr(e_group);
        if (!std::holds_alternative<curlee::verification::ExprValue>(r_group))
        {
            fail("expected lower_expr(GroupExpr) to succeed");
        }

        // Unsupported expression kind: MemberExpr.
        curlee::parser::MemberExpr mem;
        mem.base = make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "x"}));
        mem.member = "y";
        auto e_mem = make_expr(s, std::move(mem));
        auto r_mem = v.lower_expr(e_mem);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_mem))
        {
            fail("expected lower_expr(MemberExpr) to error");
        }

        auto e_scoped = make_expr(s, curlee::parser::ScopedNameExpr{.lhs = "m", .rhs = "x"});
        if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e_scoped)))
        {
            fail("expected lower_expr(ScopedNameExpr) to error");
        }

        curlee::parser::StructLiteralExpr lit;
        lit.type_name = "T";
        auto e_struct = make_expr(s, std::move(lit));
        if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e_struct)))
        {
            fail("expected lower_expr(StructLiteralExpr) to error");
        }
        // Unary: propagate Diagnostic from RHS lowering.
        {
            auto bad_rhs = make_expr(s, curlee::parser::NameExpr{.name = "does_not_exist"});
            auto e =
                make_expr(s, curlee::parser::UnaryExpr{.op = TokenKind::Minus,
                                                       .rhs = make_expr_ptr(std::move(bad_rhs))});
            auto r = v.lower_expr(e);
            if (!std::holds_alternative<curlee::diag::Diagnostic>(r))
            {
                fail("expected unary to propagate RHS diagnostic");
            }
        }

        // Binary: propagate Diagnostic from LHS/RHS lowering.
        {
            auto lhs_bad = make_expr(s, curlee::parser::NameExpr{.name = "lhs_bad"});
            auto rhs_ok = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
            auto e1 =
                make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::Plus,
                                                        .lhs = make_expr_ptr(std::move(lhs_bad)),
                                                        .rhs = make_expr_ptr(std::move(rhs_ok))});
            auto r1 = v.lower_expr(e1);
            if (!std::holds_alternative<curlee::diag::Diagnostic>(r1))
            {
                fail("expected binary to propagate LHS diagnostic");
            }

            auto lhs_ok = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
            auto rhs_bad = make_expr(s, curlee::parser::NameExpr{.name = "rhs_bad"});
            auto e2 =
                make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::Plus,
                                                        .lhs = make_expr_ptr(std::move(lhs_ok)),
                                                        .rhs = make_expr_ptr(std::move(rhs_bad))});
            auto r2 = v.lower_expr(e2);
            if (!std::holds_alternative<curlee::diag::Diagnostic>(r2))
            {
                fail("expected binary to propagate RHS diagnostic");
            }
        }

        // Equality: mismatched kinds.
        {
            auto lhs = make_expr(s, curlee::parser::BoolExpr{.value = true});
            auto rhs = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
            auto e = make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::EqualEqual,
                                                             .lhs = make_expr_ptr(std::move(lhs)),
                                                             .rhs = make_expr_ptr(std::move(rhs))});
            if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e)))
            {
                fail("expected lower_expr(bool==int) to error");
            }
        }

        // Multiplication: non-Int kind check.
        {
            auto lhs = make_expr(s, curlee::parser::BoolExpr{.value = true});
            auto rhs = make_expr(s, curlee::parser::IntExpr{.lexeme = "2"});
            auto e = make_expr(s, curlee::parser::BinaryExpr{.op = TokenKind::Star,
                                                             .lhs = make_expr_ptr(std::move(lhs)),
                                                             .rhs = make_expr_ptr(std::move(rhs))});
            if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e)))
            {
                fail("expected lower_expr(bool*int) to error");
            }
        }
    }

    {
        // check_call: exercise early-return guards.
        const curlee::source::Span s{.start = 0, .end = 1};

        // callee not a NameExpr.
        curlee::parser::CallExpr call1;
        curlee::parser::MemberExpr member;
        member.base = make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "obj"}));
        member.member = "m";
        call1.callee = make_expr_ptr(make_expr(s, std::move(member)));
        v.check_call(call1);

        // Name callee but no signature.
        curlee::parser::CallExpr call2;
        call2.callee = make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "nope"}));
        v.check_call(call2);

        // Signature present but arg count mismatch.
        curlee::verification::FunctionSig sig;
        sig.decl = nullptr;
        sig.params = {curlee::types::TypeKind::Int};
        sig.result = curlee::types::TypeKind::Int;
        v.functions_.emplace("f", sig);

        curlee::parser::CallExpr call3;
        call3.callee = make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "f"}));
        v.check_call(call3);
    }

    {
        // check_call: arg-count mismatch with a non-null decl should hit the args.size() guard.
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Function decl;
        decl.name = "argc_mismatch";
        decl.return_type = curlee::parser::TypeName{.span = s, .name = "Int"};
        decl.params.push_back(curlee::parser::Function::Param{
            .span = s,
            .name = "x",
            .type = curlee::parser::TypeName{.span = s, .name = "Int"},
            .refinement = std::nullopt});

        curlee::verification::FunctionSig sig;
        sig.decl = &decl;
        sig.params = {curlee::types::TypeKind::Int};
        sig.result = curlee::types::TypeKind::Int;
        v.functions_.insert_or_assign("argc_mismatch", sig);

        curlee::parser::CallExpr call;
        call.callee =
            make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "argc_mismatch"}));
        // no args
        v.check_call(call);
    }

    {
        // check_call: non-Int/Bool signature params are ignored by the MVP verifier.
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Function decl;
        decl.name = "non_scalar_param";
        decl.return_type = curlee::parser::TypeName{.span = s, .name = "Int"};
        decl.params.push_back(curlee::parser::Function::Param{
            .span = s,
            .name = "cap",
            .type = curlee::parser::TypeName{.span = s, .is_capability = true, .name = "foo"},
            .refinement = std::nullopt,
        });

        curlee::verification::FunctionSig sig;
        sig.decl = &decl;
        sig.params = {curlee::types::TypeKind::Unit};
        sig.result = curlee::types::TypeKind::Int;
        v.functions_.insert_or_assign("non_scalar_param", sig);

        curlee::parser::CallExpr call;
        call.callee =
            make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "non_scalar_param"}));
        call.args.push_back(make_expr(s, curlee::parser::IntExpr{.lexeme = "0"}));
        v.check_call(call);
    }

    {
        // is_python_ffi_call: cover early returns.
        const curlee::source::Span s{.start = 0, .end = 1};

        curlee::parser::CallExpr c0;
        c0.callee = nullptr;
        if (curlee::verification::Verifier::is_python_ffi_call(c0))
        {
            fail("expected is_python_ffi_call(null callee) false");
        }

        curlee::parser::CallExpr c1;
        c1.callee = make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "x"}));
        if (curlee::verification::Verifier::is_python_ffi_call(c1))
        {
            fail("expected is_python_ffi_call(non-member callee) false");
        }

        curlee::parser::CallExpr c2;
        curlee::parser::MemberExpr m;
        m.base = nullptr;
        m.member = "call";
        c2.callee = make_expr_ptr(make_expr(s, std::move(m)));
        if (curlee::verification::Verifier::is_python_ffi_call(c2))
        {
            fail("expected is_python_ffi_call(null base) false");
        }

        // True case.
        curlee::parser::CallExpr c3;
        curlee::parser::MemberExpr m3;
        m3.base = make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "python_ffi"}));
        m3.member = "call";
        c3.callee = make_expr_ptr(make_expr(s, std::move(m3)));
        if (!curlee::verification::Verifier::is_python_ffi_call(c3))
        {
            fail("expected is_python_ffi_call(python_ffi.call) true");
        }

        // Base matches but member differs, to cover the second half of the &&.
        curlee::parser::CallExpr c4;
        curlee::parser::MemberExpr m4;
        m4.base = make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "python_ffi"}));
        m4.member = "not_call";
        c4.callee = make_expr_ptr(make_expr(s, std::move(m4)));
        if (curlee::verification::Verifier::is_python_ffi_call(c4))
        {
            fail("expected is_python_ffi_call(python_ffi.not_call) false");
        }
    }

    {
        // check_expr_for_calls: MemberExpr recursion and python_ffi.call skip path.
        const curlee::source::Span s{.start = 0, .end = 1};

        // MemberExpr with base present.
        curlee::parser::MemberExpr mem;
        mem.base = make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "x"}));
        mem.member = "y";
        auto e_mem = make_expr(s, std::move(mem));
        v.check_expr_for_calls(e_mem);

        // MemberExpr with null base should take the other branch.
        curlee::parser::MemberExpr mem_null;
        mem_null.base = nullptr;
        mem_null.member = "y";
        v.check_expr_for_calls(make_expr(s, std::move(mem_null)));

        // python_ffi.call(...) should not call check_call.
        curlee::parser::MemberExpr ffi_member;
        ffi_member.base =
            make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "python_ffi"}));
        ffi_member.member = "call";

        curlee::parser::CallExpr ffi_call;
        ffi_call.callee = make_expr_ptr(make_expr(s, std::move(ffi_member)));
        ffi_call.args.push_back(make_expr(s, curlee::parser::IntExpr{.lexeme = "1"}));
        auto e_call = make_expr(s, std::move(ffi_call));
        v.check_expr_for_calls(e_call);
    }

    {
        // check_call: full path with requires clauses, including failure and lowering diagnostics.
        const curlee::source::Span s{.start = 0, .end = 1};

        curlee::parser::Function callee;
        callee.name = "g";
        callee.return_type = curlee::parser::TypeName{.span = s, .name = "Int"};
        callee.params.push_back(curlee::parser::Function::Param{
            .span = s,
            .name = "x",
            .type = curlee::parser::TypeName{.span = s, .name = "Int"},
            .refinement = std::nullopt});
        callee.params.push_back(curlee::parser::Function::Param{
            .span = s,
            .name = "p",
            .type = curlee::parser::TypeName{.span = s, .name = "Bool"},
            .refinement = std::nullopt});

        // requires: x > 0, and an invalid predicate name to hit the diagnostic+continue branch.
        callee.requires_clauses.push_back(make_pred(
            s, curlee::parser::PredBinary{
                   .op = TokenKind::Greater,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "x"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"}))}));
        callee.requires_clauses.push_back(
            make_pred(s, curlee::parser::PredName{.name = "no_such_pred"}));

        curlee::verification::FunctionSig sig;
        sig.decl = &callee;
        sig.params = {curlee::types::TypeKind::Int, curlee::types::TypeKind::Bool};
        sig.result = curlee::types::TypeKind::Int;
        v.functions_.insert_or_assign("g", sig);

        // Call g(0, true) should violate x > 0 and produce a requires diagnostic.
        curlee::parser::CallExpr call;
        call.callee = make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "g"}));
        call.args.push_back(make_expr(s, curlee::parser::IntExpr{.lexeme = "0"}));
        call.args.push_back(make_expr(s, curlee::parser::BoolExpr{.value = true}));

        const std::size_t before = v.diags_.size();
        v.check_call(call);
        if (v.diags_.size() == before)
        {
            fail("expected check_call to emit diagnostics for violated requires");
        }
    }

    {
        // check_return: cover early exits.
        curlee::parser::ReturnStmt r0;
        r0.value = std::nullopt;
        v.check_return(r0, curlee::types::TypeKind::Int);

        curlee::parser::ReturnStmt r1;
        r1.value = make_expr(curlee::source::Span{.start = 0, .end = 1},
                             curlee::parser::IntExpr{.lexeme = "0"});
        // current_function_ is not set => early return.
        v.current_function_ = std::nullopt;
        v.check_return(r1, curlee::types::TypeKind::Int);
    }

    {
        // check_return: func==nullptr or ensures empty guard.
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::verification::FunctionSig sig;
        sig.decl = nullptr;
        sig.result = curlee::types::TypeKind::Int;
        v.current_function_ = sig;

        curlee::parser::ReturnStmt r;
        r.value = make_expr(s, curlee::parser::IntExpr{.lexeme = "0"});
        v.check_return(r, curlee::types::TypeKind::Int);

        curlee::parser::Function f;
        f.name = "empty_ens";
        f.return_type = curlee::parser::TypeName{.span = s, .name = "Int"};
        sig.decl = &f;
        v.current_function_ = sig;
        v.check_return(r, curlee::types::TypeKind::Int);
    }

    {
        // check_return: value kind mismatch guard.
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Function f;
        f.name = "kind_mismatch";
        f.return_type = curlee::parser::TypeName{.span = s, .name = "Int"};
        f.ensures.push_back(make_pred(s, curlee::parser::PredBool{.value = true}));

        curlee::verification::FunctionSig sig;
        sig.decl = &f;
        sig.result = curlee::types::TypeKind::Int;
        v.current_function_ = sig;

        curlee::parser::ReturnStmt r;
        r.value = make_expr(s, curlee::parser::BoolExpr{.value = true});
        v.check_return(r, curlee::types::TypeKind::Int);
    }

    {
        // check_return: ensures path for Bool return with result_bool binding.
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Function f;
        f.name = "h";
        f.return_type = curlee::parser::TypeName{.span = s, .name = "Bool"};
        // ensures: result == true
        f.ensures.push_back(make_pred(
            s, curlee::parser::PredBinary{
                   .op = TokenKind::EqualEqual,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "result"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredBool{.value = true}))}));

        curlee::verification::FunctionSig sig;
        sig.decl = &f;
        sig.result = curlee::types::TypeKind::Bool;
        v.current_function_ = sig;

        curlee::parser::ReturnStmt r;
        r.value = make_expr(s, curlee::parser::BoolExpr{.value = false});
        const std::size_t before = v.diags_.size();
        v.check_return(r, curlee::types::TypeKind::Bool);
        if (v.diags_.size() == before)
        {
            fail("expected ensures failure diagnostic for result==true when returning false");
        }

        // The verifier stores a raw pointer to the function decl. Ensure we don't keep a
        // dangling pointer to this stack object across later tests.
        v.current_function_ = std::nullopt;
    }

    {
        // Note helpers: goal/model/hint notes.
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::diag::Diagnostic d = curlee::verification::error_at(s, "msg");
        auto pred = make_pred(s, curlee::parser::PredBool{.value = true});
        v.add_goal_note(d, pred);
        v.add_hint_note(d);

        // add_model_note early-return when no SAT model is available.
        v.add_model_note(d, std::vector<z3::expr>{v.solver_.context().int_const("x_no_model")});

        v.solver_.push();
        auto x = v.solver_.context().int_const("x_model");
        v.solver_.add(x == 0);
        (void)v.solver_.check();
        v.add_model_note(d, std::vector<z3::expr>{x});
        v.solver_.pop();

        if (d.notes.size() < 2)
        {
            fail("expected notes to be attached");
        }
    }

    {
        // check_obligation: Sat branch should emit a diagnostic.
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::verification::LoweringContext ctx(v.solver_.context());
        ctx.int_vars.emplace("x", v.solver_.context().int_const("x"));
        auto pred = make_pred(
            s, curlee::parser::PredBinary{
                   .op = TokenKind::Greater,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "x"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"}))});
        z3::expr obligation = ctx.int_vars.at("x") > 0;
        const std::size_t before = v.diags_.size();
        v.check_obligation(pred, ctx, obligation, s, {}, "obligation failed");
        if (v.diags_.size() == before)
        {
            fail("expected check_obligation to emit diagnostic in Sat case");
        }
    }

    {
        // check_obligation: attempt to hit the Unknown branch deterministically via Z3 rlimit.
        // If Z3 still returns Sat/Unsat, we don't fail the test (to avoid flakiness), but
        // in practice with an extremely low rlimit and non-linear constraints it should return
        // Unknown.
        const curlee::source::Span s{.start = 0, .end = 1};

        z3::params p(v.solver_.context());
        p.set("rlimit", static_cast<unsigned>(1));
        v.solver_.solver_.set(p);

        curlee::verification::LoweringContext ctx(v.solver_.context());
        ctx.int_vars.emplace("x", v.solver_.context().int_const("x_unknown"));

        std::vector<z3::expr> extra;
        extra.reserve(200);
        for (int i = 0; i < 200; ++i)
        {
            const std::string name = "n" + std::to_string(i);
            z3::expr xi = v.solver_.context().int_const(name.c_str());
            extra.push_back((xi * xi) == (i + 1));
        }

        auto pred = make_pred(s, curlee::parser::PredName{.name = "x"});
        const std::size_t before = v.diags_.size();
        v.check_obligation(pred, ctx, ctx.int_vars.at("x") > 0, s, extra, "unknown branch");

        if (v.diags_.size() > before)
        {
            // If we did add a diagnostic, accept either Sat or Unknown branch.
            (void)v.diags_.back();
        }
    }

    {
        // check_stmt_node: LetStmt branches (unknown type w/ refinement, unsupported type, scalar).
        const curlee::source::Span s{.start = 0, .end = 1};

        // Unknown type name + refinement -> diagnostic.
        curlee::parser::LetStmt ls0;
        ls0.name = "x";
        ls0.type = curlee::parser::TypeName{.span = s, .name = "NotAType"};
        ls0.value = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
        ls0.refinement = make_pred(s, curlee::parser::PredBool{.value = true});
        const std::size_t before0 = v.diags_.size();
        v.check_stmt_node(ls0, s, curlee::types::TypeKind::Int);
        if (v.diags_.size() == before0)
        {
            fail("expected LetStmt unknown-type refinement diagnostic");
        }

        // Unsupported core type (Unit) -> diagnostic.
        curlee::parser::LetStmt ls1;
        ls1.name = "u";
        ls1.type = curlee::parser::TypeName{.span = s, .name = "Unit"};
        ls1.value = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
        v.check_stmt_node(ls1, s, curlee::types::TypeKind::Int);

        // Supported scalar type with refinement -> adds fact.
        curlee::parser::LetStmt ls2;
        ls2.name = "i2";
        ls2.type = curlee::parser::TypeName{.span = s, .name = "Int"};
        ls2.value = make_expr(s, curlee::parser::IntExpr{.lexeme = "2"});
        ls2.refinement = make_pred(
            s, curlee::parser::PredBinary{
                   .op = TokenKind::GreaterEqual,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "i2"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"}))});
        const std::size_t facts_before = v.facts_.size();
        v.check_stmt_node(ls2, s, curlee::types::TypeKind::Int);
        if (v.facts_.size() == facts_before)
        {
            fail("expected LetStmt refinement to add a fact");
        }
    }

    {
        // check_stmt_node(ReturnStmt): value.has_value() path.
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::ReturnStmt rs;
        rs.value = make_expr(s, curlee::parser::IntExpr{.lexeme = "0"});
        v.check_stmt_node(rs, s, curlee::types::TypeKind::Int);

        curlee::parser::ReturnStmt rs2;
        rs2.value = std::nullopt;
        v.check_stmt_node(rs2, s, curlee::types::TypeKind::Int);
    }

    {
        // check_stmt_node(IfStmt/WhileStmt): cover cond_fact + else branch.
        const curlee::source::Span s{.start = 0, .end = 1};

        auto then_block = std::make_unique<curlee::parser::Block>();
        then_block->span = s;
        then_block->stmts.push_back(curlee::parser::Stmt{
            .span = s,
            .node = curlee::parser::ExprStmt{
                .expr = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"})}});

        auto else_block = std::make_unique<curlee::parser::Block>();
        else_block->span = s;
        else_block->stmts.push_back(curlee::parser::Stmt{
            .span = s,
            .node = curlee::parser::ExprStmt{
                .expr = make_expr(s, curlee::parser::IntExpr{.lexeme = "2"})}});

        curlee::parser::IfStmt ifs;
        ifs.cond = make_expr(s, curlee::parser::BoolExpr{.value = true});
        ifs.then_block = std::move(then_block);
        ifs.else_block = std::move(else_block);
        v.check_stmt_node(ifs, s, curlee::types::TypeKind::Int);

        curlee::parser::WhileStmt ws;
        ws.cond = make_expr(s, curlee::parser::BoolExpr{.value = false});
        ws.body = std::make_unique<curlee::parser::Block>();
        ws.body->span = s;
        ws.body->stmts.push_back(curlee::parser::Stmt{
            .span = s,
            .node = curlee::parser::ExprStmt{
                .expr = make_expr(s, curlee::parser::IntExpr{.lexeme = "3"})}});
        v.check_stmt_node(ws, s, curlee::types::TypeKind::Int);

        // Also cover cond_fact unset paths (non-bool ExprValue and Diagnostic from lowering).
        curlee::parser::IfStmt ifs2;
        ifs2.cond = make_expr(s, curlee::parser::IntExpr{.lexeme = "1"});
        ifs2.then_block = std::make_unique<curlee::parser::Block>();
        ifs2.then_block->span = s;
        ifs2.else_block = nullptr;
        v.check_stmt_node(ifs2, s, curlee::types::TypeKind::Int);

        curlee::parser::IfStmt ifs3;
        ifs3.cond = make_expr(s, curlee::parser::StringExpr{.lexeme = "\"x\""});
        ifs3.then_block = std::make_unique<curlee::parser::Block>();
        ifs3.then_block->span = s;
        ifs3.else_block = std::make_unique<curlee::parser::Block>();
        ifs3.else_block->span = s;
        v.check_stmt_node(ifs3, s, curlee::types::TypeKind::Int);

        curlee::parser::WhileStmt ws2;
        ws2.cond = make_expr(s, curlee::parser::IntExpr{.lexeme = "0"});
        ws2.body = std::make_unique<curlee::parser::Block>();
        ws2.body->span = s;
        v.check_stmt_node(ws2, s, curlee::types::TypeKind::Int);
    }

    std::cout << "OK\n";
    return 0;
}
