// SPDX-License-Identifier: MIT
#include <cstdlib>
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

static void write_all(const fs::path& path, const std::string& contents)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        fail("failed to open file for writing: " + path.string());
    }
    out << contents;
}

static void expect_contains(const std::string& haystack, const std::string& needle,
                            const std::string& what)
{
    if (haystack.find(needle) == std::string::npos)
    {
        fail("expected " + what + " to contain '" + needle + "'\n---\n" + haystack + "\n---");
    }
}

int main()
{
    const fs::path run_success =
        find_repo_relative(fs::path("tests") / "fixtures" / "run_success.curlee");
    const fs::path infinite =
        find_repo_relative(fs::path("tests") / "fixtures" / "run_infinite_loop.curlee");
    const fs::path fuel_drift =
        find_repo_relative(fs::path("tests") / "fixtures" / "run_fuel_drift.curlee");
    const fs::path type_error =
        find_repo_relative(fs::path("tests") / "fixtures" / "check_type_error.curlee");
    const fs::path rng_seeded =
        find_repo_relative(fs::path("tests") / "fixtures" / "run_rng_seeded.curlee");

    // diag-format parsing and option-gating behavior.
    {
        std::string out;
        std::string err;

        int rc = run_cli_capture({"curlee", "check", "--diag-format"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for missing --diag-format value");
        }
        expect_contains(err, "expected format after --diag-format", "stderr");

        rc = run_cli_capture({"curlee", "check", "--diag-format", "yaml", type_error.string()}, out,
                             err);
        if (rc != 2)
        {
            fail("expected usage exit code for invalid --diag-format value");
        }
        expect_contains(err, "expected --diag-format to be one of: text, json", "stderr");

        rc = run_cli_capture({"curlee", "check", "--diag-format=yaml", type_error.string()}, out,
                             err);
        if (rc != 2)
        {
            fail("expected usage exit code for invalid --diag-format= value");
        }
        expect_contains(err, "expected --diag-format= to be one of: text, json", "stderr");
    }

    // Structured diagnostics text mode is explicit and stable.
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture(
            {"curlee", "check", "--diag-format", "text", type_error.string()}, out, err);
        if (rc == 0)
        {
            fail("expected check --diag-format text to fail on type error fixture");
        }
        expect_contains(err, "error:", "stderr");
    }

    // Structured diagnostics contract: JSON output includes stable fields and span object.
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture(
            {"curlee", "check", "--diag-format=json", type_error.string()}, out, err);
        if (rc == 0)
        {
            fail("expected check --diag-format json to fail on type error fixture");
        }
        expect_contains(err, "\"kind\":\"diagnostic\"", "stderr");
        expect_contains(err, "\"severity\":\"error\"", "stderr");
        expect_contains(err, "\"file\":", "stderr");
        expect_contains(err, "\"span\":{\"start\":", "stderr");
    }

    // Structured diagnostics JSON includes notes for capability-gated bundle runs.
    {
        const fs::path dir = fs::temp_directory_path() / "curlee_cli_profile_bundle";
        fs::remove_all(dir);
        fs::create_directories(dir);

        const fs::path entry = dir / "main.curlee";
        const fs::path bundle = dir / "main.bundle";
        write_all(entry,
                  R"(fn main() -> Int {
  return 0;
}
)");

        std::string out;
        std::string err;
        int rc = run_cli_capture(
            {"curlee", "bundle", "build", "--cap", "io.stdout", entry.string(), bundle.string()},
            out, err);
        if (rc != 0)
        {
            fail("expected bundle build to succeed for diagnostics/profile tests; stderr=" + err);
        }

        rc = run_cli_capture(
            {"curlee", "run", "--diag-format", "json", "--bundle", bundle.string(), entry.string()},
            out, err);
        if (rc == 0)
        {
            fail("expected bundle run without required cap to fail");
        }
        expect_contains(err, "\"notes\":[{", "stderr");
        expect_contains(err, "bundle manifest requires capability", "stderr");

        fs::remove_all(dir);
    }

    // Profile text output for successful run.
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture(
            {"curlee", "run", "--profile", "--fuel", "1000", run_success.string()}, out, err);
        if (rc != 0)
        {
            fail("expected run --profile to succeed; stderr=" + err);
        }
        expect_contains(out, "curlee run: result", "stdout");
        expect_contains(out, "curlee profile: steps=", "stdout");
        expect_contains(out, "fuel_used=", "stdout");
        expect_contains(out, "rng_seed=none", "stdout");
        expect_contains(out, "ok=true", "stdout");
    }

    // Profile JSON output for successful run.
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture(
            {"curlee", "run", "--profile-format", "json", run_success.string()}, out, err);
        if (rc != 0)
        {
            fail("expected run --profile-format json to succeed; stderr=" + err);
        }
        expect_contains(out, "\"kind\":\"profile\"", "stdout");
        expect_contains(out, "\"steps\":", "stdout");
        expect_contains(out, "\"fuel_used\":", "stdout");
        expect_contains(out, "\"rng_seed\":null", "stdout");
        expect_contains(out, "\"ok\":true", "stdout");
    }

    // Seeded RNG run: deterministic result + profile seed metadata.
    {
        std::string out_a;
        std::string err_a;
        std::string out_b;
        std::string err_b;

        const int rc_a = run_cli_capture({"curlee", "run", "--cap", "rng.seeded", "--seed", "123",
                                          "--profile-format", "json", rng_seeded.string()},
                                         out_a, err_a);
        if (rc_a != 0)
        {
            fail("expected seeded rng run to succeed; stderr=" + err_a);
        }

        const int rc_b = run_cli_capture({"curlee", "run", "--cap", "rng.seeded", "--seed", "123",
                                          "--profile-format", "json", rng_seeded.string()},
                                         out_b, err_b);
        if (rc_b != 0)
        {
            fail("expected repeated seeded rng run to succeed; stderr=" + err_b);
        }

        if (out_a != out_b)
        {
            fail("expected deterministic run/profile output for same seed and program");
        }
        expect_contains(out_a, "\"rng_seed\":123", "stdout");
    }

    // Missing seed produces deterministic diagnostic when rng capability is granted.
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture(
            {"curlee", "run", "--cap", "rng.seeded", rng_seeded.string()}, out, err);
        if (rc == 0)
        {
            fail("expected rng run without --seed to fail");
        }
        expect_contains(err, "missing RNG seed; pass --seed <n>", "stderr");
    }

    // Missing rng capability is diagnosed before execution.
    {
        std::string out;
        std::string err;
        const int rc =
            run_cli_capture({"curlee", "run", "--seed", "123", rng_seeded.string()}, out, err);
        if (rc == 0)
        {
            fail("expected rng run without capability to fail");
        }
        expect_contains(err, "capability not granted: rng.seeded", "stderr");
    }

    // Profile text output via explicit --profile-format text.
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture(
            {"curlee", "run", "--profile-format", "text", run_success.string()}, out, err);
        if (rc != 0)
        {
            fail("expected run --profile-format text to succeed; stderr=" + err);
        }
        expect_contains(out, "curlee profile: steps=", "stdout");
    }

    // Profile output is still emitted in failure mode (deterministic out-of-fuel).
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture(
            {"curlee", "run", "--profile", "--fuel", "10", infinite.string()}, out, err);
        if (rc == 0)
        {
            fail("expected run --profile to fail under low fuel");
        }
        expect_contains(err, "out of fuel", "stderr");
        expect_contains(err, "curlee profile: steps=", "stderr");
        expect_contains(err, "ok=false", "stderr");
    }

    // Profile JSON output on failure path.
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture(
            {"curlee", "run", "--profile-format=json", "--fuel", "10", infinite.string()}, out,
            err);
        if (rc == 0)
        {
            fail("expected run --profile-format=json to fail under low fuel");
        }
        expect_contains(err, "\"kind\":\"profile\"", "stderr");
        expect_contains(err, "\"ok\":false", "stderr");
    }

    // Bundle run emits profile output for success and failure paths.
    {
        const fs::path dir = fs::temp_directory_path() / "curlee_cli_profile_bundle_runtime";
        fs::remove_all(dir);
        fs::create_directories(dir);

        const fs::path entry_ok = dir / "main_ok.curlee";
        const fs::path bundle_ok = dir / "main_ok.bundle";
        const fs::path bundle_fail = dir / "main_fail.bundle";
        write_all(entry_ok,
                  R"(fn main() -> Int {
  return 7;
}
)");

        std::string out;
        std::string err;
        int rc = run_cli_capture({"curlee", "bundle", "build", "--cap", "io.stdout",
                                  entry_ok.string(), bundle_ok.string()},
                                 out, err);
        if (rc != 0)
        {
            fail("expected success bundle build to succeed; stderr=" + err);
        }

        rc = run_cli_capture({"curlee", "bundle", "build", infinite.string(), bundle_fail.string()},
                             out, err);
        if (rc != 0)
        {
            fail("expected failing bundle fixture build to succeed; stderr=" + err);
        }

        rc = run_cli_capture({"curlee", "run", "--profile", "--cap", "io.stdout", "--bundle",
                              bundle_ok.string(), entry_ok.string()},
                             out, err);
        if (rc != 0)
        {
            fail("expected bundle run --profile success path to succeed; stderr=" + err);
        }
        expect_contains(out, "curlee profile: steps=", "stdout");
        expect_contains(out, "ok=true", "stdout");

        rc = run_cli_capture({"curlee", "run", "--profile", "--fuel", "10", "--bundle",
                              bundle_fail.string(), infinite.string()},
                             out, err);
        if (rc == 0)
        {
            fail("expected bundle run --profile failure path to fail under low fuel");
        }
        expect_contains(err, "out of fuel", "stderr");
        expect_contains(err, "curlee profile: steps=", "stderr");
        expect_contains(err, "ok=false", "stderr");

        fs::remove_all(dir);
    }

    // Bundle run forwards --seed into VM RNG state.
    {
        const fs::path dir = fs::temp_directory_path() / "curlee_cli_profile_bundle_rng";
        fs::remove_all(dir);
        fs::create_directories(dir);

        const fs::path entry = dir / "main_rng.curlee";
        const fs::path bundle = dir / "main_rng.bundle";
        write_all(entry,
                  R"(fn main(rng: cap rng.seeded) -> Int {
  return __rng_next_int(1000, rng);
}
)");

        std::string out;
        std::string err;
        int rc = run_cli_capture(
            {"curlee", "bundle", "build", "--cap", "rng.seeded", entry.string(), bundle.string()},
            out, err);
        if (rc != 0)
        {
            fail("expected rng bundle build to succeed; stderr=" + err);
        }

        std::string out_a;
        std::string err_a;
        std::string out_b;
        std::string err_b;
        rc = run_cli_capture({"curlee", "run", "--cap", "rng.seeded", "--seed", "55",
                              "--profile-format", "json", "--bundle", bundle.string(),
                              entry.string()},
                             out_a, err_a);
        if (rc != 0)
        {
            fail("expected seeded rng bundle run to succeed; stderr=" + err_a);
        }

        rc = run_cli_capture({"curlee", "run", "--cap", "rng.seeded", "--seed", "55",
                              "--profile-format", "json", "--bundle", bundle.string(),
                              entry.string()},
                             out_b, err_b);
        if (rc != 0)
        {
            fail("expected repeated seeded rng bundle run to succeed; stderr=" + err_b);
        }

        if (out_a != out_b)
        {
            fail("expected deterministic bundle run output for same seed");
        }
        expect_contains(out_a, "\"rng_seed\":55", "stdout");

        fs::remove_all(dir);
    }

    // Option gating: invalid profile format fails with usage error.
    {
        std::string out;
        std::string err;
        int rc = run_cli_capture({"curlee", "run", "--profile-format"}, out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for missing --profile-format value");
        }
        expect_contains(err, "expected format after --profile-format", "stderr");

        rc = run_cli_capture({"curlee", "run", "--profile-format", "yaml", run_success.string()},
                             out, err);
        if (rc != 2)
        {
            fail("expected usage exit code for invalid --profile-format");
        }
        expect_contains(err, "expected --profile-format to be one of: text, json", "stderr");

        rc = run_cli_capture({"curlee", "run", "--profile-format=yaml", run_success.string()}, out,
                             err);
        if (rc != 2)
        {
            fail("expected usage exit code for invalid --profile-format=");
        }
        expect_contains(err, "expected --profile-format= to be one of: text, json", "stderr");
    }

    // Static-fuel drift oracle (issue #262): the profile-vs-declared warning is
    // advisory and NON-gating. After the soundness fixes (shadowed loop variants
    // fail closed), a fuel-annotated main that passes static verification cannot
    // exceed its declared bound at runtime (the cost model is now a sound
    // over-approximation), so a successful within-bound run must succeed with NO
    // warning, and the mechanism never changes exit semantics.
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture(
            {"curlee", "run", "--profile", "--fuel", "1000", fuel_drift.string()}, out, err);
        if (rc != 0)
        {
            fail("expected drift fixture to run successfully; stderr=" + err);
        }
        expect_contains(out, "curlee profile:", "stdout");
        if (err.find("curlee warning: runtime fuel_used") != std::string::npos)
        {
            fail("expected no fuel drift warning on a within-bound successful run");
        }
    }

    // The out-of-fuel failure path still emits the profile (and any advisory
    // warning) without the warning being gating: the run fails due to the VM
    // sandbox, and the profile reflects the consumed fuel.
    {
        std::string out;
        std::string err;
        const int rc = run_cli_capture(
            {"curlee", "run", "--profile", "--fuel", "2", fuel_drift.string()}, out, err);
        if (rc == 0)
        {
            fail("expected drift fixture to exhaust fuel under a tiny sandbox limit");
        }
        expect_contains(err, "out of fuel", "stderr");
        expect_contains(err, "curlee profile: steps=2 fuel_limit=2 fuel_used=2", "stderr");
    }

    std::cout << "OK\n";
    return 0;
}
