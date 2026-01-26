#include <cstdlib>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/resolver/resolver.h>
#include <iostream>
#include <string>
#include <variant>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static curlee::parser::Program parse_ok(const std::string& src)
{
    using namespace curlee;

    const auto lexed = lexer::lex(src);
    if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
    {
        fail("lex failed unexpectedly");
    }

    const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
    auto parsed = parser::parse(toks);
    if (!std::holds_alternative<parser::Program>(parsed))
    {
        fail("parse failed unexpectedly");
    }

    return std::get<parser::Program>(std::move(parsed));
}

int main()
{
    using namespace curlee;

    {
        const std::string src = R"(fn print(x: Int) -> Unit {
  return 0;
}

fn main() -> Unit {
  print(1);
  return 0;
})";

        const auto program = parse_ok(src);
        const auto res = resolver::resolve(program);
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success on happy path");
        }
    }

    {
        const std::string src = R"(fn main() -> Unit {
  foo(1);
  return 0;
})";

        const auto program = parse_ok(src);
        const auto res = resolver::resolve(program);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error on unknown name");
        }

        const auto& ds = std::get<std::vector<diag::Diagnostic>>(res);
        if (ds.empty())
        {
            fail("expected at least one diagnostic for unknown name");
        }
    }

    {
        const std::string src = R"(fn main() -> Unit {
  let x: Int = 1;
  let x: Int = 2;
  return x;
})";

        const auto program = parse_ok(src);
        const auto res = resolver::resolve(program);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error on duplicate definition");
        }

        const auto& ds = std::get<std::vector<diag::Diagnostic>>(res);
        if (ds.empty())
        {
            fail("expected at least one diagnostic for duplicate definition");
        }
    }

    {
        const std::string src = R"(fn main() -> Unit {
  let x: Int = 1;
  { let x: Int = 2; x; }
  x;
  return 0;
})";

        const auto program = parse_ok(src);
        const auto res = resolver::resolve(program);
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success on block shadowing");
        }

        const auto& r = std::get<resolver::Resolution>(res);
        if (r.uses.size() != 2)
        {
            fail("expected exactly two name uses in block shadowing test");
        }

        // Symbol 0 is the function. Then `let x = 1` (outer) then `let x = 2` (inner).
        if (r.uses[0].target.value == r.uses[1].target.value)
        {
            fail("expected inner and outer x to resolve to different symbols");
        }
    }

    {
        const std::string src = R"(import foo.bar;

fn main() -> Unit {
  return 0;
})";

        const auto program = parse_ok(src);
        const auto res = resolver::resolve(program);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error when imports are present");
        }

        const auto& ds = std::get<std::vector<diag::Diagnostic>>(res);
        bool found = false;
        for (const auto& d : ds)
        {
            if (d.message.find("imports are not implemented") != std::string::npos)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            fail("expected import-not-implemented diagnostic");
        }
    }

    std::cout << "OK\n";
    return 0;
}
