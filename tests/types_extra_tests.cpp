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

    // include/curlee/types/type.h: Type equality and stringify helpers.
    {
        using curlee::types::Type;
        using curlee::types::TypeKind;

        if (!(Type{.kind = TypeKind::Int} == Type{.kind = TypeKind::Int}))
        {
            fail("expected Int == Int");
        }
        if (Type{.kind = TypeKind::Int} == Type{.kind = TypeKind::Bool})
        {
            fail("expected Int != Bool");
        }

        if (!(Type{.kind = TypeKind::Capability, .name = "io.stdout"} ==
              Type{.kind = TypeKind::Capability, .name = "io.stdout"}))
        {
            fail("expected cap io.stdout == cap io.stdout");
        }
        if (Type{.kind = TypeKind::Capability, .name = "io.stdout"} ==
            Type{.kind = TypeKind::Capability, .name = "io.stdin"})
        {
            fail("expected different cap names to compare unequal");
        }

        if (curlee::types::to_string(static_cast<TypeKind>(999)) != "<unknown>")
        {
            fail("expected to_string(unknown TypeKind) to return <unknown>");
        }

        // Cover remaining switch cases at runtime (avoid constexpr folding).
        const TypeKind kinds[] = {
            TypeKind::Int,        TypeKind::Bool,   TypeKind::String, TypeKind::Unit,
            TypeKind::Capability, TypeKind::Struct, TypeKind::Enum,
        };
        for (std::size_t i = 0; i < (sizeof(kinds) / sizeof(kinds[0])); ++i)
        {
            const TypeKind k = kinds[i];
            const auto s = curlee::types::to_string(k);
            if (s.empty())
            {
                fail("expected to_string(TypeKind) to be non-empty");
            }
        }
        if (curlee::types::to_string(Type{.kind = TypeKind::Capability, .name = "python.ffi"}) !=
            "python.ffi")
        {
            fail("expected to_string(Type{cap python.ffi}) to return python.ffi");
        }
    }

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

    // match: scrutinee expression type failure should short-circuit match checking.
    {
        const std::string src = R"(enum E { A; }
fn main() -> Unit {
    match (unknown_name) {
        E::A => { return; }
    }
    return;
})";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "unknown name 'unknown_name'");
    }

    // match: non-enum scrutinee and arm-body traversal.
    {
        const std::string src = R"(enum E { A; }
fn main(v: Int) -> Unit {
    match (v) {
        E::A => { let x: Int = 1; x + 1; return; }
    }
    return;
})";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "match expects an enum value");
    }

    // match: non-enum path still handles null arm body in manually-shaped AST.
    {
        const std::string src = R"(enum E { A; }
        fn main(v: Int) -> Unit {
            match (v) {
            E::A => { return; }
            }
            return;
        })";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        auto program = std::get<parser::Program>(std::move(parsed));
        auto* match_stmt = std::get_if<parser::MatchStmt>(&program.functions[0].body.stmts[0].node);
        if (match_stmt == nullptr || match_stmt->arms.empty())
            fail("expected match statement with one arm");
        match_stmt->arms[0].body.reset();
        const auto res = types::type_check(program);
        expect_diag(res, "match expects an enum value");
    }

    // match: enum mismatch + unknown variant + payload binding rules + duplicate arm.
    {
        const std::string src = R"(enum E { A(Int); B; C; }
enum F { A; }
fn main(v: E) -> Unit {
    match (v) {
        F::A => { return; }
        E::Nope(x) => { return; }
        E::A => { return; }
        E::B(x) => { return; }
        E::B => { return; }
    }
    return;
})";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "match arm uses enum 'F' but matched value is 'E'");
        expect_diag(res, "unknown variant 'Nope' for enum 'E'");
        expect_diag(res, "must bind payload with (name)");
        expect_diag(res, "cannot bind payload (variant has no payload)");
        expect_diag(res, "duplicate match arm for variant 'E::B'");
    }

    // match: unknown-variant and valid-variant paths with null arm body (manually-shaped AST).
    {
        const std::string src = R"(enum E { A; }
        fn main(v: E) -> Unit {
            match (v) {
            E::Nope => { return; }
            }
            return;
        })";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        auto program = std::get<parser::Program>(std::move(parsed));
        auto* match_stmt = std::get_if<parser::MatchStmt>(&program.functions[0].body.stmts[0].node);
        if (match_stmt == nullptr || match_stmt->arms.empty())
            fail("expected match statement with one arm");
        match_stmt->arms[0].body.reset();
        const auto res = types::type_check(program);
        expect_diag(res, "unknown variant 'Nope' for enum 'E'");
    }

    {
        const std::string src = R"(enum E { A; }
        fn main(v: E) -> Unit {
            match (v) {
            E::A => { return; }
            }
            return;
        })";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        auto program = std::get<parser::Program>(std::move(parsed));
        auto* match_stmt = std::get_if<parser::MatchStmt>(&program.functions[0].body.stmts[0].node);
        if (match_stmt == nullptr || match_stmt->arms.empty())
            fail("expected match statement with one arm");
        match_stmt->arms[0].body.reset();
        const auto res = types::type_check(program);
        expect_ok(res);
    }

    // match: non-exhaustive message formatting with multiple missing variants.
    {
        const std::string src = R"(enum E { A; B; C; }
fn main(v: E) -> Unit {
    match (v) {
        E::A => { return; }
    }
    return;
})";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "missing arms E::B, E::C");
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
        const std::string src = "fn main(p: cap python.ffi) -> Unit { python_ffi.call(); }";
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
        const std::string src =
            "fn main(p: cap python.ffi) -> Unit { unsafe { python_ffi.call(1); } }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "python_ffi.call is stubbed and currently takes 0 arguments");
    }

    // capability types: accept cap names without dots (is_capability=true path)
    {
        const std::string src = "fn main(p: cap foo) -> Unit { return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    // print() argument arity and type
    {
        const std::string src = "fn main(p: cap io.stdout) -> Unit { print(); }";
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
        const std::string src = "struct T { x: Int; } fn main(p: cap io.stdout) -> Unit { let t: T "
                                "= T{ x: 1 }; print(t); }";
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
        const std::string src = "fn main(p: cap io.stdout) -> Unit { print(true); return; }";
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
        const std::string src = "fn main(p: cap io.stdout) -> Unit { print(\"a\"); return; }";
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

    // cannot redeclare reserved builtin function at tail of builtin-name list
    {
        const std::string src =
            "fn __set_insert_int() -> Unit { return; } fn main() -> Unit { return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "cannot declare builtin function '__set_insert_int'");
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
        const std::string src = "fn main(p: cap io.stdout) -> Unit { print(1); return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    // print: exercise require_capability multiple times to cover vector growth/non-growth.
    {
        const std::string src =
            "fn main(p: cap io.stdout) -> Unit { print(1); print(2); print(3); print(4); return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_ok(res);
    }

    // print: missing capability value path (covers has_capability_value + require_capability error
    // branch)
    {
        const std::string src = "fn main(p: cap other) -> Unit { print(true); return; }";
        const auto lexed = lexer::lex(src);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
            fail("lex failed");
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            fail("parse failed");
        const auto res = types::type_check(std::get<parser::Program>(parsed));
        expect_diag(res, "missing capability value 'io.stdout'");
    }

    {
        const std::string src = "fn main(p: cap io.stdout) -> Unit { print(nope); return; }";
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

    // Builtin call typing coverage: exercise arity/type-error branches for runtime intrinsics.
    {
        const auto run_tc = [](const std::string& src) -> types::TypeCheckResult
        {
            const auto lexed = lexer::lex(src);
            if (std::holds_alternative<diag::Diagnostic>(lexed))
                fail("lex failed");
            const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
            if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
                fail("parse failed");
            return types::type_check(std::get<parser::Program>(parsed));
        };

        expect_diag(run_tc("fn main() -> String { return __read_line(); }"),
                    "__read_line expects exactly 1 argument");

        expect_diag(run_tc("fn main(r: cap fs.read) -> String { return __fs_read_text(1, r); }"),
                    "fs path must be String");

        expect_diag(
            run_tc("fn main(w: cap fs.write) -> Unit { __fs_write_text(1, \"x\", w); return; }"),
            "fs path must be String");

        expect_diag(
            run_tc("fn main(w: cap fs.write) -> Unit { __fs_write_text(\"a\", 1, w); return; }"),
            "fs content must be String");

        expect_diag(
            run_tc("fn main(t: cap io.tty) -> Unit { __tty_write_at(true, 0, \"x\", t); return; }"),
            "tty row must be Int");

        expect_diag(
            run_tc("fn main(t: cap io.tty) -> Unit { __tty_write_at(0, true, \"x\", t); return; }"),
            "tty col must be Int");

        expect_diag(
            run_tc("fn main(t: cap io.tty) -> Unit { __tty_write_at(0, 0, 1, t); return; }"),
            "tty text must be String");

        expect_diag(
            run_tc("fn main(r: cap rng.seeded) -> Int { return __rng_next_int(true, r); }"),
            "rng max must be Int");

        expect_diag(run_tc("fn main() -> Unit { __vec_new_int(true); return; }"),
                "vec max length must be Int");

        expect_diag(run_tc("fn main() -> Unit { __vec_push_int(__vec_new_int(4), true); return; }"),
                    "vec value must be Int");

        expect_diag(run_tc("fn main() -> Int { return __vec_get_int(__vec_new_int(4), true); }"),
                    "vec index must be Int");

        expect_diag(
            run_tc("fn main() -> Unit { __vec_set_int(__vec_new_int(4), true, 1); return; }"),
            "vec index must be Int");

        expect_diag(
            run_tc("fn main() -> Unit { __vec_set_int(__vec_new_int(4), 0, true); return; }"),
            "vec value must be Int");

        expect_diag(run_tc("fn main() -> Unit { __vec_new_bool(true); return; }"),
                    "vec max length must be Int");

        expect_diag(
            run_tc("fn main() -> Unit { __vec_push_bool(__vec_new_bool(4), 1); return; }"),
            "vec value must be Bool");

        expect_diag(
            run_tc("fn main() -> Bool { return __vec_get_bool(__vec_new_bool(4), true); }"),
            "vec index must be Int");

        expect_diag(
            run_tc("fn main() -> Unit { __vec_set_bool(__vec_new_bool(4), true, false); return; }"),
            "vec index must be Int");

        expect_diag(
            run_tc("fn main() -> Unit { __vec_set_bool(__vec_new_bool(4), 0, 1); return; }"),
            "vec value must be Bool");

        expect_diag(run_tc("fn main() -> Bool { return __set_has_int(__set_new_int(), true); }"),
                    "set value must be Int");

        expect_diag(
            run_tc("fn main() -> Unit { __set_insert_int(__set_new_int(), true); return; }"),
            "set value must be Int");

        expect_diag(run_tc("enum E { A(Int); } fn main() -> Bool { return variant_is(1, E::A); }"),
                    "variant_is expects enum value as first argument");

        expect_diag(
            run_tc("enum E { A(Int); } fn main() -> Int { return variant_unwrap(1, E::A); }"),
            "variant_unwrap expects enum value as first argument");

        expect_diag(
            run_tc("enum E { A(Int); } fn main() -> Int { let e: E = E::A(1); return variant_unwrap(e, e); }"),
            "variant_unwrap expects enum variant tag as second argument");

        expect_diag(
            run_tc("fn main(r: cap fs.read) -> String { return __fs_read_text(\"p\"); }"),
            "__fs_read_text expects exactly 2 argument");

        expect_diag(
            run_tc("fn main(w: cap fs.write) -> Unit { __fs_write_text(\"p\", \"x\"); return; }"),
            "__fs_write_text expects exactly 3 argument");

        expect_diag(
            run_tc("fn main(t: cap io.tty) -> Unit { __tty_clear(); return; }"),
            "__tty_clear expects exactly 1 argument");

        expect_diag(
            run_tc("fn main(t: cap io.tty) -> Unit { __tty_write_at(0, 0, \"x\"); return; }"),
            "__tty_write_at expects exactly 4 argument");

        expect_diag(
            run_tc("fn main(t: cap io.tty) -> Unit { __tty_flush(); return; }"),
            "__tty_flush expects exactly 1 argument");

        expect_diag(
            run_tc("fn main(r: cap rng.seeded) -> Int { return __rng_next_int(10); }"),
            "__rng_next_int expects exactly 2 argument");

        expect_diag(run_tc("fn main() -> Int { return __vec_len_int(); }"),
                    "__vec_len_int expects exactly 1 argument");

        expect_diag(run_tc("fn main() -> Unit { __vec_push_int(__vec_new_int(4)); return; }"),
                    "__vec_push_int expects exactly 2 argument");

        expect_diag(run_tc("fn main() -> Int { return __vec_get_int(__vec_new_int(4)); }"),
                    "__vec_get_int expects exactly 2 argument");

        expect_diag(run_tc("fn main() -> Unit { __vec_set_int(__vec_new_int(4), 0); return; }"),
                    "__vec_set_int expects exactly 3 argument");

        expect_diag(run_tc("fn main() -> Int { return __vec_len_bool(); }"),
                    "__vec_len_bool expects exactly 1 argument");

        expect_diag(run_tc("fn main() -> Unit { __vec_new_int(); return; }"),
                "__vec_new_int expects exactly 1 argument");

        expect_diag(run_tc("fn main() -> Unit { __vec_new_bool(); return; }"),
                "__vec_new_bool expects exactly 1 argument");

        expect_diag(run_tc("fn main() -> Int { return __set_new_int(1); }"),
                "__set_new_int expects exactly 0 argument");

        expect_diag(run_tc("fn main() -> Unit { __vec_push_bool(__vec_new_bool(4)); return; }"),
                    "__vec_push_bool expects exactly 2 argument");

        expect_diag(run_tc("fn main() -> Bool { return __vec_get_bool(__vec_new_bool(4)); }"),
                    "__vec_get_bool expects exactly 2 argument");

        expect_diag(
            run_tc("fn main() -> Unit { __vec_set_bool(__vec_new_bool(4), 0); return; }"),
            "__vec_set_bool expects exactly 3 argument");

        expect_diag(run_tc("fn main() -> Bool { return __set_has_int(__set_new_int()); }"),
                    "__set_has_int expects exactly 2 argument");

        expect_diag(
            run_tc("fn main() -> Unit { __set_insert_int(__set_new_int()); return; }"),
            "__set_insert_int expects exactly 2 argument");

        expect_diag(run_tc("enum E { A(Int); } fn main() -> Bool { return variant_is(1); }"),
                    "variant_is expects exactly 2 argument");

        expect_diag(
            run_tc("enum E { A(Int); } fn main() -> Bool { let e: E = E::A(1); return variant_is(e, e); }"),
            "variant_is expects enum variant tag as second argument");

        expect_diag(
            run_tc("enum E { A(Int); } enum F { A(Int); } fn main() -> Bool { let e: E = E::A(1); return variant_is(e, F::A); }"),
            "variant_is enum mismatch between value and variant tag");

        expect_diag(
            run_tc("enum E { A(Int); } fn main() -> Bool { let e: E = E::A(1); return variant_is(e, E::Nope); }"),
            "unknown variant 'Nope' for enum 'E'");

        expect_diag(run_tc("enum E { A(Int); } fn main() -> Int { return variant_unwrap(1); }"),
                    "variant_unwrap expects exactly 2 argument");

        expect_diag(
            run_tc("enum E { A(Int); } enum F { A(Int); } fn main() -> Int { let e: E = E::A(1); return variant_unwrap(e, F::A); }"),
            "variant_unwrap enum mismatch between value and variant tag");

        expect_diag(
            run_tc("enum E { A(Int); } fn main() -> Int { let e: E = E::A(1); return variant_unwrap(e, E::Nope); }"),
            "unknown variant 'Nope' for enum 'E'");

        expect_diag(
            run_tc("enum E { A(Int); B; } fn main() -> Int { let e: E = E::B; return variant_unwrap(e, E::B); }"),
            "variant_unwrap expects a payload-carrying variant");

        expect_ok(run_tc("fn main(r: cap fs.read) -> String { return __fs_read_text(\"p\", r); }"));

        expect_ok(
            run_tc("fn main(w: cap fs.write) -> Unit { __fs_write_text(\"p\", \"x\", w); return; }"));

        expect_ok(run_tc("fn main(t: cap io.tty) -> Unit { __tty_clear(t); return; }"));

        expect_ok(
            run_tc("fn main(t: cap io.tty) -> Unit { __tty_write_at(0, 0, \"x\", t); return; }"));

        expect_ok(run_tc("fn main(t: cap io.tty) -> Unit { __tty_flush(t); return; }"));

        expect_ok(run_tc("fn main(r: cap rng.seeded) -> Int { return __rng_next_int(10, r); }"));

        expect_ok(run_tc("fn main() -> Int { return __vec_len_int(__vec_new_int(4)); }"));

        expect_ok(run_tc("fn main() -> Unit { __vec_push_int(__vec_new_int(4), 1); return; }"));

        expect_ok(run_tc("fn main() -> Int { return __vec_get_int(__vec_new_int(4), 0); }"));

        expect_ok(
            run_tc("fn main() -> Unit { __vec_set_int(__vec_new_int(4), 0, 1); return; }"));

        expect_ok(run_tc("fn main() -> Int { return __vec_len_bool(__vec_new_bool(4)); }"));

        expect_ok(
            run_tc("fn main() -> Unit { __vec_push_bool(__vec_new_bool(4), true); return; }"));

        expect_ok(
            run_tc("fn main() -> Bool { return __vec_get_bool(__vec_new_bool(4), 0); }"));

        expect_ok(
            run_tc("fn main() -> Unit { __vec_set_bool(__vec_new_bool(4), 0, false); return; }"));

        expect_ok(run_tc("fn main() -> Bool { return __set_has_int(__set_new_int(), 1); }"));

        expect_ok(
            run_tc("fn main() -> Unit { __set_insert_int(__set_new_int(), 1); return; }"));

        expect_ok(
            run_tc("enum E { A(Int); } fn main() -> Bool { let e: E = E::A(1); return variant_is(e, E::A); }"));

        expect_ok(
            run_tc("enum E { A(Int); } fn main() -> Int { let e: E = E::A(1); return variant_unwrap(e, E::A); }"));

        // Exercise short-circuit branches where check_expr(...) fails and returns nullopt.
        expect_diag(
            run_tc("fn main(r: cap fs.read) -> String { return __fs_read_text(missing, r); }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("fn main(w: cap fs.write) -> Unit { __fs_write_text(missing, \"x\", w); return; }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("fn main(w: cap fs.write) -> Unit { __fs_write_text(\"p\", missing, w); return; }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("fn main(t: cap io.tty) -> Unit { __tty_write_at(missing, 0, \"x\", t); return; }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("fn main(t: cap io.tty) -> Unit { __tty_write_at(0, missing, \"x\", t); return; }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("fn main(t: cap io.tty) -> Unit { __tty_write_at(0, 0, missing, t); return; }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("fn main(r: cap rng.seeded) -> Int { return __rng_next_int(missing, r); }"),
            "unknown name 'missing'");

        expect_diag(run_tc("fn main() -> Unit { __vec_new_int(missing); return; }"),
                    "unknown name 'missing'");

        expect_diag(
            run_tc("fn main() -> Unit { __vec_push_int(__vec_new_int(4), missing); return; }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("fn main() -> Int { return __vec_get_int(__vec_new_int(4), missing); }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("fn main() -> Unit { __vec_set_int(__vec_new_int(4), missing, 1); return; }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("fn main() -> Unit { __vec_set_int(__vec_new_int(4), 0, missing); return; }"),
            "unknown name 'missing'");

        expect_diag(run_tc("fn main() -> Unit { __vec_new_bool(missing); return; }"),
                    "unknown name 'missing'");

        expect_diag(
            run_tc("fn main() -> Unit { __vec_push_bool(__vec_new_bool(4), missing); return; }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("fn main() -> Bool { return __vec_get_bool(__vec_new_bool(4), missing); }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("fn main() -> Unit { __vec_set_bool(__vec_new_bool(4), missing, false); return; }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("fn main() -> Unit { __vec_set_bool(__vec_new_bool(4), 0, missing); return; }"),
            "unknown name 'missing'");

        expect_diag(run_tc("fn main() -> Bool { return __set_has_int(__set_new_int(), missing); }"),
                    "unknown name 'missing'");

        expect_diag(
            run_tc("fn main() -> Unit { __set_insert_int(__set_new_int(), missing); return; }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("enum E { A(Int); } fn main() -> Bool { return variant_is(missing, E::A); }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("enum E { A(Int); } fn main() -> Int { return variant_unwrap(missing, E::A); }"),
            "unknown name 'missing'");

        expect_diag(
            run_tc("enum E { A(Int); } fn main() -> Int { let e: E = E::A(1); return variant_unwrap(e, E::B); }"),
            "unknown variant 'B' for enum 'E'");

        expect_ok(run_tc("fn main() -> Unit { let v: Vec<Bool> = __vec_new_bool(4); return; }"));

        expect_ok(run_tc("fn main() -> Unit { let s: Set<Int> = __set_new_int(); return; }"));

        expect_diag(
            run_tc("fn main() -> Unit { let v: Vec<Int> = __vec_new_bool(4); return; }"),
            "type mismatch in let");

        expect_diag(run_tc("fn main() -> Unit { let v: Vec = __vec_new_int(4); return; }"),
                    "Vec type requires one type argument");

        expect_diag(run_tc("fn main() -> Unit { let s: Set = __set_new_int(); return; }"),
                    "Set type requires one type argument");

        expect_diag(run_tc("fn main() -> Unit { let x: Int<Bool> = 1; return; }"),
                    "type 'Int' does not take type arguments");

        expect_diag(run_tc("fn main() -> Unit { let v: Vec<Nope> = __vec_new_int(4); return; }"),
                    "unknown collection element type 'Nope'");

        expect_ok(run_tc(
            "fn id<T>(x: T) -> T { return x; } fn main() -> Int { return id(41); }"));

        expect_diag(run_tc("fn dup<T>(x: T, y: T) -> T { return x; } fn main() -> Int { return dup(1, false); }"),
                    "argument type mismatch for call to 'dup'");

        expect_ok(run_tc("struct Box<T> { value: T; } fn main() -> Int { let b: Box<Int> = Box<Int>{ value: 7 }; return b.value; }"));

        expect_diag(run_tc("struct Box<T> { value: T; } fn main() -> Unit { let b: Box = Box<Int>{ value: 1 }; return; }"),
                "type 'Box' requires one type argument");

        expect_diag(run_tc("struct Box<T> { value: T; } fn main() -> Unit { let b: Box<Int> = Box<Int>{ value: false }; return; }"),
                "field 'value' type mismatch: expected Int, got Bool");
    }

    std::cout << "OK\n";
    return 0;
}
