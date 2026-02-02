#include <cstdlib>
#include <curlee/bundle/bundle.h>
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

static fs::path temp_path(const std::string& name)
{
    return fs::temp_directory_path() / name;
}

int main()
{
    const fs::path fixture =
        find_repo_relative(fs::path("tests") / "fixtures" / "run_success.curlee");

    // top-level: help flag variants
    {
        for (const std::string& flag : {std::string("-h"), std::string("help")})
        {
            std::string out;
            std::string err;
            const int rc = run_cli_capture({"curlee", flag}, out, err);
            if (rc != 0)
            {
                fail("expected help variant to exit 0");
            }
            if (out.find("usage:") == std::string::npos)
            {
                fail("expected help variant stdout to contain usage");
            }
        }
    }

    // top-level: version flag variant
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "version"}, out, err);
        if (rc != 0)
        {
            fail("expected version to exit 0");
        }
        if (!err.empty())
        {
            fail("expected version stderr to be empty");
        }
        if (out.find("curlee ") == std::string::npos || out.find("sha=") == std::string::npos)
        {
            fail("expected version output to contain 'curlee ' and 'sha='");
        }
    }

    // parse: missing <file.curlee> (covers generic <command> <file.curlee> usage error)
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "parse"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for missing parse file");
        }
        expect_contains(err, "error: expected <command> <file.curlee>", "stderr");
    }

    // parse: missing file path is surfaced as an error diagnostic
    {
        std::string out;
        std::string err;
        const int rc =
            run_cli_capture({"curlee", "parse", "/tmp/curlee_missing_12345.curlee"}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for missing file");
        }
        expect_contains(err, "error: failed to open file", "stderr");
    }

    // run: missing <file.curlee>
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "run"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for missing run file");
        }
        expect_contains(err, "error: expected <file.curlee>", "stderr");
    }

    // run: unknown option
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "run", "--nope", "x.curlee"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for unknown option");
        }
        expect_contains(err, "error: unknown option: --nope", "stderr");
    }

    // run: missing value after --cap
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "run", "--cap"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for missing --cap arg");
        }
        expect_contains(err, "error: expected capability name after --cap", "stderr");
    }

    // run: --capability alias
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture(
            {"curlee", "run", "--capability", "io:stdout", "--fuel", "0"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for missing run file (with --capability)");
        }
        expect_contains(err, "error: expected <file.curlee>", "stderr");
    }

    // run: empty --cap=
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "run", "--cap=", "x.curlee"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for empty --cap=");
        }
        expect_contains(err, "error: expected capability name after --cap=", "stderr");
    }

    // run: non-empty --cap= is accepted (even if the run itself fails later)
    {
        std::string out;
        std::string err;
        const int rc =
            run_cli_capture({"curlee", "run", "--cap=io:stdout", "--fuel", "0"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for missing run file (with --cap=)");
        }
        expect_contains(err, "error: expected <file.curlee>", "stderr");
    }

    // run: missing value after --fuel
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "run", "--fuel"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for missing --fuel arg");
        }
        expect_contains(err, "error: expected integer after --fuel", "stderr");
    }

    // run: invalid --fuel
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "run", "--fuel", "abc", "x.curlee"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for invalid --fuel value");
        }
        expect_contains(err, "error: expected non-negative integer for --fuel", "stderr");
    }

    // run: invalid --fuel=
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "run", "--fuel=abc", "x.curlee"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for invalid --fuel=");
        }
        expect_contains(err, "error: expected non-negative integer for --fuel=", "stderr");
    }

    // run: empty --fuel=
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "run", "--fuel=", "x.curlee"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for empty --fuel=");
        }
        expect_contains(err, "error: expected non-negative integer for --fuel=", "stderr");
    }

    // run: missing value after --bundle
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "run", "--bundle"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for missing --bundle arg");
        }
        expect_contains(err, "error: expected bundle path after --bundle", "stderr");
    }

    // run: empty --bundle=
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "run", "--bundle=", "x.curlee"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for empty --bundle=");
        }
        expect_contains(err, "error: expected bundle path after --bundle=", "stderr");
    }

    // run: duplicate --bundle
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture(
            {"curlee", "run", "--bundle", "a.bundle", "--bundle", "b.bundle"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for duplicate --bundle");
        }
        expect_contains(err, "error: expected a single --bundle <file.bundle>", "stderr");
    }

    // run: duplicate --bundle= form
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture(
            {"curlee", "run", "--bundle=a.bundle", "--bundle=b.bundle", "x.curlee"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for duplicate --bundle=");
        }
        expect_contains(err, "error: expected a single --bundle <file.bundle>", "stderr");
    }

    // run: multiple positional <file.curlee>
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "run", "a.curlee", "b.curlee"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for multiple <file.curlee>");
        }
        expect_contains(err, "error: expected a single <file.curlee>", "stderr");
    }

    // run: bundle load failure path
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture(
            {"curlee", "run", "--bundle", "definitely_missing.bundle", "x.curlee"}, out, err);
        if (rc != 1)
        {
            fail("expected error exit code for failed bundle load");
        }
        expect_contains(err, "error: failed to load bundle: failed to open bundle", "stderr");
    }

    // fmt: wrong args
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "fmt"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for fmt missing args");
        }
        expect_contains(err, "error: expected curlee fmt", "stderr");
    }

    // bundle: wrong args
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "bundle"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for bundle missing args");
        }
        expect_contains(err, "error: expected curlee bundle", "stderr");
    }

    // bundle: unknown subcommand (requires a valid bundle file to reach the branch)
    {
        const fs::path bundle_path = temp_path("curlee_cli_bundle_unknown_subcmd.bundle");
        fs::remove(bundle_path);

        curlee::bundle::Bundle bundle;
        bundle.manifest.capabilities = {"io:stdout"};
        bundle.manifest.imports = {};
        bundle.manifest.proof = std::nullopt;
        bundle.bytecode = {0x01, 0x02, 0x03, 0x04};

        const auto write_err = curlee::bundle::write_bundle(bundle_path.string(), bundle);
        if (!write_err.message.empty())
        {
            fail("expected bundle write to succeed");
        }

        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "bundle", "wat", bundle_path.string()}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for unknown bundle subcommand");
        }
        expect_contains(err, "error: unknown bundle subcommand: wat", "stderr");

        fs::remove(bundle_path);
    }

    // cmd_read_only: unknown command requires a valid source file to load.
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture({"curlee", "wat", fixture.string()}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for unknown read-only command");
        }
        expect_contains(err, "error: unknown command: wat", "stderr");
    }

    std::cout << "OK\n";
    return 0;
}
