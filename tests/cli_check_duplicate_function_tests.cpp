#include <cstdlib>
#include <curlee/cli/cli.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

[[noreturn]] static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

struct CapturedRun
{
    int rc = 0;
    std::string out;
    std::string err;
};

static CapturedRun run_cli_capture(const std::vector<std::string>& args_in)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());

    std::vector<std::string> args = args_in;
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& s : args)
    {
        argv.push_back(s.data());
    }

    CapturedRun result;
    result.rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);

    result.out = captured_out.str();
    result.err = captured_err.str();
    return result;
}

static void expect_contains(
    const std::string& haystack,
    const std::string& needle,
    const std::string& what
)
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

static void write_file(const fs::path& path, const std::string& contents)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec)
    {
        fail("failed to create directories for: " + path.string() + ": " + ec.message());
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        fail("failed to open file: " + path.string());
    }

    f << contents;
    if (!f)
    {
        fail("failed to write file: " + path.string());
    }
}

int main()
{
    // check: duplicate function name across imported modules.
    {
        const auto pid = static_cast<unsigned long>(::getpid());
        const fs::path dir =
            fs::temp_directory_path() / ("curlee_cli_dup_fn_" + std::to_string(pid));

        // Entry imports a and b; both define foo().
        write_file(
            dir / "entry.curlee",
            "import a;\nimport b;\n\nfn main() -> Int {\n  return 0;\n}\n"
        );

        write_file(dir / "a.curlee", "fn foo() -> Int {\n  return 1;\n}\n");
        write_file(dir / "b.curlee", "fn foo() -> Int {\n  return 2;\n}\n");

        const auto run = run_cli_capture({"curlee", "check", (dir / "entry.curlee").string()});
        if (run.rc != 1)
        {
            fail("expected error exit code for duplicate imported function");
        }

        expect_empty(run.out, "stdout");
        expect_contains(run.err, "error:", "stderr");
        expect_contains(run.err, "duplicate function across modules: 'foo'", "stderr");
        expect_contains(run.err, "conflict while importing", "stderr");
    }

    return 0;
}
