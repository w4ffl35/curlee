#include <array>
#include <cstdlib>
#include <curlee/cli/cli.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

[[noreturn]] static void fail(const std::string& msg)
{
    std::cerr << "CLI IMPORT ERROR TEST FAILURE: " << msg << "\n";
    std::exit(1);
}

struct CapturedRun
{
    int rc = 0;
    std::string out;
    std::string err;
};

static CapturedRun run_check_capture(const std::string& entry_arg)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());

    std::string command = "curlee";
    std::string subcommand = "check";
    std::string path = entry_arg;
    std::array<char*, 3> argv = {command.data(), subcommand.data(), path.data()};

    CapturedRun run;
    run.rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);

    run.out = captured_out.str();
    run.err = captured_err.str();
    return run;
}

static void expect_contains(
    const std::string& haystack,
    std::string_view needle,
    std::string_view what
)
{
    const bool found = haystack.find(needle) != std::string::npos;
    if (found)
    {
        return;
    }
    fail("expected " + std::string(what) + " to contain '" + std::string(needle) +
         "'\n---\n" + haystack + "\n---");
}

static void expect_empty(const std::string& s, const std::string& what)
{
    if (s.empty())
    {
        return;
    }
    fail("expected " + what + " to be empty\n---\n" + s + "\n---");
}

static void expect_check_exit(const CapturedRun& run, int expected_rc, const std::string& context)
{
    if (run.rc != expected_rc)
    {
        fail("unexpected exit code for " + context + ": expected " + std::to_string(expected_rc) +
             ", got " + std::to_string(run.rc) + "; stderr: " + run.err);
    }
}

static void expect_check_failure_contains(const std::string& entry_arg,
                                          const std::string& context,
                                          std::initializer_list<std::string> needles)
{
    const CapturedRun run = run_check_capture(entry_arg);
    expect_check_exit(run, 1, context);
    expect_empty(run.out, "stdout");
    for (const std::string& needle : needles)
    {
        expect_contains(run.err, needle, "stderr");
    }
}

static void expect_check_success_clean(const std::string& entry_arg, const std::string& context)
{
    const CapturedRun run = run_check_capture(entry_arg);
    expect_check_exit(run, 0, context);
    expect_empty(run.out, "stdout");
    expect_empty(run.err, "stderr");
}

static fs::path write_temp_curlee(const std::string& stem, const std::string& contents)
{
    const unsigned long pid = static_cast<unsigned long>(::getpid());
    const fs::path base_dir = fs::temp_directory_path();
    const fs::path dir = base_dir / "curlee_cli_tests";

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
    {
        fail("failed to create temp dir: " + dir.string() + ": " + ec.message());
    }

    const fs::path path = dir / (stem + "_" + std::to_string(pid) + ".curlee");

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        fail("failed to open temp file: " + path.string());
    }
    f << contents;
    if (!f)
    {
        fail("failed to write temp file: " + path.string());
    }

    return path;
}

static fs::path make_temp_dir(const std::string& stem)
{
    const auto pid = static_cast<unsigned long>(::getpid());
    const fs::path dir =
        fs::temp_directory_path() / "curlee_cli_tests" / (stem + "_" + std::to_string(pid));

    std::error_code ec;
    fs::remove_all(dir, ec);
    ec = {};
    fs::create_directories(dir, ec);
    if (ec)
    {
        fail("failed to create temp dir: " + dir.string() + ": " + ec.message());
    }
    return dir;
}

static fs::path make_rel_dir(const std::string& stem)
{
    const auto pid = static_cast<unsigned long>(::getpid());
    const fs::path dir = fs::path("curlee_cli_rel") / (stem + "_" + std::to_string(pid));

    std::error_code ec;
    fs::remove_all(dir, ec);
    ec = {};
    fs::create_directories(dir, ec);
    if (ec)
    {
        fail("failed to create temp dir: " + dir.string() + ": " + ec.message());
    }
    return dir;
}

static void write_file(const fs::path& path, std::string_view contents)
{
    const fs::path parent = path.parent_path();
    std::error_code mkdir_ec;
    fs::create_directories(parent, mkdir_ec);
    if (mkdir_ec)
    {
        fail("failed to create dirs for: " + parent.string() + ": " + mkdir_ec.message());
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        fail("failed to open temp file: " + path.string());
    }
    output << contents;
    if (!output.good())
    {
        fail("failed to write temp file: " + path.string());
    }
}

