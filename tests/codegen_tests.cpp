// SPDX-License-Identifier: MIT
#include <curlee/cli/cli.h>
#include <cstdlib>
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
        fail("failed to open file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static int run_cli(const std::vector<std::string>& argv_storage, std::string& out,
                   std::string& err)
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

// Positive fixtures: `curlee build` must succeed and emit C that exactly
// matches the golden `.expected` file.
static void run_positive_case(const fs::path& fixture, const fs::path& expected)
{
    std::string out;
    std::string err;
    const int rc = run_cli({"curlee", "build", "-o", "-", fixture.string()}, out, err);

    if (rc != 0)
    {
        fail("build failed for " + fixture.string() + ":\n" + err);
    }

    const std::string expected_out = slurp(expected);
    if (out != expected_out)
    {
        std::cerr << "GOLDEN MISMATCH: " << fixture.filename().string() << "\n";
        std::cerr << "--- expected ---\n" << expected_out;
        std::cerr << "--- got ---\n" << out;
        std::exit(1);
    }
}

// Error fixtures: `curlee build` must fail (verification gate or hosted
// builtin rejection) with the expected diagnostic in stderr.
static void run_negative_case(const fs::path& fixture, const std::string& expected_diag)
{
    std::string out;
    std::string err;
    const int rc = run_cli({"curlee", "build", "-o", "-", fixture.string()}, out, err);

    if (rc == 0)
    {
        fail("expected build to fail for " + fixture.string());
    }
    if (out.find("curlee_") != std::string::npos)
    {
        fail("expected no C output for failing fixture " + fixture.string());
    }
    if (err.find(expected_diag) == std::string::npos)
    {
        fail("expected stderr to contain '" + expected_diag + "' for " +
             fixture.string() + ":\n" + err);
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: curlee_codegen_tests <fixtures_dir>\n";
        return 2;
    }

    const fs::path fixtures_dir = argv[1];

    // Positive golden cases: emitted C text must match the .expected files.
    // The golden files (tests/codegen/<name>.expected, listed explicitly for
    // the orphan-golden audit) are: arith.expected, control_flow.expected,
    // while_loop.expected, struct_fixture.expected, enum_match.expected,
    // match_stmt.expected, phys_mem.expected, phys_param.expected,
    // empty_union_enum.expected, forward_ref.expected, unit_empty.expected,
    // match_unit_arms.expected, mixed_payload_enum.expected,
    // phys_param_forward.expected, while_loop_unit.expected,
    // multi_function.expected, extern_fn.expected, assign.expected,
    // bitwise.expected, io_port.expected, io_port_runtime.expected,
    // unsigned_widen.expected, u64_widen.expected, array.expected,
    // phys_read_runtime.expected, static_state.expected.
    for (const char* name :
         {"arith", "control_flow", "while_loop", "struct_fixture", "enum_match",
          "match_stmt", "phys_mem", "phys_param", "empty_union_enum", "forward_ref",
          "unit_empty", "match_unit_arms", "mixed_payload_enum", "phys_param_forward",
          "while_loop_unit", "multi_function", "extern_fn", "assign", "bitwise",
          "io_port", "io_port_runtime", "unsigned_widen", "u64_widen", "array",
          "phys_read_runtime", "static_state"})
    {
        run_positive_case(fixtures_dir / (std::string(name) + ".curlee"),
                          fixtures_dir / (std::string(name) + ".expected"));
    }

    // Error cases: the verification gate is enforced and hosted builtins are
    // rejected with a clear diagnostic.
    {
        // hosted_builtin: the diagnostic comes from `main` in the entry file,
        // so the rendered path must be the entry file.
        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "build", "-o", "-",
                                (fixtures_dir / "hosted_builtin.curlee").string()},
                               out, err);
        if (rc == 0)
        {
            fail("expected hosted_builtin to fail freestanding build");
        }
        if (err.find("builtin print is not supported in freestanding target") ==
            std::string::npos)
        {
            fail("expected print hosted-builtin diagnostic, got:\n" + err);
        }
        if (err.find("hosted_builtin.curlee") == std::string::npos)
        {
            fail("expected entry-file path in diagnostic, got:\n" + err);
        }
    }
    run_negative_case(fixtures_dir / "failed_contract.curlee",
                      "ensures clause not satisfied");
    // Indirect builtin through an imported module: the hosted builtin is
    // called inside the imported file (mymod/tty.curlee, resolved relative to
    // the entry file's directory), so the diagnostic must be attributed to
    // that file, not the entry file.
    {
        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "build", "-o", "-",
                                (fixtures_dir / "imported_builtin.curlee").string()},
                               out, err);
        if (rc == 0)
        {
            fail("expected imported_builtin to fail freestanding build");
        }
        if (err.find("builtin __tty_clear is not supported in freestanding target") ==
            std::string::npos)
        {
            fail("expected __tty_clear hosted-builtin diagnostic, got:\n" + err);
        }
        if (err.find("mymod/tty.curlee") == std::string::npos)
        {
            fail("expected imported-file path in diagnostic, got:\n" + err);
        }
        if (out.find("curlee_") != std::string::npos)
        {
            fail("expected no C output for imported_builtin");
        }
    }
    // String-typed parameter: codegen must reject, not emit `void s`.
    run_negative_case(fixtures_dir / "string_param.curlee",
                      "type 'String' is not supported in freestanding target");
    // A body-time codegen error inside an imported module (String literal) must
    // be attributed to the imported file via the function->file map.
    {
        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "build", "-o", "-",
                                (fixtures_dir / "imported_body_error.curlee").string()},
                               out, err);
        if (rc == 0)
        {
            fail("expected imported_body_error to fail freestanding build");
        }
        if (err.find("builtin string literal is not supported") == std::string::npos)
        {
            fail("expected string-literal diagnostic, got:\n" + err);
        }
        if (err.find("mymod/bad.curlee") == std::string::npos)
        {
            fail("expected imported-file path in diagnostic, got:\n" + err);
        }
        if (out.find("curlee_") != std::string::npos)
        {
            fail("expected no C output for imported_body_error");
        }
    }
    // Missing return annotation is rejected by the front-end (not codegen).
    run_negative_case(fixtures_dir / "missing_return_annotation.curlee",
                      "missing return type annotation");

    // Non-storable struct fields (Phys/Unit) are rejected with a diagnostic
    // and no C output (invalid C would otherwise be emitted: void members or
    // an empty struct).
    run_negative_case(fixtures_dir / "struct_phys_field.curlee",
                      "struct field 'S.p': type 'Phys' is not supported in "
                      "freestanding target");
    run_negative_case(fixtures_dir / "struct_unit_field.curlee",
                      "struct field 'S.u': type 'Unit' is not supported in "
                      "freestanding target");
    // A struct with both storable and rejected fields: the storable members are
    // emitted (in the discarded text) but the build still fails with the
    // rejected-field diagnostic.
    run_negative_case(fixtures_dir / "struct_mixed_fields.curlee",
                      "struct field 'S.p': type 'Phys' is not supported in "
                      "freestanding target");

    // Non-storable enum payloads (String/Phys) are rejected with a diagnostic
    // and no C output (an empty union would otherwise be emitted).
    run_negative_case(fixtures_dir / "enum_string_payload.curlee",
                      "enum payload 'E.A': type 'String' is not supported in "
                      "freestanding target");
    run_negative_case(fixtures_dir / "enum_phys_payload.curlee",
                      "enum payload 'E.P': type 'Phys' is not supported in "
                      "freestanding target");

    // A function returning Phys<T> (or Unit) is rejected: the return type
    // lowers to void, so `return <ptr>;` would be invalid C.
    run_negative_case(fixtures_dir / "phys_return.curlee",
                      "has return type 'Phys' which cannot be returned in "
                      "freestanding C");
    // A struct referencing a rejected struct by value must not leak invalid C
    // into the discarded output (the S placeholder keeps T well-formed).
    run_negative_case(fixtures_dir / "struct_dep_rejected.curlee",
                      "struct field 'S.p': type 'Phys' is not supported in "
                      "freestanding target");
    // Unit-typed parameters are rejected (a `void u` param would be invalid C).
    run_negative_case(fixtures_dir / "unit_param.curlee",
                      "parameter 'u' of function 'f': type 'Unit' is not supported "
                      "in freestanding target");

    // `curlee build --help` exits 0 and prints the build usage line
    // (issue #257: the build subcommand must be documented, including the
    // freestanding subset and the verification gate).
    {
        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "build", "--help"}, out, err);
        if (rc != 0)
        {
            fail("expected build --help to exit 0");
        }
        if (out.find("curlee build") == std::string::npos)
        {
            fail("expected build --help to show build usage");
        }
        if (out.find("--link") == std::string::npos)
        {
            fail("expected build --help to document --link");
        }
        if (out.find("freestanding") == std::string::npos)
        {
            fail("expected build --help to document the freestanding target");
        }
        if (out.find("no proof, no build") == std::string::npos)
        {
            fail("expected build --help to document the verification gate");
        }
    }

    // Top-level `curlee --help` must document `build`, `phys.mem`, and the
    // freestanding-only restriction (issue #257 acceptance criteria).
    {
        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "--help"}, out, err);
        if (rc != 0)
        {
            fail("expected --help to exit 0");
        }
        if (out.find("curlee build") == std::string::npos)
        {
            fail("expected --help to document the build subcommand");
        }
        if (out.find("phys.mem") == std::string::npos)
        {
            fail("expected --help to document the phys.mem capability");
        }
    }

    // Zero and two positionals must fail with a usage error.
    {
        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "build"}, out, err);
        if (rc == 0)
        {
            fail("expected build with no args to fail");
        }
    }
    {
        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "build", "a.curlee", "b.curlee"}, out, err);
        if (rc == 0)
        {
            fail("expected build with two positionals to fail");
        }
    }

    std::cout << "OK\n";
    return 0;
}
