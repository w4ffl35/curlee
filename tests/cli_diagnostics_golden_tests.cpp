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

static bool run_check_requires_divide_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/check_requires_divide.curlee";

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
        std::cerr << "expected non-zero exit code for check-requires-divide\n";
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

static bool run_check_refinement_implies_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/check_refinement_implies.curlee";

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

    if (rc != 0)
    {
        std::cerr << "expected zero exit code for refinement-implies check\n";
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

static bool run_run_requires_divide_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/check_requires_divide.curlee";

    std::vector<std::string> argv_storage = {"curlee", "run", rel_path};
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
        std::cerr << "expected non-zero exit code for run-requires-divide\n";
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

static bool run_check_ensures_fail_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/check_ensures_fail.curlee";

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
        std::cerr << "expected non-zero exit code for check-ensures-fail\n";
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

static bool run_run_ensures_fail_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/check_ensures_fail.curlee";

    std::vector<std::string> argv_storage = {"curlee", "run", rel_path};
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
        std::cerr << "expected non-zero exit code for run-ensures-fail\n";
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

static bool run_run_success_case(const fs::path& out_golden_path, const fs::path& err_golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/run_success.curlee";

    std::vector<std::string> argv_storage = {"curlee", "run", rel_path};
    std::vector<char*> argv;
    argv.reserve(argv_storage.size());
    for (auto& s : argv_storage)
    {
        argv.push_back(s.data());
    }

    const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);

    const std::string got_out = captured_out.str();
    const std::string got_err = captured_err.str();
    const std::string expected_out = slurp(out_golden_path);
    const std::string expected_err = slurp(err_golden_path);

    if (rc != 0)
    {
        std::cerr << "expected zero exit code for run-success\n";
        return false;
    }

    if (got_err != expected_err)
    {
        std::cerr << "GOLDEN MISMATCH (stderr): " << err_golden_path.filename().string() << "\n";
        std::cerr << "--- expected ---\n" << expected_err;
        std::cerr << "--- got ---\n" << got_err;
        return false;
    }

    if (got_out != expected_out)
    {
        std::cerr << "GOLDEN MISMATCH (stdout): " << out_golden_path.filename().string() << "\n";
        std::cerr << "--- expected ---\n" << expected_out;
        std::cerr << "--- got ---\n" << got_out;
        return false;
    }

    return true;
}

