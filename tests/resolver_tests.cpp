#include <cstdlib>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/resolver/resolver.h>
#include <curlee/source/source_file.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
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

static curlee::resolver::ResolveResult resolve_with_source(const std::string& src,
                                                           const std::string& path)
{
    const auto program = parse_ok(src);
    const curlee::source::SourceFile file{.path = path, .contents = src};
    return curlee::resolver::resolve(program, file);
}

static curlee::resolver::ResolveResult
resolve_with_source(const std::string& src, const std::string& path,
                    std::optional<std::filesystem::path> entry_dir)
{
    const auto program = parse_ok(src);
    const curlee::source::SourceFile file{.path = path, .contents = src};
    return curlee::resolver::resolve(program, file, std::move(entry_dir));
}

static void write_file(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        fail("failed to write file: " + path.string());
    }
    out << contents;
}

int main()
{
    using namespace curlee;

    {
        // Cover resolve(program) overload (no SourceFile).
        const std::string src = R"(fn main() -> Unit { return 0; })";
        const auto program = parse_ok(src);
        const auto res = resolver::resolve(program);
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for resolve(program) overload");
        }
    }

    {
        // Imports require a source file path: cover the base_path_==null diagnostic.
        const std::string src = R"(import foo.bar;

fn main() -> Unit { return 0; })";
        const auto program = parse_ok(src);
        const auto res = resolver::resolve(program);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error when imports are present without a source path");
        }
    }

    {
        const std::string src = R"(fn print(x: Int) -> Unit {
  return 0;
}

fn main() -> Unit {
  print(1);
  return 0;
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_ok.curlee");
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

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_unknown.curlee");
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

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_dup.curlee");
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

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_shadow.curlee");
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
        const std::string src = R"(fn main() -> Unit {
  let x: Int = 1;
  if (x == 1) { let x: Int = 2; x; } else { x; }
  while (x == 1) { x; }
  return 0;
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_if_while.curlee");
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for if/while scoping");
        }
    }

    {
        const std::string src = R"(import foo.bar;

fn main() -> Unit {
  return 0;
})";

        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / "curlee_resolver_tests";
        const fs::path module_dir = base / "foo";
        fs::create_directories(module_dir);
        const fs::path module_path = module_dir / "bar.curlee";
        write_file(module_path,
                   R"(struct S {
    x: Int;
}

enum E {
    V;
}

