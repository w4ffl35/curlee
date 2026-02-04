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

static void expect_ok(const curlee::types::TypeCheckResult& res)
{
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(res))
    {
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(res);
        std::cerr << "unexpected diagnostics:\n";
        for (const auto& d : diags)
        {
            std::cerr << "  - " << d.message << "\n";
        }
        fail("expected type checking to succeed");
    }
}

int main()
{
    using namespace curlee;

    // duplicate type names (struct/struct and struct/enum)
    {
        const std::string src =
            "struct S { x: Int; } struct S { y: Int; } enum S { A; } fn main() -> Unit { return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "duplicate type name 'S'");
    }

    // duplicate enum name (exercise enums_.contains(name) branch)
    {
        const std::string src = "enum E { A; } enum E { B; } fn main() -> Unit { return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "duplicate type name 'E'");
    }

    // enum payload type name resolution: payload present but unknown type
    {
        const std::string src = "enum E { A(Foo); } fn main() -> Unit { return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown type 'Foo'");
    }

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

    // python_ffi.<not-call>(...) should not be treated as python_ffi.call
    {
        const std::string src = "fn main() -> Unit { python_ffi.nope(); return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown module qualifier in call: 'python_ffi'");
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

    // print: Bool and String are accepted (exercise short-circuit paths)
    {
        const std::string src = "fn main() -> Unit { print(true); return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    {
        const std::string src = "fn main() -> Unit { print(\"a\"); return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    // if/while condition: check_expr failure should not emit type-mismatch diag
    {
        const std::string src = "fn main() -> Int { if (nope) { return 0; } return 0; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown name 'nope'");
    }

    {
        const std::string src = "fn main() -> Int { while (nope) { return 0; } return 0; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown name 'nope'");
    }

    // binary: either side failing to type-check should propagate (early return)
    {
        const std::string src = "fn main() -> Int { return nope + 1; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown name 'nope'");
    }

    {
        const std::string src = "fn main() -> Int { return 1 + nope; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown name 'nope'");
    }

    // '+' success: String + String
    {
        const std::string src = "fn main() -> String { return \"a\" + \"b\"; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    // '+' mismatch: Int + Bool (exercise rhs-side of the Int check)
    {
        const std::string src = "fn main() -> Int { return 1 + true; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "'+' expects Int+Int or String+String");
    }

    // binary operator typing: arithmetic, comparison, boolean mismatches
    {
        const std::string src = "fn main() -> Int { return 1 - true; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "arithmetic operators expect Int operands");
    }

    {
        const std::string src = "fn main() -> Int { return true - 1; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "arithmetic operators expect Int operands");
    }

    {
        const std::string src = "fn main() -> Bool { return 1 < true; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "comparison operators expect Int operands");
    }

    {
        const std::string src = "fn main() -> Bool { return true < 1; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "comparison operators expect Int operands");
    }

    {
        const std::string src = "fn main() -> Bool { return true && 1; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "boolean operators expect Bool operands");
    }

    {
        const std::string src = "fn main() -> Bool { return 1 && true; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "boolean operators expect Bool operands");
    }

    // binary operator success cases for remaining switch branches
    {
        const std::string src = "fn main() -> Bool { return 1 == 2; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    {
        const std::string src = "fn main() -> Bool { return 1 != 2; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    {
        const std::string src = "fn main() -> Bool { return 1 <= 2; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    {
        const std::string src = "fn main() -> Bool { return 1 > 2; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    {
        const std::string src = "fn main() -> Bool { return 1 >= 2; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    {
        const std::string src = "fn main() -> Int { return 2 * 3; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    {
        const std::string src = "fn main() -> Int { return 6 / 2; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    {
        const std::string src = "fn main() -> Bool { return true || false; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    // enum payload type mismatch in constructor call
    {
        const std::string src = "enum E { A(Int); } fn main() -> E { return E::A(true); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "enum payload type mismatch");
    }

    // enum payload: correct type and arg expression failure paths
    {
        const std::string src = "enum E { A(Int); } fn main() -> E { return E::A(1); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    {
        const std::string src = "enum E { A(Int); } fn main() -> E { return E::A(nope); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown name 'nope'");
    }

    // module-qualified call: member chain fails when base is not a name/member (e.g. foo().bar())
    {
        const std::string src =
            "fn foo() -> Unit { return; } fn main() -> Unit { foo().bar(); return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "only direct calls are supported");
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

    // cannot redeclare builtin function print
    {
        const std::string src = "fn print() -> Unit { return; } fn main() -> Unit { return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "cannot declare builtin function 'print'");
    }

    // duplicate type name between struct/enum
    {
        const std::string src = "struct S { x: Int; } enum S { A; } fn main() -> Unit { return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "duplicate type name 'S'");
    }

    // struct literal: missing required field
    {
        const std::string src = "struct Point { x: Int; y: Int; } fn main() -> Unit { let p: Point "
                                "= Point{ x: 1 }; p; return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "missing required field");
    }

    // struct literal: unknown field and still checks initializer
    {
        const std::string src = "struct Point { x: Int; } fn main() -> Unit { let p: Point = "
                                "Point{ z: true, x: 1 }; p; return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown field 'z' for struct 'Point'");
    }

    // struct literal: field type mismatch
    {
        const std::string src = "struct Point { x: Int; } fn main() -> Unit { let p: Point = "
                                "Point{ x: true }; p; return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "type mismatch: expected Int, got Bool");
    }

    // member access: cannot access field on non-struct
    {
        const std::string src = "fn main() -> Unit { let x: Int = 1; x.y; return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "cannot access field 'y' on non-struct type Int");
    }

    // name resolution: function names are not values
    {
        const std::string src =
            "fn f() -> Int { return 1; } fn main() -> Int { let x: Int = f; return x; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "is not a value");
    }

    // calls: wrong arity / argument type mismatch
    {
        const std::string src =
            "fn f(x: Int) -> Int { return x; } fn main() -> Int { return f(); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "wrong number of arguments for call to 'f'");
    }

    {
        const std::string src =
            "fn f(x: Int) -> Int { return x; } fn main() -> Int { return f(true); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "argument type mismatch for call to 'f'");
    }

    // module-qualified calls: ok with import key or alias
    {
        const std::string src =
            "import foo.bar; fn f() -> Int { return 0; } fn main() -> Int { return foo.bar.f(); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    {
        const std::string src = "import foo.bar as baz; fn f() -> Int { return 0; } fn main() -> "
                                "Int { return baz.f(); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    // module-qualified call: non-name base should be rejected (collect_member_chain fails)
    {
        const std::string src = "fn f() -> Int { return 0; } fn g() -> Int { return 0; } fn main() "
                                "-> Int { return (f()).g(); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "only direct calls are supported");
    }

    // duplicate struct name (covers duplicate check in struct collection pass)
    {
        const std::string src =
            "struct S { x: Int; } struct S { y: Int; } fn main() -> Unit { return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "duplicate type name 'S'");
    }

    // struct field: unknown field type (covers type_from_ast failure in struct field resolution)
    {
        const std::string src = "struct S { x: NoSuch; } fn main() -> Unit { return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown type 'NoSuch'");
    }

    // unknown name (NameExpr)
    {
        const std::string src = "fn main() -> Unit { nope; return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown name 'nope'");
    }

    // unary expr: propagates unknown name failure
    {
        const std::string src = "fn main() -> Unit { -nope; return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown name 'nope'");
    }

    // binary expr: short-circuits when either side fails
    {
        const std::string src = "fn main() -> Unit { nope + 1; return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown name 'nope'");
    }

    // string concatenation
    {
        const std::string src = "fn main() -> String { return \"a\" + \"b\"; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    // arithmetic/comparison/boolean operator mismatches
    {
        const std::string src = "fn main() -> Unit { 1 - true; return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "arithmetic operators expect Int operands");
    }

    {
        const std::string src = "fn main() -> Unit { 1 < true; return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "comparison operators expect Int operands");
    }

    {
        const std::string src = "fn main() -> Unit { true && 1; return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "boolean operators expect Bool operands");
    }

    // member access: base expression failure propagates
    {
        const std::string src = "fn main() -> Unit { nope.x; return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown name 'nope'");
    }

    // call: callee is not a direct name/member/scoped (e.g. group expr)
    {
        const std::string src = "fn f() -> Unit { return; } fn main() -> Unit { (f)(); return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "only direct calls are supported");
    }

    // print: ok + error when arg fails to type-check
    {
        const std::string src = "fn main() -> Unit { print(1); return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    {
        const std::string src = "fn main() -> Unit { print(nope); return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown name 'nope'");
    }

    // call: argument expr fails to type-check but does not abort the call check
    {
        const std::string src =
            "fn f(x: Int) -> Int { return x; } fn main() -> Int { return f(nope); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown name 'nope'");
    }

    // scoped call: unknown variant
    {
        const std::string src = "enum E { A; } fn main() -> E { return E::B(); }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown variant 'B' for enum 'E'");
    }

    // scoped name: variant with no payload can be used as a value
    {
        const std::string src = "enum E { A; } fn main() -> E { return E::A; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    // struct literal: unknown struct type
    {
        const std::string src = "fn main() -> Unit { NoSuch{}; return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown struct type 'NoSuch'");
    }

    // struct literal: known field but initializer fails to type-check
    {
        const std::string src =
            "struct S { x: Int; } fn main() -> Unit { let s: S = S{ x: nope }; s; return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown name 'nope'");
    }

    // struct literal: multiple missing fields exercises the list formatting
    {
        const std::string src = "struct S { a: Int; b: Int; c: Int; } fn main() -> Unit { let s: S "
                                "= S{ a: 1 }; s; return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "missing required fields");
    }

    // function signature: unknown param type
    {
        const std::string src = "fn f(x: NoSuch) -> Unit { return; } fn main() -> Unit { return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown type 'NoSuch'");
    }

    // AST robustness: exercise unsupported binary operator (covers implicit switch-default edge).
    {
        using curlee::lexer::TokenKind;
        using curlee::parser::BinaryExpr;
        using curlee::parser::Block;
        using curlee::parser::Expr;
        using curlee::parser::ExprStmt;
        using curlee::parser::Function;
        using curlee::parser::IntExpr;
        using curlee::parser::Program;
        using curlee::parser::ReturnStmt;
        using curlee::parser::Stmt;
        using curlee::parser::TypeName;
        using curlee::source::Span;

        Program program;

        Function main_fn;
        main_fn.span = Span{.start = 0, .end = 0};
        main_fn.name = "main";
        main_fn.return_type = TypeName{.span = Span{.start = 0, .end = 0}, .name = "Unit"};
        main_fn.body = Block{.span = Span{.start = 0, .end = 0}, .stmts = {}};

        Expr lhs;
        lhs.id = 1;
        lhs.span = Span{.start = 0, .end = 0};
        lhs.node = IntExpr{.lexeme = "1"};

        Expr rhs;
        rhs.id = 2;
        rhs.span = Span{.start = 0, .end = 0};
        rhs.node = IntExpr{.lexeme = "2"};

        Expr bin;
        bin.id = 3;
        bin.span = Span{.start = 0, .end = 0};
        bin.node = BinaryExpr{.op = TokenKind::KwFn,
                              .lhs = std::make_unique<Expr>(std::move(lhs)),
                              .rhs = std::make_unique<Expr>(std::move(rhs))};

        Stmt expr_stmt;
        expr_stmt.span = Span{.start = 0, .end = 0};
        expr_stmt.node = ExprStmt{.expr = std::move(bin)};

        Stmt ret_stmt;
        ret_stmt.span = Span{.start = 0, .end = 0};
        ret_stmt.node = ReturnStmt{.value = std::nullopt};

        main_fn.body.stmts.push_back(std::move(expr_stmt));
        main_fn.body.stmts.push_back(std::move(ret_stmt));
        program.functions.push_back(std::move(main_fn));

        const auto res = types::type_check(program);
        expect_diag(res, "unsupported binary operator");
    }

    std::cout << "OK\n";
    return 0;
}
