// SPDX-License-Identifier: MIT
#include <curlee/cli/cli.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

[[noreturn]] static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
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

static std::filesystem::path temp_path(const std::string& name)
{
    return std::filesystem::temp_directory_path() / name;
}

static std::string slurp(const std::filesystem::path& path)
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

static void write_all(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        throw std::runtime_error("failed to open file for writing: " + path.string());
    }
    out << contents;
}

static void expect_error_contains(const std::vector<std::string>& argv_storage,
                                  std::string_view needle)
{
    std::string out;
    std::string err;
    const int rc = run_cli(argv_storage, out, err);
    if (rc == 0)
    {
        fail("expected command to fail");
    }
    if (err.find(needle) == std::string::npos)
    {
        fail("expected stderr to contain: " + std::string(needle) + "\nactual stderr: " + err);
    }
}

int main()
{
    namespace fs = std::filesystem;

    unsetenv("CURLEE_STDLIB_ROOT");

    const fs::path dir = temp_path("curlee_cli_deps_lock_tests");
    fs::remove_all(dir);
    fs::create_directories(dir / "lib");

    const fs::path entry = dir / "main.curlee";
    const fs::path mod_a = dir / "lib" / "a.curlee";
    const fs::path mod_b = dir / "lib" / "b.curlee";
    const fs::path lock_path = dir / "deps.lock";

    {
        const std::vector<std::pair<std::vector<std::string>, std::string>> bad_argvs = {
            {{"curlee", "deps"}, "expected curlee deps <lock|verify>"},
            {{"curlee", "deps", "nope"}, "unknown deps subcommand"},
            {{"curlee", "deps", "lock", "--root"}, "expected path after --root"},
            {{"curlee", "deps", "lock", "--root="}, "expected path after --root="},
            {{"curlee", "deps", "lock", "--stdlib-root"}, "expected path after --stdlib-root"},
            {{"curlee", "deps", "lock", "--stdlib-root="},
             "expected path after --stdlib-root="},
            {{"curlee", "deps", "lock", "--nope", "a.curlee", "deps.lock"},
             "unknown option: --nope"},
            {{"curlee", "deps", "lock", "only-entry.curlee"}, "expected curlee deps lock"},
            {{"curlee", "deps", "verify", "only-entry.curlee"}, "expected curlee deps verify"},
        };

        for (const auto& [argv_storage, needle] : bad_argvs)
        {
            expect_error_contains(argv_storage, needle);
        }
    }

    write_all(entry,
              R"(import lib.b;
import lib.a;

fn main() -> Int {
  return lib.a.one() + lib.b.two();
}
)");

    write_all(mod_a,
              R"(fn one() -> Int {
  return 1;
}
)");

    write_all(mod_b,
              R"(fn two() -> Int {
  return 2;
}
)");

    {
        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "deps", "lock", entry.string(), lock_path.string()},
                               out, err);
        if (rc != 0)
        {
            fail("expected deps lock without --root to succeed; stderr: " + err);
        }

        const int verify_rc =
            run_cli({"curlee", "deps", "verify", entry.string(), lock_path.string()}, out, err);
        if (verify_rc != 0)
        {
            fail("expected deps verify without --root to succeed; stderr: " + err);
        }
    }

    {
        std::string out;
        std::string err;
        const int rc =
            run_cli({"curlee", "deps", "lock", "--root", dir.string(), "main.curlee",
                     lock_path.string()},
                    out, err);
        if (rc != 0)
        {
            fail("expected deps lock to succeed; stderr: " + err);
        }
        if (out.find("curlee deps lock: wrote ") == std::string::npos)
        {
            fail("expected deps lock stdout to mention output path");
        }
        if (!err.empty())
        {
            fail("expected deps lock stderr to be empty");
        }

        const std::string lock_body = slurp(lock_path);
        if (lock_body.find("CURLEE_DEPS_LOCK\n") == std::string::npos)
        {
            fail("expected lockfile header");
        }

        const std::size_t a_pos = lock_body.find("entry/lib/a:");
        const std::size_t b_pos = lock_body.find("entry/lib/b:");
        if (a_pos == std::string::npos || b_pos == std::string::npos)
        {
            fail("expected lockfile imports to include entry/lib/a and entry/lib/b pins");
        }
        if (a_pos > b_pos)
        {
            fail("expected lockfile imports to be sorted deterministically by module id");
        }
    }

    {
        std::string out;
        std::string err;
        const int rc =
            run_cli({"curlee", "deps", "lock", "--root", dir.string(), "main.curlee",
                     dir.string()},
                    out, err);
        if (rc == 0)
        {
            fail("expected deps lock to fail when lockfile path is a directory");
        }
        if (err.find("deps lock failed: failed to open lockfile for writing") ==
            std::string::npos)
        {
            fail("expected lockfile write failure diagnostic");
        }
    }

    {
        std::string out;
        std::string err;
        const int rc =
            run_cli({"curlee", "deps", "verify", "--root", dir.string(), "main.curlee",
                     lock_path.string()},
                    out, err);
        if (rc != 0)
        {
            fail("expected deps verify to succeed; stderr: " + err);
        }
        if (out != "curlee deps verify: ok\n")
        {
            fail("unexpected deps verify stdout: " + out);
        }
        if (!err.empty())
        {
            fail("expected deps verify stderr to be empty");
        }
    }

    write_all(mod_b,
              R"(fn two() -> Int {
  return 9;
}
)");

    {
        std::string out;
        std::string err;
        const int rc =
            run_cli({"curlee", "deps", "verify", "--root", dir.string(), "main.curlee",
                     lock_path.string()},
                    out, err);
        if (rc == 0)
        {
            fail("expected deps verify to fail after dependency drift");
        }
        if (err.find("deps verify failed: imports mismatch") == std::string::npos)
        {
            fail("expected deps verify mismatch diagnostic");
        }
    }

    {
        std::string lock_body = slurp(lock_path);
        const auto imports_pos = lock_body.find("imports=");
        if (imports_pos == std::string::npos)
        {
            fail("expected imports line in lockfile");
        }

        const auto imports_end = lock_body.find('\n', imports_pos);
        if (imports_end == std::string::npos)
        {
            fail("expected newline after imports line");
        }

        lock_body.insert(imports_end, ",entry/ghost:deadbeef");
        write_all(lock_path, lock_body);

        std::string out;
        std::string err;
        const int rc =
            run_cli({"curlee", "deps", "verify", "--root", dir.string(), "main.curlee",
                     lock_path.string()},
                    out, err);
        if (rc == 0)
        {
            fail("expected deps verify to fail for mismatched import count");
        }
        if (err.find("deps verify failed: imports mismatch") == std::string::npos)
        {
            fail("expected imports mismatch diagnostic for extra lock pin");
        }

        run_cli({"curlee", "deps", "lock", "--root", dir.string(), "main.curlee",
                 lock_path.string()},
                out, err);
    }

    write_all(mod_b,
              R"(fn two() -> Int {
  return 2;
}
)");
    write_all(entry,
              R"(import lib.b;
import lib.a;

fn main() -> Int {
  return lib.a.one();
}
)");

    {
        std::string out;
        std::string err;
        const int rc =
            run_cli({"curlee", "deps", "verify", "--root", dir.string(), "main.curlee",
                     lock_path.string()},
                    out, err);
        if (rc == 0)
        {
            fail("expected deps verify to fail after entry change");
        }
        if (err.find("deps verify failed: entry hash mismatch") == std::string::npos)
        {
            fail("expected deps verify entry hash mismatch diagnostic");
        }
    }

    {
        const fs::path bad_lock = dir / "bad.lock";
        write_all(bad_lock, "not a lockfile\n");

        std::string out;
        std::string err;
        const int rc =
            run_cli({"curlee", "deps", "verify", "--root", dir.string(), "main.curlee",
                     bad_lock.string()},
                    out, err);
        if (rc == 0)
        {
            fail("expected deps verify to fail for invalid lockfile");
        }
        if (err.find("deps verify failed: invalid lockfile header") == std::string::npos)
        {
            fail("expected invalid lockfile diagnostic");
        }
    }

    {
        const fs::path missing_lock = dir / "missing.lock";
        expect_error_contains({"curlee", "deps", "verify", "--root", dir.string(),
                               "main.curlee", missing_lock.string()},
                              "deps verify failed: failed to open lockfile");
    }

    {
        const fs::path bad_lock = dir / "bad_missing_format.lock";
        write_all(bad_lock,
                  "CURLEE_DEPS_LOCK\n"
                  "entry_hash=abc\n"
                  "imports=entry/lib/a:dead\n");
        expect_error_contains({"curlee", "deps", "verify", "--root", dir.string(),
                               "main.curlee", bad_lock.string()},
                              "deps verify failed: missing lockfile format version");
    }

    {
        const fs::path bad_lock = dir / "bad_invalid_version.lock";
        write_all(bad_lock,
                  "CURLEE_DEPS_LOCK\n"
                  "format_version=abc\n"
                  "entry_hash=abc\n"
                  "imports=entry/lib/a:dead\n");
        expect_error_contains({"curlee", "deps", "verify", "--root", dir.string(),
                               "main.curlee", bad_lock.string()},
                              "deps verify failed: invalid lockfile format version");
    }

    {
        const fs::path bad_lock = dir / "bad_unsupported_version.lock";
        write_all(bad_lock,
                  "CURLEE_DEPS_LOCK\n"
                  "format_version=99\n"
                  "entry_hash=abc\n"
                  "imports=entry/lib/a:dead\n");
        expect_error_contains({"curlee", "deps", "verify", "--root", dir.string(),
                               "main.curlee", bad_lock.string()},
                              "deps verify failed: unsupported lockfile format version");
    }

    {
        const fs::path bad_lock = dir / "bad_missing_entry_hash.lock";
        write_all(bad_lock,
                  "CURLEE_DEPS_LOCK\n"
                  "format_version=1\n"
                  "imports=entry/lib/a:dead\n");
        expect_error_contains({"curlee", "deps", "verify", "--root", dir.string(),
                               "main.curlee", bad_lock.string()},
                              "deps verify failed: missing entry_hash");
    }

    {
        const fs::path bad_lock = dir / "bad_import_pin.lock";
        write_all(bad_lock,
                  "CURLEE_DEPS_LOCK\n"
                  "format_version=1\n"
                  "entry_hash=abc\n"
                  "imports=,entry/lib/a,\n");
        expect_error_contains({"curlee", "deps", "verify", "--root", dir.string(),
                               "main.curlee", bad_lock.string()},
                              "deps verify failed: invalid lockfile import pin");
    }

    {
        const fs::path stdlib_root = dir / "stdlib";
        fs::create_directories(stdlib_root / "std");
        write_all(stdlib_root / "std" / "math.curlee",
                  R"(fn two() -> Int {
  return 2;
}
)");

        const fs::path std_entry = dir / "stdlib_entry.curlee";
        const fs::path std_lock = dir / "stdlib.lock";
        write_all(std_entry,
                  R"(import std.math;

fn main() -> Int {
  return std.math.two();
}
)");

        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "deps", "lock", "--root=" + dir.string(),
                    "--stdlib-root=" + stdlib_root.string(),
                    "stdlib_entry.curlee", std_lock.string()},
                               out, err);
        if (rc != 0)
        {
            fail("expected deps lock with stdlib-root to succeed; stderr: " + err);
        }

        const std::string std_lock_body = slurp(std_lock);
        if (std_lock_body.find("/std/math:") == std::string::npos ||
            std_lock_body.find("stdlib") == std::string::npos)
        {
            fail("expected stdlib lock pin with deterministic stdlib id");
        }

        const fs::path std_lock_alt = dir / "stdlib-alt.lock";
        const int rc_alt = run_cli({"curlee", "deps", "lock", "--root", dir.string(),
                                    "--stdlib-root", stdlib_root.string(),
                                    "stdlib_entry.curlee", std_lock_alt.string()},
                                   out, err);
        if (rc_alt != 0)
        {
            fail("expected deps lock with separate --stdlib-root arg to succeed; stderr: " +
                 err);
        }
    }

    {
        const fs::path mismatch_root = dir / "mismatch_root";
        const fs::path outside_dir = dir / "outside";
        fs::create_directories(mismatch_root);
        fs::create_directories(outside_dir / "lib");

        const fs::path outside_entry = outside_dir / "main.curlee";
        write_all(outside_entry,
                  R"(import lib.x;

fn main() -> Int {
  return lib.x.one();
}
)");
        write_all(outside_dir / "lib" / "x.curlee",
                  R"(fn one() -> Int {
  return 1;
}
)");

        expect_error_contains(
            {"curlee", "deps", "lock", "--root", mismatch_root.string(),
             outside_entry.string(), (dir / "outside.lock").string()},
            "cannot generate deterministic dependency pin for import outside entry/stdlib roots");
    }

    {
        const fs::path missing_entry = dir / "missing.curlee";
        expect_error_contains({"curlee", "deps", "lock", "--root", dir.string(),
                               missing_entry.string(), (dir / "missing.lock").string()},
                              "failed to open file");

        expect_error_contains({"curlee", "deps", "verify", "--root", dir.string(),
                               missing_entry.string(), lock_path.string()},
                              "failed to open file");
    }

    fs::remove_all(dir);
    return 0;
}