fn helper() -> Unit { return 0; })");

        const fs::path main_path = base / "main.curlee";
        const auto res = resolve_with_source(src, main_path.string());
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success when import exists");
        }
    }

    {
        // Imported module lex failure.
        const std::string src = R"(import foo.bad;

fn main() -> Unit { return 0; })";

        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / "curlee_resolver_tests_import_lex_fail";
        const fs::path module_dir = base / "foo";
        fs::create_directories(module_dir);
        write_file(module_dir / "bad.curlee", "@");

        const fs::path main_path = base / "main.curlee";
        const auto res = resolve_with_source(src, main_path.string());
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error when imported module fails to lex");
        }
    }

    {
        // Imported module parse failure.
        const std::string src = R"(import foo.bad;

fn main() -> Unit { return 0; })";

        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / "curlee_resolver_tests_import_parse_fail";
        const fs::path module_dir = base / "foo";
        fs::create_directories(module_dir);
        write_file(module_dir / "bad.curlee", "fn");

        const fs::path main_path = base / "main.curlee";
        const auto res = resolve_with_source(src, main_path.string());
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error when imported module fails to parse");
        }
    }

    {
        // Import aliasing + qualified call via alias.
        const std::string src = R"(import foo.bar as baz;

fn main() -> Unit {
  baz.helper();
  return 0;
})";

        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / "curlee_resolver_tests_import_alias";
        const fs::path module_dir = base / "foo";
        fs::create_directories(module_dir);
        const fs::path module_path = module_dir / "bar.curlee";
        write_file(module_path, "fn helper() -> Unit { return 0; }");

        const fs::path main_path = base / "main.curlee";
        const auto res = resolve_with_source(src, main_path.string());
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for alias-qualified call");
        }
    }

    {
        // Qualified reference via full import path: foo.bar.helper()
        const std::string src = R"(import foo.bar;

fn main() -> Unit {
  foo.bar.helper();
  return 0;
})";

        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / "curlee_resolver_tests_qualified_path";
        const fs::path module_dir = base / "foo";
        fs::create_directories(module_dir);
        const fs::path module_path = module_dir / "bar.curlee";
        write_file(module_path, "fn helper() -> Unit { return 0; }");

        const fs::path main_path = base / "main.curlee";
        const auto res = resolve_with_source(src, main_path.string());
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for path-qualified call");
        }
    }

    {
        // Grouped base expression should *not* be treated as module-qualified; this hits
        // collect_member_chain(false) in MemberExpr resolution.
        const std::string src = R"(fn main() -> Unit {
  let x: Int = 0;
  (x).field;
  return 0;
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_group_member.curlee");
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for grouped member access");
        }
    }

    {
        // python_ffi.call requires an unsafe context.
        const std::string src = R"(fn main() -> Unit {
  python_ffi.call();
  return 0;
})";

        const auto res =
            resolve_with_source(src, "tests/fixtures/resolve_ffi_unsafe_required.curlee");
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error for python_ffi.call outside unsafe");
        }
    }

    {
        // python_ffi.call inside unsafe should not emit the unsafe-context diagnostic.
        const std::string src = R"(fn main() -> Unit {
  unsafe { python_ffi.call(); }
  return 0;
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_ffi_unsafe_ok.curlee");
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for python_ffi.call inside unsafe");
        }
    }

    {
        // python_ffi.<not-call> is treated as a builtin module reference but should not trip the
        // unsafe-context check.
        const std::string src = R"(fn main() -> Unit {
  python_ffi.other();
  return 0;
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_ffi_other.curlee");
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for python_ffi.other()");
        }
    }

    {
        // Unknown qualified name should produce a deterministic diagnostic.
        const std::string src = R"(import foo.bar as baz;

fn main() -> Unit {
  baz.missing();
  return 0;
})";

        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / "curlee_resolver_tests_unknown_qualified";
        const fs::path module_dir = base / "foo";
        fs::create_directories(module_dir);
        const fs::path module_path = module_dir / "bar.curlee";
        write_file(module_path, "fn helper() -> Unit { return 0; }");

        const fs::path main_path = base / "main.curlee";
        const auto res = resolve_with_source(src, main_path.string());
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error for unknown qualified name");
        }
    }

    {
        // Alias conflicts with a local top-level function name.
        const std::string src = R"(import foo.bar as helper;

fn helper() -> Unit { return 0; }
fn main() -> Unit { return 0; })";

        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / "curlee_resolver_tests_alias_conflict";
        const fs::path module_dir = base / "foo";
        fs::create_directories(module_dir);
        const fs::path module_path = module_dir / "bar.curlee";
        write_file(module_path, "fn helper() -> Unit { return 0; }");

        const fs::path main_path = base / "main.curlee";
        const auto res = resolve_with_source(src, main_path.string());
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error for alias conflict");
        }
    }

    {
        // Cross-root resolution: module lives under a subdirectory, but it imports a sibling
        // module that exists only in the entry directory.
        // Entry dir: <base>
        // Importing file dir: <base>/sub
        // Import 'shared' must resolve via entry dir fallback.

        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / "curlee_resolver_tests_cross_root";
        fs::create_directories(base / "sub");

        write_file(base / "shared.curlee", "fn shared() -> Unit { return 0; }");
        write_file(base / "sub" / "m.curlee",
                   R"(import shared;

fn m() -> Unit {
  return 0;
})");

        const std::ifstream in((base / "sub" / "m.curlee").string(), std::ios::binary);
        if (!in)
        {
            fail("failed to read module file");
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();

        const auto res =
            resolve_with_source(buffer.str(), (base / "sub" / "m.curlee").string(), base);
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for cross-root import resolution");
        }
    }

    {
        const std::string src = R"(import missing.mod;

fn main() -> Unit {
  return 0;
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_missing_import.curlee");
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error when import is missing");
        }

        const auto& ds = std::get<std::vector<diag::Diagnostic>>(res);
        bool found = false;
        bool has_expected_path_note = false;
        for (const auto& d : ds)
        {
            if (d.message.find("import not found") != std::string::npos)
            {
                found = true;
                for (const auto& note : d.notes)
                {
                    if (note.message.find("expected module at") != std::string::npos)
                    {
                        has_expected_path_note = true;
                        break;
                    }
                }
                break;
            }
        }
        if (!found)
        {
            fail("expected import-not-found diagnostic");
        }
        if (!has_expected_path_note)
        {
            fail("expected 'expected module at' note for missing import");
        }
    }

    {
        // Cover resolve(program, source) empty-path branch.
        const std::string src = R"(fn main() -> Unit { return 0; })";
        const auto program = parse_ok(src);
        const curlee::source::SourceFile file{.path = "", .contents = src};
        const auto res = resolver::resolve(program, file);
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for empty source.path");
        }
    }

    {
        // Cover resolve(program, source, entry_dir) empty-path branch.
        const std::string src = R"(fn main() -> Unit { return 0; })";
        const auto program = parse_ok(src);
        const curlee::source::SourceFile file{.path = "", .contents = src};
        const auto res = resolver::resolve(program, file, std::filesystem::path("/tmp"));
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for empty source.path with entry_dir");
        }
    }

    {
        // Cover StringExpr resolution (and corresponding visitor instantiation).
        const std::string src = R"(fn main() -> Unit {
  let s: String = "hi";
  s;
  return 0;
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_string_expr.curlee");
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for StringExpr");
        }
    }

    {
        // Cover predicate resolution visitor cases: PredBool, PredGroup, PredUnary.
        const std::string src = R"(fn main() -> Int [
  requires !(true);
] {
  return 0;
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_predicates.curlee");
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for predicate forms");
        }
    }

    {
        // Cover ensures predicate resolution when the name is not `result`.
        const std::string src = R"(fn main(x: Int) -> Int [
  ensures x == x;
] {
  return x;
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_ensures_name.curlee");
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for ensures name use");
        }
    }

    std::cout << "OK\n";
    return 0;
}
