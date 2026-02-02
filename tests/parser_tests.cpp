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
            fail("expected parse error containing: " + needle);
        }
    };

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

    // Top-level declaration error.
    expect_parse_error_contains("let x: Int = 1;\n",
                                "expected 'import', 'struct', 'enum', or 'fn'");

    // Import declaration errors.
    expect_parse_error_contains("import ;\n", "expected module name after 'import'");
    expect_parse_error_contains("import foo.;\n", "expected identifier after '.' in import path");
    expect_parse_error_contains("import foo as ;\n", "expected identifier after 'as' in import declaration");
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
    expect_parse_error_contains("fn f(x Int) -> Int { return 0; }\n", "expected ':' after parameter name");
    expect_parse_error_contains("fn f(x: ) -> Int { return 0; }\n", "expected type name");

    std::cout << "OK\n";
    return 0;
}
