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

struct CapturedRun
{
    int rc = 0;
    std::string out;
    std::string err;
};

class CliTestFixture
{
  public:
    [[noreturn]] static void fail(std::string_view message)
    {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }

    static CapturedRun run(std::initializer_list<std::string> args_list)
    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;

        std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());

        std::vector<std::string> args(args_list);
        std::vector<char*> argv;
        argv.reserve(args.size());
        for (std::string& arg : args)
        {
            argv.push_back(arg.data());
        }

        CapturedRun result;
        result.rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);

        result.out = captured_out.str();
        result.err = captured_err.str();
        return result;
    }

    static void require_empty(const std::string& value, std::string_view label)
    {
        if (value.empty())
        {
            return;
        }
        fail("expected " + std::string(label) + " to be empty");
    }

    static void require_contains(const std::string& haystack, std::string_view needle,
                                 std::string_view label)
    {
        if (haystack.find(needle) != std::string::npos)
        {
            return;
        }
        fail("expected " + std::string(label) + " to contain '" + std::string(needle) + "'");
    }

    static void write_file(const fs::path& path, std::string_view contents)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        if (ec)
        {
            fail("failed to create directories for " + path.string());
        }

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            fail("failed to open file " + path.string());
        }
        file << contents;
        if (!file)
        {
            fail("failed to write file " + path.string());
        }
    }
};

int main()
{
    // check: import cycle detected (a <-> b)
    {
        const auto pid = static_cast<unsigned long>(::getpid());
        const fs::path dir =
            fs::temp_directory_path() / ("curlee_cli_cycle_" + std::to_string(pid));

        // entry imports a
        CliTestFixture::write_file(dir / "entry.curlee",
                       "import a;\n\nfn main() -> Int {\n  return 0;\n}\n");

        // a imports b
        CliTestFixture::write_file(dir / "a.curlee",
                       "import b;\n\nfn f() -> Int {\n  return 0;\n}\n");

        // b imports a (cycle)
        CliTestFixture::write_file(dir / "b.curlee",
                                   "import a;\n\nfn g() -> Int {\n  return 0;\n}\n");

        const auto run = CliTestFixture::run({"curlee", "check", (dir / "entry.curlee").string()});
        if (run.rc != 1)
        {
            CliTestFixture::fail("expected error exit code for import cycle");
        }

        CliTestFixture::require_empty(run.out, "stdout");
        CliTestFixture::require_contains(run.err, "error:", "stderr");
        CliTestFixture::require_contains(run.err, "import cycle detected", "stderr");
        CliTestFixture::require_contains(run.err, "cycle involves", "stderr");
    }

    return 0;
}
