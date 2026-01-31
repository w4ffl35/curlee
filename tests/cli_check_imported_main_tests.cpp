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

static void write_file(const fs::path& path, const std::string& contents)
{
    fs::create_directories(path.parent_path());

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
    // check: imported modules must not define 'main'
    {
        const auto pid = static_cast<unsigned long>(::getpid());
        const fs::path dir =
            fs::temp_directory_path() / ("curlee_cli_imported_main_" + std::to_string(pid));

        write_file(dir / "entry.curlee", "import mod;\n\nfn main() -> Int {\n  return 0;\n}\n");

        write_file(dir / "mod.curlee", "fn main() -> Int {\n  return 0;\n}\n");

        std::string out;
        std::string err;
        const int rc =
            run_cli_capture({"curlee", "check", (dir / "entry.curlee").string()}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for imported module defining main");
        }

        expect_empty(out, "stdout");
        expect_contains(err, "error:", "stderr");
        expect_contains(err, "imported modules must not define 'main'", "stderr");
    }

    return 0;
}