int main()
{
    // check: missing import produces a deterministic CLI diagnostic.
    {
        const std::string import_path = "missing.module";
        const fs::path entry = write_temp_curlee("check_import_not_found",
                                                 "import " + import_path +
                                                     ";\n\nfn main() -> Int {\n  return 0;\n}\n");

        const fs::path expected = entry.parent_path() / "missing" / "module.curlee";

        expect_check_failure_contains(entry.string(),
                                      "check import failure",
                                      {
                                          "error:",
                                          "import not found: '" + import_path + "'",
                                          "expected module at " + expected.string(),
                                      });
    }

    // check: entry lex errors are surfaced via CLI.
    {
        const fs::path entry = write_temp_curlee("check_entry_lex_error", "@\n");

        expect_check_failure_contains(entry.string(), "entry lex error", {"error:"});
    }

    // check: import resolves to an existing path that cannot be loaded as a file.
    {
        const fs::path dir = make_temp_dir("check_import_load_error");
        const fs::path entry = dir / "main.curlee";
        const fs::path bad_module = dir / "foo" / "bad.curlee";

        std::error_code ec;
        fs::create_directories(bad_module.parent_path(), ec);
        if (ec)
        {
            fail("failed to create bad module parent dir: " + ec.message());
        }

        write_file(bad_module, "fn helper() -> Int { return 7; }\n");
        fs::permissions(bad_module, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::remove, ec);
        if (ec)
        {
            fail("failed to remove module read permission: " + ec.message());
        }

        write_file(entry, "import foo.bad;\n\nfn main() -> Int {\n  return 0;\n}\n");

        const CapturedRun run = run_check_capture(entry.string());

        std::error_code restore_ec;
        fs::permissions(bad_module, fs::perms::owner_read, fs::perm_options::add, restore_ec);

        expect_check_exit(run, 1, "import load failure");
        expect_empty(run.out, "stdout");
        expect_contains(run.err, "failed to load imported module:", "stderr");
    }

    // check: importing yourself is an import cycle (entry file is already in the visiting set).
    {
        const fs::path dir = make_temp_dir("check_self_import_cycle");
        const fs::path entry = dir / "main.curlee";

        write_file(entry, "import main;\n\nfn main() -> Int {\n  return 0;\n}\n");

        expect_check_failure_contains(entry.string(),
                                      "self import cycle",
                                      {"import cycle detected"});
    }

    // check: same as above, but using relative paths (exercises different filesystem/string paths).
    {
        const fs::path dir = make_rel_dir("check_self_import_cycle_rel");
        const fs::path entry = dir / "main.curlee";

        write_file(entry, "import main;\n\nfn main() -> Int {\n  return 0;\n}\n");

        const std::string entry_arg = (fs::path(".") / entry).string();
        expect_check_failure_contains(entry_arg,
                                      "self import cycle (relative)",
                                      {"import cycle detected"});

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // check: module lex errors are surfaced.
    {
        const fs::path dir = make_temp_dir("check_imported_module_lex_error");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "foo" / "bar.curlee";

        write_file(entry, "import foo.bar;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "@\n");

        expect_check_failure_contains(entry.string(), "module lex error", {"error:"});
    }

    // check: module parse errors are surfaced (covers vector diagnostics rendering).
    {
        const fs::path dir = make_temp_dir("check_imported_module_parse_error");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "foo" / "bar.curlee";

        write_file(entry, "import foo.bar;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "fn\n");

        expect_check_failure_contains(entry.string(), "module parse error", {"error:"});
    }

    // check: imported modules must not define main.
    {
        const fs::path dir = make_temp_dir("check_imported_module_main");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";

        write_file(entry, "import dep;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "fn main() -> Int { return 123; }\n");

        expect_check_failure_contains(entry.string(),
                                      "imported main",
                                      {"imported modules must not define 'main'"});
    }

    // check: module imports that fail to load produce an error anchored at the importing module.
    {
        const fs::path dir = make_temp_dir("check_module_import_not_found");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";

        write_file(entry, "import dep;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "import missing;\n\nfn foo() -> Int { return 1; }\n");

        expect_check_failure_contains(entry.string(),
                                      "module import load failure",
                                      {"import not found"});
    }

    // check: nested module failures propagate without emitting a second diagnostic.
    {
        const fs::path dir = make_temp_dir("check_nested_import_failure");
        const fs::path entry = dir / "main.curlee";
        const fs::path a = dir / "a.curlee";
        const fs::path b = dir / "b.curlee";

        write_file(entry, "import a;\n\nfn main() -> Int { return 0; }\n");
        write_file(a, "import b;\n\nfn foo() -> Int { return 1; }\n");
        write_file(b, "@\n");

        expect_check_failure_contains(entry.string(), "nested import failure", {"error:"});
    }

    // check: resolver errors in imported modules are rendered.
    {
        const fs::path dir = make_temp_dir("check_imported_module_resolver_error");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";

        write_file(entry, "import dep;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "fn foo() -> Int { return x; }\n");

        expect_check_failure_contains(entry.string(),
                                      "resolver error in module",
                                      {"error:"});
    }

    // check: type errors in imported modules are rendered.
    {
        const fs::path dir = make_temp_dir("check_imported_module_type_error");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";

        write_file(entry, "import dep;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "fn foo() -> Int { return true; }\n");

        expect_check_failure_contains(entry.string(), "type error in module", {"error:"});
    }

    // check: verification errors in imported modules are rendered.
    {
        const fs::path dir = make_temp_dir("check_imported_module_verification_error");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";

        write_file(entry, "import dep;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "fn foo() -> Int [\n  ensures result > 0;\n] {\n  return 0;\n}\n");

        expect_check_failure_contains(entry.string(),
                                      "verification error in module",
                                      {"ensures clause not satisfied"});
    }

    // check: duplicate function names across modules are rejected during merge.
    {
        const fs::path dir = make_temp_dir("check_duplicate_function_across_modules");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";

        write_file(
            entry,
            "import dep;\n\nfn foo() -> Int { return 1; }\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "fn foo() -> Int { return 2; }\n");

        expect_check_failure_contains(entry.string(),
                                      "duplicate function across modules",
                                      {
                                          "duplicate function across modules: 'foo'",
                                          "conflict while importing",
                                      });
    }

    // check: duplicate function across modules (relative paths variant).
    {
        const fs::path dir = make_rel_dir("check_duplicate_function_across_modules_rel");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";

        write_file(
            entry,
            "import dep;\n\nfn foo() -> Int { return 1; }\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "fn foo() -> Int { return 2; }\n");

        const std::string entry_arg = (fs::path(".") / entry).string();
        expect_check_failure_contains(entry_arg,
                                      "duplicate function across modules (relative)",
                                      {
                                          "duplicate function across modules: 'foo'",
                                          "conflict while importing",
                                      });

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // check: CURLEE_DEBUG_IMPORTS prints import search paths.
    {
        const fs::path dir = make_temp_dir("check_debug_imports");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "sub" / "m.curlee";
        const fs::path shared = dir / "shared.curlee";

        write_file(entry, "import sub.m;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "import shared;\n\nfn foo() -> Int { return 1; }\n");
        write_file(shared, "fn helper() -> Int { return 2; }\n");

        ::setenv("CURLEE_DEBUG_IMPORTS", "1", 1);
        const CapturedRun run = run_check_capture(entry.string());
        ::unsetenv("CURLEE_DEBUG_IMPORTS");

        expect_check_exit(run, 0, "debug imports enabled");
        expect_contains(run.err, "[import] trying ", "stderr");
        expect_contains(run.err, "[import] ok: ", "stderr");
    }

    // check: CURLEE_DEBUG_IMPORTS also logs per-root load failures.
    {
        const fs::path dir = make_temp_dir("check_debug_imports_failed");
        const fs::path entry = dir / "main.curlee";

        write_file(entry, "import missing.module;\n\nfn main() -> Int { return 0; }\n");

        ::setenv("CURLEE_DEBUG_IMPORTS", "1", 1);
        const CapturedRun run = run_check_capture(entry.string());
        ::unsetenv("CURLEE_DEBUG_IMPORTS");

        expect_check_exit(run, 1, "debug imports missing module");
        expect_empty(run.out, "stdout");
        expect_contains(run.err, "[import] trying ", "stderr");
        expect_contains(run.err, "[import] failed:", "stderr");
        expect_contains(run.err, "import not found:", "stderr");
    }

    // check: duplicate imports reuse already-loaded module files.
    {
        const fs::path dir = make_temp_dir("check_duplicate_imports");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "foo" / "bar.curlee";

        write_file(entry, "import foo.bar;\nimport foo.bar;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "fn foo() -> Int { return 7; }\n");

        expect_check_success_clean(entry.string(), "duplicate imports");
    }

    // check: shared modules are only checked once (visited short-circuit inside check_module).
    {
        const fs::path dir = make_temp_dir("check_diamond_imports");
        const fs::path entry = dir / "main.curlee";
        const fs::path a = dir / "a.curlee";
        const fs::path b = dir / "b.curlee";
        const fs::path shared = dir / "shared.curlee";

        write_file(entry, "import a;\nimport b;\n\nfn main() -> Int { return 0; }\n");
        write_file(a, "import shared;\n\nfn fa() -> Int { return 1; }\n");
        write_file(b, "import shared;\n\nfn fb() -> Int { return 2; }\n");
        write_file(shared, "fn helper() -> Int { return 3; }\n");

        expect_check_success_clean(entry.string(), "diamond imports");
    }

    return 0;
}
