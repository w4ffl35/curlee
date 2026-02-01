#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/types/type_check.h>
#include <iostream>
#include <string>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static void expect_diag(const curlee::types::TypeCheckResult& res, const std::string& substr)
{
    if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(res))
    {
        fail("expected diagnostics containing: " + substr);
    }
    const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(res);
    for (const auto& d : diags)
    {
        if (d.message.find(substr) != std::string::npos)
        {
            return;
        }
    }
    fail("did not find diagnostic containing: " + substr);
}

int main()
{
    using namespace curlee;

    // unknown type name in return annotation
    {
        const std::string src = "fn main() -> Foo { return 1; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown type 'Foo'");
    }

    // missing return type annotation
    {
        const std::string src = "fn f(x: Int) { return; } fn main() -> Int { return 0; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "missing return type annotation for function 'f'");
    }

    // call: unknown module qualifier
    {
        const std::string src = "fn main() -> Int { return foo.bar(); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown module qualifier in call: 'foo'");
    }

    // call: unknown enum type in scoped call
    {
        const std::string src = "fn main() -> Int { return NoSuch::A(); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown enum type 'NoSuch'");
    }

    // enum variant: does not take a payload
    {
        const std::string src = "enum E { A; } fn main() -> E { return E::A(1); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "does not take a payload");
    }

    // enum variant: expects exactly 1 payload argument
    {
        const std::string src = "enum E { A(Int); } fn main() -> E { return E::A(); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "expects exactly 1 payload argument");
    }

    // python_ffi.call requires unsafe context
    {
        const std::string src = "fn main() -> Unit { python_ffi.call(); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "python_ffi.call requires an unsafe context");
    }

    // python_ffi.call stubbed args inside unsafe
    {
        const std::string src = "fn main() -> Unit { unsafe { python_ffi.call(1); } }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "python_ffi.call is stubbed and currently takes 0 arguments");
    }

    // print() argument arity and type
    {
        const std::string src = "fn main() -> Unit { print(); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "print expects exactly 1 argument");
    }

    {
        const std::string src =
            "struct T { x: Int; } fn main() -> Unit { let t: T = T{ x: 1 }; print(t); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "print only supports Int, Bool, or String");
    }

    // unknown function name
    {
        const std::string src = "fn main() -> Int { return notfound(); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown function 'notfound'");
    }

    std::cout << "OK\n";
    return 0;
}
