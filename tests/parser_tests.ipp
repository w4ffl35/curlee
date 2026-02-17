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

    auto expect_parse_error_contains = [](const std::string& src, const std::string& needle)
    {
        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on invalid program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            fail("expected parse to fail");
        }

        const auto& diags = std::get<std::vector<diag::Diagnostic>>(parsed);
        bool found = false;
        for (const auto& d : diags)
        {
            if (d.message.find(needle) != std::string::npos)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            std::cerr << "parse diags:\n";
            for (const auto& d : diags)
            {
                std::cerr << "  - " << d.message << "\n";
            }
            fail("expected parse error containing: " + needle);
        }
    };

    // Import ordering: imports must be before other top-level decls.
    {
        const std::string src = R"(fn f() -> Unit { return; }
import foo;
)";
        expect_parse_error_contains(
            src, "import declarations must appear before any other top-level declarations");
    }

    // Struct declaration: duplicate field name.
    {
        const std::string src = R"(struct S {
  x: Int;
  x: Int;
}
fn main() -> Unit { return; }
)";
        expect_parse_error_contains(src, "duplicate field name in struct declaration");
    }

    // Enum declaration: duplicate variant name.
    {
        const std::string src = R"(enum E {
  A;
  A;
}
fn main() -> Unit { return; }
)";
        expect_parse_error_contains(src, "duplicate variant name in enum declaration");
    }

    // Struct literal: duplicate field initializer.
    {
        const std::string src = R"(struct Point { x: Int; }
fn main() -> Unit {
  let p: Point = Point { x: 1, x: 2 };
  return;
}
)";
        expect_parse_error_contains(src, "duplicate field in struct literal");
    }

    // Capability types: parse_type success (qualified + unqualified) + dump formatting.
    {
        const std::string src = R"(fn main(p0: cap io.stdout, p1: cap foo) -> Unit { return; })";
        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on capability-type program");
        }
        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on capability-type program");
        }
        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("cap io.stdout") == std::string::npos)
        {
            fail("dump missing cap io.stdout type");
        }
        if (dumped.find("cap foo") == std::string::npos)
        {
            fail("dump missing cap foo type");
        }
    }

    // Capability types: missing capability name after 'cap'.
    {
        const std::string src = R"(fn main(p: cap) -> Unit { return; })";
        expect_parse_error_contains(src, "expected capability name after 'cap'");
    }

    // Capability types: missing identifier after '.' in qualified name.
    {
        const std::string src = R"(fn main(p: cap io.) -> Unit { return; })";
        expect_parse_error_contains(src, "expected identifier after '.' in capability name");
    }

    // Capability types: whitespace not allowed in qualified capability names.
    {
        const std::string src = R"(fn main(p: cap io .stdout) -> Unit { return; })";
        expect_parse_error_contains(src, "whitespace is not allowed in qualified capability names");
    }

    {
        const std::string src = R"(fn main(p: cap io. stdout) -> Unit { return; })";
        expect_parse_error_contains(src, "whitespace is not allowed in qualified capability names");
    }

    // Struct literal: allow trailing comma.
    {
        const std::string src = R"(struct Point { x: Int; y: Int; }
fn main() -> Unit {
  let p: Point = Point { x: 1, y: 2, };
  return;
}
)";
        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on trailing-comma struct literal program");
        }
        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on trailing-comma struct literal program");
        }
    }

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

    // Binary operator parsing coverage: exercise all precedence levels.
    {
        const std::string src = R"(fn main() -> Unit {
  let a: Int = 1 + 2 * 3 - 4 / 5;
  let b: Bool = (1 < 2) && (3 <= 4) && (5 > 6) && (7 >= 8);
  let c: Bool = (1 == 2) || (3 != 4);
  let d: Bool = true || false && true;
  return;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on operator coverage program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on operator coverage program");
        }
    }

    // Imports, structs and enums.
    {
        const std::string src = R"(import foo.bar as baz;

struct Point {
  x: Int;
  y: Int;
}

enum Option {
  Some(Int);
  None;
}

fn main() -> Unit {
  return;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on import/struct/enum program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on import/struct/enum program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        if (prog.imports.size() != 1 || prog.structs.size() != 1 || prog.enums.size() != 1 ||
            prog.functions.size() != 1)
        {
            fail("expected exactly one import, struct, enum, and function");
        }
        if (prog.structs[0].fields.size() != 2)
        {
            fail("expected Point to have two fields");
        }
        if (prog.enums[0].variants.size() != 2)
        {
            fail("expected Option to have two variants");
        }
    }

    // Dumper: group/unary expr + multi-arg call formatting.
    {
        const std::string src = R"(fn main() -> Unit {
  let x: Int = -(1);
  foo(1, 2);
  return;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on dumper group/unary/call program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on dumper group/unary/call program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("minus (1)") == std::string::npos)
        {
            fail("dump missing unary/group expression");
        }
        if (dumped.find("foo(1, 2)") == std::string::npos)
        {
            fail("dump missing multi-arg call formatting");
        }
    }

    // Dumper: dump_program newline insertion between multiple blocks.
    {
        const std::string src = R"(import foo;
import bar as baz;

struct A { x: Int; }
struct B { y: Int; }

enum E1 { V1; }
enum E2 { V2; }

fn f() -> Unit { return; }
fn g() -> Unit { return; }
)";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on dumper multi-block program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on dumper multi-block program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("import foo") == std::string::npos ||
            dumped.find("import bar") == std::string::npos)
        {
            fail("dump missing imports");
        }
        if (dumped.find("struct A") == std::string::npos ||
            dumped.find("struct B") == std::string::npos ||
            dumped.find("enum E1") == std::string::npos ||
            dumped.find("enum E2") == std::string::npos ||
            dumped.find("fn f") == std::string::npos || dumped.find("fn g") == std::string::npos)
        {
            fail("dump missing multi-block decls");
        }
    }

    // Expressions: cover all operator tiers (||, &&, ==/!=, comparisons, +/-, */).
    {
        const std::string src = R"(fn expr_ops() -> Unit {
  (true || false) && true;
  1 == 2;
  1 != 2;
  1 < 2;
  1 <= 2;
  1 > 2;
  1 >= 2;
  1 + 2;
  1 - 2;
  1 * 2;
  1 / 2;
  return;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on expr-ops program");
        }
        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on expr-ops program");
        }

        const std::string dumped = parser::dump(std::get<parser::Program>(parsed));
        if (dumped.find("or_or") == std::string::npos ||
            dumped.find("and_and") == std::string::npos ||
            dumped.find("equal_equal") == std::string::npos ||
            dumped.find("bang_equal") == std::string::npos ||
            dumped.find("less") == std::string::npos ||
            dumped.find("less_equal") == std::string::npos ||
            dumped.find("greater") == std::string::npos ||
            dumped.find("greater_equal") == std::string::npos ||
            dumped.find("plus") == std::string::npos || dumped.find("minus") == std::string::npos ||
            dumped.find("star") == std::string::npos || dumped.find("slash") == std::string::npos)
        {
            fail("dump missing expression operator(s)");
        }
    }

    // Predicates: cover equality/comparisons/arithmetic in the predicate grammar (incl. true
    // literal).
    {
        const std::string src = R"(fn pred_all_ops(a: Int, b: Int) -> Unit
  [ requires true;
    requires a == b;
    requires a != b;
    requires a < b;
    requires a <= b;
    requires a > b;
    requires a >= b;
    requires (a + b) > (a - b);
    requires (a * b) > (a / b);
  ]
{
  return;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on pred-all-ops program");
        }
        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on pred-all-ops program");
        }

        const std::string dumped = parser::dump(std::get<parser::Program>(parsed));
        if (dumped.find("true") == std::string::npos ||
            dumped.find("equal_equal") == std::string::npos ||
            dumped.find("bang_equal") == std::string::npos ||
            dumped.find("greater") == std::string::npos ||
            dumped.find("slash") == std::string::npos)
        {
            fail("dump missing predicate operator(s)");
        }
    }

    // Struct literals: cover empty and non-empty dumping + expr-id assignment traversal.
    {
        const std::string src = R"(fn struct_lit() -> Unit {
  T{};
  T{ x: 1, y: 2 };
  return;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on struct-literal program");
        }
        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on struct-literal program");
        }
        const std::string dumped = parser::dump(std::get<parser::Program>(parsed));
        if (dumped.find("T{}") == std::string::npos ||
            dumped.find("T{ x: 1, y: 2 }") == std::string::npos)
        {
            fail("dump missing struct literal formatting");
        }
    }

    // Dumper: if/else branch.
    {
        const std::string src = R"(fn if_else() -> Unit {
  if (true) { return; } else { return; }
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on if/else program");
        }
        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on if/else program");
        }
        const std::string dumped = parser::dump(std::get<parser::Program>(parsed));
        if (dumped.find(" else ") == std::string::npos)
        {
            fail("dump missing else block");
        }
    }

    // Dumper: if without else (covers else_block == nullptr branch).
    {
        const std::string src = R"(fn if_no_else() -> Unit {
  if (true) { return; }
  return;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on if-without-else program");
        }
        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on if-without-else program");
        }
        (void)parser::dump(std::get<parser::Program>(parsed));
    }

    // Dumper: contract printing when requires is empty but ensures is non-empty.
    {
        const std::string src = R"(fn ensures_only() -> Unit
  [ ensures true; ]
{
  return;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on ensures-only program");
        }
        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on ensures-only program");
        }
        const std::string dumped = parser::dump(std::get<parser::Program>(parsed));
        if (dumped.find("ensures true") == std::string::npos)
        {
            fail("dump missing ensures-only contract");
        }
    }

    // Dumper: cover has_types short-circuit cases and optional return type.
    {
        // Imports only: no extra newline after imports.
        const std::string src_imports_only = R"(import foo;
import bar;
)";
        const auto lexed = lexer::lex(src_imports_only);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on imports-only program");
        }
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on imports-only program");
        }
        (void)parser::dump(std::get<parser::Program>(parsed));
    }

    {
        // Imports + functions (no types): newline after imports uses !functions.empty(),
        // and dump_function covers the "no return type" branch.
        const std::string src_imports_fn_only = R"(import foo;

fn main() { return; }
)";
        const auto lexed = lexer::lex(src_imports_fn_only);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on imports+fn-only program");
        }
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on imports+fn-only program");
        }
        const std::string dumped = parser::dump(std::get<parser::Program>(parsed));
        if (dumped.find("fn main()") == std::string::npos)
        {
            fail("dump missing function without return type");
        }
    }

    {
        // Enums only: has_types is true via enums when structs are empty.
        const std::string src_enums_only = R"(import foo;

enum E { V; }
)";
        const auto lexed = lexer::lex(src_enums_only);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on enums-only program");
        }
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on enums-only program");
        }
        (void)parser::dump(std::get<parser::Program>(parsed));
    }

    {
        // Types only (no functions): covers has_types && !functions.empty() false branch.
        const std::string src_types_only = R"(struct A { x: Int; }
enum E { V; }
)";
        const auto lexed = lexer::lex(src_types_only);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on types-only program");
        }
        const auto parsed = parser::parse(std::get<std::vector<lexer::Token>>(lexed));
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on types-only program");
        }
        (void)parser::dump(std::get<parser::Program>(parsed));
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
        if (dumped.find("denominator") == std::string::npos ||
            dumped.find("bang_equal") == std::string::npos || dumped.find("0") == std::string::npos)
        {
            fail("dump missing requires predicate");
        }
        if (dumped.find("result") == std::string::npos ||
            dumped.find("star") == std::string::npos ||
            dumped.find("denominator") == std::string::npos)
        {
            fail("dump missing ensures predicate");
        }
        if (dumped.find("numerator: Int") == std::string::npos ||
            dumped.find("denominator: Int") == std::string::npos)
        {
            fail("dump missing typed parameters");
        }
    }

    {
        const std::string src = R"(fn pred_ops(a: Int, b: Int) -> Int
  [ requires ((a < b) && (b <= a + 10)) || !false;
    ensures (result + 1) >= (a - b); ]
{
  return a;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on predicate-ops program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on predicate-ops program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("and_and") == std::string::npos ||
            dumped.find("or_or") == std::string::npos)
        {
            fail("dump missing &&/|| predicate operators");
        }
        if (dumped.find("bang false") == std::string::npos)
        {
            fail("dump missing unary ! predicate");
        }
        if (dumped.find("a plus 10") == std::string::npos ||
            dumped.find("a minus b") == std::string::npos)
        {
            fail("dump missing +/- predicate expressions");
        }
        if (dumped.find("less") == std::string::npos ||
            dumped.find("less_equal") == std::string::npos ||
            dumped.find("greater_equal") == std::string::npos)
        {
            fail("dump missing comparison predicate operators");
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
    return;
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
        const std::string src = R"(fn main() -> Unit {
  unsafe { let x: Int = 1; x; }
  return;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on unsafe-block program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on unsafe-block program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("unsafe {") == std::string::npos)
        {
            fail("dump missing unsafe block");
        }
    }

    {
        const std::string src = R"(fn main() -> Unit {
  return;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on return-void program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on return-void program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("return;") == std::string::npos)
        {
            fail("dump missing return; statement");
        }
    }

    {
        const std::string src = R"(fn main() -> Unit {
  if (1 < 2) { return; } else { return; }
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on if/else program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on if/else program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("if (") == std::string::npos || dumped.find(" else ") == std::string::npos)
        {
            fail("dump missing if/else statement");
        }
    }

    {
        const std::string src = R"(fn main() -> Unit {
  while (1 < 2) { return; }
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on while program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on while program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("while (") == std::string::npos)
        {
            fail("dump missing while statement");
        }
    }

    {
        const std::string src = R"(fn main() -> Bool {
  let b: Bool = true;
  return b;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on bool literal program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on bool literal program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("true") == std::string::npos)
        {
            fail("dump missing bool literal");
        }
        if (dumped.find("let b: Bool") == std::string::npos)
        {
            fail("dump missing Bool-typed let binding");
        }
    }

    {
        const std::string src = R"(import foo.bar;

fn main() -> Unit {
  return 0;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on import program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on import program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("import foo.bar;") == std::string::npos)
        {
            fail("dump missing import declaration");
        }
    }

    {
        const std::string src = R"(import foo.bar as baz;

fn main() -> Unit {
  return 0;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on import-alias program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on import-alias program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("import foo.bar as baz;") == std::string::npos)
        {
            fail("dump missing import alias declaration");
        }
    }

    {
        const std::string src = R"(fn main() -> Unit {
  return 0;
}