static bool run_run_success_with_cap_case(const fs::path& out_golden_path,
                                          const fs::path& err_golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/run_success.curlee";

    std::vector<std::string> argv_storage = {"curlee", "run", "--cap", "python:ffi", rel_path};
    std::vector<char*> argv;
    argv.reserve(argv_storage.size());
    for (auto& s : argv_storage)
    {
        argv.push_back(s.data());
    }

    const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);

    const std::string got_out = captured_out.str();
    const std::string got_err = captured_err.str();
    const std::string expected_out = slurp(out_golden_path);
    const std::string expected_err = slurp(err_golden_path);

    if (rc != 0)
    {
        std::cerr << "expected zero exit code for run-success-with-cap\n";
        return false;
    }

    if (got_err != expected_err)
    {
        std::cerr << "GOLDEN MISMATCH (stderr): " << err_golden_path.filename().string() << "\n";
        std::cerr << "--- expected ---\n" << expected_err;
        std::cerr << "--- got ---\n" << got_err;
        return false;
    }

    if (got_out != expected_out)
    {
        std::cerr << "GOLDEN MISMATCH (stdout): " << out_golden_path.filename().string() << "\n";
        std::cerr << "--- expected ---\n" << expected_out;
        std::cerr << "--- got ---\n" << got_out;
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

static bool run_check_type_error_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/check_type_error.curlee";

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
        std::cerr << "expected non-zero exit code for check-type-error\n";
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

static bool run_check_if_condition_type_error_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/check_if_condition_type_error.curlee";

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
        std::cerr << "expected non-zero exit code for if-condition-type-error\n";
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

static bool run_check_python_ffi_requires_unsafe_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/check_python_ffi_requires_unsafe.curlee";

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
        std::cerr << "expected non-zero exit code for python-ffi-requires-unsafe\n";
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

static bool run_run_python_ffi_case(std::vector<std::string> argv_storage,
                                    const fs::path& out_golden_path,
                                    const fs::path& err_golden_path, const std::string& case_name,
                                    int expected_exit_code)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    std::vector<char*> argv;
    argv.reserve(argv_storage.size());
    for (auto& s : argv_storage)
    {
        argv.push_back(s.data());
    }

    const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);

    const std::string got_out = captured_out.str();
    const std::string got_err = captured_err.str();
    const std::string expected_out = slurp(out_golden_path);
    const std::string expected_err = slurp(err_golden_path);

    if (expected_exit_code == 0 && rc != 0)
    {
        std::cerr << "expected exit code 0 for " << case_name << "\n";
        return false;
    }

    if (expected_exit_code != 0 && rc == 0)
    {
        std::cerr << "expected non-zero exit code for " << case_name << "\n";
        return false;
    }

    if (got_err != expected_err)
    {
        std::cerr << "GOLDEN MISMATCH (stderr): " << err_golden_path.filename().string() << "\n";
        std::cerr << "--- expected ---\n" << expected_err;
        std::cerr << "--- got ---\n" << got_err;
        return false;
    }

    if (got_out != expected_out)
    {
        std::cerr << "GOLDEN MISMATCH (stdout): " << out_golden_path.filename().string() << "\n";
        std::cerr << "--- expected ---\n" << expected_out;
        std::cerr << "--- got ---\n" << got_out;
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
    const fs::path check_requires_divide_golden = dir / "check_requires_divide.golden";
    const fs::path check_refinement_implies_golden = dir / "check_refinement_implies.golden";
    const fs::path check_unknown_name_golden = dir / "check_unknown_name.golden";
    const fs::path check_type_error_golden = dir / "check_type_error.golden";
    const fs::path check_if_condition_type_error_golden =
        dir / "check_if_condition_type_error.golden";
    const fs::path run_requires_divide_golden = dir / "run_requires_divide.golden";
    const fs::path check_ensures_fail_golden = dir / "check_ensures_fail.golden";
    const fs::path run_ensures_fail_golden = dir / "run_ensures_fail.golden";
    const fs::path run_success_out_golden = dir / "run_success.stdout.golden";
    const fs::path run_success_err_golden = dir / "run_success.stderr.golden";
    const fs::path check_python_ffi_requires_unsafe_golden =
        dir / "check_python_ffi_requires_unsafe.golden";
    const fs::path run_python_ffi_missing_cap_out_golden =
        dir / "run_python_ffi_missing_cap.stdout.golden";
    const fs::path run_python_ffi_missing_cap_err_golden =
        dir / "run_python_ffi_missing_cap.stderr.golden";
    const fs::path run_python_ffi_not_implemented_out_golden =
        dir / "run_python_ffi_not_implemented.stdout.golden";
    const fs::path run_python_ffi_not_implemented_err_golden =
        dir / "run_python_ffi_not_implemented.stderr.golden";

    try
    {
        if (!run_missing_file_case(golden))
        {
            return 1;
        }

        if (!run_check_requires_divide_case(check_requires_divide_golden))
        {
            return 1;
        }

        if (!run_check_refinement_implies_case(check_refinement_implies_golden))
        {
            return 1;
        }

        if (!run_check_unknown_name_case(check_unknown_name_golden))
        {
            return 1;
        }

        if (!run_check_type_error_case(check_type_error_golden))
        {
            return 1;
        }

        if (!run_check_if_condition_type_error_case(check_if_condition_type_error_golden))
        {
            return 1;
        }

        if (!run_check_python_ffi_requires_unsafe_case(check_python_ffi_requires_unsafe_golden))
        {
            return 1;
        }

        if (!run_run_requires_divide_case(run_requires_divide_golden))
        {
            return 1;
        }

        if (!run_check_ensures_fail_case(check_ensures_fail_golden))
        {
            return 1;
        }

        if (!run_run_ensures_fail_case(run_ensures_fail_golden))
        {
            return 1;
        }

        if (!run_run_success_case(run_success_out_golden, run_success_err_golden))
        {
            return 1;
        }

        if (!run_run_success_with_cap_case(run_success_out_golden, run_success_err_golden))
        {
            return 1;
        }

        {
            const std::string rel_path = "tests/fixtures/run_python_ffi.curlee";
            const std::vector<std::string> argv_storage = {"curlee", "run", rel_path};
            if (!run_run_python_ffi_case(argv_storage, run_python_ffi_missing_cap_out_golden,
                                         run_python_ffi_missing_cap_err_golden,
                                         "run-python-ffi-missing-cap", 1))
            {
                return 1;
            }
        }

        {
            const std::string rel_path = "tests/fixtures/run_python_ffi.curlee";
            const std::vector<std::string> argv_storage = {"curlee", "run", "--cap", "python:ffi",
                                                           rel_path};
            if (!run_run_python_ffi_case(argv_storage, run_python_ffi_not_implemented_out_golden,
                                         run_python_ffi_not_implemented_err_golden,
                                         "run-python-ffi-not-implemented", 0))
            {
                return 1;
            }
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
