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

[[noreturn]] static void fail(std::string_view msg)
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

static CapturedRun run_check_capture(const fs::path& entry)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());

    std::string command = "curlee";
    std::string subcommand = "check";
    std::string arg = entry.string();
    std::array<char*, 3> argv = {command.data(), subcommand.data(), arg.data()};

    CapturedRun run;
    run.rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);

    run.out = captured_out.str();
    run.err = captured_err.str();
    return run;
}

static void require_contains(const std::string& haystack, std::string_view needle)
{
    if (haystack.find(needle) != std::string::npos)
    {
        return;
    }
    fail("missing expected stderr fragment");
}

static void require_empty(const std::string& text, std::string_view channel)
{
    if (text.empty())
    {
        return;
    }
    fail("expected empty " + std::string(channel));
}

static void write_file(const fs::path& path, std::string_view contents)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec)
    {
        fail("failed to create parent directories");
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
    // check: imported modules must not define 'main'
    {
        const auto pid = static_cast<unsigned long>(::getpid());
        const fs::path dir =
            fs::temp_directory_path() / ("curlee_cli_imported_main_" + std::to_string(pid));

        write_file(dir / "entry.curlee", "import mod;\n\nfn main() -> Int {\n  return 0;\n}\n");

        write_file(dir / "mod.curlee", "fn main() -> Int {\n  return 0;\n}\n");

        const CapturedRun run = run_check_capture(dir / "entry.curlee");
        if (run.rc != 1)
        {
            fail("expected error exit code for imported module defining main");
        }

        require_empty(run.out, "stdout");
        require_contains(run.err, "error:");
        require_contains(run.err, "imported modules must not define 'main'");
    }

    return 0;
}
