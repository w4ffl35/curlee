// SPDX-License-Identifier: MIT
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
        // Import-not-found diagnostic includes an expected module path note.
        const std::string src = R"(import no.such.module;

fn main() -> Unit { return 0; })";

        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / "curlee_resolver_tests_import_missing";
        fs::create_directories(base);

        const fs::path main_path = base / "main.curlee";
        const auto res = resolve_with_source(src, main_path.string());
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error for missing import");
        }

        const auto& ds = std::get<std::vector<diag::Diagnostic>>(res);
        bool found_expected_module_note = false;
        for (const auto& d : ds)
        {
            if (d.message.find("import not found") == std::string::npos)
            {
                continue;
            }
            for (const auto& n : d.notes)
            {
                if (n.message.find("expected module at ") != std::string::npos)
                {
                    found_expected_module_note = true;
                    break;
                }
            }
        }
        if (!found_expected_module_note)
        {
            fail("expected missing import diagnostic note with expected module path");
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
        const std::string src = R"(fn main(input: cap io.stdin) -> String {
  return __read_line(input);
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_read_line_builtin.curlee");
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for __read_line builtin call");
        }
    }

    {
        const std::string src = R"(fn main(screen: cap io.tty) -> Unit {
  __tty_clear(screen);
  __tty_write_at(0, 0, "x", screen);
  __tty_flush(screen);
  return 0;
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_tty_builtins.curlee");
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for __tty_* builtin calls");
        }
    }

    {
        const std::string src = R"(fn main(rng: cap rng.seeded) -> Int {
  return __rng_next_int(10, rng);
})";

        const auto res =
            resolve_with_source(src, "tests/fixtures/resolve_rng_builtin.curlee");
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for __rng_next_int builtin call");
        }
    }

    {
        const std::string src = R"(fn main(r: cap fs.read, w: cap fs.write) -> Unit {
  let text: String = __fs_read_text("in.txt", r);
  __fs_write_text("out.txt", text, w);
  return;
})";

        const auto res =
            resolve_with_source(src, "tests/fixtures/resolve_fs_builtins.curlee");
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for __fs_* builtin calls");
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
        // Imported module exists but cannot be read.
        const std::string src = R"(import foo.bad;

