// SPDX-License-Identifier: MIT
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

[[noreturn]] static void die(std::string_view message)
{
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
}

int main()
{
    const auto pid = static_cast<unsigned long>(::getpid());

    struct RunResult
    {
        int code = 0;
        std::string out;
        std::string err;
    };

    const auto write_fixture = [pid](std::string_view stem, std::string_view source_text)
    {
        const fs::path parent = fs::temp_directory_path() / "curlee_cli_tests";
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec)
        {
            die("failed to create temp directory");
        }

        const fs::path file = parent / (std::string(stem) + "_" + std::to_string(pid) + ".curlee");
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            die("failed to open fixture file");
        }
        output << source_text;
        if (!output.good())
        {
            die("failed to write fixture file");
        }
        return file;
    };

    const auto invoke = [](std::initializer_list<std::string> args_in)
    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());

        std::vector<std::string> args(args_in);
        std::vector<char*> argv;
        argv.reserve(args.size());
        for (std::string& arg : args)
        {
            argv.push_back(arg.data());
        }

        RunResult run;
        run.code = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        run.out = captured_out.str();
        run.err = captured_err.str();
        return run;
    };

    const auto require_contains = [](const std::string& text, std::string_view needle)
    {
        if (text.find(needle) != std::string::npos)
        {
            return;
        }
        die("expected output to contain required text");
    };

    const auto require_empty = [](const std::string& text, std::string_view label)
    {
        if (text.empty())
        {
            return;
        }
        die("expected empty " + std::string(label));
    };

    // lex: lexer error is surfaced as a diagnostic
    {
        const fs::path path = write_fixture("lex_error", "@\n");
        const RunResult run = invoke({"curlee", "lex", path.string()});
        if (run.code != 1)
        {
            die("expected error exit code for lex error");
        }
        require_empty(run.out, "stdout");
        require_contains(run.err, "error:");
        require_contains(run.err, "invalid character");
    }

    // parse: parser error is surfaced as one or more diagnostics
    {
        const fs::path path = write_fixture("parse_error", "fn\n");
        const RunResult run = invoke({"curlee", "parse", path.string()});
        if (run.code != 1)
        {
            die("expected error exit code for parse error");
        }
        require_empty(run.out, "stdout");
        require_contains(run.err, "error:");
    }

    // parse: lexer error is surfaced as a diagnostic (parse calls lex internally)
    {
        const fs::path path = write_fixture("parse_lex_error", "@\n");
        const RunResult run = invoke({"curlee", "parse", path.string()});
        if (run.code != 1)
        {
            die("expected error exit code for parse lex error");
        }
        require_empty(run.out, "stdout");
        require_contains(run.err, "error:");
        require_contains(run.err, "invalid character");
    }

    // parse: successful parse dumps AST
    {
        const fs::path path = write_fixture("parse_ok", "fn main() -> Int {\n  return 0;\n}\n");
        const RunResult run = invoke({"curlee", "parse", path.string()});
        if (run.code != 0)
        {
            die("expected success exit code for parse");
        }
        require_empty(run.err, "stderr");
        require_contains(run.out, "fn main");
        require_contains(run.out, "return");
    }

    // shorthand: `curlee file.curlee` behaves like `curlee run file.curlee`
    {
        const fs::path path =
            write_fixture("shorthand_run", "fn main() -> Int {\n  return 0;\n}\n");
        const RunResult run = invoke({"curlee", path.string()});
        if (run.code != 0)
        {
            die("expected success exit code for shorthand run");
        }
        require_empty(run.err, "stderr");
        require_contains(run.out, "curlee run: result 0");
    }

    return 0;
}
