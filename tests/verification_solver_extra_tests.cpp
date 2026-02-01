#include <curlee/verification/solver.h>
#include <iostream>
#include <string>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

int main()
{
    using namespace curlee::verification;

    // model_for before any check should return nullopt
    {
        Solver s;
        auto& ctx = s.context();
        z3::expr x = ctx.int_const("x");
        const auto maybe = s.model_for({x});
        if (maybe.has_value())
        {
            fail("expected no model before check");
        }
    }

    // SAT model and formatting (also checks sorting)
    {
        Solver s;
        auto& ctx = s.context();
        z3::expr a = ctx.int_const("a");
        z3::expr b = ctx.int_const("b");
        s.add(a == 1);
        s.add(b == 2);

        const auto res = s.check();
        if (res != CheckResult::Sat)
        {
            fail("expected satisfiable result");
        }

        const auto maybe = s.model_for({b, a});
        if (!maybe.has_value())
        {
            fail("expected model to be present after SAT");
        }
        const auto model = *maybe;
        if (model.entries.size() < 2)
        {
            fail("expected at least two entries in model");
        }

        const auto fmt = Solver::format_model(model);
        // Should be sorted by name: a then b
        if (fmt != "a = 1\nb = 2")
        {
            fail(std::string("unexpected model formatting: ") + fmt);
        }
    }

    // UNSAT case should return Unsat and model_for should be nullopt
    {
        Solver s;
        auto& ctx = s.context();
        z3::expr x = ctx.int_const("x");
        s.add(x == 1);
        s.add(x == 2);

        const auto res = s.check();
        if (res != CheckResult::Unsat)
        {
            fail("expected unsatisfiable result");
        }

        const auto maybe = s.model_for({x});
        if (maybe.has_value())
        {
            fail("expected no model for UNSAT");
        }
    }

    std::cout << "OK\n";
    return 0;
}
