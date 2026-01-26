#include <curlee/cli/cli.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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

static bool run_missing_file_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/does_not_exist.cur";

    std::vector<std::string> argv_storage = {"curlee", "lex", rel_path};
    std::vector<char*> argv;
    argv.reserve(argv_storage.size());
    for (auto& s : argv_storage)
    {
        argv.push_back(s.data());
    }

    const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);

    const std::string got = captured_err.str();
    const std::string expected = slurp(golden_path);

    if (rc == 0)
    {
        std::cerr << "expected non-zero exit code for missing file\n";
        return false;
    }

    if (got != expected)
    {
        std::cerr << "GOLDEN MISMATCH: " << golden_path.filename().string() << "\n";
        std::cerr << "--- expected ---\n" << expected;
        std::cerr << "--- got ---\n" << got;
        return false;
    }

    return true;
}

static bool run_check_not_implemented_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/hello.curlee";

    std::vector<std::string> argv_storage = {"curlee", "check", rel_path};
    std::vector<char*> argv;
    argv.reserve(argv_storage.size());
    for (auto& s : argv_storage)
    {
        argv.push_back(s.data());
    }

    const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);

    const std::string got = captured_err.str();
    const std::string expected = slurp(golden_path);

    if (rc == 0)
    {
        std::cerr << "expected non-zero exit code for check-not-implemented\n";
        return false;
    }

    if (got != expected)
    {
        std::cerr << "GOLDEN MISMATCH: " << golden_path.filename().string() << "\n";
        std::cerr << "--- expected ---\n" << expected;
        std::cerr << "--- got ---\n" << got;
        return false;
    }

    return true;
}

static bool run_check_unknown_name_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/check_unknown_name.curlee";

    std::vector<std::string> argv_storage = {"curlee", "check", rel_path};
    std::vector<char*> argv;
    argv.reserve(argv_storage.size());
    for (auto& s : argv_storage)
    {
        argv.push_back(s.data());
    }

    const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);

    const std::string got = captured_err.str();
    const std::string expected = slurp(golden_path);

    if (rc == 0)
    {
        std::cerr << "expected non-zero exit code for check-unknown-name\n";
        return false;
    }

    if (got != expected)
    {
        std::cerr << "GOLDEN MISMATCH: " << golden_path.filename().string() << "\n";
        std::cerr << "--- expected ---\n" << expected;
        std::cerr << "--- got ---\n" << got;
        return false;
    }

    return true;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: curlee_cli_diagnostics_golden_tests <tests/cli_diagnostics-dir>\n";
        return 2;
    }

    const fs::path dir = fs::path(argv[1]);
    const fs::path golden = dir / "missing_file.golden";
    const fs::path check_golden = dir / "check_not_implemented.golden";
    const fs::path check_unknown_name_golden = dir / "check_unknown_name.golden";

    try
    {
        if (!run_missing_file_case(golden))
        {
            return 1;
        }

        if (!run_check_not_implemented_case(check_golden))
        {
            return 1;
        }

        if (!run_check_unknown_name_case(check_unknown_name_golden))
        {
            return 1;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "exception: " << e.what() << "\n";
        return 2;
    }

    std::cout << "OK\n";
    return 0;
}