fn main() -> Unit { return 0; })";

        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / "curlee_resolver_tests_import_load_fail";
        const fs::path module_dir = base / "foo";
        std::error_code ec;
        fs::remove_all(base, ec);
        fs::create_directories(module_dir, ec);
        if (ec)
        {
            fail("failed to create temp module dir: " + ec.message());
        }

        const fs::path module_path = module_dir / "bad.curlee";
        write_file(module_path, "fn helper() -> Unit { return 0; }");
        fs::permissions(module_path, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::remove, ec);
        if (ec)
        {
            fail("failed to remove module read permission: " + ec.message());
        }

        const fs::path main_path = base / "main.curlee";
        const auto res = resolve_with_source(src, main_path.string());

        std::error_code restore_ec;
        fs::permissions(module_path, fs::perms::owner_read, fs::perm_options::add, restore_ec);

        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error when imported module cannot be loaded");
        }

        const auto& ds = std::get<std::vector<diag::Diagnostic>>(res);
        bool saw_load_error = false;
        for (const auto& d : ds)
        {
            if (d.message.find("failed to load imported module") != std::string::npos)
            {
                saw_load_error = true;
                break;
            }
        }
        if (!saw_load_error)
        {
            fail("expected load-failure diagnostic for imported module");
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
        // python_ffi.call is not part of the v1 surface.
        const std::string src = R"(fn main() -> Unit {
  python_ffi.call();
  return 0;
})";

        const auto res =
            resolve_with_source(src, "tests/fixtures/resolve_ffi_unsafe_required.curlee");
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error for python_ffi.call in v1");
        }
    }

    {
        // python_ffi.call inside unsafe is still unsupported in v1.
        const std::string src = R"(fn main() -> Unit {
  unsafe { python_ffi.call(); }
  return 0;
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_ffi_unsafe_ok.curlee");
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error for python_ffi.call inside unsafe");
        }
    }

    {
        // Match-arm body may be absent in manually-constructed ASTs; resolver should still
        // traverse safely.
        const std::string src = R"(enum E { A; }
fn main(v: E) -> Unit {
  match (v) {
    E::A => { return; }
  }
  return;
})";

        auto program = parse_ok(src);
        if (program.functions.empty())
        {
            fail("expected function in parsed program");
        }
        auto* match_stmt =
            std::get_if<curlee::parser::MatchStmt>(&program.functions[0].body.stmts[0].node);
        if (match_stmt == nullptr || match_stmt->arms.empty())
        {
            fail("expected first statement to be match with at least one arm");
        }
        match_stmt->arms[0].body.reset();

        const curlee::source::SourceFile file{
            .path = "tests/fixtures/resolve_match_null_body.curlee", .contents = src};
        const auto res = curlee::resolver::resolve(program, file);
        if (!std::holds_alternative<curlee::resolver::Resolution>(res))
        {
            fail("expected resolver success for match arm with null body");
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
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error for python_ffi.other()");
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
        // Ambiguous import across roots must be rejected deterministically.
        // Entry dir: <base>
        // Importing file dir: <base>/sub
        // Both <base>/shared.curlee and <base>/sub/shared.curlee exist.

        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / "curlee_resolver_tests_ambiguous_import";
        fs::create_directories(base / "sub");

        write_file(base / "shared.curlee", "fn shared() -> Unit { return 0; }");
        write_file(base / "sub" / "shared.curlee", "fn shared() -> Unit { return 0; }");
                const std::string src = R"(import shared;

fn main() -> Unit {
    return 0;
})";

                write_file(base / "sub" / "m.curlee", src);

                const auto res = resolve_with_source(src, (base / "sub" / "m.curlee").string(), base);
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error for ambiguous import");
        }

        const auto& ds = std::get<std::vector<diag::Diagnostic>>(res);
        bool found = false;
        std::size_t found_module_notes = 0;
        for (const auto& d : ds)
        {
            if (d.message.find("ambiguous import") != std::string::npos)
            {
                found = true;
                for (const auto& note : d.notes)
                {
                    if (note.message.find("found module at") != std::string::npos)
                    {
                        ++found_module_notes;
                    }
                }
                break;
            }
        }

        if (!found)
        {
            fail("expected ambiguous-import diagnostic");
        }
        if (found_module_notes < 2)
        {
            fail("expected at least two 'found module at' notes for ambiguous import");
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
        // Match: payload binder introduces an arm-local name and arm body is traversed.
        const std::string src = R"(enum E {
    A(Int);
    B;
}
fn main(v: E) -> Unit {
    match (v) {
        E::A(x) => { x; }
        E::B => { return; }
    }
    return;
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_match_payload.curlee");
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for match payload binding");
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

    {
        // A contract-block ghost snapshot whose name collides with a parameter
        // name must be rejected loudly (duplicate definition), never silently
        // shadowed.
        const std::string src = R"(fn t(snap: Int) -> Int [
  ghost let snap = snap;
  ensures result == snap;
] {
  return snap;
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_snap_param_collide.curlee");
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected resolver error on snapshot/param name collision");
        }
        const auto& ds = std::get<std::vector<diag::Diagnostic>>(res);
        if (ds.empty())
        {
            fail("expected at least one diagnostic for snapshot/param collision");
        }
    }

    {
        // Phys<T> nodes must not crash the resolver: `phys<U32>(lit)`, `.read()`, `.write(v)`
        // contain no unresolved variable references.
        const std::string src = R"(fn main(pm: cap phys.mem) -> Unit {
  unsafe {
    let fb: Phys<U32> = phys<U32>(0xFD00_0000);
    fb.write(0xFF8800);
    let v: U32 = fb.read();
  }
  return;
})";

        const auto res = resolve_with_source(src, "tests/fixtures/resolve_phys_mem.curlee");
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for Phys nodes");
        }
    }

    // Extern functions (issue #256): an extern declaration must register in the
    // function table (callable) and must not collide with a body'd function.
    {
        // Duplicate: extern `putc` collides with a body'd `putc`.
        const std::string src = R"(
extern fn putc(c: Int) -> Unit;
fn putc(c: Int) -> Unit { return; }
fn main() -> Unit { return; }
)";
        const auto res = resolve_with_source(src, "tests/fixtures/resolve_extern_dup.curlee");
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected duplicate function error for extern-vs-body collision");
        }
        bool found = false;
        for (const auto& d : std::get<std::vector<diag::Diagnostic>>(res))
        {
            if (d.message.find("duplicate function") != std::string::npos)
            {
                found = true;
            }
        }
        if (!found)
        {
            fail("expected 'duplicate function' diagnostic for extern-vs-body collision");
        }
    }

    {
        // Two extern declarations of the same name are also duplicates.
        const std::string src = R"(
extern fn putc(c: Int) -> Unit;
extern fn putc(c: Int) -> Unit;
fn main() -> Unit { return; }
)";
        const auto res = resolve_with_source(src, "tests/fixtures/resolve_extern_dup2.curlee");
        if (!std::holds_alternative<std::vector<diag::Diagnostic>>(res))
        {
            fail("expected duplicate function error for two extern declarations");
        }
    }

    {
        // Extern fn + a main that calls it resolves cleanly.
        const std::string src = R"(
extern fn putc(c: Int) -> Unit;
fn main() -> Unit {
  putc(65);
  return;
}
)";
        const auto res = resolve_with_source(src, "tests/fixtures/resolve_extern_ok.curlee");
        if (!std::holds_alternative<resolver::Resolution>(res))
        {
            fail("expected resolver success for extern fn call");
        }
    }

    std::cout << "OK\n";
    return 0;
}
