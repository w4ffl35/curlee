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

static std::string slurp(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static int run_cli(const std::vector<std::string>& argv_storage, std::string& out, std::string& err)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    std::vector<std::string> args = argv_storage;
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& s : args)
    {
        argv.push_back(s.data());
    }

    const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);

    out = captured_out.str();
    err = captured_err.str();
    return rc;
}

static void run_golden_case(const fs::path& stdout_golden, const fs::path& stderr_golden,
                            const std::vector<std::string>& argv, bool expect_success)
{
    std::string out;
    std::string err;
    const int rc = run_cli(argv, out, err);

    const std::string expected_out = slurp(stdout_golden);
    const std::string expected_err = slurp(stderr_golden);

    if (expect_success && rc != 0)
    {
        fail("expected success but got rc=" + std::to_string(rc));
    }
    if (!expect_success && rc == 0)
    {
        fail("expected failure but got rc=0");
    }

    if (out != expected_out)
    {
        std::cerr << "GOLDEN MISMATCH: " << stdout_golden.filename().string() << "\n";
        std::cerr << "--- expected ---\n" << expected_out;
        std::cerr << "--- got ---\n" << out;
        std::exit(1);
    }

    if (err != expected_err)
    {
        std::cerr << "GOLDEN MISMATCH: " << stderr_golden.filename().string() << "\n";
        std::cerr << "--- expected ---\n" << expected_err;
        std::cerr << "--- got ---\n" << err;
        std::exit(1);
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: curlee_cli_bundle_golden_tests <golden_dir>\n";
        return 2;
    }

    const fs::path golden_dir = argv[1];

    {
        const fs::path bundle = golden_dir / "current_v1.bundle";
        run_golden_case(golden_dir / "current_v1.stdout.golden",
                        golden_dir / "current_v1.stderr.golden",
                        {"curlee", "bundle", "info", bundle.string()}, true);
    }

    {
        const fs::path bundle = golden_dir / "future_v2.bundle";
        run_golden_case(golden_dir / "future_v2.stdout.golden",
                        golden_dir / "future_v2.stderr.golden",
                        {"curlee", "bundle", "verify", bundle.string()}, false);
    }

    std::cout << "OK\n";
    return 0;
}
