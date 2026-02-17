#include <cstdlib>
#include <curlee/cli/cli.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
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

bool check_exit_code(const std::string& case_name, int rc, bool expect_zero)
{
    if (expect_zero && rc != 0)
    {
        std::cerr << "expected exit code 0 for " << case_name << "\n";
        return false;
    }
    if (!expect_zero && rc == 0)
    {
        std::cerr << "expected non-zero exit code for " << case_name << "\n";
        return false;
    }
    return true;
}

bool check_text_equal(const std::string& label,
                     const fs::path& golden_path,
                     const std::string& got,
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

bool run_stderr_case(const std::string& case_name,
                     std::vector<std::string> argv_storage,
                     const fs::path& err_golden_path,
                     bool expect_zero)
{
    const CapturedRun run = run_cli(std::move(argv_storage));
    if (!check_exit_code(case_name, run.rc, expect_zero))
    {
        return false;
    }

    const std::string expected_err = slurp(err_golden_path);
    return check_text_equal("stderr", err_golden_path, run.err, expected_err);
}

bool run_stdio_case(const std::string& case_name,
                    std::vector<std::string> argv_storage,
                    const fs::path& out_golden_path,
                    const fs::path& err_golden_path,
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

} // namespace

int main(int argc, char** argv)
{
    if (argc != 8)
    {
        std::cerr
            << "usage: curlee_cli_diagnostics_golden_tests <tests/cli_diagnostics-dir> "
               "<fake-python-runner-error> <fake-python-runner-hang> <fake-python-runner-spam> "
               "<fake-python-runner-env-check> <fake-python-runner-sandbox-required> "
               "<fake-bwrap>\n";
        return 2;
    }

    const fs::path dir = fs::path(argv[1]);
    const std::string fake_runner_error = argv[2];
    const std::string fake_runner_hang = argv[3];
    const std::string fake_runner_spam = argv[4];
    const std::string fake_runner_env_check = argv[5];
    const std::string fake_runner_sandbox_required = argv[6];
    const std::string fake_bwrap = argv[7];
    (void)fake_runner_error;
    (void)fake_runner_hang;
    (void)fake_runner_spam;
    (void)fake_runner_env_check;
    (void)fake_runner_sandbox_required;
    (void)fake_bwrap;

    const fs::path rel_missing_file = "tests/fixtures/does_not_exist.cur";
    const fs::path rel_requires_divide = "tests/fixtures/check_requires_divide.curlee";
    const fs::path rel_refinement_implies = "tests/fixtures/check_refinement_implies.curlee";
    const fs::path rel_if_path_sensitive = "tests/fixtures/check_requires_if_path_sensitive.curlee";
    const fs::path rel_while_condition_fact =
        "tests/fixtures/check_requires_while_condition_fact.curlee";
    const fs::path rel_unknown_name = "tests/fixtures/check_unknown_name.curlee";
    const fs::path rel_type_error = "tests/fixtures/check_type_error.curlee";
    const fs::path rel_if_condition_type_error =
        "tests/fixtures/check_if_condition_type_error.curlee";
    const fs::path rel_struct_ok = "tests/fixtures/check_struct_ok.curlee";
    const fs::path rel_ensures_fail = "tests/fixtures/check_ensures_fail.curlee";
    const fs::path rel_run_success = "tests/fixtures/run_success.curlee";
    const fs::path rel_run_missing_stdout_cap = "tests/fixtures/r.curlee";

    try
    {
        if (!run_stderr_case("missing-file",
                             {"curlee", "lex", rel_missing_file.string()},
                             dir / "missing_file.golden",
                             false))
        {
            return 1;
        }

        if (!run_stderr_case("check-requires-divide",
                             {"curlee", "check", rel_requires_divide.string()},
                             dir / "check_requires_divide.golden",
                             false))
        {
            return 1;
        }

        if (!run_stderr_case("check-refinement-implies",
                             {"curlee", "check", rel_refinement_implies.string()},
                             dir / "check_refinement_implies.golden",
                             true))
        {
            return 1;
        }

        if (!run_stderr_case("check-requires-if-path-sensitive",
                             {"curlee", "check", rel_if_path_sensitive.string()},
                             dir / "check_requires_if_path_sensitive.golden",
                             true))
        {
            return 1;
        }

        if (!run_stderr_case("check-requires-while-condition-fact",
                             {"curlee", "check", rel_while_condition_fact.string()},
                             dir / "check_requires_while_condition_fact.golden",
                             true))
        {
            return 1;
        }

        if (!run_stderr_case("check-unknown-name",
                             {"curlee", "check", rel_unknown_name.string()},
                             dir / "check_unknown_name.golden",
                             false))
        {
            return 1;
        }

        if (!run_stderr_case("check-type-error",
                             {"curlee", "check", rel_type_error.string()},
                             dir / "check_type_error.golden",
                             false))
        {
            return 1;
        }

        if (!run_stderr_case("check-if-condition-type-error",
                             {"curlee", "check", rel_if_condition_type_error.string()},
                             dir / "check_if_condition_type_error.golden",
                             false))
        {
            return 1;
        }

        if (!run_stderr_case("check-struct-ok",
                             {"curlee", "check", rel_struct_ok.string()},
                             dir / "check_struct_ok.golden",
                             true))
        {
            return 1;
        }

        if (!run_stderr_case("run-requires-divide",
                             {"curlee", "run", rel_requires_divide.string()},
                             dir / "run_requires_divide.golden",
                             false))
        {
            return 1;
        }

        if (!run_stderr_case("check-ensures-fail",
                             {"curlee", "check", rel_ensures_fail.string()},
                             dir / "check_ensures_fail.golden",
                             false))
        {
            return 1;
        }

        if (!run_stderr_case("run-ensures-fail",
                             {"curlee", "run", rel_ensures_fail.string()},
                             dir / "run_ensures_fail.golden",
                             false))
        {
            return 1;
        }

        if (!run_stderr_case("run-struct-not-runnable",
                             {"curlee", "run", rel_struct_ok.string()},
                             dir / "run_struct_not_runnable.golden",
                             false))
        {
            return 1;
        }

        if (!run_stderr_case("run-missing-stdout-cap",
                             {"curlee", "run", rel_run_missing_stdout_cap.string()},
                             dir / "run_missing_stdout_cap.golden",
                             false))
        {
            return 1;
        }

        if (!run_stdio_case("run-with-stdout-cap",
                            {"curlee", "run", "--cap", "io.stdout",
                             rel_run_missing_stdout_cap.string()},
                            dir / "run_with_stdout_cap.stdout.golden",
                            dir / "run_with_stdout_cap.stderr.golden",
                            true))
        {
            return 1;
        }

        if (!run_stdio_case("run-success",
                            {"curlee", "run", rel_run_success.string()},
                            dir / "run_success.stdout.golden",
                            dir / "run_success.stderr.golden",
                            true))
        {
            return 1;
        }

        if (!run_stdio_case("run-success-with-cap",
                            {"curlee", "run", "--cap", "python.ffi", rel_run_success.string()},
                            dir / "run_success.stdout.golden",
                            dir / "run_success.stderr.golden",
                            true))
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
