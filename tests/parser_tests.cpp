#include <cstdlib>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <iostream>
#include <string>
#include <variant>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

int main()
{
    using namespace curlee;

    {
        const std::string src = R"(fn main() -> Unit {
    let x: Int = 1 + 2;
    print("hello, world");
    return x;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on valid program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on valid program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        if (prog.functions.size() != 1)
        {
            fail("expected exactly one function");
        }
        if (prog.functions[0].name != "main")
        {
            fail("expected function name 'main'");
        }

        const std::string dumped = parser::dump(prog);
        if (dumped.find("fn main() -> Unit") == std::string::npos)
        {
            fail("dump missing function header");
        }
        if (dumped.find("print(\"hello, world\")") == std::string::npos)
        {
            fail("dump missing call expression");
        }
        if (dumped.find("let x: Int") == std::string::npos)
        {
            fail("dump missing typed let binding");
        }
    }

    {
        const std::string src = R"(fn divide(numerator: Int, denominator: Int) -> Int
  [ requires denominator != 0;
    ensures result * denominator == numerator; ]
{
  return numerator / denominator;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on valid contract program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on valid contract program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("fn divide") == std::string::npos)
        {
            fail("dump missing divide header");
        }
        if (dumped.find("requires") == std::string::npos ||
            dumped.find("ensures") == std::string::npos)
        {
            fail("dump missing requires/ensures");
        }
        if (dumped.find("numerator: Int") == std::string::npos ||
            dumped.find("denominator: Int") == std::string::npos)
        {
            fail("dump missing typed parameters");
        }
    }

    {
        const std::string src = R"(fn takes_pos(x: Int where x > 0) -> Int {
  let y: Int where y > 0 = x;
  return y;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on valid refinement program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on valid refinement program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("where") == std::string::npos)
        {
            fail("dump missing where refinement");
        }
    }

    {
        const std::string src = R"(fn main() -> Unit {
  let x: Int = 1;
  { let y: Int = 2; y; }
  x;
  return 0;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on valid nested-block program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on valid nested-block program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("{ let y: Int") == std::string::npos)
        {
            fail("dump missing nested block statement");
        }
    }

    {
        const std::string src =
            "fn main() { let x = 1 }"; // missing semicolon + missing closing brace
        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed unexpectedly on error program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            fail("expected parse error");
        }

        const auto& ds = std::get<std::vector<diag::Diagnostic>>(parsed);
        if (ds.empty())
        {
            fail("expected at least one diagnostic");
        }
        if (!ds[0].span.has_value())
        {
            fail("expected diagnostic span");
        }
    }

    {
        const std::string src = R"(fn main() -> Unit {
  let x: Int = ;
  let y: Int = ;
  return 0;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed unexpectedly on recovery program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            fail("expected parse errors for recovery program");
        }

        const auto& ds = std::get<std::vector<diag::Diagnostic>>(parsed);
        if (ds.size() < 2)
        {
            fail("expected multiple diagnostics from one file");
        }
    }

    std::cout << "OK\n";
    return 0;
}
