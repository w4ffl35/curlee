#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>

#include <cstdlib>
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
  let x = 1 + 2;
  print("hello, world");
  return x;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed)) {
            fail("lex failed on valid program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed)) {
            fail("parse failed on valid program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        if (prog.functions.size() != 1) {
            fail("expected exactly one function");
        }
        if (prog.functions[0].name != "main") {
            fail("expected function name 'main'");
        }

        const std::string dumped = parser::dump(prog);
        if (dumped.find("fn main() -> Unit") == std::string::npos) {
            fail("dump missing function header");
        }
        if (dumped.find("print(\"hello, world\")") == std::string::npos) {
            fail("dump missing call expression");
        }
    }

    {
        const std::string src = "fn main() { let x = 1 }"; // missing semicolon + missing closing brace
        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed)) {
            fail("lex failed unexpectedly on error program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<diag::Diagnostic>(parsed)) {
            fail("expected parse error");
        }

        const auto& d = std::get<diag::Diagnostic>(parsed);
        if (!d.span.has_value()) {
            fail("expected diagnostic span");
        }
    }

    std::cout << "OK\n";
    return 0;
}
