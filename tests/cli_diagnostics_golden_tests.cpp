// SPDX-License-Identifier: MIT
#include <cstdlib>
#include <curlee/cli/cli.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{

struct CapturedRun
{
    int rc = 0;
    std::string out;
    std::string err;
};

struct EnvVarGuard
{
    std::string key;
    bool had_old = false;
    std::string old_value;

    explicit EnvVarGuard(std::string key_in) : key(std::move(key_in))
    {
        if (const char* old = std::getenv(key.c_str()); old != nullptr)
        {
            had_old = true;
            old_value = old;
        }
    }

    ~EnvVarGuard()
    {
        if (had_old)
        {
            ::setenv(key.c_str(), old_value.c_str(), 1);
            return;
        }
        ::unsetenv(key.c_str());
    }
};

std::string slurp(const fs::path& path)
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

CapturedRun run_cli(std::vector<std::string> argv_storage)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    std::streambuf* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    std::streambuf* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    std::vector<char*> argv;
    argv.reserve(argv_storage.size());
    for (std::string& s : argv_storage)
    {
        argv.push_back(s.data());
    }

    CapturedRun run;
    run.rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);
    run.out = captured_out.str();
    run.err = captured_err.str();
    return run;
}

CapturedRun run_cli_with_stdin(std::vector<std::string> argv_storage, const std::string& stdin_text)
{
    std::istringstream input(stdin_text);
    std::streambuf* old_cin = std::cin.rdbuf(input.rdbuf());
    std::cin.clear();

    const auto run = run_cli(std::move(argv_storage));

    std::cin.rdbuf(old_cin);
    return run;
}

bool check_exit_code(const std::string& case_name, int rc, bool expect_zero)
{
    if (expect_zero && rc != 0)
    {
        std::cerr << "expected zero exit code for " << case_name << "\n";
        return false;
    }
    if (!expect_zero && rc == 0)
    {
        std::cerr << "expected non-zero exit code for " << case_name << "\n";
        return false;
    }
    return true;
}

static bool run_check_requires_if_path_sensitive_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/check_requires_if_path_sensitive.curlee";

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
        std::cerr << "expected zero exit code for check-requires-if-path-sensitive\n";
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

static bool run_check_requires_while_condition_fact_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/check_requires_while_condition_fact.curlee";

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
        std::cerr << "expected zero exit code for check-requires-while-condition-fact\n";
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

static bool run_run_struct_not_runnable_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/check_struct_ok.curlee";

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
        std::cerr << "expected non-zero exit code for run-struct-not-runnable\n";
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

    std::vector<std::string> argv_storage = {"curlee", "run", "--cap", "python.ffi", rel_path};
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

static bool run_check_match_missing_arm_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/check_match_missing_arm.curlee";

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
        std::cerr << "expected non-zero exit code for check-match-missing-arm\n";
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

static bool run_check_struct_ok_case(const fs::path& golden_path)
{
    std::ostringstream captured_out;
    std::ostringstream captured_err;

    auto* old_cout = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_cerr = std::cerr.rdbuf(captured_err.rdbuf());

    const std::string rel_path = "tests/fixtures/check_struct_ok.curlee";

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
        std::cerr << "expected zero exit code for check-struct-ok\n";
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

bool check_text_equal(const std::string& label, const fs::path& golden_path, const std::string& got,
                      const std::string& expected);

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

    if (rc != expected_exit_code)
    {
        std::cerr << "expected exit code " << expected_exit_code << " for " << case_name << ", got "
                  << rc << "\n";
        return false;
    }

    if (!check_text_equal("stderr", err_golden_path, got_err, expected_err))
    {
        return false;
    }
    return check_text_equal("stdout", out_golden_path, got_out, expected_out);
}

bool check_text_equal(const std::string& label, const fs::path& golden_path, const std::string& got,
                      const std::string& expected)
{
    if (got == expected)
    {
        return true;
    }

    std::cerr << "GOLDEN MISMATCH (" << label << "): " << golden_path.filename().string() << "\n";
    std::cerr << "--- expected ---\n" << expected;
    std::cerr << "--- got ---\n" << got;
    return false;
}

bool run_stderr_case(const std::string& case_name, std::vector<std::string> argv_storage,
                     const fs::path& err_golden_path, bool expect_zero)
{
    const CapturedRun run = run_cli(std::move(argv_storage));
    if (!check_exit_code(case_name, run.rc, expect_zero))
    {
        return false;
    }

    const std::string expected_err = slurp(err_golden_path);
    return check_text_equal("stderr", err_golden_path, run.err, expected_err);
}

bool run_stdio_case(const std::string& case_name, std::vector<std::string> argv_storage,
                    const fs::path& out_golden_path, const fs::path& err_golden_path,
                    bool expect_zero)
{
    const CapturedRun run = run_cli(std::move(argv_storage));
    if (!check_exit_code(case_name, run.rc, expect_zero))
    {
        return false;
    }

    const std::string expected_out = slurp(out_golden_path);
    const std::string expected_err = slurp(err_golden_path);

    if (!check_text_equal("stderr", err_golden_path, run.err, expected_err))
    {
        return false;
    }
    return check_text_equal("stdout", out_golden_path, run.out, expected_out);
}

bool run_stdio_case_with_stdin(const std::string& case_name, std::vector<std::string> argv_storage,
                               std::string stdin_text, const fs::path& out_golden_path,
                               const fs::path& err_golden_path, bool expect_zero)
{
    const CapturedRun run = run_cli_with_stdin(std::move(argv_storage), stdin_text);
    if (!check_exit_code(case_name, run.rc, expect_zero))
    {
        return false;
    }

    const std::string expected_out = slurp(out_golden_path);
    const std::string expected_err = slurp(err_golden_path);

    if (!check_text_equal("stderr", err_golden_path, run.err, expected_err))
    {
        return false;
    }
    return check_text_equal("stdout", out_golden_path, run.out, expected_out);
}