import foo.bar;
)";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on import-after-fn program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            fail("expected parse error for import after function");
        }

        const auto& ds = std::get<std::vector<diag::Diagnostic>>(parsed);
        bool found = false;
        for (const auto& d : ds)
        {
            if (d.message.find(
                    "import declarations must appear before any other top-level declarations") !=
                std::string::npos)
            {
                if (!d.span.has_value())
                {
                    fail("expected span for import-order diagnostic");
                }

                const std::size_t import_pos = src.find("import");
                if (import_pos == std::string::npos)
                {
                    fail("test bug: couldn't find 'import' in source");
                }
                if (d.span->start != import_pos)
                {
                    fail("expected diagnostic span to point at offending import");
                }

                bool has_hint = false;
                for (const auto& note : d.notes)
                {
                    if (note.message.find("move this import above") != std::string::npos)
                    {
                        has_hint = true;
                        break;
                    }
                }
                if (!has_hint)
                {
                    fail("expected actionable hint note for import-order diagnostic");
                }

                found = true;
                break;
            }
        }
        if (!found)
        {
            fail("expected import-order diagnostic");
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

    // Structured data (struct/enum) parsing
    {
        const std::string src = R"(struct Point {
  x: Int;
  y: Int;
}

enum OptionInt {
  None;
  Some(Int);
}

fn main() -> Int {
  let p: Point = Point{ x: 1, y: 2 };
  let o: OptionInt = OptionInt::Some(123);
  return p.x;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on structured-data program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on structured-data program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        if (prog.structs.empty() || prog.enums.empty())
        {
            fail("expected struct and enum declarations in program");
        }
        if (prog.structs[0].span.start >= prog.structs[0].span.end)
        {
            fail("expected struct decl to have a non-empty span");
        }
        if (prog.enums[0].span.start >= prog.enums[0].span.end)
        {
            fail("expected enum decl to have a non-empty span");
        }

        const std::string dumped = parser::dump(prog);
        if (dumped.find("struct Point") == std::string::npos)
        {
            fail("dump missing struct declaration");
        }
        if (dumped.find("enum OptionInt") == std::string::npos)
        {
            fail("dump missing enum declaration");
        }
        if (dumped.find("Point{ x: 1, y: 2 }") == std::string::npos)
        {
            fail("dump missing struct literal");
        }
        if (dumped.find("OptionInt::Some") == std::string::npos)
        {
            fail("dump missing enum constructor");
        }
        if (dumped.find("return p.x") == std::string::npos)
        {
            fail("dump missing field access");
        }
        (void)prog.functions[0];
    }

    {
        const std::string src = "fn first() -> Unit {\n"
                                "  first;\n"
                                "}\n\n"
                                "import a;\n\n"
                                "fn second() -> Unit {\n"
                                "  second;\n"
                                "}\n";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on import-ordering program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            fail("expected parse to fail on import ordering");
        }

        const auto& diags = std::get<std::vector<diag::Diagnostic>>(parsed);
        bool found_import_ordering = false;
        bool found_note_move_import = false;
        bool found_note_first_decl = false;
        for (const auto& d : diags)
        {
            if (d.message.find("import declarations must appear before") == std::string::npos)
            {
                continue;
            }
            found_import_ordering = true;
            for (const auto& note : d.notes)
            {
                if (note.message.find("move this import above") != std::string::npos)
                {
                    found_note_move_import = true;
                }
                if (note.message.find("first declaration is here") != std::string::npos)
                {
                    found_note_first_decl = true;
                    if (!note.span.has_value())
                    {
                        fail("expected 'first declaration is here' note to have a span");
                    }
                }
            }
        }

        if (!found_import_ordering)
        {
            fail("expected import-ordering diagnostic");
        }
        if (!found_note_move_import)
        {
            fail("expected note suggesting moving import");
        }
        if (!found_note_first_decl)
        {
            fail("expected note pointing at first declaration");
        }
    }

    {
        const std::string src = R"(struct Point {
  x: Int;
  y: Int;
}

