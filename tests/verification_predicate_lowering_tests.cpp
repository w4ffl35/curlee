#include <cstdlib>
#include <curlee/verification/predicate_lowering.h>
#include <curlee/verification/solver.h>
#include <iostream>
#include <memory>

using curlee::lexer::TokenKind;
using curlee::parser::Pred;
using curlee::parser::PredBinary;
using curlee::parser::PredInt;
using curlee::parser::PredName;
using curlee::parser::PredUnary;
using curlee::source::Span;

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static Pred make_int(std::string_view lexeme)
{
    return Pred{.span = Span{}, .node = PredInt{.lexeme = lexeme}};
}

static Pred make_name(std::string_view name)
{
    return Pred{.span = Span{}, .node = PredName{.name = name}};
}

static Pred make_unary(TokenKind op, Pred rhs)
{
    PredUnary unary{.op = op, .rhs = std::make_unique<Pred>(std::move(rhs))};
    return Pred{.span = Span{}, .node = std::move(unary)};
}

static Pred make_binary(TokenKind op, Pred lhs, Pred rhs)
{
    PredBinary binary{.op = op,
                      .lhs = std::make_unique<Pred>(std::move(lhs)),
                      .rhs = std::make_unique<Pred>(std::move(rhs))};
    return Pred{.span = Span{}, .node = std::move(binary)};
}

int main()
{
    using namespace curlee::verification;

    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);
        auto x = ctx.int_const("x");
        lower_ctx.int_vars.emplace("x", x);

        auto pred = make_binary(TokenKind::AndAnd,
                                make_binary(TokenKind::Greater, make_name("x"), make_int("0")),
                                make_binary(TokenKind::Less, make_name("x"), make_int("3")));

        auto lowered = lower_predicate(pred, lower_ctx);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected predicate lowering to succeed");
        }

        solver.add(std::get<z3::expr>(lowered));
        solver.add(x == 5);
        if (solver.check() != CheckResult::Unsat)
        {
            fail("expected predicate constraints to reject x == 5");
        }
    }

    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_name("missing");
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected unknown predicate name diagnostic");
        }
    }

    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);
        auto x = ctx.int_const("x");
        auto y = ctx.int_const("y");
        lower_ctx.int_vars.emplace("x", x);
        lower_ctx.int_vars.emplace("y", y);

        auto pred = make_binary(TokenKind::Star, make_name("x"), make_name("y"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected non-linear multiplication to be rejected");
        }
    }

    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);
        auto flag = ctx.bool_const("flag");
        lower_ctx.bool_vars.emplace("flag", flag);

        auto pred = make_unary(TokenKind::Bang, make_name("flag"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected boolean predicate lowering to succeed");
        }
    }

    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);
        auto result = ctx.int_const("result");
        lower_ctx.result_int = result;

        auto pred = make_binary(TokenKind::EqualEqual, make_name("result"), make_int("0"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected result binding to lower correctly");
        }

        solver.add(std::get<z3::expr>(lowered));
        solver.add(result == 1);
        if (solver.check() != CheckResult::Unsat)
        {
            fail("expected result binding to constrain the model");
        }
    }

    std::cout << "OK\n";
    return 0;
}