bool run_stderr_case_with_stdin(const std::string& case_name, std::vector<std::string> argv_storage,
                                std::string stdin_text, const fs::path& err_golden_path,
                                bool expect_zero)
{
    const CapturedRun run = run_cli_with_stdin(std::move(argv_storage), stdin_text);
    if (!check_exit_code(case_name, run.rc, expect_zero))
    {
        return false;
    }

    const std::string expected_err = slurp(err_golden_path);
    return check_text_equal("stderr", err_golden_path, run.err, expected_err);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: curlee_cli_diagnostics_golden_tests <tests/cli_diagnostics-dir>\n";
        return 2;
    }

    const fs::path dir = fs::path(argv[1]);
    const fs::path stdlib_v1_root = fs::path("stdlib") / "v1";

    const fs::path rel_missing_file = fs::path("tests/fixtures/does_not_exist.cur");
    const fs::path rel_requires_divide = fs::path("tests/fixtures/check_requires_divide.curlee");
    const fs::path rel_refinement_implies =
        fs::path("tests/fixtures/check_refinement_implies.curlee");
    const fs::path rel_if_path_sensitive =
        fs::path("tests/fixtures/check_requires_if_path_sensitive.curlee");
    const fs::path rel_while_condition_fact =
        fs::path("tests/fixtures/check_requires_while_condition_fact.curlee");
    const fs::path rel_unknown_name = fs::path("tests/fixtures/check_unknown_name.curlee");
    const fs::path rel_type_error = fs::path("tests/fixtures/check_type_error.curlee");
    const fs::path rel_struct_ok = fs::path("tests/fixtures/check_struct_ok.curlee");
    const fs::path rel_ensures_fail = fs::path("tests/fixtures/check_ensures_fail.curlee");
    const fs::path rel_extern_assumed = fs::path("tests/fixtures/check_extern_assumed.curlee");
    const fs::path rel_phys_framebuffer = fs::path("tests/fixtures/check_phys_framebuffer.curlee");
    const fs::path rel_phys_read_contract =
        fs::path("tests/fixtures/check_phys_read_contract.curlee");
    const fs::path rel_phys_read_requires =
        fs::path("tests/fixtures/check_phys_read_requires.curlee");
    const fs::path rel_phys_mem_ok = fs::path("tests/fixtures/check_phys_mem_ok.curlee");
    const fs::path rel_phys_non_literal_addr =
        fs::path("tests/fixtures/check_phys_non_literal_addr.curlee");
    const fs::path rel_phys_outside_unsafe =
        fs::path("tests/fixtures/check_phys_outside_unsafe.curlee");
    const fs::path rel_phys_missing_cap = fs::path("tests/fixtures/check_phys_missing_cap.curlee");
    const fs::path rel_phys_arithmetic = fs::path("tests/fixtures/check_phys_arithmetic.curlee");
    const fs::path rel_phys_string_element =
        fs::path("tests/fixtures/check_phys_string_element.curlee");
    const fs::path rel_phys_read_non_phys =
        fs::path("tests/fixtures/check_phys_read_non_phys.curlee");
    const fs::path rel_io_port_ok = fs::path("tests/fixtures/check_io_port_ok.curlee");
    // Large Int literals in [2^63, 2^64-1] (issue #276 review finding: the
    // verifier's IntExpr lowering must not corrupt them to negative).
    const fs::path rel_int_literal_64bit =
        fs::path("tests/fixtures/check_int_literal_64bit.curlee");
    const fs::path rel_int_literal_64bit_fail =
        fs::path("tests/fixtures/check_int_literal_64bit_fail.curlee");
    // Int literals at or above 2^64 (issue #276 review round 2: hex and
    // underscore lexemes must not abort the process via an uncaught
    // z3::exception from the raw lexeme; they must lower to their exact value
    // like the plain decimal form).
    const fs::path rel_int_literal_2_64 =
        fs::path("tests/fixtures/check_int_literal_2_64.curlee");
    const fs::path rel_int_literal_2_64_fail =
        fs::path("tests/fixtures/check_int_literal_2_64_fail.curlee");
    const fs::path rel_io_port_outside_unsafe =
        fs::path("tests/fixtures/check_io_port_outside_unsafe.curlee");
    const fs::path rel_io_port_missing_cap =
        fs::path("tests/fixtures/check_io_port_missing_cap.curlee");
    const fs::path rel_io_port_runtime_port =
        fs::path("tests/fixtures/check_io_port_runtime_port.curlee");
    const fs::path rel_io_port_non_int_port =
        fs::path("tests/fixtures/check_io_port_non_int_port.curlee");
    const fs::path rel_io_port_read_contract =
        fs::path("tests/fixtures/check_io_port_read_contract.curlee");
    const fs::path rel_io_port_read_contract_runtime =
        fs::path("tests/fixtures/check_io_port_read_contract_runtime.curlee");
    const fs::path rel_io_port_value_type =
        fs::path("tests/fixtures/check_io_port_value_type.curlee");
    // Runtime-address physical memory reads (issue #279): the mb2.c multiboot2
    // tag walk — a general Int/U64 address (runtime base + mutable cursor) reads
    // opaque values that can be compared/bit-tested but never contracted on; a
    // non-integer address is rejected by the type checker.
    const fs::path rel_phys_read_runtime =
        fs::path("tests/fixtures/check_phys_read_runtime.curlee");
    const fs::path rel_phys_read_runtime_addr =
        fs::path("tests/fixtures/check_phys_read_runtime_addr.curlee");
    const fs::path rel_phys_read_runtime_contract =
        fs::path("tests/fixtures/check_phys_read_runtime_contract.curlee");
    // Runtime-address physical memory writes (issue #285): the fb.c pixel-write
    // pattern — a general Int/U64 address (runtime framebuffer base + mutable
    // pixel cursor) with the color value adapting from an Int literal; a
    // non-integer address and a mismatched value width are rejected by the type
    // checker.
    const fs::path rel_phys_write_runtime =
        fs::path("tests/fixtures/check_phys_write_runtime.curlee");
    const fs::path rel_phys_write_runtime_addr =
        fs::path("tests/fixtures/check_phys_write_runtime_addr.curlee");
    const fs::path rel_phys_write_runtime_value =
        fs::path("tests/fixtures/check_phys_write_runtime_value.curlee");
    // Address-of a Curlee-owned fixed-size array (issue #286): the DMA
    // descriptor-filling primitive — an Int address for a local/static array
    // binding that flows into the runtime phys builtins (issues #279/#285)
    // and a descriptor's U64 field (Int -> U64 widening, issue #277). Scalar
    // and indexed-element targets and uses outside `unsafe` are rejected by
    // the type checker; the value is trusted/opaque to the verifier (stable
    // per array binding, never assumed numerically).
    const fs::path rel_addr_of = fs::path("tests/fixtures/check_addr_of.curlee");
    const fs::path rel_addr_of_scalar =
        fs::path("tests/fixtures/check_addr_of_scalar.curlee");
    const fs::path rel_addr_of_outside_unsafe =
        fs::path("tests/fixtures/check_addr_of_outside_unsafe.curlee");
    const fs::path rel_addr_of_element =
        fs::path("tests/fixtures/check_addr_of_element.curlee");
    const fs::path rel_addr_of_opaque =
        fs::path("tests/fixtures/check_addr_of_opaque.curlee");
    const fs::path rel_addr_of_shadow_static =
        fs::path("tests/fixtures/check_addr_of_shadow_static.curlee");
    const fs::path rel_addr_of_shadow_local =
        fs::path("tests/fixtures/check_addr_of_shadow_local.curlee");
    const fs::path rel_addr_of_shadow_scoped =
        fs::path("tests/fixtures/check_addr_of_shadow_scoped.curlee");
    const fs::path rel_addr_of_shadow_honest =
        fs::path("tests/fixtures/check_addr_of_shadow_honest.curlee");
    const fs::path rel_unsigned_inspect =
        fs::path("tests/fixtures/check_unsigned_inspect.curlee");
    // U64 construction from Int/literal (issue #277).
    const fs::path rel_u64_widen = fs::path("tests/fixtures/check_u64_widen.curlee");
    const fs::path rel_u64_widen_oob =
        fs::path("tests/fixtures/check_u64_widen_oob.curlee");
    const fs::path rel_run_missing_stdout_cap = fs::path("tests/fixtures/r.curlee");
    const fs::path rel_run_missing_tty_cap = fs::path("tests/fixtures/run_missing_tty_cap.curlee");
    const fs::path rel_run_read_line = fs::path("tests/fixtures/run_read_line.curlee");
    const fs::path rel_run_tty_order = fs::path("tests/fixtures/run_tty_order.curlee");
    const fs::path rel_run_success = fs::path("tests/fixtures/run_success.curlee");
    const fs::path rel_run_read_line_over_limit =
        fs::path("tests/fixtures/run_read_line_over_limit.curlee");
    const fs::path rel_fuel_straightline_low =
        fs::path("tests/fixtures/check_fuel_straightline_low.curlee");
    const fs::path rel_fuel_straightline_ok =
        fs::path("tests/fixtures/check_fuel_straightline_ok.curlee");
    const fs::path rel_fuel_loop_no_decreases =
        fs::path("tests/fixtures/check_fuel_loop_no_decreases.curlee");
    const fs::path rel_fuel_callgraph_ok =
        fs::path("tests/fixtures/check_fuel_callgraph_ok.curlee");
    const fs::path rel_fuel_callgraph_low =
        fs::path("tests/fixtures/check_fuel_callgraph_low.curlee");
    const fs::path rel_fuel_extern_ok = fs::path("tests/fixtures/check_fuel_extern_ok.curlee");
    const fs::path rel_fuel_extern_missing =
        fs::path("tests/fixtures/check_fuel_extern_missing.curlee");
    const fs::path rel_fuel_loop_shadowed_variant =
        fs::path("tests/fixtures/check_fuel_loop_shadowed_variant.curlee");
    const fs::path rel_fuel_nested_call_low =
        fs::path("tests/fixtures/check_fuel_nested_call_low.curlee");
    const fs::path rel_fuel_recursive =
        fs::path("tests/fixtures/check_fuel_recursive.curlee");
    // Runtime-address physical writes (issue #285, review round 1): the fuel
    // cost model must charge the volatile store (addr + value + 1), so a
    // `[ fuel 1; ]` bound on one write must FAIL, and a sufficient bound (4)
    // must pass.
    const fs::path rel_fuel_phys_write_low =
        fs::path("tests/fixtures/check_fuel_phys_write_low.curlee");
    const fs::path rel_fuel_phys_write_ok =
        fs::path("tests/fixtures/check_fuel_phys_write_ok.curlee");
    // Assignment statements (issue #268) diagnostics.
    const fs::path rel_assign_bounded_sum =
        fs::path("tests/fixtures/check_assign_bounded_sum.curlee");
    const fs::path rel_assign_type_mismatch =
        fs::path("tests/fixtures/check_assign_type_mismatch.curlee");
    const fs::path rel_assign_unknown = fs::path("tests/fixtures/check_assign_unknown.curlee");
    const fs::path rel_assign_param = fs::path("tests/fixtures/check_assign_param.curlee");
    const fs::path rel_assign_shadowed_invariant =
        fs::path("tests/fixtures/check_assign_shadowed_invariant.curlee");
    const fs::path rel_assign_loop_poststate =
        fs::path("tests/fixtures/check_assign_loop_poststate.curlee");
    const fs::path rel_run_assign = fs::path("tests/fixtures/run_assign.curlee");
    // Fixed-size arrays (issue #278) diagnostics.
    const fs::path rel_array_ring = fs::path("tests/fixtures/check_array_ring.curlee");
    const fs::path rel_array_oob_const =
        fs::path("tests/fixtures/check_array_oob_const.curlee");
    const fs::path rel_array_oob_neg = fs::path("tests/fixtures/check_array_oob_neg.curlee");
    const fs::path rel_array_oob_unproven =
        fs::path("tests/fixtures/check_array_oob_unproven.curlee");
    const fs::path rel_array_len_mismatch =
        fs::path("tests/fixtures/check_array_len_mismatch.curlee");
    const fs::path rel_array_elem_mismatch =
        fs::path("tests/fixtures/check_array_elem_mismatch.curlee");
    const fs::path rel_array_reject_multi =
        fs::path("tests/fixtures/check_array_reject_multi.curlee");
    const fs::path rel_array_reject_bare =
        fs::path("tests/fixtures/check_array_reject_bare.curlee");
    const fs::path rel_array_reject_field =
        fs::path("tests/fixtures/check_array_reject_field.curlee");
    const fs::path rel_array_reject_param =
        fs::path("tests/fixtures/check_array_reject_param.curlee");
    const fs::path rel_array_reject_return =
        fs::path("tests/fixtures/check_array_reject_return.curlee");
    const fs::path rel_array_reject_ghost =
        fs::path("tests/fixtures/check_array_reject_ghost.curlee");
    const fs::path rel_array_reject_unsupported_elem =
        fs::path("tests/fixtures/check_array_reject_unsupported_elem.curlee");
    const fs::path rel_run_array_vm_reject =
        fs::path("tests/fixtures/run_array_vm_reject.curlee");
    // Module-level mutable state (issue #287) diagnostics.
    const fs::path rel_static_state = fs::path("tests/fixtures/check_static_state.curlee");
    const fs::path rel_static_contract_global =
        fs::path("tests/fixtures/check_static_contract_global.curlee");
    const fs::path rel_static_requires_global =
        fs::path("tests/fixtures/check_static_requires_global.curlee");
    const fs::path rel_static_invariant_global =
        fs::path("tests/fixtures/check_static_invariant_global.curlee");
    const fs::path rel_static_type_mismatch =
        fs::path("tests/fixtures/check_static_type_mismatch.curlee");
    const fs::path rel_static_non_literal =
        fs::path("tests/fixtures/check_static_non_literal.curlee");
    const fs::path rel_static_duplicate =
        fs::path("tests/fixtures/check_static_duplicate.curlee");
    const fs::path rel_static_fn_conflict =
        fs::path("tests/fixtures/check_static_fn_conflict.curlee");
    const fs::path rel_static_unsupported_type =
        fs::path("tests/fixtures/check_static_unsupported_type.curlee");
    const fs::path rel_static_array_len_mismatch =
        fs::path("tests/fixtures/check_static_array_len_mismatch.curlee");
    const fs::path rel_static_array_not_repeat =
        fs::path("tests/fixtures/check_static_array_not_repeat.curlee");
    const fs::path rel_static_u8_scalar =
        fs::path("tests/fixtures/check_static_u8_scalar.curlee");
    const fs::path rel_static_oob = fs::path("tests/fixtures/check_static_oob.curlee");
    const fs::path rel_run_global_state = fs::path("tests/fixtures/run_global_state.curlee");
    const fs::path rel_run_static_array_vm_reject =
        fs::path("tests/fixtures/run_static_array_vm_reject.curlee");

    const fs::path check_requires_divide_golden = dir / "check_requires_divide.golden";
    const fs::path check_refinement_implies_golden = dir / "check_refinement_implies.golden";
    const fs::path check_requires_if_path_sensitive_golden =
        dir / "check_requires_if_path_sensitive.golden";
    const fs::path check_requires_while_condition_fact_golden =
        dir / "check_requires_while_condition_fact.golden";
    const fs::path check_unknown_name_golden = dir / "check_unknown_name.golden";
    const fs::path check_type_error_golden = dir / "check_type_error.golden";
    const fs::path check_match_missing_arm_golden = dir / "check_match_missing_arm.golden";
    const fs::path check_if_condition_type_error_golden =
        dir / "check_if_condition_type_error.golden";
    const fs::path run_requires_divide_golden = dir / "run_requires_divide.golden";
    const fs::path check_ensures_fail_golden = dir / "check_ensures_fail.golden";
    const fs::path run_ensures_fail_golden = dir / "run_ensures_fail.golden";
    const fs::path run_struct_not_runnable_golden = dir / "run_struct_not_runnable.golden";
    const fs::path run_success_out_golden = dir / "run_success.stdout.golden";
    const fs::path run_success_err_golden = dir / "run_success.stderr.golden";
    const fs::path check_python_ffi_requires_unsafe_golden =
        dir / "check_python_ffi_requires_unsafe.golden";

    EnvVarGuard stdlib_root_guard("CURLEE_STDLIB_ROOT");
    ::setenv("CURLEE_STDLIB_ROOT", stdlib_v1_root.string().c_str(), 1);

    const fs::path run_python_ffi_sandbox_required_out_golden =
        dir / "run_python_ffi_sandbox_required.stdout.golden";
    const fs::path run_python_ffi_sandbox_required_err_golden =
        dir / "run_python_ffi_sandbox_required.stderr.golden";

    const fs::path run_python_ffi_sandboxed_out_golden =
        dir / "run_python_ffi_sandboxed.stdout.golden";
    const fs::path run_python_ffi_sandboxed_err_golden =
        dir / "run_python_ffi_sandboxed.stderr.golden";
    const fs::path run_graphics_window_init_failed_out_golden =
        dir / "run_graphics_window_init_failed.stdout.golden";
    const fs::path run_graphics_window_init_failed_err_golden =
        dir / "run_graphics_window_init_failed.stderr.golden";
    const fs::path run_graphics_window_present_failed_out_golden =
        dir / "run_graphics_window_present_failed.stdout.golden";
    const fs::path run_graphics_window_present_failed_err_golden =
        dir / "run_graphics_window_present_failed.stderr.golden";

    try
    {
        if (!run_stderr_case("missing-file", {"curlee", "lex", rel_missing_file.string()},
                             dir / "missing_file.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-requires-divide",
                             {"curlee", "check", rel_requires_divide.string()},
                             dir / "check_requires_divide.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-refinement-implies",
                             {"curlee", "check", rel_refinement_implies.string()},
                             dir / "check_refinement_implies.golden", true))
        {
            return 1;
        }

        if (!run_stderr_case("check-requires-if-path-sensitive",
                             {"curlee", "check", rel_if_path_sensitive.string()},
                             dir / "check_requires_if_path_sensitive.golden", true))
        {
            return 1;
        }

        if (!run_stderr_case("check-requires-while-condition-fact",
                             {"curlee", "check", rel_while_condition_fact.string()},
                             dir / "check_requires_while_condition_fact.golden", true))
        {
            return 1;
        }

        if (!run_stderr_case("check-unknown-name", {"curlee", "check", rel_unknown_name.string()},
                             dir / "check_unknown_name.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-type-error", {"curlee", "check", rel_type_error.string()},
                             dir / "check_type_error.golden", false))
        {
            return 1;
        }

        if (!run_check_match_missing_arm_case(check_match_missing_arm_golden))
        {
            return 1;
        }

        if (!run_check_if_condition_type_error_case(check_if_condition_type_error_golden))
        {
            return 1;
        }

        if (!run_stderr_case("check-struct-ok", {"curlee", "check", rel_struct_ok.string()},
                             dir / "check_struct_ok.golden", true))
        {
            return 1;
        }

        // Front-end Phys<T> support (issue #252): positive fixture passes type checking.
        if (!run_stderr_case("check-phys-mem-ok", {"curlee", "check", rel_phys_mem_ok.string()},
                             dir / "check_phys_mem_ok.golden", true))
        {
            return 1;
        }

        if (!run_stderr_case("check-phys-non-literal-addr",
                             {"curlee", "check", rel_phys_non_literal_addr.string()},
                             dir / "check_phys_non_literal_addr.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-phys-outside-unsafe",
                             {"curlee", "check", rel_phys_outside_unsafe.string()},
                             dir / "check_phys_outside_unsafe.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-phys-missing-cap",
                             {"curlee", "check", rel_phys_missing_cap.string()},
                             dir / "check_phys_missing_cap.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-phys-arithmetic",
                             {"curlee", "check", rel_phys_arithmetic.string()},
                             dir / "check_phys_arithmetic.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-phys-string-element",
                             {"curlee", "check", rel_phys_string_element.string()},
                             dir / "check_phys_string_element.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-phys-read-non-phys",
                             {"curlee", "check", rel_phys_read_non_phys.string()},
                             dir / "check_phys_read_non_phys.golden", false))
        {
            return 1;
        }

        // Verifier: trusted/opaque Phys<T> deref semantics (issue #253). The positive
        // framebuffer fixture must pass with zero obligations; the read-contract fixture must
        // fail with the opaque-value note.
        if (!run_stderr_case("check-phys-framebuffer",
                             {"curlee", "check", rel_phys_framebuffer.string()},
                             dir / "check_phys_framebuffer.golden", true))
        {
            return 1;
        }

        if (!run_stderr_case("check-phys-read-contract",
                             {"curlee", "check", rel_phys_read_contract.string()},
                             dir / "check_phys_read_contract.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-phys-read-requires",
                             {"curlee", "check", rel_phys_read_requires.string()},
                             dir / "check_phys_read_requires.golden", false))
        {
            return 1;
        }

        // x86 port I/O builtins (issue #269/#276): positive constant-port and
        // runtime-port fixtures verify with zero obligations; gate and port-type
        // violations are hard errors; contracts cannot see through opaque port
        // reads (constant or runtime port).
        if (!run_stderr_case("check-io-port-ok", {"curlee", "check", rel_io_port_ok.string()},
                             dir / "check_io_port_ok.golden", true))
        {
            return 1;
        }

        if (!run_stderr_case("check-io-port-runtime-port",
                             {"curlee", "check", rel_io_port_runtime_port.string()},
                             dir / "check_io_port_runtime_port.golden", true))
        {
            return 1;
        }

        // Large Int literals (issue #276 review finding regression guards): the
        // positive fixture verifies that 2^63 (decimal, hex, underscore) and
        // -(2^63) lower to their exact values; the negative fixture proves the
        // verifier still checks (the corrupted-literal bug made this contract
        // pass vacuously with the sign flipped).
        if (!run_stderr_case("check-int-literal-64bit",
                             {"curlee", "check", rel_int_literal_64bit.string()},
                             dir / "check_int_literal_64bit.golden", true))
        {
            return 1;
        }

        if (!run_stderr_case("check-int-literal-64bit-fail",
                             {"curlee", "check", rel_int_literal_64bit_fail.string()},
                             dir / "check_int_literal_64bit_fail.golden", false))
        {
            return 1;
        }

        // Int literals at or above 2^64 (issue #276 review round 2): the
        // positive fixture proves hex and underscore forms of 2^64 lower to
        // their exact value (the old code aborted with an uncaught
        // z3::exception); the negative fixture proves the verifier still
        // checks, with a model showing the correct positive numeral.
        if (!run_stderr_case("check-int-literal-2-64",
                             {"curlee", "check", rel_int_literal_2_64.string()},
                             dir / "check_int_literal_2_64.golden", true))
        {
            return 1;
        }

        if (!run_stderr_case("check-int-literal-2-64-fail",
                             {"curlee", "check", rel_int_literal_2_64_fail.string()},
                             dir / "check_int_literal_2_64_fail.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-io-port-outside-unsafe",
                             {"curlee", "check", rel_io_port_outside_unsafe.string()},
                             dir / "check_io_port_outside_unsafe.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-io-port-missing-cap",
                             {"curlee", "check", rel_io_port_missing_cap.string()},
                             dir / "check_io_port_missing_cap.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-io-port-non-int-port",
                             {"curlee", "check", rel_io_port_non_int_port.string()},
                             dir / "check_io_port_non_int_port.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-io-port-read-contract",
                             {"curlee", "check", rel_io_port_read_contract.string()},
                             dir / "check_io_port_read_contract.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-io-port-read-contract-runtime",
                             {"curlee", "check", rel_io_port_read_contract_runtime.string()},
                             dir / "check_io_port_read_contract_runtime.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-io-port-value-type",
                             {"curlee", "check", rel_io_port_value_type.string()},
                             dir / "check_io_port_value_type.golden", false))
        {
            return 1;
        }

        // Runtime-address physical memory reads (issue #279): the mb2.c
        // multiboot2 tag walk checks cleanly with a runtime base + mutable byte
        // cursor (opaque reads, comparison/bit-test only); a Bool address is
        // rejected by the type checker; a refinement on a read value is rejected
        // (never silently proven) exactly like Phys<T>.read()/port_in*.
        if (!run_stderr_case("check-phys-read-runtime",
                             {"curlee", "check", rel_phys_read_runtime.string()},
                             dir / "check_phys_read_runtime.golden", true))
        {
            return 1;
        }
        if (!run_stderr_case("check-phys-read-runtime-addr",
                             {"curlee", "check", rel_phys_read_runtime_addr.string()},
                             dir / "check_phys_read_runtime_addr.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-phys-read-runtime-contract",
                             {"curlee", "check", rel_phys_read_runtime_contract.string()},
                             dir / "check_phys_read_runtime_contract.golden", false))
        {
            return 1;
        }

        // Runtime-address physical memory writes (issue #285): the fb.c
        // pixel-write pattern checks cleanly with a runtime base + mutable pixel
        // cursor (trusted/opaque side effects); a Bool address and a mismatched
        // value width are rejected by the type checker.
        if (!run_stderr_case("check-phys-write-runtime",
                             {"curlee", "check", rel_phys_write_runtime.string()},
                             dir / "check_phys_write_runtime.golden", true))
        {
            return 1;
        }
        if (!run_stderr_case("check-phys-write-runtime-addr",
                             {"curlee", "check", rel_phys_write_runtime_addr.string()},
                             dir / "check_phys_write_runtime_addr.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-phys-write-runtime-value",
                             {"curlee", "check", rel_phys_write_runtime_value.string()},
                             dir / "check_phys_write_runtime_value.golden", false))
        {
            return 1;
        }

        // Address-of a Curlee-owned fixed-size array (issue #286): the
        // virtio-shaped descriptor fill checks cleanly (stable opaque
        // addresses — two addr_of(q) calls are provably equal — an Int ->
        // U64 widening into a descriptor field, a phys_read_u8 round-trip of
        // an array-index write, and the virtqueue PFN `base >> 12` setup).
        // A scalar target, an indexed element target and use outside `unsafe`
        // are rejected by the type checker; an ensures claiming a specific
        // numeric address is unprovable (the value is trusted/opaque).
        if (!run_stderr_case("check-addr-of", {"curlee", "check", rel_addr_of.string()},
                             dir / "check_addr_of.golden", true))
        {
            return 1;
        }
        if (!run_stderr_case("check-addr-of-scalar",
                             {"curlee", "check", rel_addr_of_scalar.string()},
                             dir / "check_addr_of_scalar.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-addr-of-outside-unsafe",
                             {"curlee", "check", rel_addr_of_outside_unsafe.string()},
                             dir / "check_addr_of_outside_unsafe.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-addr-of-element",
                             {"curlee", "check", rel_addr_of_element.string()},
                             dir / "check_addr_of_element.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-addr-of-opaque",
                             {"curlee", "check", rel_addr_of_opaque.string()},
                             dir / "check_addr_of_opaque.golden", false))
        {
            return 1;
        }

        // Shadowing regression (issue #286 review round 1): the addr_of
        // opaque constant is keyed by BINDING, not name, so two different
        // arrays that share a name (a local shadowing a static, or nested
        // shadowed locals) map to DISTINCT constants. `ensures result == 0`
        // over their difference is therefore unprovable and both shadow
        // fixtures must FAIL check (before the fix they verified and the
        // emitted kernels contradicted the contract at runtime).
        if (!run_stderr_case("check-addr-of-shadow-static",
                             {"curlee", "check", rel_addr_of_shadow_static.string()},
                             dir / "check_addr_of_shadow_static.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-addr-of-shadow-local",
                             {"curlee", "check", rel_addr_of_shadow_local.string()},
                             dir / "check_addr_of_shadow_local.golden", false))
        {
            return 1;
        }
        // Positive controls: shadowing must NOT leak across scopes (an
        // addr_of(q) before and after a nested shadow is the same static
        // binding, so equality is provable), and the honest shadowing pattern
        // (no contract on the address difference) must check cleanly.
        if (!run_stderr_case("check-addr-of-shadow-scoped",
                             {"curlee", "check", rel_addr_of_shadow_scoped.string()},
                             dir / "check_addr_of_shadow_scoped.golden", true))
        {
            return 1;
        }
        if (!run_stderr_case("check-addr-of-shadow-honest",
                             {"curlee", "check", rel_addr_of_shadow_honest.string()},
                             dir / "check_addr_of_shadow_honest.golden", true))
        {
            return 1;
        }

        // Unsigned widening / comparisons / bitwise on port reads (issue #274):
        // the putc_driver busy-wait, vbe readback validation, and the
        // division-vs-bitwise glyph equivalence all check cleanly, while
        // contracts still cannot see through the opaque reads.
        if (!run_stderr_case("check-unsigned-inspect",
                             {"curlee", "check", rel_unsigned_inspect.string()},
                             dir / "check_unsigned_inspect.golden", true))
        {
            return 1;
        }

        // U64 construction from Int/literal (issue #277): Int -> U64 widening
        // in `let` and struct fields, U64 literal adaptation, and U64 == U64
        // comparisons all check cleanly; a literal at or above 2^32 is
        // rejected as out of range rather than silently truncated.
        if (!run_stderr_case("check-u64-widen",
                             {"curlee", "check", rel_u64_widen.string()},
                             dir / "check_u64_widen.golden", true))
        {
            return 1;
        }
        if (!run_stderr_case("check-u64-widen-oob",
                             {"curlee", "check", rel_u64_widen_oob.string()},
                             dir / "check_u64_widen_oob.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("run-requires-divide", {"curlee", "run", rel_requires_divide.string()},
                             dir / "run_requires_divide.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("check-ensures-fail", {"curlee", "check", rel_ensures_fail.string()},
                             dir / "check_ensures_fail.golden", false))
        {
            return 1;
        }

        // Extern functions (issue #256): `curlee check` passes (exit 0) with the
        // "extern boundary: contract assumed, not verified" Note rendered.
        if (!run_stderr_case("check-extern-assumed",
                             {"curlee", "check", rel_extern_assumed.string()},
                             dir / "check_extern_assumed.golden", true))
        {
            return 1;
        }

        // `curlee run` on a program containing extern functions is rejected by
        // the VM with a clear diagnostic (non-zero exit).
        if (!run_stderr_case("run-extern-vm-reject", {"curlee", "run", rel_extern_assumed.string()},
                             dir / "run_extern_vm_reject.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("run-ensures-fail", {"curlee", "run", rel_ensures_fail.string()},
                             dir / "run_ensures_fail.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("run-struct-not-runnable", {"curlee", "run", rel_struct_ok.string()},
                             dir / "run_struct_not_runnable.golden", true))
        {
            return 1;
        }

        if (!run_stderr_case("run-missing-stdout-cap",
                             {"curlee", "run", rel_run_missing_stdout_cap.string()},
                             dir / "run_missing_stdout_cap.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("run-missing-tty-cap",
                             {"curlee", "run", rel_run_missing_tty_cap.string()},
                             dir / "run_missing_tty_cap.golden", false))
        {
            return 1;
        }

        if (!run_stderr_case("run-missing-stdin-cap-json",
                             {"curlee", "run", "--diag-format", "json", rel_run_read_line.string()},
                             dir / "run_missing_stdin_cap.json.golden", false))
        {
            return 1;
        }

        if (!run_stdio_case(
                "run-tty-order", {"curlee", "run", "--cap", "io.tty", rel_run_tty_order.string()},
                dir / "run_tty_order.stdout.golden", dir / "run_tty_order.stderr.golden", true))
        {
            return 1;
        }

        if (!run_stdio_case(
                "run-with-stdout-cap",
                {"curlee", "run", "--cap", "io.stdout", rel_run_missing_stdout_cap.string()},
                dir / "run_with_stdout_cap.stdout.golden",
                dir / "run_with_stdout_cap.stderr.golden", true))
        {
            return 1;
        }

        if (!run_stdio_case("run-success", {"curlee", "run", rel_run_success.string()},
                            dir / "run_success.stdout.golden", dir / "run_success.stderr.golden",
                            true))
        {
            return 1;
        }

        // Assignment statements execute in the VM (issue #268): the bounded-sum
        // loop + straight-line reassignment yield 6 + 2 = 8.
        if (!run_stdio_case("run-assign", {"curlee", "run", rel_run_assign.string()},
                            dir / "run_assign.stdout.golden", dir / "run_assign.stderr.golden",
                            true))
        {
            return 1;
        }

        // Module-level mutable state runs in the VM (issue #287): the scalar
        // statics live in persistent low slots of the locals array, so
        // separate function calls observe each other's mutations (bump/bump/
        // peek/bump -> 1,2,2,3 => 1223). If the statics were re-initialized
        // per call, peek() would see 0 and the result would differ.
        if (!run_stdio_case("run-global-state", {"curlee", "run", rel_run_global_state.string()},
                            dir / "run_global_state.stdout.golden",
                            dir / "run_global_state.stderr.golden", true))
        {
            return 1;
        }

        if (!run_stdio_case("run-success-with-cap",
                            {"curlee", "run", "--cap", "python.ffi", rel_run_success.string()},
                            dir / "run_success.stdout.golden", dir / "run_success.stderr.golden",
                            true))
        {
            return 1;
        }

        if (!run_stdio_case_with_stdin(
                "run-read-line-success",
                {"curlee", "run", "--cap", "io.stdin", rel_run_read_line.string()},
                "hello from stdin\n", dir / "run_read_line.stdout.golden",
                dir / "run_read_line.stderr.golden", true))
        {
            return 1;
        }

        if (!run_stdio_case_with_stdin(
                "run-read-line-eof",
                {"curlee", "run", "--cap", "io.stdin", rel_run_read_line.string()}, "",
                dir / "run_read_line_eof.stdout.golden", dir / "run_read_line_eof.stderr.golden",
                true))
        {
            return 1;
        }

        if (!run_stderr_case_with_stdin("run-read-line-over-limit-json",
                                        {"curlee", "run", "--cap", "io.stdin", "--diag-format",
                                         "json", rel_run_read_line_over_limit.string()},
                                        std::string(4097, 'a') + "\n",
                                        dir / "run_read_line_over_limit.json.golden", false))
        {
            return 1;
        }

        // Graphics backend init failure surfaces deterministic runtime diagnostic.
        {
            (void)setenv("CURLEE_GFX_WINDOW_INIT_FAIL", "1", 1);

            const std::string rel_path = "tests/fixtures/run_success.curlee";
            const std::vector<std::string> argv_storage = {
                "curlee", "run", "--graphics=window", "--cap", "gfx.window", rel_path};
            if (!run_run_python_ffi_case(argv_storage, run_graphics_window_init_failed_out_golden,
                                         run_graphics_window_init_failed_err_golden,
                                         "run-graphics-window-init-failed", 1))
            {
                return 1;
            }

            (void)unsetenv("CURLEE_GFX_WINDOW_INIT_FAIL");
        }

        // Graphics backend present failure surfaces deterministic runtime diagnostic.
        {
            (void)setenv("CURLEE_GFX_WINDOW_PRESENT_FAIL", "1", 1);

            const std::string rel_path = "tests/fixtures/run_success.curlee";
            const std::vector<std::string> argv_storage = {
                "curlee", "run", "--graphics=window", "--cap", "gfx.window", rel_path};
            if (!run_run_python_ffi_case(argv_storage,
                                         run_graphics_window_present_failed_out_golden,
                                         run_graphics_window_present_failed_err_golden,
                                         "run-graphics-window-present-failed", 1))
            {
                return 1;
            }

            (void)unsetenv("CURLEE_GFX_WINDOW_PRESENT_FAIL");
        }

        // Static fuel bounds / WCET (issue #262) diagnostics.
        if (!run_stderr_case("check-fuel-straightline-ok",
                             {"curlee", "check", rel_fuel_straightline_ok.string()},
                             dir / "check_fuel_straightline_ok.golden", true))
        {
            return 1;
        }
        if (!run_stderr_case("check-fuel-straightline-low",
                             {"curlee", "check", rel_fuel_straightline_low.string()},
                             dir / "check_fuel_straightline_low.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-fuel-loop-no-decreases",
                             {"curlee", "check", rel_fuel_loop_no_decreases.string()},
                             dir / "check_fuel_loop_no_decreases.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-fuel-callgraph-ok",
                             {"curlee", "check", rel_fuel_callgraph_ok.string()},
                             dir / "check_fuel_callgraph_ok.golden", true))
        {
            return 1;
        }
        if (!run_stderr_case("check-fuel-callgraph-low",
                             {"curlee", "check", rel_fuel_callgraph_low.string()},
                             dir / "check_fuel_callgraph_low.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-fuel-extern-ok",
                             {"curlee", "check", rel_fuel_extern_ok.string()},
                             dir / "check_fuel_extern_ok.golden", true))
        {
            return 1;
        }
        if (!run_stderr_case("check-fuel-extern-missing",
                             {"curlee", "check", rel_fuel_extern_missing.string()},
                             dir / "check_fuel_extern_missing.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-fuel-loop-shadowed-variant",
                             {"curlee", "check", rel_fuel_loop_shadowed_variant.string()},
                             dir / "check_fuel_loop_shadowed_variant.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-fuel-nested-call-low",
                             {"curlee", "check", rel_fuel_nested_call_low.string()},
                             dir / "check_fuel_nested_call_low.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-fuel-recursive",
                             {"curlee", "check", rel_fuel_recursive.string()},
                             dir / "check_fuel_recursive.golden", false))
        {
            return 1;
        }
        // Runtime-address physical writes (issue #285): regression for the
        // review finding that RuntimePhysWriteExpr fell through to the leaf
        // default (0 instructions). The write is now priced addr + value + 1,
        // so fuel 1 must fail and fuel 4 must pass.
        if (!run_stderr_case("check-fuel-phys-write-low",
                             {"curlee", "check", rel_fuel_phys_write_low.string()},
                             dir / "check_fuel_phys_write_low.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-fuel-phys-write-ok",
                             {"curlee", "check", rel_fuel_phys_write_ok.string()},
                             dir / "check_fuel_phys_write_ok.golden", true))
        {
            return 1;
        }

        // Assignment statements (issue #268) diagnostics.
        if (!run_stderr_case("check-assign-bounded-sum",
                             {"curlee", "check", rel_assign_bounded_sum.string()},
                             dir / "check_assign_bounded_sum.golden", true))
        {
            return 1;
        }
        if (!run_stderr_case("check-assign-type-mismatch",
                             {"curlee", "check", rel_assign_type_mismatch.string()},
                             dir / "check_assign_type_mismatch.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-assign-unknown",
                             {"curlee", "check", rel_assign_unknown.string()},
                             dir / "check_assign_unknown.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-assign-param",
                             {"curlee", "check", rel_assign_param.string()},
                             dir / "check_assign_param.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-assign-shadowed-invariant",
                             {"curlee", "check", rel_assign_shadowed_invariant.string()},
                             dir / "check_assign_shadowed_invariant.golden", false))
        {
            return 1;
        }
        // Regression for the #268 rework: the loop post-state carries the
        // loop-carried bindings into the continuation, so a false postcondition
        // over the final value is rejected (previously verified vacuously).
        if (!run_stderr_case("check-assign-loop-poststate",
                             {"curlee", "check", rel_assign_loop_poststate.string()},
                             dir / "check_assign_loop_poststate.golden", false))
        {
            return 1;
        }

        // Fixed-size arrays (issue #278) diagnostics.
        // Acceptance #1-#4 pass: `[T; N]` + indexed read/write, the fb.c
        // tool_queue ring pattern, rx_buf_state-style U8 bookkeeping, bounded
        // symbolic indices via loop invariants, read-over-write coherence.
        if (!run_stderr_case("check-array-ring",
                             {"curlee", "check", rel_array_ring.string()},
                             dir / "check_array_ring.golden", true))
        {
            return 1;
        }
        // Acceptance #2: a constant out-of-bounds index is rejected (read and
        // write), never silently compiled to C.
        if (!run_stderr_case("check-array-oob-const",
                             {"curlee", "check", rel_array_oob_const.string()},
                             dir / "check_array_oob_const.golden", false))
        {
            return 1;
        }
        // A constant negative index is always out of bounds.
        if (!run_stderr_case("check-array-oob-neg",
                             {"curlee", "check", rel_array_oob_neg.string()},
                             dir / "check_array_oob_neg.golden", false))
        {
            return 1;
        }
        // A symbolic index with no bounding facts cannot be proven in 0..N-1:
        // the verifier's per-access bounds obligation fails with a model note.
        if (!run_stderr_case("check-array-oob-unproven",
                             {"curlee", "check", rel_array_oob_unproven.string()},
                             dir / "check_array_oob_unproven.golden", false))
        {
            return 1;
        }
        // MVP rejection diagnostics: length mismatch, element-type mismatch on
        // write, multi-dimensional indexing, bare-array-as-value, arrays in
        // struct fields / params / return types / ghost snapshots, and
        // unsupported element types (Bool).
        if (!run_stderr_case("check-array-len-mismatch",
                             {"curlee", "check", rel_array_len_mismatch.string()},
                             dir / "check_array_len_mismatch.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-array-elem-mismatch",
                             {"curlee", "check", rel_array_elem_mismatch.string()},
                             dir / "check_array_elem_mismatch.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-array-reject-multi",
                             {"curlee", "check", rel_array_reject_multi.string()},
                             dir / "check_array_reject_multi.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-array-reject-bare",
                             {"curlee", "check", rel_array_reject_bare.string()},
                             dir / "check_array_reject_bare.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-array-reject-field",
                             {"curlee", "check", rel_array_reject_field.string()},
                             dir / "check_array_reject_field.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-array-reject-param",
                             {"curlee", "check", rel_array_reject_param.string()},
                             dir / "check_array_reject_param.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-array-reject-return",
                             {"curlee", "check", rel_array_reject_return.string()},
                             dir / "check_array_reject_return.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-array-reject-ghost",
                             {"curlee", "check", rel_array_reject_ghost.string()},
                             dir / "check_array_reject_ghost.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-array-reject-unsupported-elem",
                             {"curlee", "check", rel_array_reject_unsupported_elem.string()},
                             dir / "check_array_reject_unsupported_elem.golden", false))
        {
            return 1;
        }
        // `run` (VM bytecode) rejects arrays: they are freestanding-only in
        // the MVP (no VM storage or lowering for IndexExpr/ArrayLiteralExpr).
        if (!run_stderr_case("run-array-vm-reject",
                             {"curlee", "run", rel_run_array_vm_reject.string()},
                             dir / "run_array_vm_reject.golden", false))
        {
            return 1;
        }
        // ...including module-level arrays (issue #287): the scalar half of
        // module state runs in the VM (see run-global-state above), the array
        // half is freestanding-only, rejected by the VM emitter.
        if (!run_stderr_case("run-static-array-vm-reject",
                             {"curlee", "run", rel_run_static_array_vm_reject.string()},
                             dir / "run_static_array_vm_reject.golden", false))
        {
            return 1;
        }

        // Module-level mutable state (issue #287) diagnostics.
        // Acceptance criteria 1-4 pass (see check_static_state.curlee): a
        // module-level scalar and [T; N] array are declared, read, and
        // reassigned from multiple functions; the net_stack.c shape (scalar
        // phase/seq/port fields + a 256-byte response buffer) and the
        // vbe_state.c shape (a probe writing 4 scalar globals, read later by
        // a separate function) are expressible end-to-end.
        if (!run_stderr_case("check-static-state",
                             {"curlee", "check", rel_static_state.string()},
                             dir / "check_static_state.golden", true))
        {
            return 1;
        }
        // The verifier story is explicit (MVP scope): module-level state is
        // opaque per function (a fresh uninterpreted symbol per global), so
        // contracts mentioning a global are rejected with a hard diagnostic
        // rather than silently modeled — ensures, requires, and loop
        // invariants all get the module-state message.
        if (!run_stderr_case("check-static-contract-global",
                             {"curlee", "check", rel_static_contract_global.string()},
                             dir / "check_static_contract_global.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-static-requires-global",
                             {"curlee", "check", rel_static_requires_global.string()},
                             dir / "check_static_requires_global.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-static-invariant-global",
                             {"curlee", "check", rel_static_invariant_global.string()},
                             dir / "check_static_invariant_global.golden", false))
        {
            return 1;
        }
        // Front-end diagnostics: literal-only initializers, declared-type
        // matching (same adaptation rules as `let`), root-scope duplicates,
        // MVP type gate, array repeat-length matching, and the unsigned
        // scalar `let`-mirror rule (`static p: U8 = 0;` is rejected — only
        // U64 scalars and unsigned ARRAYS accept literals).
        if (!run_stderr_case("check-static-type-mismatch",
                             {"curlee", "check", rel_static_type_mismatch.string()},
                             dir / "check_static_type_mismatch.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-static-non-literal",
                             {"curlee", "check", rel_static_non_literal.string()},
                             dir / "check_static_non_literal.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-static-duplicate",
                             {"curlee", "check", rel_static_duplicate.string()},
                             dir / "check_static_duplicate.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-static-fn-conflict",
                             {"curlee", "check", rel_static_fn_conflict.string()},
                             dir / "check_static_fn_conflict.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-static-unsupported-type",
                             {"curlee", "check", rel_static_unsupported_type.string()},
                             dir / "check_static_unsupported_type.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-static-array-len-mismatch",
                             {"curlee", "check", rel_static_array_len_mismatch.string()},
                             dir / "check_static_array_len_mismatch.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-static-array-not-repeat",
                             {"curlee", "check", rel_static_array_not_repeat.string()},
                             dir / "check_static_array_not_repeat.golden", false))
        {
            return 1;
        }
        if (!run_stderr_case("check-static-u8-scalar",
                             {"curlee", "check", rel_static_u8_scalar.string()},
                             dir / "check_static_u8_scalar.golden", false))
        {
            return 1;
        }
        // Global arrays are still bounds-checked exactly like local arrays:
        // the VALUE of a global is opaque across calls, but a constant
        // out-of-bounds write is never silently compiled to C.
        if (!run_stderr_case("check-static-oob",
                             {"curlee", "check", rel_static_oob.string()},
                             dir / "check_static_oob.golden", false))
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
