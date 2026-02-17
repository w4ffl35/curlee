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
        die("expected diagnostic to contain requested text");
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

    return 0;
}
