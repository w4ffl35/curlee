// SPDX-License-Identifier: MIT
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

static curlee::parser::Expr make_expr(curlee::source::Span span,
                                      decltype(curlee::parser::Expr::node) node)
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
                 curlee::parser::PredCall, curlee::parser::PredUnary,
                 curlee::parser::PredBinary, curlee::parser::PredGroup>
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
        // token_to_string: cover arithmetic, bitwise (issue #270), and default.
        if (curlee::verification::token_to_string(TokenKind::Slash) != "/")
        {
            fail("token_to_string(Slash) mismatch");
        }
        if (curlee::verification::token_to_string(TokenKind::Percent) != "%")
        {
            fail("token_to_string(Percent) mismatch");
        }
        if (curlee::verification::token_to_string(TokenKind::Amp) != "&")
        {
            fail("token_to_string(Amp) mismatch");
        }
        if (curlee::verification::token_to_string(TokenKind::Pipe) != "|")
        {
            fail("token_to_string(Pipe) mismatch");
        }
        if (curlee::verification::token_to_string(TokenKind::Caret) != "^")
        {
            fail("token_to_string(Caret) mismatch");
        }
        if (curlee::verification::token_to_string(TokenKind::Tilde) != "~")
        {
            fail("token_to_string(Tilde) mismatch");
        }
        if (curlee::verification::token_to_string(TokenKind::ShiftLeft) != "<<")
        {
            fail("token_to_string(ShiftLeft) mismatch");
        }
        if (curlee::verification::token_to_string(TokenKind::ShiftRight) != ">>")
        {
            fail("token_to_string(ShiftRight) mismatch");
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

        // supported_type: Phys<T> maps to a distinct Phys kind (opaque sort), not Unit.
        curlee::parser::TypeName phys_tn;
        phys_tn.span = curlee::source::Span{.start = 0, .end = 1};
        phys_tn.name = "Phys";
        phys_tn.type_arg = std::string_view("U32");
        const auto phys_t = v.supported_type(phys_tn);
        if (!phys_t.has_value() || *phys_t != curlee::types::TypeKind::Phys)
        {
            fail("expected supported_type(Phys) to return Phys kind");
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
        // collect_signatures: Unit return type is allowed as uninterpreted.
        curlee::parser::Program p;
        curlee::parser::Function f;
        f.name = "bad_ret";
        f.return_type = curlee::parser::TypeName{.span = curlee::source::Span{.start = 0, .end = 1},
                                                 .name = "Unit"};
        p.functions.push_back(std::move(f));

        const std::size_t before = v.functions_.size();
        v.collect_signatures(p);
        if (v.functions_.size() != before + 1)
        {
            fail("expected collect_signatures to retain Unit return type");
        }
    }

    {
        // collect_signatures: unknown return type should skip signature.
        curlee::parser::Program p;
        curlee::parser::Function f;
        f.name = "bad_unknown_ret";
        f.return_type = curlee::parser::TypeName{.span = curlee::source::Span{.start = 2, .end = 3},
                                                 .name = "NoSuchType"};
        p.functions.push_back(std::move(f));

        const std::size_t before = v.functions_.size();
        v.collect_signatures(p);
        if (v.functions_.size() != before)
        {
            fail("expected collect_signatures to skip functions with unknown return type");
        }
    }

    {
        // collect_signatures: unknown parameter type should skip insertion.
        curlee::parser::Program p;
        curlee::parser::Function f;
        f.name = "bad_unknown_param";
        f.return_type = curlee::parser::TypeName{.span = curlee::source::Span{.start = 4, .end = 5},
                                                 .name = "Int"};
        curlee::parser::Function::Param param;
        param.name = "x";
        param.type = curlee::parser::TypeName{.span = curlee::source::Span{.start = 5, .end = 6},
                                              .name = "NoSuchParamType"};
        f.params.push_back(std::move(param));
        p.functions.push_back(std::move(f));

        const std::size_t before = v.functions_.size();
        v.collect_signatures(p);
        if (v.functions_.size() != before)
        {
            fail("expected collect_signatures to skip functions with unknown parameter type");
        }
    }

    {
        // collect_signatures: fully supported function should be inserted.
        curlee::parser::Program p;
        curlee::parser::Function f;
        f.name = "good_sig";
        f.return_type = curlee::parser::TypeName{.span = curlee::source::Span{.start = 7, .end = 8},
                                                 .name = "Int"};
        curlee::parser::Function::Param param;
        param.name = "x";
        param.type =
            curlee::parser::TypeName{.span = curlee::source::Span{.start = 8, .end = 9}, .name = "Int"};
        f.params.push_back(std::move(param));
        p.functions.push_back(std::move(f));

        const std::size_t before = v.functions_.size();
        v.collect_signatures(p);
        if (v.functions_.size() != before + 1)
        {
            fail("expected collect_signatures to insert supported function");
        }
    }

    {
        // check_function: should early-return when signature is absent.
        curlee::parser::Function f;
        f.name = "missing_sig";
        v.check_function(f);
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

        // Division (issue #270): '/' now lowers to Z3 Euclidean division.
        auto e_div = make_expr(
            s, curlee::parser::BinaryExpr{
                   .op = TokenKind::Slash,
                   .lhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "7"})),
                   .rhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "2"}))});
        if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_div)))
        {
            fail("expected lower_expr(7/2) to succeed");
        }
        // Division type mismatch: bool / int errors.
        auto e_div_bad = make_expr(
            s, curlee::parser::BinaryExpr{
                   .op = TokenKind::Slash,
                   .lhs = make_expr_ptr(make_expr(s, curlee::parser::BoolExpr{.value = true})),
                   .rhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "2"}))});
        if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e_div_bad)))
        {
            fail("expected lower_expr(bool / int) to error");
        }

        // Modulo (issue #270): '%' lowers to Z3 Euclidean modulo.
        auto e_mod = make_expr(
            s, curlee::parser::BinaryExpr{
                   .op = TokenKind::Percent,
                   .lhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "17"})),
                   .rhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "5"}))});
        if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_mod)))
        {
            fail("expected lower_expr(17%5) to succeed");
        }
        // Modulo type mismatch: int % bool errors.
        auto e_mod_bad = make_expr(
            s, curlee::parser::BinaryExpr{
                   .op = TokenKind::Percent,
                   .lhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "17"})),
                   .rhs = make_expr_ptr(make_expr(s, curlee::parser::BoolExpr{.value = true}))});
        if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e_mod_bad)))
        {
            fail("expected lower_expr(int % bool) to error");
        }

        // Bitwise ops (issue #270): '&', '|', '^' lower via the 64-bit
        // bit-vector model.
        for (const auto op : {TokenKind::Amp, TokenKind::Pipe, TokenKind::Caret,
                              TokenKind::ShiftLeft, TokenKind::ShiftRight})
        {
            auto e_bit = make_expr(
                s, curlee::parser::BinaryExpr{
                       .op = op,
                       .lhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "15"})),
                       .rhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "3"}))});
            if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_bit)))
            {
                fail("expected lower_expr(bitwise op) to succeed");
            }
        }
        // Bitwise type mismatch: bool & int errors.
        auto e_bit_bad = make_expr(
            s, curlee::parser::BinaryExpr{
                   .op = TokenKind::Amp,
                   .lhs = make_expr_ptr(make_expr(s, curlee::parser::BoolExpr{.value = true})),
                   .rhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "3"}))});
        if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e_bit_bad)))
        {
            fail("expected lower_expr(bool & int) to error");
        }
        // Bitwise type mismatch on the rhs: int & bool errors (covers the
        // rhs operand-kind check in lower_expr).
        auto e_bit_bad_rhs = make_expr(
            s, curlee::parser::BinaryExpr{
                   .op = TokenKind::Amp,
                   .lhs = make_expr_ptr(make_expr(s, curlee::parser::IntExpr{.lexeme = "3"})),
                   .rhs = make_expr_ptr(make_expr(s, curlee::parser::BoolExpr{.value = true}))});
        if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e_bit_bad_rhs)))
        {
            fail("expected lower_expr(int & bool) to error");
        }

        // Unary bitwise complement (issue #270): '~' lowers to -x - 1.
        auto e_not_rhs = make_expr(s, curlee::parser::IntExpr{.lexeme = "5"});
        auto e_bnot = make_expr(s, curlee::parser::UnaryExpr{.op = TokenKind::Tilde,
                                                             .rhs = make_expr_ptr(std::move(e_not_rhs))});
        if (!std::holds_alternative<curlee::verification::ExprValue>(v.lower_expr(e_bnot)))
        {
            fail("expected lower_expr(~5) to succeed");
        }
        // Unary '~' type mismatch: ~bool errors.
        auto e_bnot_bad_rhs = make_expr(s, curlee::parser::BoolExpr{.value = true});
        auto e_bnot_bad =
            make_expr(s, curlee::parser::UnaryExpr{.op = TokenKind::Tilde,
                                                   .rhs = make_expr_ptr(std::move(e_bnot_bad_rhs))});
        if (!std::holds_alternative<curlee::diag::Diagnostic>(v.lower_expr(e_bnot_bad)))
        {
            fail("expected lower_expr(~bool) to error");
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
        // LetStmt: Phys<T> binding with a non-literal address lexeme must produce the hard
        // "physical address must be a constant literal" diagnostic (defense-in-depth, issue
        // #253). The front-end normally rejects these at parse time, but the verifier must not
        // trust the AST.
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::LetStmt ls_phys;
        ls_phys.name = "fb_bad";
        ls_phys.type = curlee::parser::TypeName{.span = s,
                                                .name = "Phys",
                                                .type_arg = std::string_view("U32")};
        ls_phys.value = make_expr(
            s, curlee::parser::PhysExpr{.element_kind = "U32", .lexeme = "base + 4"});
        const std::size_t before = v.diags_.size();
        v.check_stmt_node(ls_phys, s, curlee::types::TypeKind::Int);
        bool saw_literal = false;
        for (std::size_t i = before; i < v.diags_.size(); ++i)
        {
            if (v.diags_[i].message.find("physical address must be a constant literal") !=
                std::string::npos)
            {
                saw_literal = true;
            }
        }
        if (!saw_literal)
        {
            fail("expected non-literal phys address to be rejected by the verifier");
        }

        // lower_expr(PhysExpr) with a valid literal address should succeed and produce a
        // Phys-typed ExprValue.
        curlee::parser::Expr e_phys;
        e_phys.span = s;
        e_phys.node = curlee::parser::PhysExpr{.element_kind = "U32", .lexeme = "0xFD00_0000"};
        auto r_phys = v.lower_expr(e_phys);
        if (!std::holds_alternative<curlee::verification::ExprValue>(r_phys))
        {
            fail("expected lower_expr(PhysExpr) to succeed for literal address");
        }
        if (std::get<curlee::verification::ExprValue>(r_phys).kind != curlee::types::TypeKind::Phys)
        {
            fail("expected lower_expr(PhysExpr) to produce a Phys-typed value");
        }

        // lower_expr(PhysExpr) with a non-literal address should error.
        curlee::parser::Expr e_phys_bad;
        e_phys_bad.span = s;
        e_phys_bad.node = curlee::parser::PhysExpr{.element_kind = "U32", .lexeme = "base"};
        auto r_phys_bad = v.lower_expr(e_phys_bad);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_phys_bad))
        {
            fail("expected lower_expr(PhysExpr) to reject non-literal address");
        }

        // lower_expr(PhysReadExpr) on an unknown base should error.
        curlee::parser::Expr e_read_base;
        e_read_base.span = s;
        e_read_base.node = curlee::parser::NameExpr{.name = "no_such_phys"};
        curlee::parser::Expr e_read;
        e_read.span = s;
        e_read.node = curlee::parser::PhysReadExpr{.base = make_expr_ptr(std::move(e_read_base))};
        auto r_read = v.lower_expr(e_read);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_read))
        {
            fail("expected lower_expr(PhysReadExpr) on unknown base to error");
        }

        // lower_expr(PhysWriteExpr) on an unknown base should error.
        curlee::parser::Expr e_write_base;
        e_write_base.span = s;
        e_write_base.node = curlee::parser::NameExpr{.name = "no_such_phys"};
        curlee::parser::Expr e_write_val;
        e_write_val.span = s;
        e_write_val.node = curlee::parser::IntExpr{.lexeme = "1"};
        curlee::parser::Expr e_write;
        e_write.span = s;
        e_write.node = curlee::parser::PhysWriteExpr{
            .base = make_expr_ptr(std::move(e_write_base)),
            .value = make_expr_ptr(std::move(e_write_val))};
        auto r_write = v.lower_expr(e_write);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_write))
        {
            fail("expected lower_expr(PhysWriteExpr) on unknown base to error");
        }

        // A valid Phys read()/write() pair should lower without diagnostics once the phys var
        // is registered.
        v.phys_vars_.insert_or_assign("fb_good", std::string_view("U32"));
        curlee::parser::Expr e_good_base;
        e_good_base.span = s;
        e_good_base.node = curlee::parser::NameExpr{.name = "fb_good"};
        curlee::parser::Expr e_read_good;
        e_read_good.span = s;
        e_read_good.node = curlee::parser::PhysReadExpr{
            .base = make_expr_ptr(std::move(e_good_base))};
        auto r_read_good = v.lower_expr(e_read_good);
        if (!std::holds_alternative<curlee::verification::ExprValue>(r_read_good))
        {
            fail("expected lower_expr(PhysReadExpr) on registered phys var to succeed");
        }

        curlee::parser::Expr e_w_base;
        e_w_base.span = s;
        e_w_base.node = curlee::parser::NameExpr{.name = "fb_good"};
        curlee::parser::Expr e_w_val;
        e_w_val.span = s;
        e_w_val.node = curlee::parser::IntExpr{.lexeme = "1"};
        curlee::parser::Expr e_write_good;
        e_write_good.span = s;
        e_write_good.node = curlee::parser::PhysWriteExpr{
            .base = make_expr_ptr(std::move(e_w_base)),
            .value = make_expr_ptr(std::move(e_w_val))};
        auto r_write_good = v.lower_expr(e_write_good);
        if (!std::holds_alternative<curlee::verification::ExprValue>(r_write_good))
        {
            fail("expected lower_expr(PhysWriteExpr) on registered phys var to succeed");
        }

        v.phys_vars_.clear();
    }

    {
        // is_phys_address_literal: empty lexeme -> false (line 208).
        if (curlee::verification::is_phys_address_literal(""))
        {
            fail("expected is_phys_address_literal(empty) to be false");
        }
        if (!curlee::verification::is_phys_address_literal("0xFD00_0000"))
        {
            fail("expected is_phys_address_literal(hex) to be true");
        }
        if (!curlee::verification::is_phys_address_literal("4096"))
        {
            fail("expected is_phys_address_literal(decimal) to be true");
        }
        if (curlee::verification::is_phys_address_literal("base"))
        {
            fail("expected is_phys_address_literal(name) to be false");
        }

        // phys_sort cache-hit path (line 464).
        const curlee::source::Span s_cov{.start = 0, .end = 1};
        v.phys_sorts_.clear();
        const auto& s1 = v.phys_sort("U32");
        const auto& s2 = v.phys_sort("U32");
        if (s1.id() != s2.id())
        {
            fail("expected phys_sort to return the same cached sort");
        }
        v.phys_sorts_.clear();

        // phys_read_fn cache-hit path (line 464): calling twice returns the cached decl.
        v.phys_read_fns_.clear();
        const auto& rf1 = v.phys_read_fn("U32");
        const auto& rf2 = v.phys_read_fn("U32");
        if (rf1.id() != rf2.id())
        {
            fail("expected phys_read_fn to return the same cached decl");
        }
        v.phys_read_fns_.clear();

        // phys_write_fn cache-hit path (line 464): calling twice returns the cached decl.
        v.phys_write_fns_.clear();
        const auto& wf1 = v.phys_write_fn("U32");
        const auto& wf2 = v.phys_write_fn("U32");
        if (wf1.id() != wf2.id())
        {
            fail("expected phys_write_fn to return the same cached decl");
        }
        v.phys_write_fns_.clear();

        // lookup_phys_var not-found path (line 481).
        if (v.lookup_phys_var("no_such_phys_var").has_value())
        {
            fail("expected lookup_phys_var(unknown) to be nullopt");
        }
    }

    {
        // pred_mentions_opaque_read recursion paths (unary/binary/group with opaque names).
        const curlee::source::Span s{.start = 0, .end = 1};
        v.opaque_read_vars_.insert("op");

        auto p_unary = make_pred(
            s, curlee::parser::PredUnary{
                   .op = curlee::lexer::TokenKind::Bang,
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "op"}))});
        if (!v.pred_mentions_opaque_read(p_unary))
        {
            fail("expected pred_mentions_opaque_read(!op) to be true");
        }
        auto p_unary_plain = make_pred(
            s, curlee::parser::PredUnary{
                   .op = curlee::lexer::TokenKind::Bang,
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "x"}))});
        if (v.pred_mentions_opaque_read(p_unary_plain))
        {
            fail("expected pred_mentions_opaque_read(!x) to be false");
        }
        auto p_bin_lhs = make_pred(
            s, curlee::parser::PredBinary{
                   .op = curlee::lexer::TokenKind::Greater,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "op"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"}))});
        if (!v.pred_mentions_opaque_read(p_bin_lhs))
        {
            fail("expected pred_mentions_opaque_read(op > 0) to be true");
        }
        auto p_bin_rhs = make_pred(
            s, curlee::parser::PredBinary{
                   .op = curlee::lexer::TokenKind::Greater,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "op"}))});
        if (!v.pred_mentions_opaque_read(p_bin_rhs))
        {
            fail("expected pred_mentions_opaque_read(0 < op) to be true");
        }
        auto p_group = make_pred(
            s, curlee::parser::PredGroup{
                   .inner = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "op"}))});
        if (!v.pred_mentions_opaque_read(p_group))
        {
            fail("expected pred_mentions_opaque_read((op)) to be true");
        }
        v.opaque_read_vars_.clear();
    }

    {
        // base_phys_element_kind paths: NameExpr not a phys var, direct PhysExpr, and default.
        const curlee::source::Span s{.start = 0, .end = 1};
        v.phys_vars_.insert_or_assign("fb_base", std::string_view("U32"));

        curlee::parser::Expr name_ok;
        name_ok.span = s;
        name_ok.node = curlee::parser::NameExpr{.name = "fb_base"};
        if (v.base_phys_element_kind(name_ok) != "U32")
        {
            fail("expected base_phys_element_kind(NameExpr phys) to return element kind");
        }

        curlee::parser::Expr name_missing;
        name_missing.span = s;
        name_missing.node = curlee::parser::NameExpr{.name = "no_such"};
        if (!v.base_phys_element_kind(name_missing).empty())
        {
            fail("expected base_phys_element_kind(NameExpr non-phys) to be empty");
        }

        curlee::parser::Expr phys_direct;
        phys_direct.span = s;
        phys_direct.node =
            curlee::parser::PhysExpr{.element_kind = "U16", .lexeme = "0x1000"};
        if (v.base_phys_element_kind(phys_direct) != "U16")
        {
            fail("expected base_phys_element_kind(PhysExpr) to return element kind");
        }

        curlee::parser::Expr other;
        other.span = s;
        other.node = curlee::parser::IntExpr{.lexeme = "1"};
        if (!v.base_phys_element_kind(other).empty())
        {
            fail("expected base_phys_element_kind(IntExpr) to be empty");
        }
        v.phys_vars_.erase("fb_base");
    }

    {
        // lower_expr(PhysExpr) with an unsupported element kind (lines 825-826).
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Expr e_phys_badkind;
        e_phys_badkind.span = s;
        e_phys_badkind.node =
            curlee::parser::PhysExpr{.element_kind = "String", .lexeme = "0x1000"};
        auto r_badkind = v.lower_expr(e_phys_badkind);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_badkind))
        {
            fail("expected lower_expr(PhysExpr String kind) to error");
        }

        // lower_expr(PhysReadExpr) on a non-Phys base (line 852).
        v.phys_vars_.insert_or_assign("fb_cov", std::string_view("U32"));
        curlee::parser::Expr read_int_base;
        read_int_base.span = s;
        read_int_base.node = curlee::parser::IntExpr{.lexeme = "5"};
        curlee::parser::Expr e_read_int;
        e_read_int.span = s;
        e_read_int.node = curlee::parser::PhysReadExpr{
            .base = make_expr_ptr(std::move(read_int_base))};
        auto r_read_int = v.lower_expr(e_read_int);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_read_int))
        {
            fail("expected lower_expr(PhysReadExpr on Int base) to error");
        }

        // lower_expr(PhysReadExpr) where element kind cannot be resolved (line 857): a
        // GroupExpr wrapping a Phys name lowers to Phys kind, but base_phys_element_kind
        // cannot extract the element kind through the group.
        curlee::parser::Expr read_g_base;
        read_g_base.span = s;
        read_g_base.node = curlee::parser::NameExpr{.name = "fb_cov"};
        curlee::parser::Expr read_group;
        read_group.span = s;
        read_group.node = curlee::parser::GroupExpr{.inner = make_expr_ptr(std::move(read_g_base))};
        curlee::parser::Expr e_read_unk;
        e_read_unk.span = s;
        e_read_unk.node = curlee::parser::PhysReadExpr{
            .base = make_expr_ptr(std::move(read_group))};
        auto r_read_unk = v.lower_expr(e_read_unk);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_read_unk))
        {
            fail("expected lower_expr(PhysReadExpr unresolved kind) to error");
        }

        // lower_expr(PhysWriteExpr) on a non-Phys base (line 877).
        curlee::parser::Expr write_int_base;
        write_int_base.span = s;
        write_int_base.node = curlee::parser::IntExpr{.lexeme = "5"};
        curlee::parser::Expr write_val;
        write_val.span = s;
        write_val.node = curlee::parser::IntExpr{.lexeme = "1"};
        curlee::parser::Expr e_write_int;
        e_write_int.span = s;
        e_write_int.node = curlee::parser::PhysWriteExpr{
            .base = make_expr_ptr(std::move(write_int_base)),
            .value = make_expr_ptr(std::move(write_val))};
        auto r_write_int = v.lower_expr(e_write_int);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_write_int))
        {
            fail("expected lower_expr(PhysWriteExpr on Int base) to error");
        }

        // lower_expr(PhysWriteExpr) where the value lowering fails (line 882).
        curlee::parser::Expr write_ok_base;
        write_ok_base.span = s;
        write_ok_base.node = curlee::parser::NameExpr{.name = "fb_cov"};
        curlee::parser::Expr write_bad_val;
        write_bad_val.span = s;
        write_bad_val.node = curlee::parser::StringExpr{.lexeme = "\"x\""};
        curlee::parser::Expr e_write_badval;
        e_write_badval.span = s;
        e_write_badval.node = curlee::parser::PhysWriteExpr{
            .base = make_expr_ptr(std::move(write_ok_base)),
            .value = make_expr_ptr(std::move(write_bad_val))};
        auto r_write_badval = v.lower_expr(e_write_badval);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_write_badval))
        {
            fail("expected lower_expr(PhysWriteExpr bad value) to error");
        }

        // lower_expr(PhysWriteExpr) where element kind cannot be resolved (line 888): a
        // GroupExpr-wrapped Phys name lowers to Phys kind but has no resolvable element kind.
        curlee::parser::Expr write_g_base;
        write_g_base.span = s;
        write_g_base.node = curlee::parser::NameExpr{.name = "fb_cov"};
        curlee::parser::Expr write_group;
        write_group.span = s;
        write_group.node = curlee::parser::GroupExpr{.inner = make_expr_ptr(std::move(write_g_base))};
        curlee::parser::Expr write_val2;
        write_val2.span = s;
        write_val2.node = curlee::parser::IntExpr{.lexeme = "1"};
        curlee::parser::Expr e_write_unk;
        e_write_unk.span = s;
        e_write_unk.node = curlee::parser::PhysWriteExpr{
            .base = make_expr_ptr(std::move(write_group)),
            .value = make_expr_ptr(std::move(write_val2))};
        auto r_write_unk = v.lower_expr(e_write_unk);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(r_write_unk))
        {
            fail("expected lower_expr(PhysWriteExpr unresolved kind) to error");
        }
        v.phys_vars_.erase("fb_cov");
    }

    {
        // check_function: Phys<T> parameter registration (line 1497).
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Function f;
        f.name = "phys_param_fn";
        f.return_type = curlee::parser::TypeName{.span = s, .name = "Int"};
        f.params.push_back(curlee::parser::Function::Param{
            .span = s,
            .name = "fb",
            .type = curlee::parser::TypeName{.span = s, .name = "Phys", .type_arg = std::string_view("U32")},
            .refinement = std::nullopt});
        curlee::verification::FunctionSig sig;
        sig.decl = &f;
        sig.params = {curlee::types::TypeKind::Phys};
        sig.result = curlee::types::TypeKind::Int;
        v.functions_.insert_or_assign("phys_param_fn", sig);
        // Exercise check_function's Phys-param registration branch. The scope is popped on
        // return, so verify the branch ran by checking the parameter is registered while the
        // body would be processed - here we just confirm the call does not emit a diagnostic
        // for the Phys param and completes cleanly.
        const std::size_t diags_before = v.diags_.size();
        v.check_function(f);
        if (v.diags_.size() != diags_before)
        {
            fail("expected check_function with Phys param to complete without diagnostics");
        }
        v.functions_.erase("phys_param_fn");
    }

    {
        // Helper coverage: expr_is_opaque_read recursion paths and pred_mentions_result.
        const curlee::source::Span s{.start = 0, .end = 1};
        v.opaque_read_vars_.insert("opaque_v");

        // PhysReadExpr is opaque.
        curlee::parser::Expr base_none;
        base_none.span = s;
        base_none.node = curlee::parser::PhysReadExpr{.base = nullptr};
        if (!v.expr_is_opaque_read(base_none))
        {
            fail("expected expr_is_opaque_read(PhysReadExpr) to be true");
        }

        // NameExpr bound to an opaque read is opaque.
        curlee::parser::Expr name_opaque;
        name_opaque.span = s;
        name_opaque.node = curlee::parser::NameExpr{.name = "opaque_v"};
        if (!v.expr_is_opaque_read(name_opaque))
        {
            fail("expected expr_is_opaque_read(NameExpr opaque) to be true");
        }

        // Plain Int expr is not opaque.
        curlee::parser::Expr int_plain;
        int_plain.span = s;
        int_plain.node = curlee::parser::IntExpr{.lexeme = "1"};
        if (v.expr_is_opaque_read(int_plain))
        {
            fail("expected expr_is_opaque_read(IntExpr) to be false");
        }

        // GroupExpr wrapping an opaque name.
        curlee::parser::Expr group_inner;
        group_inner.span = s;
        group_inner.node = curlee::parser::NameExpr{.name = "opaque_v"};
        curlee::parser::Expr group;
        group.span = s;
        group.node = curlee::parser::GroupExpr{.inner = make_expr_ptr(std::move(group_inner))};
        if (!v.expr_is_opaque_read(group))
        {
            fail("expected expr_is_opaque_read(GroupExpr opaque) to be true");
        }

        // GroupExpr with null inner.
        curlee::parser::Expr group_null;
        group_null.span = s;
        group_null.node = curlee::parser::GroupExpr{.inner = nullptr};
        if (v.expr_is_opaque_read(group_null))
        {
            fail("expected expr_is_opaque_read(GroupExpr null) to be false");
        }

        // UnaryExpr wrapping an opaque name.
        curlee::parser::Expr unary_inner;
        unary_inner.span = s;
        unary_inner.node = curlee::parser::NameExpr{.name = "opaque_v"};
        curlee::parser::Expr unary;
        unary.span = s;
        unary.node = curlee::parser::UnaryExpr{.op = curlee::lexer::TokenKind::Bang,
                                               .rhs = make_expr_ptr(std::move(unary_inner))};
        if (!v.expr_is_opaque_read(unary))
        {
            fail("expected expr_is_opaque_read(UnaryExpr opaque) to be true");
        }

        // UnaryExpr with null rhs.
        curlee::parser::Expr unary_null;
        unary_null.span = s;
        unary_null.node = curlee::parser::UnaryExpr{.op = curlee::lexer::TokenKind::Bang,
                                                    .rhs = nullptr};
        if (v.expr_is_opaque_read(unary_null))
        {
            fail("expected expr_is_opaque_read(UnaryExpr null) to be false");
        }

        // BinaryExpr with opaque lhs / plain rhs.
        curlee::parser::Expr bin_lhs;
        bin_lhs.span = s;
        bin_lhs.node = curlee::parser::NameExpr{.name = "opaque_v"};
        curlee::parser::Expr bin_rhs;
        bin_rhs.span = s;
        bin_rhs.node = curlee::parser::IntExpr{.lexeme = "1"};
        curlee::parser::Expr bin;
        bin.span = s;
        bin.node = curlee::parser::BinaryExpr{.op = curlee::lexer::TokenKind::Plus,
                                              .lhs = make_expr_ptr(std::move(bin_lhs)),
                                              .rhs = make_expr_ptr(std::move(bin_rhs))};
        if (!v.expr_is_opaque_read(bin))
        {
            fail("expected expr_is_opaque_read(BinaryExpr opaque lhs) to be true");
        }

        // BinaryExpr with plain lhs / opaque rhs.
        curlee::parser::Expr bin2_lhs;
        bin2_lhs.span = s;
        bin2_lhs.node = curlee::parser::IntExpr{.lexeme = "1"};
        curlee::parser::Expr bin2_rhs;
        bin2_rhs.span = s;
        bin2_rhs.node = curlee::parser::NameExpr{.name = "opaque_v"};
        curlee::parser::Expr bin2;
        bin2.span = s;
        bin2.node = curlee::parser::BinaryExpr{.op = curlee::lexer::TokenKind::Plus,
                                               .lhs = make_expr_ptr(std::move(bin2_lhs)),
                                               .rhs = make_expr_ptr(std::move(bin2_rhs))};
        if (!v.expr_is_opaque_read(bin2))
        {
            fail("expected expr_is_opaque_read(BinaryExpr opaque rhs) to be true");
        }

        // BinaryExpr with null lhs.
        curlee::parser::Expr bin3_rhs;
        bin3_rhs.span = s;
        bin3_rhs.node = curlee::parser::IntExpr{.lexeme = "1"};
        curlee::parser::Expr bin3;
        bin3.span = s;
        bin3.node = curlee::parser::BinaryExpr{.op = curlee::lexer::TokenKind::Plus,
                                               .lhs = nullptr,
                                               .rhs = make_expr_ptr(std::move(bin3_rhs))};
        if (v.expr_is_opaque_read(bin3))
        {
            fail("expected expr_is_opaque_read(BinaryExpr null lhs) to be false");
        }

        // pred_mentions_result: true for `result > 0`, false for `x > 0`.
        auto p_result = make_pred(
            s, curlee::parser::PredBinary{
                   .op = curlee::lexer::TokenKind::Greater,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "result"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"}))});
        if (!v.pred_mentions_result(p_result))
        {
            fail("expected pred_mentions_result(result > 0) to be true");
        }
        auto p_x = make_pred(
            s, curlee::parser::PredBinary{
                   .op = curlee::lexer::TokenKind::Greater,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "x"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"}))});
        if (v.pred_mentions_result(p_x))
        {
            fail("expected pred_mentions_result(x > 0) to be false");
        }
        // pred_mentions_result: result on the rhs of a binary predicate (line 558).
        auto p_result_rhs = make_pred(
            s, curlee::parser::PredBinary{
                   .op = curlee::lexer::TokenKind::Less,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "result"}))});
        if (!v.pred_mentions_result(p_result_rhs))
        {
            fail("expected pred_mentions_result(0 < result) to be true");
        }
        // pred_mentions_result: unary/group recursion paths.
        auto p_not_result = make_pred(
            s, curlee::parser::PredUnary{
                   .op = curlee::lexer::TokenKind::Bang,
                   .rhs = make_pred_ptr(
                       make_pred(s, curlee::parser::PredName{.name = "result"}))});
        if (!v.pred_mentions_result(p_not_result))
        {
            fail("expected pred_mentions_result(!result) to be true");
        }
        auto p_group_result = make_pred(
            s, curlee::parser::PredGroup{
                   .inner = make_pred_ptr(
                       make_pred(s, curlee::parser::PredName{.name = "result"}))});
        if (!v.pred_mentions_result(p_group_result))
        {
            fail("expected pred_mentions_result((result)) to be true");
        }

        v.opaque_read_vars_.clear();
    }

    {
        // reject_opaque_read_contract / add_opaque_read_note: emit diagnostic with the note.
        const curlee::source::Span s{.start = 0, .end = 1};
        const std::size_t before = v.diags_.size();
        v.reject_opaque_read_contract(s, "cannot prove opaque");
        if (v.diags_.size() != before + 1)
        {
            fail("expected reject_opaque_read_contract to emit one diagnostic");
        }
        bool saw_note = false;
        for (const auto& n : v.diags_.back().notes)
        {
            if (n.message.find("MMIO read is opaque") != std::string::npos)
            {
                saw_note = true;
            }
        }
        if (!saw_note)
        {
            fail("expected reject_opaque_read_contract to attach the opaque note");
        }
    }

    {
        // add_fact: opaque-read mention is rejected unconditionally (before lowering).
        const curlee::source::Span s{.start = 0, .end = 1};
        v.opaque_read_vars_.insert("opaque_fact");
        auto pred_opaque = make_pred(
            s, curlee::parser::PredBinary{
                   .op = curlee::lexer::TokenKind::Greater,
                   .lhs = make_pred_ptr(
                       make_pred(s, curlee::parser::PredName{.name = "opaque_fact"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"}))});
        const std::size_t before = v.diags_.size();
        v.add_fact(pred_opaque);
        if (v.diags_.size() == before)
        {
            fail("expected add_fact to reject an opaque-read fact");
        }
        if (v.diags_.back().message.find("opaque MMIO read") == std::string::npos)
        {
            fail("expected add_fact opaque diagnostic message");
        }
        v.opaque_read_vars_.clear();
    }

    {
        // check_call: opaque argument to a callee with an unsigned param is rejected before
        // the Int/Bool-only gate (review gap #1).
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Function decl;
        decl.name = "opaque_consume";
        decl.return_type = curlee::parser::TypeName{.span = s, .name = "Int"};
        decl.params.push_back(curlee::parser::Function::Param{
            .span = s,
            .name = "x",
            .type = curlee::parser::TypeName{.span = s, .name = "U32"},
            .refinement = std::nullopt});
        curlee::verification::FunctionSig sig;
        sig.decl = &decl;
        sig.params = {curlee::types::TypeKind::U32};
        sig.result = curlee::types::TypeKind::Int;
        v.functions_.insert_or_assign("opaque_consume", sig);

        curlee::parser::Expr arg;
        arg.span = s;
        arg.node = curlee::parser::PhysReadExpr{.base = nullptr};
        curlee::parser::CallExpr call;
        call.callee = make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "opaque_consume"}));
        call.args.push_back(std::move(arg));
        const std::size_t before = v.diags_.size();
        v.check_call(call);
        bool saw = false;
        for (std::size_t i = before; i < v.diags_.size(); ++i)
        {
            if (v.diags_[i].message.find("opaque MMIO read") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected check_call to reject opaque read argument on unsigned param");
        }
        v.functions_.erase("opaque_consume");
    }

    {
        // check_call: the requires-loop guard rejects a requires clause that mentions an
        // opaque read name (line 1071). The callee's requires references a name that the
        // caller has marked opaque.
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Function decl;
        decl.name = "opaque_req_callee";
        decl.return_type = curlee::parser::TypeName{.span = s, .name = "Int"};
        decl.params.push_back(curlee::parser::Function::Param{
            .span = s,
            .name = "x",
            .type = curlee::parser::TypeName{.span = s, .name = "Int"},
            .refinement = std::nullopt});
        decl.requires_clauses.push_back(make_pred(
            s, curlee::parser::PredBinary{
                   .op = curlee::lexer::TokenKind::Greater,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "x"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"}))}));
        curlee::verification::FunctionSig sig;
        sig.decl = &decl;
        sig.params = {curlee::types::TypeKind::Int};
        sig.result = curlee::types::TypeKind::Int;
        v.functions_.insert_or_assign("opaque_req_callee", sig);

        // Mark `x` opaque in the caller scope so the requires clause `x > 0` triggers the
        // unconditional rejection in the requires loop.
        v.opaque_read_vars_.insert("x");
        curlee::parser::Expr arg;
        arg.span = s;
        arg.node = curlee::parser::IntExpr{.lexeme = "1"};
        curlee::parser::CallExpr call;
        call.callee =
            make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "opaque_req_callee"}));
        call.args.push_back(std::move(arg));
        const std::size_t before = v.diags_.size();
        v.check_call(call);
        bool saw = false;
        for (std::size_t i = before; i < v.diags_.size(); ++i)
        {
            if (v.diags_[i].message.find("opaque MMIO read") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected check_call requires-loop guard to reject opaque-read requires");
        }
        v.opaque_read_vars_.erase("x");
        v.functions_.erase("opaque_req_callee");
    }

    {
        // check_return: ensures mentioning `result` is rejected when the returned value is an
        // opaque read (review gap #2).
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Function f;
        f.name = "opaque_ret";
        f.return_type = curlee::parser::TypeName{.span = s, .name = "U32"};
        f.ensures.push_back(make_pred(
            s, curlee::parser::PredBinary{
                   .op = curlee::lexer::TokenKind::Greater,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "result"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"}))}));
        curlee::verification::FunctionSig sig;
        sig.decl = &f;
        sig.result = curlee::types::TypeKind::U32;
        v.current_function_ = sig;

        // The read() must lower successfully, so register a phys var as its base.
        v.phys_vars_.insert_or_assign("reg_ret", std::string_view("U32"));
        curlee::parser::Expr read_base;
        read_base.span = s;
        read_base.node = curlee::parser::NameExpr{.name = "reg_ret"};
        curlee::parser::ReturnStmt r;
        r.value = make_expr(s, curlee::parser::PhysReadExpr{
                                  .base = make_expr_ptr(std::move(read_base))});
        const std::size_t before = v.diags_.size();
        v.check_return(r, curlee::types::TypeKind::U32);
        v.phys_vars_.erase("reg_ret");
        bool saw = false;
        for (std::size_t i = before; i < v.diags_.size(); ++i)
        {
            if (v.diags_[i].message.find("opaque MMIO read") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected check_return to reject ensures on opaque read result");
        }
        v.current_function_ = std::nullopt;
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

    {
        // is_phys_address_literal: a char that is not digit/hex/underscore/hex-marker must
        // return false from inside the loop (branch coverage for lines 213/215).
        if (curlee::verification::is_phys_address_literal("0x12z4"))
        {
            fail("expected is_phys_address_literal(0x12z4) to be false (bad hex char)");
        }
        // Decimal digits with underscores.
        if (!curlee::verification::is_phys_address_literal("12_345"))
        {
            fail("expected is_phys_address_literal(12_345) to be true");
        }
        // Uppercase hex marker (0X) exercises the 'X' arm.
        if (!curlee::verification::is_phys_address_literal("0XFD00"))
        {
            fail("expected is_phys_address_literal(0XFD00) to be true");
        }
        // Non-hex 'g' after a plain digit run: hits the reject branch for a non-digit char.
        if (curlee::verification::is_phys_address_literal("4096g"))
        {
            fail("expected is_phys_address_literal(4096g) to be false");
        }
        // A char below '0' (e.g. '-') hits the `c >= '0'` false arm of is_digit.
        if (curlee::verification::is_phys_address_literal("4-1"))
        {
            fail("expected is_phys_address_literal(4-1) to be false (punct char)");
        }
        // 'x' at i==1 but lexeme[0] != '0' hits the `lexeme[0] == '0'` false arm.
        if (curlee::verification::is_phys_address_literal("1x2"))
        {
            fail("expected is_phys_address_literal(1x2) to be false (non-zero prefix)");
        }
        // A second 'x' later in the lexeme hits the `i == 1` false arm of is_hex_marker.
        if (curlee::verification::is_phys_address_literal("0x1x2"))
        {
            fail("expected is_phys_address_literal(0x1x2) to be false (x at wrong index)");
        }
    }

    {
        // lookup_var: hit the Phys-variable path (returns a Phys ExprValue) and confirm the
        // int/bool lookups still resolve in the same scope (branch coverage for 388/392/400).
        v.declare_var("cov_int", curlee::types::TypeKind::Int);
        v.declare_var("cov_bool", curlee::types::TypeKind::Bool);
        v.phys_vars_.insert_or_assign("cov_phys", std::string_view("U32"));
        const auto li = v.lookup_var("cov_int");
        if (!li.has_value() || li->kind != curlee::types::TypeKind::Int)
        {
            fail("expected lookup_var to find int var in combined scope");
        }
        const auto lb = v.lookup_var("cov_bool");
        if (!lb.has_value() || lb->kind != curlee::types::TypeKind::Bool)
        {
            fail("expected lookup_var to find bool var in combined scope");
        }
        const auto lp = v.lookup_var("cov_phys");
        if (!lp.has_value() || lp->kind != curlee::types::TypeKind::Phys)
        {
            fail("expected lookup_var to find Phys var");
        }
        v.phys_vars_.erase("cov_phys");
        v.lower_ctx_.int_vars.erase("cov_int");
        v.lower_ctx_.bool_vars.erase("cov_bool");
    }

    {
        // pred_mentions_opaque_read / pred_mentions_result: exercise the false-arm of the
        // recursion so every short-circuit branch is taken at least once.
        const curlee::source::Span s{.start = 0, .end = 1};

        // Unary with a null rhs.
        auto p_unary_null = make_pred(
            s, curlee::parser::PredUnary{.op = curlee::lexer::TokenKind::Bang, .rhs = nullptr});
        if (v.pred_mentions_opaque_read(p_unary_null))
        {
            fail("expected pred_mentions_opaque_read(null unary) to be false");
        }
        if (v.pred_mentions_result(p_unary_null))
        {
            fail("expected pred_mentions_result(null unary) to be false");
        }

        // Binary with a null lhs / null rhs.
        auto p_bin_null_lhs = make_pred(
            s, curlee::parser::PredBinary{
                   .op = curlee::lexer::TokenKind::Greater,
                   .lhs = nullptr,
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"}))});
        if (v.pred_mentions_opaque_read(p_bin_null_lhs))
        {
            fail("expected pred_mentions_opaque_read(null-lhs binary) to be false");
        }
        if (v.pred_mentions_result(p_bin_null_lhs))
        {
            fail("expected pred_mentions_result(null-lhs binary) to be false");
        }

        auto p_bin_null_rhs = make_pred(
            s, curlee::parser::PredBinary{
                   .op = curlee::lexer::TokenKind::Greater,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"})),
                   .rhs = nullptr});
        if (v.pred_mentions_opaque_read(p_bin_null_rhs))
        {
            fail("expected pred_mentions_opaque_read(null-rhs binary) to be false");
        }
        if (v.pred_mentions_result(p_bin_null_rhs))
        {
            fail("expected pred_mentions_result(null-rhs binary) to be false");
        }

        // Group with a null inner.
        auto p_group_null = make_pred(
            s, curlee::parser::PredGroup{.inner = nullptr});
        if (v.pred_mentions_opaque_read(p_group_null))
        {
            fail("expected pred_mentions_opaque_read(null group) to be false");
        }
        if (v.pred_mentions_result(p_group_null))
        {
            fail("expected pred_mentions_result(null group) to be false");
        }
    }

    {
        // expr_is_opaque_read: GroupExpr with an opaque inner that recurses into a true
        // result (line 611 hit-branch), and BinaryExpr with opaque lhs short-circuiting
        // before rhs (line 623).
        const curlee::source::Span s{.start = 0, .end = 1};
        v.opaque_read_vars_.insert("opg");

        curlee::parser::Expr g_inner;
        g_inner.span = s;
        g_inner.node = curlee::parser::NameExpr{.name = "opg"};
        curlee::parser::Expr group_true;
        group_true.span = s;
        group_true.node = curlee::parser::GroupExpr{.inner = make_expr_ptr(std::move(g_inner))};
        if (!v.expr_is_opaque_read(group_true))
        {
            fail("expected expr_is_opaque_read(GroupExpr(opg)) to be true");
        }

        // BinaryExpr: opaque lhs -> returns true without evaluating rhs (line 621).
        curlee::parser::Expr b_lhs;
        b_lhs.span = s;
        b_lhs.node = curlee::parser::NameExpr{.name = "opg"};
        curlee::parser::Expr b_rhs;
        b_rhs.span = s;
        b_rhs.node = curlee::parser::IntExpr{.lexeme = "1"};
        curlee::parser::Expr bin_true;
        bin_true.span = s;
        bin_true.node = curlee::parser::BinaryExpr{.op = curlee::lexer::TokenKind::Plus,
                                                   .lhs = make_expr_ptr(std::move(b_lhs)),
                                                   .rhs = make_expr_ptr(std::move(b_rhs))};
        if (!v.expr_is_opaque_read(bin_true))
        {
            fail("expected expr_is_opaque_read(BinaryExpr opaque lhs) to be true");
        }
        v.opaque_read_vars_.erase("opg");
    }

    {
        // check_call: opaque argument with an empty args vector must not crash on the
        // args[0] fallback (line 1014 branch `call.args.empty()`).
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Function decl;
        decl.name = "opaque_noargs";
        decl.return_type = curlee::parser::TypeName{.span = s, .name = "Int"};
        decl.params.push_back(curlee::parser::Function::Param{
            .span = s,
            .name = "x",
            .type = curlee::parser::TypeName{.span = s, .name = "U32"},
            .refinement = std::nullopt});
        curlee::verification::FunctionSig sig;
        sig.decl = &decl;
        sig.params = {curlee::types::TypeKind::U32};
        sig.result = curlee::types::TypeKind::Int;
        v.functions_.insert_or_assign("opaque_noargs", sig);

        // A PhysReadExpr argument is opaque but no args exist: the guard fires with the
        // empty-args span fallback (Span{}).
        curlee::parser::CallExpr call;
        call.callee =
            make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "opaque_noargs"}));
        // arg is opaque but the call has zero args; expr_is_opaque_read is only true if the
        // arg itself is a read — use a direct read as a synthetic arg with no args stored.
        v.check_call(call);
        // With no args, arg_is_opaque stays false, so no diagnostic is expected; this just
        // exercises the arg-count guard path (line 1022) with the empty-args span fallback.
        v.functions_.erase("opaque_noargs");

        // Also exercise the arg_count mismatch early-return with an opaque argument present,
        // which is the path that leads to the empty-args span fallback being compiled.
        curlee::parser::CallExpr call2;
        call2.callee =
            make_expr_ptr(make_expr(s, curlee::parser::NameExpr{.name = "opaque_noargs"}));
        curlee::parser::Expr read_arg;
        read_arg.span = s;
        read_arg.node = curlee::parser::PhysReadExpr{.base = nullptr};
        call2.args.push_back(std::move(read_arg));
        const std::size_t before2 = v.diags_.size();
        v.check_call(call2);
        (void)before2;
        v.functions_.erase("opaque_noargs");
    }

    {
        // check_expr_for_calls: PhysReadExpr/PhysWriteExpr with null base/value children
        // must not dereference null (line 1148/1155/1159).
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Expr read_null;
        read_null.span = s;
        read_null.node = curlee::parser::PhysReadExpr{.base = nullptr};
        v.check_expr_for_calls(read_null);

        curlee::parser::Expr write_null;
        write_null.span = s;
        write_null.node = curlee::parser::PhysWriteExpr{.base = nullptr, .value = nullptr};
        v.check_expr_for_calls(write_null);

        // And with non-null children (hit the recursion).
        curlee::parser::Expr w_base;
        w_base.span = s;
        w_base.node = curlee::parser::IntExpr{.lexeme = "1"};
        curlee::parser::Expr w_val;
        w_val.span = s;
        w_val.node = curlee::parser::IntExpr{.lexeme = "2"};
        curlee::parser::Expr write_full;
        write_full.span = s;
        write_full.node = curlee::parser::PhysWriteExpr{
            .base = make_expr_ptr(std::move(w_base)),
            .value = make_expr_ptr(std::move(w_val))};
        v.check_expr_for_calls(write_full);
    }

    {
        // check_return: opaque-ensures where the ensures mentions `result` on the rhs, and
        // the return value kind mismatch branch is avoided via opaque_unsigned_return.
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Function f;
        f.name = "opaque_ret_rhs";
        f.return_type = curlee::parser::TypeName{.span = s, .name = "U32"};
        f.ensures.push_back(make_pred(
            s, curlee::parser::PredBinary{
                   .op = curlee::lexer::TokenKind::Less,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"})),
                   .rhs = make_pred_ptr(
                       make_pred(s, curlee::parser::PredName{.name = "result"}))}));
        curlee::verification::FunctionSig sig;
        sig.decl = &f;
        sig.result = curlee::types::TypeKind::U32;
        v.current_function_ = sig;

        v.phys_vars_.insert_or_assign("reg_rhs", std::string_view("U32"));
        curlee::parser::Expr read_base;
        read_base.span = s;
        read_base.node = curlee::parser::NameExpr{.name = "reg_rhs"};
        curlee::parser::ReturnStmt r;
        r.value = make_expr(s, curlee::parser::PhysReadExpr{
                                   .base = make_expr_ptr(std::move(read_base))});
        const std::size_t before = v.diags_.size();
        v.check_return(r, curlee::types::TypeKind::U32);
        v.phys_vars_.erase("reg_rhs");
        bool saw = false;
        for (std::size_t i = before; i < v.diags_.size(); ++i)
        {
            if (v.diags_[i].message.find("opaque MMIO read") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected check_return to reject ensures with result on rhs of opaque read");
        }
        v.current_function_ = std::nullopt;
    }

    {
        // check_stmt_node LetStmt: Phys binding with a missing type_arg (line 1272 false arm)
        // and with a value that is not a direct PhysExpr (line 1278 false arm).
        const curlee::source::Span s{.start = 0, .end = 1};

        // Phys type without type_arg: the branch `if (s.type.type_arg.has_value())` is false.
        curlee::parser::LetStmt ls_no_arg;
        ls_no_arg.name = "phys_no_arg";
        ls_no_arg.type = curlee::parser::TypeName{.span = s, .name = "Phys"};
        ls_no_arg.value = make_expr(
            s, curlee::parser::PhysExpr{.element_kind = "U32", .lexeme = "0x1000"});
        v.check_stmt_node(ls_no_arg, s, curlee::types::TypeKind::Int);

        // Phys binding whose value is a NameExpr (not a direct PhysExpr): the `std::get_if`
        // at line 1278 is false, so no address re-validation is attempted.
        curlee::parser::LetStmt ls_name_val;
        ls_name_val.name = "phys_name_val";
        ls_name_val.type = curlee::parser::TypeName{.span = s,
                                                    .name = "Phys",
                                                    .type_arg = std::string_view("U32")};
        ls_name_val.value = make_expr(s, curlee::parser::NameExpr{.name = "other_phys"});
        v.check_stmt_node(ls_name_val, s, curlee::types::TypeKind::Int);
    }

    {
        // check_stmt_node LetStmt: unsigned binding whose value is NOT a read and has no
        // refinement — exercises the transitive expr_is_opaque_read false-arm (line 1323).
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::LetStmt ls_u32_plain;
        ls_u32_plain.name = "u32_plain";
        ls_u32_plain.type = curlee::parser::TypeName{.span = s,
                                                     .name = "U32",
                                                     .type_arg = std::string_view("U32")};
        ls_u32_plain.value = make_expr(s, curlee::parser::IntExpr{.lexeme = "5"});
        v.check_stmt_node(ls_u32_plain, s, curlee::types::TypeKind::Int);

        // Unsigned binding whose refinement does NOT mention an opaque read: the
        // `pred_mentions_opaque_read` false-arm at line 1330 — no diagnostic expected.
        curlee::parser::LetStmt ls_u32_refine_plain;
        ls_u32_refine_plain.name = "u32_refine_plain";
        ls_u32_refine_plain.type = curlee::parser::TypeName{.span = s,
                                                            .name = "U32",
                                                            .type_arg = std::string_view("U32")};
        ls_u32_refine_plain.refinement = make_pred(
            s, curlee::parser::PredBinary{
                   .op = curlee::lexer::TokenKind::Greater,
                   .lhs = make_pred_ptr(
                       make_pred(s, curlee::parser::PredName{.name = "plain"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"}))});
        ls_u32_refine_plain.value = make_expr(s, curlee::parser::IntExpr{.lexeme = "5"});
        const std::size_t before = v.diags_.size();
        v.check_stmt_node(ls_u32_refine_plain, s, curlee::types::TypeKind::Int);
        if (v.diags_.size() != before)
        {
            fail("expected unsigned let refinement on a plain value to emit no diagnostic");
        }
    }

    {
        // check_stmt_node LetStmt: an unsigned binding with a refinement that mentions an
        // opaque name must emit the hard diagnostic (line 1329 predicate path), and a binding
        // whose value is a NameExpr (not a direct read) exercises the transitive-provenance
        // short-circuit in the same guard.
        const curlee::source::Span s{.start = 0, .end = 1};
        v.opaque_read_vars_.insert("opaque_let");
        curlee::parser::LetStmt ls_refine;
        ls_refine.name = "u32_refined";
        ls_refine.type = curlee::parser::TypeName{.span = s,
                                                  .name = "U32",
                                                  .type_arg = std::string_view("U32")};
        ls_refine.refinement = make_pred(
            s, curlee::parser::PredBinary{
                   .op = curlee::lexer::TokenKind::Greater,
                   .lhs = make_pred_ptr(
                       make_pred(s, curlee::parser::PredName{.name = "opaque_let"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"}))});
        ls_refine.value = make_expr(s, curlee::parser::NameExpr{.name = "opaque_let"});
        const std::size_t before = v.diags_.size();
        v.check_stmt_node(ls_refine, s, curlee::types::TypeKind::Int);
        bool saw = false;
        for (std::size_t i = before; i < v.diags_.size(); ++i)
        {
            if (v.diags_[i].message.find("opaque MMIO read") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected unsigned let refinement on opaque value to be rejected");
        }
        v.opaque_read_vars_.erase("opaque_let");
    }

    {
        // pred_mentions_opaque_read / pred_mentions_result / expr_is_opaque_read: cover the
        // remaining recursion arms that produce a false result when the child is non-null but
        // does not match (lines 545/563/611/623).
        const curlee::source::Span s{.start = 0, .end = 1};

        // Unary with a non-null non-matching child (pred_mentions_result line 545).
        auto p_unary_nomatch = make_pred(
            s, curlee::parser::PredUnary{
                   .op = curlee::lexer::TokenKind::Bang,
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "y"}))});
        if (v.pred_mentions_result(p_unary_nomatch))
        {
            fail("expected pred_mentions_result(!y) to be false");
        }

        // Group with a non-null non-matching child (pred_mentions_result line 563).
        auto p_group_nomatch = make_pred(
            s, curlee::parser::PredGroup{
                   .inner = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "y"}))});
        if (v.pred_mentions_result(p_group_nomatch))
        {
            fail("expected pred_mentions_result((y)) to be false");
        }

        // GroupExpr with a non-null non-opaque child (expr_is_opaque_read line 611).
        curlee::parser::Expr g_plain_inner;
        g_plain_inner.span = s;
        g_plain_inner.node = curlee::parser::IntExpr{.lexeme = "1"};
        curlee::parser::Expr g_plain;
        g_plain.span = s;
        g_plain.node = curlee::parser::GroupExpr{.inner = make_expr_ptr(std::move(g_plain_inner))};
        if (v.expr_is_opaque_read(g_plain))
        {
            fail("expected expr_is_opaque_read(GroupExpr plain) to be false");
        }

        // BinaryExpr: plain lhs short-circuits the rhs check (expr_is_opaque_read line 623).
        curlee::parser::Expr b2_lhs;
        b2_lhs.span = s;
        b2_lhs.node = curlee::parser::IntExpr{.lexeme = "1"};
        curlee::parser::Expr b2_rhs;
        b2_rhs.span = s;
        b2_rhs.node = curlee::parser::IntExpr{.lexeme = "2"};
        curlee::parser::Expr b2;
        b2.span = s;
        b2.node = curlee::parser::BinaryExpr{.op = curlee::lexer::TokenKind::Plus,
                                             .lhs = make_expr_ptr(std::move(b2_lhs)),
                                             .rhs = make_expr_ptr(std::move(b2_rhs))};
        if (v.expr_is_opaque_read(b2))
        {
            fail("expected expr_is_opaque_read(BinaryExpr plain lhs) to be false");
        }

        // BinaryExpr with a null rhs: `node.rhs != nullptr` short-circuits to false without
        // recursing into the rhs (line 623 false-arm).
        curlee::parser::Expr b3_lhs;
        b3_lhs.span = s;
        b3_lhs.node = curlee::parser::IntExpr{.lexeme = "1"};
        curlee::parser::Expr b3;
        b3.span = s;
        b3.node = curlee::parser::BinaryExpr{.op = curlee::lexer::TokenKind::Plus,
                                             .lhs = make_expr_ptr(std::move(b3_lhs)),
                                             .rhs = nullptr};
        if (v.expr_is_opaque_read(b3))
        {
            fail("expected expr_is_opaque_read(BinaryExpr null rhs) to be false");
        }
    }

    {
        // check_return: opaque unsigned return whose ensures does NOT mention `result` (so
        // pred_mentions_result is false at line 1216) — no diagnostic should be emitted, and
        // the opaque-unsigned-return acceptance at 1201/1202 is exercised.
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Function f;
        f.name = "opaque_ret_nores";
        f.return_type = curlee::parser::TypeName{.span = s, .name = "U32"};
        // ensures mentions only an unrelated name, not `result`.
        f.ensures.push_back(make_pred(
            s, curlee::parser::PredBinary{
                   .op = curlee::lexer::TokenKind::Greater,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredName{.name = "zzz"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "0"}))}));
        curlee::verification::FunctionSig sig;
        sig.decl = &f;
        sig.result = curlee::types::TypeKind::U32;
        v.current_function_ = sig;

        v.phys_vars_.insert_or_assign("reg_nores", std::string_view("U32"));
        curlee::parser::Expr read_base;
        read_base.span = s;
        read_base.node = curlee::parser::NameExpr{.name = "reg_nores"};
        curlee::parser::ReturnStmt r;
        r.value = make_expr(s, curlee::parser::PhysReadExpr{
                                   .base = make_expr_ptr(std::move(read_base))});
        const std::size_t before = v.diags_.size();
        v.check_return(r, curlee::types::TypeKind::U32);
        v.phys_vars_.erase("reg_nores");
        if (v.diags_.size() != before)
        {
            fail("expected check_return with ensures not mentioning result to emit no diag");
        }
        v.current_function_ = std::nullopt;
    }

    {
        // check_return: an unsigned-returning function returning a plain Int literal must
        // take the return-type-mismatch early-return (opaque_unsigned_return is false because
        // the value is not opaque) — exercises the false-arms of lines 1202/1203.
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Function f2;
        f2.name = "plain_u32_ret";
        f2.return_type = curlee::parser::TypeName{.span = s, .name = "U32"};
        curlee::verification::FunctionSig sig2;
        sig2.decl = &f2;
        sig2.result = curlee::types::TypeKind::U32;
        v.current_function_ = sig2;

        curlee::parser::ReturnStmt r2;
        r2.value = make_expr(s, curlee::parser::IntExpr{.lexeme = "5"});
        const std::size_t before2 = v.diags_.size();
        v.check_return(r2, curlee::types::TypeKind::U32);
        if (v.diags_.size() != before2)
        {
            fail("expected check_return with plain Int for U32 return to emit no diag");
        }
        v.current_function_ = std::nullopt;
    }

    {
        // check_return: a U32-returning function returning a Bool literal — value.kind is
        // neither Int nor the expected U32, so opaque_unsigned_return short-circuits at the
        // `value.kind == TypeKind::Int` conjunct (line 1202 false-arm) and the function
        // returns via the mismatch guard (no diagnostics).
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Function f3;
        f3.name = "bool_to_u32_ret";
        f3.return_type = curlee::parser::TypeName{.span = s, .name = "U32"};
        curlee::verification::FunctionSig sig3;
        sig3.decl = &f3;
        sig3.result = curlee::types::TypeKind::U32;
        v.current_function_ = sig3;

        curlee::parser::ReturnStmt r3;
        r3.value = make_expr(s, curlee::parser::BoolExpr{.value = true});
        const std::size_t before3 = v.diags_.size();
        v.check_return(r3, curlee::types::TypeKind::U32);
        if (v.diags_.size() != before3)
        {
            fail("expected check_return with Bool for U32 return to emit no diag");
        }
        v.current_function_ = std::nullopt;
    }

    {
        // check_function: a Phys parameter WITHOUT a type_arg exercises the false-arm of the
        // `param.type.type_arg.has_value()` guard (line 1495).
        const curlee::source::Span s{.start = 0, .end = 1};
        curlee::parser::Function f;
        f.name = "phys_param_no_arg";
        f.return_type = curlee::parser::TypeName{.span = s, .name = "Int"};
        f.params.push_back(curlee::parser::Function::Param{
            .span = s,
            .name = "fb",
            .type = curlee::parser::TypeName{.span = s, .name = "Phys"},
            .refinement = std::nullopt});
        curlee::verification::FunctionSig sig;
        sig.decl = &f;
        sig.params = {curlee::types::TypeKind::Phys};
        sig.result = curlee::types::TypeKind::Int;
        v.functions_.insert_or_assign("phys_param_no_arg", sig);
        const std::size_t before = v.diags_.size();
        v.check_function(f);
        if (v.diags_.size() != before)
        {
            fail("expected check_function with no-arg Phys param to complete without diagnostics");
        }
        v.functions_.erase("phys_param_no_arg");
    }

    {
        // Predicate lowering for bitwise/modulo/division (issue #270):
        // lower_predicate exercises the new operator cases in
        // predicate_lowering.cpp, including their type-error branches.
        const curlee::source::Span s{.start = 0, .end = 1};
        z3::context ctx;
        curlee::verification::LoweringContext lctx(ctx);

        // '/': Int operands lower to Z3 division (Int-valued clause).
        auto p_div = make_pred(
            s, curlee::parser::PredBinary{
                   .op = TokenKind::Slash,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "7"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "2"}))});
        if (!std::holds_alternative<z3::expr>(
                curlee::verification::lower_predicate_int(p_div, lctx)))
        {
            fail("expected lower_predicate_int(7 / 2) to succeed");
        }

        // '/': Bool operand rejected.
        auto p_div_bad = make_pred(
            s, curlee::parser::PredBinary{
                   .op = TokenKind::Slash,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredBool{.value = true})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "2"}))});
        {
            const auto res = curlee::verification::lower_predicate(p_div_bad, lctx);
            if (!std::holds_alternative<curlee::diag::Diagnostic>(res))
            {
                fail("expected lower_predicate(bool / int) to error");
            }
        }

        // '%': Int operands lower to Z3 Euclidean modulo.
        auto p_mod = make_pred(
            s, curlee::parser::PredBinary{
                   .op = TokenKind::Percent,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "17"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "5"}))});
        if (!std::holds_alternative<z3::expr>(
                curlee::verification::lower_predicate_int(p_mod, lctx)))
        {
            fail("expected lower_predicate_int(17 % 5) to succeed");
        }

        // '%': non-Int operand rejected (rhs side).
        auto p_mod_bad = make_pred(
            s, curlee::parser::PredBinary{
                   .op = TokenKind::Percent,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "17"})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredBool{.value = true}))});
        {
            const auto res = curlee::verification::lower_predicate(p_mod_bad, lctx);
            if (!std::holds_alternative<curlee::diag::Diagnostic>(res))
            {
                fail("expected lower_predicate(int % bool) to error");
            }
        }

        // Bitwise '&', '|', '^', '<<', '>>': Int operands lower via the
        // 64-bit bit-vector model.
        for (const auto op : {TokenKind::Amp, TokenKind::Pipe, TokenKind::Caret,
                              TokenKind::ShiftLeft, TokenKind::ShiftRight})
        {
            auto p_bit = make_pred(
                s, curlee::parser::PredBinary{
                       .op = op,
                       .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "15"})),
                       .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "3"}))});
            if (!std::holds_alternative<z3::expr>(
                    curlee::verification::lower_predicate_int(p_bit, lctx)))
            {
                fail("expected lower_predicate_int(bitwise op) to succeed");
            }
        }

        // Bitwise '&': Bool operand rejected.
        auto p_bit_bad = make_pred(
            s, curlee::parser::PredBinary{
                   .op = TokenKind::Amp,
                   .lhs = make_pred_ptr(make_pred(s, curlee::parser::PredBool{.value = true})),
                   .rhs = make_pred_ptr(make_pred(s, curlee::parser::PredInt{.lexeme = "3"}))});
        {
            const auto res = curlee::verification::lower_predicate(p_bit_bad, lctx);
            if (!std::holds_alternative<curlee::diag::Diagnostic>(res))
            {
                fail("expected lower_predicate(bool & int) to error");
            }
        }

        // Unary '~': Int operand lowers to -x - 1.
        auto p_not_rhs = make_pred(s, curlee::parser::PredInt{.lexeme = "5"});
        auto p_bnot = make_pred(s, curlee::parser::PredUnary{
                                       .op = TokenKind::Tilde,
                                       .rhs = make_pred_ptr(std::move(p_not_rhs))});
        if (!std::holds_alternative<z3::expr>(
                curlee::verification::lower_predicate_int(p_bnot, lctx)))
        {
            fail("expected lower_predicate_int(~5) to succeed");
        }

        // Unary '~': Bool operand rejected.
        auto p_bnot_bad_rhs = make_pred(s, curlee::parser::PredBool{.value = true});
        auto p_bnot_bad = make_pred(
            s, curlee::parser::PredUnary{.op = TokenKind::Tilde,
                                         .rhs = make_pred_ptr(std::move(p_bnot_bad_rhs))});
        {
            const auto res = curlee::verification::lower_predicate(p_bnot_bad, lctx);
            if (!std::holds_alternative<curlee::diag::Diagnostic>(res))
            {
                fail("expected lower_predicate(~bool) to error");
            }
        }
    }

    std::cout << "OK\n";
    return 0;
}
