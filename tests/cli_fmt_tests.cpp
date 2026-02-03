#include <curlee/cli/cli.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

[[noreturn]] static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        fail("failed to open file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static void write_file(const fs::path& path, const std::string& contents)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        fail("failed to write file: " + path.string());
    }
    out << contents;
}

static fs::path find_repo_relative(const fs::path& relative)
{
    fs::path dir = fs::current_path();
    for (int i = 0; i < 8; ++i)
    {
        const fs::path candidate = dir / relative;
        if (fs::exists(candidate))
        {
            return candidate;
        }
        if (!dir.has_parent_path())
        {
            break;
        }
        dir = dir.parent_path();
    }
    fail("unable to locate repo-relative path: " + relative.string());
}

int main()
{
    const fs::path temp_dir = fs::current_path() / "cli_fmt_tests";
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "format_test.cpp";
    const fs::path quoted_path = temp_dir / "format_\"quoted\".cpp";
    const fs::path missing_path = temp_dir / "format_missing.cpp";

    const std::string unformatted = "int   main(){return 0;}\n";
    const std::string formatted = "int main()\n{\n    return 0;\n}\n";

    write_file(file_path, unformatted);

    {
        std::vector<std::string> argv_storage = {"curlee", "fmt", file_path.string()};
        std::vector<char*> argv;
        argv.reserve(argv_storage.size());
        for (auto& s : argv_storage)
        {
            argv.push_back(s.data());
        }

        const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());
        if (rc != 0)
        {
            fail("expected fmt to succeed");
        }
    }

    const auto got = read_file(file_path);
    if (got != formatted)
    {
        fail("formatted output did not match expected style");
    }

    // Paths containing quotes must be escaped correctly.
    write_file(quoted_path, unformatted);
    {
        std::vector<std::string> argv_storage = {"curlee", "fmt", quoted_path.string()};
        std::vector<char*> argv;
        argv.reserve(argv_storage.size());
        for (auto& s : argv_storage)
        {
            argv.push_back(s.data());
        }

        const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());
        if (rc != 0)
        {
            fail("expected fmt to succeed for quoted path");
        }
    }

    const auto got_quoted = read_file(quoted_path);
    if (got_quoted != formatted)
    {
        fail("formatted output did not match expected style for quoted path");
    }

    // Non-check mode should surface clang-format failures.
    fs::remove(missing_path);
    {
        std::ostringstream captured_err;
        auto* old_err = std::cerr.rdbuf(captured_err.rdbuf());

        std::vector<std::string> argv_storage = {"curlee", "fmt", missing_path.string()};
        std::vector<char*> argv;
        argv.reserve(argv_storage.size());
        for (auto& s : argv_storage)
        {
            argv.push_back(s.data());
        }

        const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());
        std::cerr.rdbuf(old_err);

        if (rc == 0)
        {
            fail("expected fmt to fail for missing file");
        }
        if (captured_err.str().find("clang-format failed") == std::string::npos)
        {
            fail("expected stderr to mention clang-format failure");
        }
    }

    write_file(file_path, unformatted);

    {
        std::vector<std::string> argv_storage = {"curlee", "fmt", "--check", file_path.string()};
        std::vector<char*> argv;
        argv.reserve(argv_storage.size());
        for (auto& s : argv_storage)
        {
            argv.push_back(s.data());
        }

        const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());
        if (rc == 0)
        {
            fail("expected fmt --check to fail on unformatted file");
        }
    }

    // Shorthand: `curlee <file.curlee>` behaves like `curlee run <file.curlee>`.
    {
        const fs::path fixture =
            find_repo_relative(fs::path("tests") / "fixtures" / "run_success.curlee");

        std::ostringstream captured;
        auto* old_buf = std::cout.rdbuf(captured.rdbuf());

        std::vector<std::string> argv_storage = {"curlee", fixture.string()};
        std::vector<char*> argv;
        argv.reserve(argv_storage.size());
        for (auto& s : argv_storage)
        {
            argv.push_back(s.data());
        }

        const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());
        std::cout.rdbuf(old_buf);

        if (rc != 0)
        {
            fail("expected shorthand run to succeed");
        }

        const std::string out = captured.str();
        if (out.find("curlee run: result 3") == std::string::npos)
        {
            fail("expected shorthand run output to include result");
        }
    }

    // Shorthand again: exercise the empty_caps() fast path after the local static is initialized.
    {
        const fs::path fixture =
            find_repo_relative(fs::path("tests") / "fixtures" / "run_success.curlee");

        std::ostringstream captured;
        auto* old_buf = std::cout.rdbuf(captured.rdbuf());

        std::vector<std::string> argv_storage = {"curlee", fixture.string()};
        std::vector<char*> argv;
        argv.reserve(argv_storage.size());
        for (auto& s : argv_storage)
        {
            argv.push_back(s.data());
        }

        const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());
        std::cout.rdbuf(old_buf);

        if (rc != 0)
        {
            fail("expected shorthand run to succeed");
        }

        const std::string out = captured.str();
        if (out.find("curlee run: result 3") == std::string::npos)
        {
            fail("expected shorthand run output to include result");
        }
    }

    // Runnable imports: module-qualified function calls should work.
    {
        const fs::path fixture =
            find_repo_relative(fs::path("tests") / "fixtures" / "qualified_call" / "main.curlee");

        std::ostringstream captured_out;
        std::ostringstream captured_err;
        auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
        auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

        std::vector<std::string> argv_storage = {"curlee", "run", fixture.string()};
        std::vector<char*> argv;
        argv.reserve(argv_storage.size());
        for (auto& s : argv_storage)
        {
            argv.push_back(s.data());
        }

        const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());
        std::cout.rdbuf(old_cout);
        std::cerr.rdbuf(old_cerr);

        if (rc != 0)
        {
            fail("expected imported-module run to succeed; stderr: " + captured_err.str());
        }

        const std::string out = captured_out.str();
        if (out.find("curlee run: result 42") == std::string::npos)
        {
            fail("expected imported-module run output to include result 42");
        }
    }

    std::cout << "OK\n";
    return 0;
}
