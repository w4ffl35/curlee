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

    std::cout << "OK\n";
    return 0;
}
