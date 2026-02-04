#include <cstdlib>
#include <curlee/verification/predicate_lowering.h>
#include <curlee/verification/solver.h>
#include <iostream>
#include <memory>

using curlee::lexer::TokenKind;
using curlee::parser::Pred;
using curlee::parser::PredBinary;
using curlee::parser::PredBool;
using curlee::parser::PredGroup;
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

static Pred make_bool(bool value)
{
    return Pred{.span = Span{}, .node = PredBool{.value = value}};
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

static Pred make_group(Pred inner)
{
    PredGroup group{.inner = std::make_unique<Pred>(std::move(inner))};
    return Pred{.span = Span{}, .node = std::move(group)};
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

    // Bool literal should lower successfully.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::EqualEqual, make_bool(true), make_bool(true));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected boolean literal predicate lowering to succeed");
        }

        solver.add(std::get<z3::expr>(lowered));
        if (solver.check() != CheckResult::Sat)
        {
            fail("expected trivial boolean equality to be satisfiable");
        }
    }

    // Grouping should be transparent.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);
        auto x = ctx.int_const("x");
        lower_ctx.int_vars.emplace("x", x);

        auto pred = make_group(make_binary(TokenKind::Greater, make_name("x"), make_int("0")));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected grouped predicate lowering to succeed");
        }

        solver.add(std::get<z3::expr>(lowered));
        solver.add(x == 0);
        if (solver.check() != CheckResult::Unsat)
        {
            fail("expected (x > 0) to reject x == 0");
        }
    }

    // Predicate must resolve to Bool.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_int("123");
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected non-bool predicate to be rejected");
        }

        const auto& d = std::get<curlee::diag::Diagnostic>(lowered);
        if (d.message != "predicate must resolve to Bool")
        {
            fail("unexpected diagnostic for non-bool predicate");
        }
    }

    // Unary '-' should work for Int (when used in a Bool context).
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::Less, make_unary(TokenKind::Minus, make_int("1")),
                                make_int("0"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected unary '-' lowering to succeed");
        }

        solver.add(std::get<z3::expr>(lowered));
        if (solver.check() != CheckResult::Sat)
        {
            fail("expected -1 < 0 to be satisfiable");
        }
    }

    // Unary '-' expects Int.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::EqualEqual,
                                make_unary(TokenKind::Minus, make_bool(true)), make_int("0"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected unary '-' type error");
        }

        const auto& d = std::get<curlee::diag::Diagnostic>(lowered);
        if (d.message != "unary '-' expects Int predicate")
        {
            fail("unexpected diagnostic for unary '-' type mismatch");
        }
    }

    // Unsupported unary operator should be rejected.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::EqualEqual, make_unary(TokenKind::Plus, make_bool(true)),
                                make_bool(true));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected unsupported unary operator to be rejected");
        }
    }

    // Boolean operators expect Bool predicates.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::AndAnd, make_int("1"), make_bool(true));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected boolean operator type error");
        }
    }

    // Boolean operators expect Bool predicates (cover rhs-type mismatch branch).
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::AndAnd, make_bool(true), make_int("1"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected boolean operator type error (rhs mismatch)");
        }
    }

    // Equality expects matching predicate types.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::EqualEqual, make_int("1"), make_bool(true));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected equality type mismatch to be rejected");
        }
    }

    // Comparison operators expect Int predicates.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::Less, make_bool(true), make_int("0"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected comparison type mismatch to be rejected");
        }
    }

    // Comparison operators expect Int predicates (cover rhs-type mismatch branch).
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::Less, make_int("0"), make_bool(true));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected comparison rhs type mismatch to be rejected");
        }
    }

    // Arithmetic operators expect Int predicates.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::EqualEqual,
                                make_binary(TokenKind::Plus, make_bool(true), make_bool(false)),
                                make_int("0"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected arithmetic type mismatch to be rejected");
        }
    }

    // Arithmetic operators expect Int predicates (cover rhs-type mismatch branch).
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::EqualEqual,
                                make_binary(TokenKind::Plus, make_int("1"), make_bool(true)),
                                make_int("0"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected arithmetic rhs type mismatch to be rejected");
        }
    }

    // Cover literal tracking in arithmetic: literal + literal.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::EqualEqual,
                                make_binary(TokenKind::Plus, make_int("1"), make_int("2")),
                                make_int("3"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected literal + literal lowering to succeed");
        }
    }

    // Cover literal tracking in arithmetic: literal + var (forces RHS literal flag evaluation).
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);
        auto x = ctx.int_const("x");
        lower_ctx.int_vars.emplace("x", x);

        auto pred = make_binary(TokenKind::EqualEqual,
                                make_binary(TokenKind::Plus, make_int("1"), make_name("x")),
                                make_int("0"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected literal + var lowering to succeed");
        }
    }

    // Linear multiplication (literal * var) should be supported.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);
        auto x = ctx.int_const("x");
        lower_ctx.int_vars.emplace("x", x);

        auto pred =
            make_binary(TokenKind::EqualEqual,
                        make_binary(TokenKind::Star, make_int("2"), make_name("x")), make_int("6"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected linear multiplication to lower successfully");
        }

        solver.add(std::get<z3::expr>(lowered));
        solver.add(x == 4);
        if (solver.check() != CheckResult::Unsat)
        {
            fail("expected 2*x == 6 to reject x == 4");
        }
    }

    // Unsupported binary operator should be rejected.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::Slash, make_int("4"), make_int("2"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected unsupported binary operator to be rejected");
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

    // result should bind to Bool when result_bool is set.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);
        auto result = ctx.bool_const("result");
        lower_ctx.result_bool = result;

        auto pred = make_binary(TokenKind::EqualEqual, make_name("result"), make_bool(true));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected result bool binding to lower correctly");
        }

        solver.add(std::get<z3::expr>(lowered));
        solver.add(result == ctx.bool_val(false));
        if (solver.check() != CheckResult::Unsat)
        {
            fail("expected result bool binding to constrain the model");
        }
    }

    // "result" name with no bindings should fall through and error.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_name("result");
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected unbound 'result' to be rejected");
        }

        const auto& d = std::get<curlee::diag::Diagnostic>(lowered);
        if (d.message != "unknown predicate name 'result'")
        {
            fail("unexpected diagnostic for unbound 'result'");
        }
    }

    // Diagnostics from lowering children should be propagated through unary.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_unary(TokenKind::Bang, make_name("missing"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected unknown predicate name to propagate through unary");
        }
    }

    // Diagnostics from lowering children should be propagated through binary.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::EqualEqual, make_bool(true), make_name("missing"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected unknown predicate name to propagate through binary");
        }
    }

    // '!' expects Bool.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_unary(TokenKind::Bang, make_int("1"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected '!' type mismatch to be rejected");
        }
        const auto& d = std::get<curlee::diag::Diagnostic>(lowered);
        if (d.message != "'!' expects Bool predicate")
        {
            fail("unexpected diagnostic for '!' type mismatch");
        }
    }

    // OrOr should work for Bool.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);
        auto flag = ctx.bool_const("flag");
        lower_ctx.bool_vars.emplace("flag", flag);

        auto pred = make_binary(TokenKind::OrOr, make_name("flag"), make_unary(TokenKind::Bang, make_name("flag")));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected OrOr predicate lowering to succeed");
        }

        solver.add(std::get<z3::expr>(lowered));
        if (solver.check() != CheckResult::Sat)
        {
            fail("expected flag || !flag to be satisfiable");
        }
    }

    // Cover additional comparison operator cases (<= and >=).
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);
        auto x = ctx.int_const("x");
        lower_ctx.int_vars.emplace("x", x);

        auto pred = make_binary(TokenKind::AndAnd, make_binary(TokenKind::LessEqual, make_name("x"), make_int("0")),
                                make_binary(TokenKind::GreaterEqual, make_name("x"), make_int("0")));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected <= and >= predicate lowering to succeed");
        }

        solver.add(std::get<z3::expr>(lowered));
        solver.add(x == 0);
        if (solver.check() != CheckResult::Sat)
        {
            fail("expected x==0 to satisfy x<=0 && x>=0");
        }
    }

    // Cover arithmetic '-' lowering in an Int context.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);
        auto x = ctx.int_const("x");
        lower_ctx.int_vars.emplace("x", x);

        auto pred = make_binary(TokenKind::Greater, make_binary(TokenKind::Minus, make_name("x"), make_int("1")),
                                make_int("0"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected arithmetic '-' lowering to succeed");
        }

        solver.add(std::get<z3::expr>(lowered));
        solver.add(x == 2);
        if (solver.check() != CheckResult::Sat)
        {
            fail("expected (x-1)>0 to be satisfiable for x==2");
        }
    }

    // '*' expects Int predicates.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::EqualEqual, make_binary(TokenKind::Star, make_bool(true), make_int("1")),
                                make_int("0"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected '*' type mismatch to be rejected");
        }
        const auto& d = std::get<curlee::diag::Diagnostic>(lowered);
        if (d.message != "'*' expects Int predicates")
        {
            fail("unexpected diagnostic for '*' type mismatch");
        }
    }

    // '*' expects Int predicates (cover rhs-type mismatch branch).
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::EqualEqual,
                                make_binary(TokenKind::Star, make_int("1"), make_bool(true)),
                                make_int("0"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected '*' rhs type mismatch to be rejected");
        }
        const auto& d = std::get<curlee::diag::Diagnostic>(lowered);
        if (d.message != "'*' expects Int predicates")
        {
            fail("unexpected diagnostic for '*' rhs type mismatch");
        }
    }

    // Literal * literal should be supported and mark multiplication as linear.
    {
        Solver solver;
        auto& ctx = solver.context();
        LoweringContext lower_ctx(ctx);

        auto pred = make_binary(TokenKind::EqualEqual,
                                make_binary(TokenKind::Star, make_int("2"), make_int("3")),
                                make_int("6"));
        auto lowered = lower_predicate(pred, lower_ctx);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lowered))
        {
            fail("expected literal multiplication to lower successfully");
        }

        solver.add(std::get<z3::expr>(lowered));
        if (solver.check() != CheckResult::Sat)
        {
            fail("expected 2*3 == 6 to be satisfiable");
        }
    }

    std::cout << "OK\n";
    return 0;
}
