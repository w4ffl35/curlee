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

namespace fs = std::filesystem;

[[noreturn]] static void die(std::string_view message)
{
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
}

int main()
{
    const auto pid = static_cast<unsigned long>(::getpid());
    const fs::path repo_root = fs::path(__FILE__).parent_path().parent_path();
    const fs::path stdlib_v1_root = repo_root / "stdlib" / "v1";

    const auto write_text = [](const fs::path& path, std::string_view text)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        if (ec)
        {
            die("failed to create parent directories for " + path.string());
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            die("failed to open file " + path.string());
        }

        output << text;
        if (!output.good())
        {
            die("failed to write file " + path.string());
        }
    };

    struct RunResult
    {
        int rc = 0;
        std::string out;
        std::string err;
    };

    const auto run_check = [](const fs::path& entry)
    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;

        std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());

        std::string command = "curlee";
        std::string subcommand = "check";
        std::string file_arg = entry.string();
        std::array<char*, 3> argv = {command.data(), subcommand.data(), file_arg.data()};

        RunResult run;
        run.rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);

        run.out = captured_out.str();
        run.err = captured_err.str();
        return run;
    };

    const auto expect_contains = [](const std::string& text, std::string_view needle)
    {
        if (text.find(needle) != std::string::npos)
        {
            return;
        }
        die("expected diagnostic to contain '" + std::string(needle) + "'\n---\n" + text +
            "\n---");
    };

    const auto expect_empty = [](const std::string& text, std::string_view label)
    {
        if (text.empty())
        {
            return;
        }
        die("expected empty " + std::string(label));
    };

    // check: parser rejects "import a.;" (missing identifier after '.')
    {
        const fs::path entry =
            fs::temp_directory_path() / "curlee_cli_tests" /
            ("check_import_path_" + std::to_string(pid) + ".curlee");

        write_text(entry, "import a.;\n\nfn main() -> Int {\n  return 0;\n}\n");

        const RunResult run = run_check(entry);
        if (run.rc != 1)
        {
            die("expected error exit code for malformed import path");
        }

        expect_empty(run.out, "stdout");
        expect_contains(run.err, "error:");
        expect_contains(run.err, "expected identifier after '.' in import path");
    }

    // check: imports can resolve from CURLEE_STDLIB_ROOT (colon-separated list).
    {
        const fs::path root =
            fs::temp_directory_path() / "curlee_cli_tests" /
            ("check_import_stdlib_env_" + std::to_string(pid));
        const fs::path entry = root / "main.curlee";
        const fs::path stdlib_root = root / "stdlib";
        const fs::path stdlib_mod = stdlib_root / "std" / "math.curlee";

        std::error_code ec;
        fs::remove_all(root, ec);

        write_text(stdlib_mod, "fn add1(x: Int) -> Int { return x + 1; }\n");
        write_text(entry,
                   "import std.math as m;\n\n"
                   "fn main() -> Int {\n"
                   "  return m.add1(41);\n"
                   "}\n");

        const std::string env_value = (root / "missing").string() + "::" + stdlib_root.string();
        ::setenv("CURLEE_STDLIB_ROOT", env_value.c_str(), 1);
        const RunResult run = run_check(entry);
        ::unsetenv("CURLEE_STDLIB_ROOT");

        if (run.rc != 0)
        {
            die("expected check to succeed with CURLEE_STDLIB_ROOT");
        }

        expect_empty(run.out, "stdout");
        expect_empty(run.err, "stderr");
    }

    // check: importing stdlib modules from repository stdlib/v1 works.
    {
        if (!fs::exists(stdlib_v1_root / "std" / "math.curlee"))
        {
            die("expected stdlib/v1/std/math.curlee to exist");
        }

        const fs::path root =
            fs::temp_directory_path() / "curlee_cli_tests" /
            ("check_import_repo_stdlib_" + std::to_string(pid));
        const fs::path entry = root / "main.curlee";

        std::error_code ec;
        fs::remove_all(root, ec);

        write_text(entry,
                   "import std.math as m;\n\n"
                   "fn main() -> Int {\n"
                   "  return m.add1(41);\n"
                   "}\n");

        ::setenv("CURLEE_STDLIB_ROOT", stdlib_v1_root.string().c_str(), 1);
        const RunResult run = run_check(entry);
        ::unsetenv("CURLEE_STDLIB_ROOT");

        if (run.rc != 0)
        {
            die("expected check to succeed with repository stdlib root");
        }

        expect_empty(run.out, "stdout");
        expect_empty(run.err, "stderr");
    }

    // check: stdlib IO signatures require explicit cap io.stdout values.
    {
        const fs::path root =
            fs::temp_directory_path() / "curlee_cli_tests" /
            ("check_stdlib_io_cap_" + std::to_string(pid));
        const fs::path entry = root / "main.curlee";
        const fs::path entry_ok = root / "main_ok.curlee";

        std::error_code ec;
        fs::remove_all(root, ec);

        write_text(entry,
                   "import std.io as io;\n\n"
                   "fn main() -> Int {\n"
                   "  return io.identity_int(7, 7);\n"
                   "}\n");

        write_text(entry_ok,
                   "import std.io as io;\n\n"
                   "fn main(out: cap io.stdout) -> Int {\n"
                   "  return io.identity_int(7, out);\n"
                   "}\n");

        ::setenv("CURLEE_STDLIB_ROOT", stdlib_v1_root.string().c_str(), 1);
        const RunResult missing_cap = run_check(entry);
        const RunResult ok = run_check(entry_ok);
        ::unsetenv("CURLEE_STDLIB_ROOT");

        if (missing_cap.rc != 1)
        {
            die("expected check to fail for invalid stdlib io capability argument type");
        }
        expect_contains(missing_cap.err, "argument type mismatch for call to 'identity_int'");

        if (ok.rc != 0)
        {
            die("expected check to succeed when stdlib io capability value is provided");
        }
        expect_empty(ok.out, "stdout");
        expect_empty(ok.err, "stderr");
    }

    // check: unsafe rules are enforced for stdlib module implementations.
    {
        const fs::path root =
            fs::temp_directory_path() / "curlee_cli_tests" /
            ("check_stdlib_unsafe_gate_" + std::to_string(pid));
        const fs::path stdlib_root = root / "stdlib";
        const fs::path bad_module = stdlib_root / "std" / "badpython.curlee";
        const fs::path entry = root / "main.curlee";

        std::error_code ec;
        fs::remove_all(root, ec);

        write_text(bad_module,
                   "fn call0(p: cap python.ffi) -> Unit {\n"
                   "  python_ffi.call();\n"
                   "  return 0;\n"
                   "}\n");

        write_text(entry,
                   "import std.badpython as bad;\n\n"
                   "fn main(p: cap python.ffi) -> Unit {\n"
                   "  bad.call0(p);\n"
                   "  return 0;\n"
                   "}\n");

        ::setenv("CURLEE_STDLIB_ROOT", stdlib_root.string().c_str(), 1);
        const RunResult run = run_check(entry);
        ::unsetenv("CURLEE_STDLIB_ROOT");

        if (run.rc != 1)
        {
            die("expected check to fail when stdlib python_ffi usage is not inside unsafe");
        }
        expect_contains(run.err, "python_ffi.call requires an unsafe context");
    }

    return 0;
}
