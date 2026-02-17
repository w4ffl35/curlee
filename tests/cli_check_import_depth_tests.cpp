#include <cerrno>
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

struct CliRun
{
    int exit_code = 0;
    std::string stdout_text;
    std::string stderr_text;
};

[[noreturn]] static void die(std::string_view message)
{
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
}

static void write_text(const fs::path& destination, std::string_view text)
{
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec)
    {
        die("unable to create parent directories for " + destination.string());
    }

    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        die("unable to open " + destination.string());
    }

    output << text;
    if (!output.good())
    {
        die("unable to write " + destination.string());
    }
}

static CliRun run_check_command(const fs::path& entry_file)
{
    std::ostringstream out_capture;
    std::ostringstream err_capture;
    std::streambuf* prior_out = std::cout.rdbuf(out_capture.rdbuf());
    std::streambuf* prior_err = std::cerr.rdbuf(err_capture.rdbuf());

    std::vector<std::string> owned_args = {"curlee", "check", entry_file.string()};
    std::vector<char*> argv;
    argv.reserve(owned_args.size());
    for (std::string& item : owned_args)
    {
        argv.push_back(item.data());
    }

    CliRun run;
    run.exit_code = curlee::cli::run(static_cast<int>(argv.size()), argv.data());

    std::cout.rdbuf(prior_out);
    std::cerr.rdbuf(prior_err);
    run.stdout_text = out_capture.str();
    run.stderr_text = err_capture.str();
    return run;
}

static void require_contains_text(
    const std::string& text,
    std::string_view needle,
    std::string_view channel_name
)
{
    if (text.find(needle) == std::string::npos)
    {
        die("expected " + std::string(channel_name) + " to include '" + std::string(needle) + "'");
    }
}

static void require_empty_text(const std::string& text, std::string_view channel_name)
{
    if (!text.empty())
    {
        die("expected empty " + std::string(channel_name));
    }
}

static std::string fn_body_return_zero(const std::string& name)
{
    return "fn " + name + "() -> Int {\n  return 0;\n}\n";
}

int main()
{
    // check: hit the max import depth guard (kMaxImportDepth=64)
    const auto pid = static_cast<unsigned long>(::getpid());
    const fs::path temp_root =
        fs::temp_directory_path() / ("curlee_cli_depth_" + std::to_string(pid));

    write_text(temp_root / "entry.curlee", "import m0;\n\nfn main() -> Int {\n  return 0;\n}\n");

    for (int depth = 0; depth < 64; ++depth)
    {
        const std::string current = "m" + std::to_string(depth);
        const std::string next = "m" + std::to_string(depth + 1);
        const std::string payload =
            "import " + next + ";\n\n" + fn_body_return_zero("f" + std::to_string(depth));
        write_text(temp_root / (current + ".curlee"), payload);
    }
    write_text(temp_root / "m64.curlee", fn_body_return_zero("f64"));

    const CliRun run = run_check_command(temp_root / "entry.curlee");
    if (run.exit_code != 1)
    {
        die("expected non-zero exit for deep import graph");
    }

    require_empty_text(run.stdout_text, "stdout");
    require_contains_text(run.stderr_text, "error:", "stderr");
    require_contains_text(run.stderr_text, "import graph too deep", "stderr");

    return 0;
}