fn main() -> Unit {
  let p: Point = Point{ x: 1, x: 2, y: 3 };
  p;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on duplicate-field program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            fail("expected parse to fail on duplicate field");
        }

        const auto& diags = std::get<std::vector<diag::Diagnostic>>(parsed);
        bool found = false;
        for (const auto& d : diags)
        {
            if (d.message.find("duplicate field") != std::string::npos)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            fail("expected duplicate-field diagnostic");
        }
    }

    {
        const std::string src = R"(struct Point {
  x: Int;
  y: Int;
}

fn main() -> Unit {
  let p: Point = Point{ x: 1 y: 2 };
  p;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on missing-separator program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            fail("expected parse to fail on missing field separator");
        }

        const auto& diags = std::get<std::vector<diag::Diagnostic>>(parsed);
        bool found = false;
        for (const auto& d : diags)
        {
            if (d.message.find("expected ','") != std::string::npos)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            fail("expected missing-separator diagnostic mentioning ','");
        }
    }

    {
        const std::string src = R"(fn main() -> Unit {
  let p: Int = 1;
  p.;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on invalid-field-access program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            fail("expected parse to fail on invalid field access syntax");
        }

        const auto& diags = std::get<std::vector<diag::Diagnostic>>(parsed);
        bool found = false;
        for (const auto& d : diags)
        {
            if (d.message.find("expected identifier after '.'") != std::string::npos)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            fail("expected invalid field access diagnostic");
        }
    }

    {
        // Import ordering: imports must come before any other top-level declarations.
        const std::string src = "fn main() -> Unit { return; }\n"
                                "import foo.bar;\n"
                                "fn other() -> Unit { return; }\n";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on import-order program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            fail("expected parse to fail on import-order violation");
        }

        const auto& diags = std::get<std::vector<diag::Diagnostic>>(parsed);
        bool found_message = false;
        bool found_first_decl_note = false;
        for (const auto& d : diags)
        {
            if (d.message.find("import declarations must appear before") != std::string::npos)
            {
                found_message = true;
                for (const auto& n : d.notes)
                {
                    if (n.message.find("first declaration is here") != std::string::npos)
                    {
                        found_first_decl_note = true;
                    }
                }
            }
        }
        if (!found_message)
        {
            fail("expected import-order diagnostic");
        }
        if (!found_first_decl_note)
        {
            fail("expected import-order diagnostic to include first declaration note");
        }
    }

    {
        // Struct literals: allow trailing commas.
        const std::string src = R"(struct Point {
  x: Int;
  y: Int;
}

