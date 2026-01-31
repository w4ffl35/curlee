#include <cstdlib>
#include <curlee/cli/cli.h>
#include <filesystem>
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

static int run_cli_capture(std::vector<std::string> argv_storage, std::string& out,
                           std::string& err)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_out = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_err = std::cerr.rdbuf(captured_err.rdbuf());

    std::vector<char*> argv;
    argv.reserve(argv_storage.size());
    for (auto& s : argv_storage)
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

int main()
{
    const fs::path run_success =
        find_repo_relative(fs::path("tests") / "fixtures" / "run_success.curlee");
    const fs::path infinite =
        find_repo_relative(fs::path("tests") / "fixtures" / "run_infinite_loop.curlee");

    // Succeeds under sufficient fuel.
    {
        std::string out;
        std::string err;
        const int rc =
            run_cli_capture({"curlee", "run", "--fuel", "1000", run_success.string()}, out, err);
        if (rc != 0)
        {
            fail("expected run to succeed under sufficient fuel; stderr=" + err);
        }
        if (out.find("curlee run: result") == std::string::npos)
        {
            fail("expected stdout to contain run result");
        }
    }

    // Deterministically fails with out-of-fuel under small fuel on an infinite loop.
    {
        std::string out;
        std::string err;
        const int rc =
            run_cli_capture({"curlee", "run", "--fuel", "10", infinite.string()}, out, err);
        if (rc == 0)
        {
            fail("expected run to fail under small fuel");
        }
        if (err.find("out of fuel") == std::string::npos)
        {
            fail("expected stderr to mention 'out of fuel'");
        }
    }

    std::cout << "OK\n";
    return 0;
}
