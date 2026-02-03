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
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static int run_cli_capture(const std::vector<std::string>& argv_storage, std::string& out,
                           std::string& err)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_out = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_err = std::cerr.rdbuf(captured_err.rdbuf());

    std::vector<std::string> args = argv_storage;
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& s : args)
    {
        argv.push_back(s.data());
    }

    const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);

    out = captured_out.str();
    err = captured_err.str();
    return rc;
}

static void expect_contains(const std::string& haystack, const std::string& needle,
                            const std::string& what)
{
    if (haystack.find(needle) == std::string::npos)
    {
        fail("expected " + what + " to contain '" + needle + "'\n---\n" + haystack + "\n---");
    }
}

static void expect_empty(const std::string& s, const std::string& what)
{
    if (!s.empty())
    {
        fail("expected " + what + " to be empty\n---\n" + s + "\n---");
    }
}

static fs::path write_temp_curlee(const std::string& stem, const std::string& contents)
{
    const auto pid = static_cast<unsigned long>(::getpid());
    const fs::path dir = fs::temp_directory_path() / "curlee_cli_tests";

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
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec)
    {
        fail("failed to create dirs for: " + path.string() + ": " + ec.message());
    }

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

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for check import failure");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "error:", "stderr");
        expect_contains(err, "import not found: '" + import_path + "'", "stderr");
        expect_contains(err, "expected module at " + expected.string(), "stderr");
    }

    // check: entry lex errors are surfaced via CLI.
    {
        const fs::path entry = write_temp_curlee("check_entry_lex_error", "@\n");

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for entry lex error");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "error:", "stderr");
    }

    // check: importing yourself is an import cycle (entry file is already in the visiting set).
    {
        const fs::path dir = make_temp_dir("check_self_import_cycle");
        const fs::path entry = dir / "main.curlee";

        write_file(entry, "import main;\n\nfn main() -> Int {\n  return 0;\n}\n");

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for self import cycle");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "import cycle detected", "stderr");
    }

    // check: same as above, but using relative paths (exercises different filesystem/string paths).
    {
        const fs::path dir = make_rel_dir("check_self_import_cycle_rel");
        const fs::path entry = dir / "main.curlee";

        write_file(entry, "import main;\n\nfn main() -> Int {\n  return 0;\n}\n");

        std::string out;
        std::string err;
        const std::string entry_arg = (fs::path(".") / entry).string();
        const int rc = run_cli_capture({"curlee", "check", entry_arg}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for self import cycle (relative)");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "import cycle detected", "stderr");

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

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for module lex error");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "error:", "stderr");
    }

    // check: module parse errors are surfaced (covers vector diagnostics rendering).
    {
        const fs::path dir = make_temp_dir("check_imported_module_parse_error");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "foo" / "bar.curlee";

        write_file(entry, "import foo.bar;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "fn\n");

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for module parse error");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "error:", "stderr");
    }

    // check: imported modules must not define main.
    {
        const fs::path dir = make_temp_dir("check_imported_module_main");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";

        write_file(entry, "import dep;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "fn main() -> Int { return 123; }\n");

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for imported main");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "imported modules must not define 'main'", "stderr");
    }

    // check: module imports that fail to load produce an error anchored at the importing module.
    {
        const fs::path dir = make_temp_dir("check_module_import_not_found");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";

        write_file(entry, "import dep;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "import missing;\n\nfn foo() -> Int { return 1; }\n");

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for module import load failure");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "import not found", "stderr");
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

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for nested import failure");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "error:", "stderr");
    }

    // check: resolver errors in imported modules are rendered.
    {
        const fs::path dir = make_temp_dir("check_imported_module_resolver_error");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";

        write_file(entry, "import dep;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "fn foo() -> Int { return x; }\n");

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for resolver error in module");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "error:", "stderr");
    }

    // check: type errors in imported modules are rendered.
    {
        const fs::path dir = make_temp_dir("check_imported_module_type_error");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";

        write_file(entry, "import dep;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "fn foo() -> Int { return true; }\n");

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for type error in module");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "error:", "stderr");
    }

    // check: verification errors in imported modules are rendered.
    {
        const fs::path dir = make_temp_dir("check_imported_module_verification_error");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";

        write_file(entry, "import dep;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "fn foo() -> Int [\n  ensures result > 0;\n] {\n  return 0;\n}\n");

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for verification error in module");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "ensures clause not satisfied", "stderr");
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

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for duplicate function across modules");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "duplicate function across modules: 'foo'", "stderr");
        expect_contains(err, "conflict while importing", "stderr");
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

        std::string out;
        std::string err;
        const std::string entry_arg = (fs::path(".") / entry).string();
        const int rc = run_cli_capture({"curlee", "check", entry_arg}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for duplicate function across modules (relative)");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "duplicate function across modules: 'foo'", "stderr");
        expect_contains(err, "conflict while importing", "stderr");

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
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        ::unsetenv("CURLEE_DEBUG_IMPORTS");

        if (rc != 0)
        {
            fail("expected check to succeed with debug imports enabled; stderr: " + err);
        }

        expect_contains(err, "[import] trying ", "stderr");
        expect_contains(err, "[import] ok: ", "stderr");
    }

    // check: CURLEE_DEBUG_IMPORTS also logs per-root load failures.
    {
        const fs::path dir = make_temp_dir("check_debug_imports_failed");
        const fs::path entry = dir / "main.curlee";

        write_file(entry, "import missing.module;\n\nfn main() -> Int { return 0; }\n");

        ::setenv("CURLEE_DEBUG_IMPORTS", "1", 1);
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        ::unsetenv("CURLEE_DEBUG_IMPORTS");

        if (rc != 1)
        {
            fail("expected check to fail for missing module with debug imports enabled");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "[import] trying ", "stderr");
        expect_contains(err, "[import] failed:", "stderr");
        expect_contains(err, "import not found:", "stderr");
    }

    // check: duplicate imports reuse already-loaded module files.
    {
        const fs::path dir = make_temp_dir("check_duplicate_imports");
        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "foo" / "bar.curlee";

        write_file(entry, "import foo.bar;\nimport foo.bar;\n\nfn main() -> Int { return 0; }\n");
        write_file(dep, "fn foo() -> Int { return 7; }\n");

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        if (rc != 0)
        {
            fail("expected check to succeed with duplicate imports; stderr: " + err);
        }
        expect_empty(err, "stderr");
        expect_empty(out, "stdout");
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

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "check", entry.string()}, out, err);
        if (rc != 0)
        {
            fail("expected check to succeed for diamond imports; stderr: " + err);
        }

        expect_empty(out, "stdout");
        expect_empty(err, "stderr");
    }

    return 0;
}