fn main() -> Unit {
  let p: Point = Point{ x: 1, y: 2, };
  p;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on trailing-comma struct literal program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on trailing-comma struct literal program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("Point{") == std::string::npos || dumped.find("y:") == std::string::npos)
        {
            fail("dump missing struct literal with fields");
        }
    }

    {
        // Struct literals: duplicate field should be rejected and note previous initializer.
        const std::string src = R"(struct Point { x: Int; }
fn main() -> Unit {
  let p: Point = Point{ x: 1, x: 2 };
  p;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on duplicate-field struct literal program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            fail("expected parse to fail on duplicate-field struct literal program");
        }

        const auto& diags = std::get<std::vector<diag::Diagnostic>>(parsed);
        bool found_message = false;
        bool found_note = false;
        for (const auto& d : diags)
        {
            if (d.message.find("duplicate field in struct literal") != std::string::npos)
            {
                found_message = true;
                for (const auto& n : d.notes)
                {
                    if (n.message.find("previous field initializer") != std::string::npos)
                    {
                        found_note = true;
                    }
                }
            }
        }
        if (!found_message)
        {
            fail("expected duplicate field in struct literal diagnostic");
        }
        if (!found_note)
        {
            fail("expected duplicate field diagnostic to include previous initializer note");
        }
    }

    {
        // Dump coverage: scoped names, member expressions, if/else, while, unsafe, and return;
        const std::string src = R"(struct S { x: Int; }
fn main() -> Unit {
  unsafe { S{ x: 1, }.x; }
  if (true) { return; } else { return; }
  while (false) { return; }
  Foo::Bar;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on dump-coverage program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on dump-coverage program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        if (dumped.find("unsafe") == std::string::npos ||
            dumped.find("if (") == std::string::npos ||
            dumped.find("while (") == std::string::npos ||
            dumped.find("Foo::Bar") == std::string::npos ||
            dumped.find(".x") == std::string::npos || dumped.find("return;") == std::string::npos)
        {
            fail("dump missing expected constructs");
        }
    }

    // Top-level declaration error.
    expect_parse_error_contains("let x: Int = 1;\n",
                                "expected 'import', 'struct', 'enum', or 'fn'");

    // Import declaration errors.
    expect_parse_error_contains("import ;\n", "expected module name after 'import'");
    expect_parse_error_contains("import foo.;\n", "expected identifier after '.' in import path");
    expect_parse_error_contains("import foo as ;\n",
                                "expected identifier after 'as' in import declaration");
    expect_parse_error_contains("import foo\n", "expected ';' after import declaration");

    // Struct declaration errors.
    expect_parse_error_contains("struct S { x Int; }\n", "expected ':' after field name");
    expect_parse_error_contains("struct S { x: Int; x: Int; }\n",
                                "duplicate field name in struct declaration");
    expect_parse_error_contains("struct S x: Int; }\n", "expected '{' after struct name");
    expect_parse_error_contains("struct S { x: Int }\n", "expected ';' after struct field");
    expect_parse_error_contains("struct S { x: Int;\n", "expected '}' after struct declaration");

    // Enum declaration errors.
    expect_parse_error_contains("enum E { V(Int; }\n", "expected ')' after enum variant payload");
    expect_parse_error_contains("enum E { V }\n", "expected ';' after enum variant");
    expect_parse_error_contains("enum E { V; V; }\n", "duplicate variant name in enum declaration");
    expect_parse_error_contains("enum E { V;\n", "expected '}' after enum declaration");

    // Function signature errors.
    expect_parse_error_contains("fn f(: Int) -> Int { return 0; }\n", "expected parameter name");
    expect_parse_error_contains("fn f(x Int) -> Int { return 0; }\n",
                                "expected ':' after parameter name");
    expect_parse_error_contains("fn f(x: ) -> Int { return 0; }\n", "expected type name");

    // Contract block errors.
    expect_parse_error_contains("fn f() -> Int [ foo; ] { return 0; }\n",
                                "expected 'requires' or 'ensures' in contract block");
    expect_parse_error_contains("fn f() -> Int [ requires true ] { return 0; }\n",
                                "expected ';' after contract clause");
    expect_parse_error_contains("fn f() -> Int [ requires true; ",
                                "expected ']' to end contract block");

    // Refinement/predicate errors.
    expect_parse_error_contains("fn f(x: Int where ) -> Int { return 0; }\n", "expected predicate");
    expect_parse_error_contains("fn f(x: Int) -> Int [ requires (true; ] { return 0; }\n",
                                "expected ')' after predicate");

    // Statement syntax errors.
    expect_parse_error_contains("fn main() -> Unit { let : Int = 1; }\n",
                                "expected identifier after 'let'");
    expect_parse_error_contains("fn main() -> Unit { unsafe 1; }\n", "expected '{' after 'unsafe'");
    expect_parse_error_contains("fn main() -> Unit { if true { return; } }\n",
                                "expected '(' after 'if'");
    expect_parse_error_contains("fn main() -> Unit { if (true { return; } }\n",
                                "expected ')' after if condition");
    expect_parse_error_contains("fn main() -> Unit { if (true) { return; } else return; }\n",
                                "expected '{' to start block");
    expect_parse_error_contains("fn main() -> Unit { while true { return; } }\n",
                                "expected '(' after 'while'");
    expect_parse_error_contains("fn main() -> Unit { return 1 }\n",
                                "expected ';' after return statement");
    expect_parse_error_contains("fn main() -> Unit { 1 }\n", "expected ';' after expression");

    {
        // Dump coverage: group + unary expressions.
        const std::string src = R"(fn main() -> Unit {
  !((1));
  ("hello");
  return;
})";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on group/unary dump program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<parser::Program>(parsed))
        {
            fail("parse failed on group/unary dump program");
        }

        const auto& prog = std::get<parser::Program>(parsed);
        const std::string dumped = parser::dump(prog);
        const std::string bang = std::string(lexer::to_string(lexer::TokenKind::Bang));
        if (dumped.find(bang) == std::string::npos)
        {
            fail("dump missing unary operator");
        }
        if (dumped.find('(') == std::string::npos || dumped.find(')') == std::string::npos)
        {
            fail("dump missing parentheses/grouping");
        }
    }

    {
        // Parser recovery coverage: multiple top-level errors in a single parse.
        // This is intentionally malformed but should produce multiple diagnostics.
        const std::string src = R"(
import foo.
struct S x: Int; }
enum E { V(Int; W; V; }
fn f(x Int) -> { return 0; }
)";

        const auto lexed = lexer::lex(src);
        if (!std::holds_alternative<std::vector<lexer::Token>>(lexed))
        {
            fail("lex failed on multi-error program");
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            fail("expected parse errors for multi-error program");
        }

        const auto& diags = std::get<std::vector<diag::Diagnostic>>(parsed);
        if (diags.size() < 3)
        {
            fail("expected multiple diagnostics for multi-error program");
        }
    }

    std::cout << "OK\n";
    return 0;
}
