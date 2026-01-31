#include <cstdlib>
#include <curlee/verification/solver.h>
#include <iostream>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

int main()
{
    using namespace curlee::verification;

    // model_for() before any check() should return nullopt.
    {
        Solver solver;
        auto& ctx = solver.context();
        auto x = ctx.int_const("x");

        if (solver.model_for({x}).has_value())
        {
            fail("expected no model before check()");
        }
    }

    {
        Solver solver;
        auto& ctx = solver.context();
        auto x = ctx.int_const("x");
        solver.add(x > 0);
        solver.add(x < 2);

        if (solver.check() != CheckResult::Sat)
        {
            fail("expected satisfiable constraints");
        }

        auto model = solver.model_for({x});
        if (!model.has_value())
        {
            fail("expected model for satisfiable constraints");
        }

        const auto rendered = Solver::format_model(*model);
        if (rendered != "x = 1")
        {
            fail("expected deterministic model output for x");
        }
    }

    {
        Solver solver;
        auto& ctx = solver.context();
        auto x = ctx.int_const("x");
        solver.add(x > 0);
        solver.add(x < 0);

        if (solver.check() != CheckResult::Unsat)
        {
            fail("expected unsatisfiable constraints");
        }

        if (solver.model_for({x}).has_value())
        {
            fail("expected no model for unsatisfiable constraints");
        }
    }

    {
        Solver solver;
        auto& ctx = solver.context();
        auto x = ctx.int_const("x");
        auto y = ctx.int_const("y");
        solver.add(x == 7);
        solver.add(y == 3);

        if (solver.check() != CheckResult::Sat)
        {
            fail("expected satisfiable constraints for formatting");
        }

        auto model = solver.model_for({x, y});
        if (!model.has_value())
        {
            fail("expected model for formatting");
        }

        const auto rendered = Solver::format_model(*model);
        if (rendered != "x = 7\ny = 3")
        {
            fail("expected deterministic model formatting order");
        }
    }

    // push()/pop() should clear cached results and models.
    {
        Solver solver;
        auto& ctx = solver.context();
        auto x = ctx.int_const("x");
        solver.add(x == 1);

        if (solver.check() != CheckResult::Sat)
        {
            fail("expected satisfiable constraints for push/pop test");
        }

        if (!solver.model_for({x}).has_value())
        {
            fail("expected model before push()");
        }

        solver.push();
        if (solver.model_for({x}).has_value())
        {
            fail("expected no model after push() (cache cleared)");
        }

        solver.pop();
        if (solver.model_for({x}).has_value())
        {
            fail("expected no model after pop() (cache cleared)");
        }
    }

    std::cout << "OK\n";
    return 0;
}
