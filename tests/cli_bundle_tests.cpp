#include <curlee/bundle/bundle.h>
#include <curlee/cli/cli.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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

int main()
{
    namespace fs = std::filesystem;
    using namespace curlee::bundle;

    const fs::path ok_path = temp_path("curlee_cli_bundle_ok.bundle");
    const fs::path ok2_path = temp_path("curlee_cli_bundle_ok2.bundle");
    const fs::path bad_path = temp_path("curlee_cli_bundle_bad.bundle");
    const fs::path bad_entry_path = temp_path("curlee_cli_bundle_bad_entry.curlee");
    const fs::path missing_path = temp_path("curlee_cli_bundle_missing.bundle");

    fs::remove(ok_path);
    fs::remove(ok2_path);
    fs::remove(bad_path);
    fs::remove(bad_entry_path);
    fs::remove(missing_path);

    Bundle bundle;
    bundle.manifest.capabilities = {"io:stdout", "net:none"};
    bundle.manifest.imports = {ImportPin{.path = "stdlib.math", .hash = "deadbeef"}};
    bundle.manifest.proof = "proof-v1";
    bundle.bytecode = {0x01, 0x02, 0x03, 0x04};

    const auto write_err = write_bundle(ok_path.string(), bundle);
    if (!write_err.message.empty())
    {
        fail("expected bundle write to succeed");
    }

    {
        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "bundle", "verify", ok_path.string()}, out, err);
        if (rc != 0)
        {
            fail("expected bundle verify to succeed");
        }
        if (!err.empty())
        {
            fail("expected bundle verify to have empty stderr");
        }
        if (out != "curlee bundle verify: ok\n")
        {
            fail("unexpected bundle verify stdout: " + out);
        }
    }

    // bundle: wrong arity is a usage error.
    {
        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "bundle", "info"}, out, err);
        if (rc == 0)
        {
            fail("expected bundle info without path to fail");
        }
        if (err.find("expected curlee bundle <verify|info> <file.bundle>") == std::string::npos)
        {
            fail("expected stderr to mention expected curlee bundle <verify|info> <file.bundle>");
        }
    }

    {
        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "bundle", "info", ok_path.string()}, out, err);
        if (rc != 0)
        {
            fail("expected bundle info to succeed");
        }
        if (!err.empty())
        {
            fail("expected bundle info to have empty stderr");
        }

        const std::string expected_hash = hash_bytes(bundle.bytecode);
        const std::string expected = "curlee bundle info:\n"
                                     "format_version: 1\n"
                                     "bytecode_hash: " +
                                     expected_hash +
                                     "\n"
                                     "capabilities: io:stdout,net:none\n"
                                     "imports: stdlib.math:deadbeef\n"
                                     "proof: present\n";

        if (out != expected)
        {
            fail("unexpected bundle info stdout: " + out);
        }
    }

    // bundle info: multi-import pins + proof absence are formatted deterministically.
    {
        Bundle bundle;
        bundle.manifest.capabilities = {"io:stdout", "net:none"};
        bundle.manifest.imports = {ImportPin{.path = "stdlib.math", .hash = "deadbeef"},
                                   ImportPin{.path = "stdlib.io", .hash = "bead"}};
        bundle.manifest.proof = std::nullopt;
        bundle.bytecode = {0x01, 0x02, 0x03, 0x04};

        const auto write_err = write_bundle(ok2_path.string(), bundle);
        if (!write_err.message.empty())
        {
            fail("expected bundle write to succeed");
        }

        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "bundle", "info", ok2_path.string()}, out, err);
        if (rc != 0)
        {
            fail("expected bundle info to succeed");
        }
        if (!err.empty())
        {
            fail("expected bundle info stderr to be empty");
        }

        const std::string expected_hash = hash_bytes(bundle.bytecode);
        const std::string expected = "curlee bundle info:\n"
                                     "format_version: 1\n"
                                     "bytecode_hash: " +
                                     expected_hash +
                                     "\n"
                                     "capabilities: io:stdout,net:none\n"
                                     "imports: stdlib.math:deadbeef,stdlib.io:bead\n"
                                     "proof: none\n";

        if (out != expected)
        {
            fail("unexpected bundle info stdout: " + out);
        }
    }

    // bundle: unknown subcommand is a usage error.
    {
        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "bundle", "nope", ok_path.string()}, out, err);
        if (rc == 0)
        {
            fail("expected unknown bundle subcommand to fail");
        }
        if (err.find("unknown bundle subcommand: nope") == std::string::npos)
        {
            fail("expected stderr to mention unknown bundle subcommand");
        }
    }

    // bundle: read failures return an error exit code.
    {
        fs::remove(missing_path);
        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "bundle", "info", missing_path.string()}, out, err);
        if (rc == 0)
        {
            fail("expected bundle info to fail for missing file");
        }
        if (err.find("error: bundle info failed:") == std::string::npos)
        {
            fail("expected stderr to mention bundle info failed");
        }
        if (!out.empty())
        {
            fail("expected bundle info stdout to be empty on error");
        }
    }

    {
        std::ofstream out(bad_path);
        out << "CURLEE_BUNDLE_V1\n";
        out << "version=" << kBundleFormatVersion << "\n";
        out << "bytecode_hash=deadbeef\n";
        out << "capabilities=io:stdout\n";
        out << "imports=stdlib.math:bead\n";
        out << "proof=\n";
        out << "bytecode=AQIDBA==\n";
        out.close();

        {
            std::ofstream entry(bad_entry_path);
            entry << "fn main() -> Int { return 0; }\n";
        }

        std::string cli_out;
        std::string cli_err;
        const int rc = run_cli({"curlee", "bundle", "verify", bad_path.string()}, cli_out, cli_err);
        if (rc == 0)
        {
            fail("expected bundle verify to fail for invalid hash");
        }
        if (cli_err.find("bytecode hash mismatch") == std::string::npos)
        {
            fail("expected stderr to mention bytecode hash mismatch");
        }

        // The same verification should gate `curlee run --bundle`.
        {
            std::string out;
            std::string err;
            const int run_rc =
                run_cli({"curlee", "run", "--bundle", bad_path.string(), bad_entry_path.string()},
                        out, err);
            if (run_rc == 0)
            {
                fail("expected run to fail when bundle cannot be loaded");
            }
            if (err.find("failed to load bundle") == std::string::npos ||
                err.find("bytecode hash mismatch") == std::string::npos)
            {
                fail("expected run stderr to mention bundle load failure and hash mismatch");
            }
            if (!out.empty())
            {
                fail("expected run stdout to be empty on failure");
            }
        }
    }

    // Bundle mode: imports must be pinned (no dynamic resolution).
    {
        const fs::path dir = temp_path("curlee_cli_bundle_pins");
        fs::remove_all(dir);
        fs::create_directories(dir);

        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";
        const fs::path bundle_path = dir / "pins.bundle";

        {
            std::ofstream out(entry);
            out << "import dep;\n";
            out << "fn main() -> Int { return foo(); }\n";
        }
        {
            std::ofstream out(dep);
            out << "fn foo() -> Int { return 7; }\n";
        }

        Bundle bundle;
        bundle.manifest.capabilities = {"io:stdout"};
        bundle.manifest.imports = {}; // dep is intentionally unpinned
        bundle.bytecode = {0x01, 0x02, 0x03, 0x04};

        const auto write_err = write_bundle(bundle_path.string(), bundle);
        if (!write_err.message.empty())
        {
            fail("expected bundle write to succeed");
        }

        std::string out;
        std::string err;
        const int rc =
            run_cli({"curlee", "run", "--bundle", bundle_path.string(), entry.string()}, out, err);
        if (rc == 0)
        {
            fail("expected run to fail for unpinned import in bundle mode");
        }
        if (err.find("import not pinned: 'dep'") == std::string::npos)
        {
            fail("expected stderr to mention unpinned import");
        }
        if (err.find("expected pin 'dep:") == std::string::npos)
        {
            fail("expected stderr to mention expected pin for dep");
        }
        if (!out.empty())
        {
            fail("expected stdout to be empty on failure");
        }

        fs::remove_all(dir);
    }

    // Bundle mode: long import names should still produce a clear "import not pinned" diagnostic.
    {
        const fs::path dir = temp_path("curlee_cli_bundle_pins_long");
        fs::remove_all(dir);
        fs::create_directories(dir);

        const std::string import_name = "averyveryverylongmodule";

        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / (import_name + ".curlee");
        const fs::path bundle_path = dir / "pins_long.bundle";

        {
            std::ofstream out(entry);
            out << "import " << import_name << ";\n";
            out << "fn main() -> Int { return 0; }\n";
        }
        {
            std::ofstream out(dep);
            out << "fn foo() -> Int { return 7; }\n";
        }

        Bundle bundle;
        bundle.manifest.capabilities = {};
        bundle.manifest.imports = {}; // import is intentionally unpinned
        bundle.bytecode = {0x01, 0x02, 0x03, 0x04};

        const auto write_err = write_bundle(bundle_path.string(), bundle);
        if (!write_err.message.empty())
        {
            fail("expected bundle write to succeed");
        }

        std::string out;
        std::string err;
        const int rc =
            run_cli({"curlee", "run", "--bundle", bundle_path.string(), entry.string()}, out, err);
        if (rc == 0)
        {
            fail("expected run to fail for unpinned long import in bundle mode");
        }
        if (err.find("import not pinned: '" + import_name + "'") == std::string::npos)
        {
            fail("expected stderr to mention unpinned import");
        }
        if (err.find("expected pin '") == std::string::npos)
        {
            fail("expected stderr to mention expected pin");
        }
        if (!out.empty())
        {
            fail("expected stdout to be empty on failure");
        }

        fs::remove_all(dir);
    }

    // Bundle mode: pinned imports must match their content hash.
    {
        const fs::path dir = temp_path("curlee_cli_bundle_pin_mismatch");
        fs::remove_all(dir);
        fs::create_directories(dir);

        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";
        const fs::path bundle_path = dir / "pin_mismatch.bundle";

        {
            std::ofstream out(entry);
            out << "import dep;\n";
            out << "fn main() -> Int { return foo(); }\n";
        }
        {
            std::ofstream out(dep);
            out << "fn foo() -> Int { return 7; }\n";
        }

        Bundle bundle;
        bundle.manifest.capabilities = {"io:stdout"};
        bundle.manifest.imports = {ImportPin{.path = "dep", .hash = "deadbeef"}};
        bundle.bytecode = {0x01, 0x02, 0x03, 0x04};

        const auto write_err = write_bundle(bundle_path.string(), bundle);
        if (!write_err.message.empty())
        {
            fail("expected bundle write to succeed");
        }

        std::string out;
        std::string err;
        const int rc =
            run_cli({"curlee", "run", "--bundle", bundle_path.string(), entry.string()}, out, err);
        if (rc == 0)
        {
            fail("expected run to fail for pinned import hash mismatch");
        }
        if (err.find("import pin hash mismatch: 'dep'") == std::string::npos)
        {
            fail("expected stderr to mention import pin hash mismatch");
        }
        if (err.find("expected hash deadbeef") == std::string::npos)
        {
            fail("expected stderr to mention expected hash");
        }
        if (err.find("actual hash ") == std::string::npos)
        {
            fail("expected stderr to mention actual hash");
        }
        if (!out.empty())
        {
            fail("expected stdout to be empty on failure");
        }

        fs::remove_all(dir);
    }

    // Bundle mode: pinned imports must match their content hash (long expected hash variant).
    {
        const fs::path dir = temp_path("curlee_cli_bundle_pin_mismatch_long");
        fs::remove_all(dir);
        fs::create_directories(dir);

        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";
        const fs::path bundle_path = dir / "pin_mismatch_long.bundle";

        {
            std::ofstream out(entry);
            out << "import dep;\n";
            out << "fn main() -> Int { return foo(); }\n";
        }
        {
            std::ofstream out(dep);
            out << "fn foo() -> Int { return 7; }\n";
        }

        const std::string long_hash(80, 'a');

        Bundle bundle;
        bundle.manifest.capabilities = {};
        bundle.manifest.imports = {ImportPin{.path = "dep", .hash = long_hash}};
        bundle.bytecode = {0x01, 0x02, 0x03, 0x04};

        const auto write_err = write_bundle(bundle_path.string(), bundle);
        if (!write_err.message.empty())
        {
            fail("expected bundle write to succeed");
        }

        std::string out;
        std::string err;
        const int rc =
            run_cli({"curlee", "run", "--bundle", bundle_path.string(), entry.string()}, out, err);
        if (rc == 0)
        {
            fail("expected run to fail for pinned import hash mismatch (long expected hash)");
        }
        if (err.find("import pin hash mismatch: 'dep'") == std::string::npos)
        {
            fail("expected stderr to mention import pin hash mismatch");
        }
        if (err.find("expected hash ") == std::string::npos)
        {
            fail("expected stderr to mention expected hash");
        }
        if (err.find("actual hash ") == std::string::npos)
        {
            fail("expected stderr to mention actual hash");
        }

        fs::remove_all(dir);
    }

    // Bundle mode: pinned imports that match are accepted.
    {
        const fs::path dir = temp_path("curlee_cli_bundle_pin_match");
        fs::remove_all(dir);
        fs::create_directories(dir);

        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";
        const fs::path bundle_path = dir / "pin_match.bundle";

        const std::string dep_src = "fn foo() -> Int { return 7; }\n";
        {
            std::ofstream out(entry);
            out << "import dep;\n";
            out << "fn main() -> Int { return 0; }\n";
        }
        {
            std::ofstream out(dep);
            out << dep_src;
        }

        std::vector<std::uint8_t> bytes;
        bytes.reserve(dep_src.size());
        for (const char c : dep_src)
        {
            bytes.push_back(static_cast<std::uint8_t>(c));
        }
        const std::string dep_hash = hash_bytes(bytes);

        Bundle bundle;
        bundle.manifest.capabilities = {};
        bundle.manifest.imports = {ImportPin{.path = "dep", .hash = dep_hash}};
        bundle.bytecode = {0x01, 0x02, 0x03, 0x04};

        const auto write_err = write_bundle(bundle_path.string(), bundle);
        if (!write_err.message.empty())
        {
            fail("expected bundle write to succeed");
        }

        std::string out;
        std::string err;
        const int rc =
            run_cli({"curlee", "run", "--bundle", bundle_path.string(), entry.string()}, out, err);
        if (rc != 0)
        {
            fail("expected run to succeed for pinned import hash match; stderr: " + err);
        }
        if (out.find("curlee run: result 0") == std::string::npos)
        {
            fail("expected run stdout to include result 0");
        }
        if (!err.empty())
        {
            fail("expected run stderr to be empty on success");
        }

        fs::remove_all(dir);
    }

    // Bundle mode: pinned imports that match are accepted (relative paths).
    {
        const fs::path dir = fs::path("curlee_cli_bundle_pin_match_rel");
        fs::remove_all(dir);
        fs::create_directories(dir);

        const fs::path entry = dir / "main.curlee";
        const fs::path dep = dir / "dep.curlee";
        const fs::path bundle_path = dir / "pin_match.bundle";

        const std::string dep_src = "fn foo() -> Int { return 7; }\n";
        {
            std::ofstream out(entry);
            out << "import dep;\n";
            out << "fn main() -> Int { return 0; }\n";
        }
        {
            std::ofstream out(dep);
            out << dep_src;
        }

        std::vector<std::uint8_t> bytes;
        bytes.reserve(dep_src.size());
        for (const char c : dep_src)
        {
            bytes.push_back(static_cast<std::uint8_t>(c));
        }
        const std::string dep_hash = hash_bytes(bytes);

        Bundle bundle;
        bundle.manifest.capabilities = {};
        bundle.manifest.imports = {ImportPin{.path = "dep", .hash = dep_hash}};
        bundle.bytecode = {0x01, 0x02, 0x03, 0x04};

        const auto write_err = write_bundle(bundle_path.string(), bundle);
        if (!write_err.message.empty())
        {
            fail("expected bundle write to succeed");
        }

        const std::string entry_arg = (fs::path(".") / entry).string();
        const std::string bundle_arg = (fs::path(".") / bundle_path).string();

        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "run", "--bundle", bundle_arg, entry_arg}, out, err);
        if (rc != 0)
        {
            fail("expected run to succeed for pinned import hash match (relative paths); stderr: " +
                 err);
        }
        if (out.find("curlee run: result 0") == std::string::npos)
        {
            fail("expected run stdout to include result 0");
        }
        if (!err.empty())
        {
            fail("expected run stderr to be empty on success");
        }

        fs::remove_all(dir);
    }

    // Bundle mode: manifest capabilities must be granted at runtime.
    {
        const fs::path dir = temp_path("curlee_cli_bundle_caps");
        fs::remove_all(dir);
        fs::create_directories(dir);

        const fs::path entry = dir / "main.curlee";
        const fs::path bundle_path = dir / "caps.bundle";

        {
            std::ofstream out(entry);
            out << "fn main() -> Int { unsafe { python_ffi.call(); } return 0; }\n";
        }

        Bundle bundle;
        bundle.manifest.capabilities = {"python:ffi"};
        bundle.manifest.imports = {};
        bundle.bytecode = {0x01, 0x02, 0x03, 0x04};

        const auto write_err = write_bundle(bundle_path.string(), bundle);
        if (!write_err.message.empty())
        {
            fail("expected bundle write to succeed");
        }

        // Denied when required capability is not granted.
        {
            std::string out;
            std::string err;
            const int rc = run_cli(
                {"curlee", "run", "--bundle", bundle_path.string(), entry.string()}, out, err);
            if (rc == 0)
            {
                fail("expected run to fail when bundle capability is not granted");
            }
            if (err.find("missing capability required by bundle: python:ffi") == std::string::npos)
            {
                fail("expected stderr to mention missing capability required by bundle");
            }
        }

        // Allowed past the bundle check when capability is granted; VM executes the runner
        // round-trip (currently a no-op handshake) and continues.
        {
            std::string out;
            std::string err;
            const int rc = run_cli({"curlee", "run", "--cap", "python:ffi", "--bundle",
                                    bundle_path.string(), entry.string()},
                                   out, err);
            if (rc != 0)
            {
                fail("expected run to succeed when bundle capability is granted");
            }
            if (!err.empty())
            {
                fail("expected stderr to be empty on success");
            }
            if (out.find("curlee run: result 0") == std::string::npos)
            {
                fail("expected stdout to include result 0");
            }
        }

        fs::remove_all(dir);
    }

    // Bundle mode: missing capability diagnostics should handle long capability names.
    {
        const fs::path dir = temp_path("curlee_cli_bundle_caps_long");
        fs::remove_all(dir);
        fs::create_directories(dir);

        const fs::path entry = dir / "main.curlee";
        const fs::path bundle_path = dir / "caps_long.bundle";

        {
            std::ofstream out(entry);
            out << "fn main() -> Int { return 0; }\n";
        }

        const std::string cap = "capability:long:12345678901234567890";

        Bundle bundle;
        bundle.manifest.capabilities = {cap};
        bundle.manifest.imports = {};
        bundle.bytecode = {0x01, 0x02, 0x03, 0x04};

        const auto write_err = write_bundle(bundle_path.string(), bundle);
        if (!write_err.message.empty())
        {
            fail("expected bundle write to succeed");
        }

        std::string out;
        std::string err;
        const int rc =
            run_cli({"curlee", "run", "--bundle", bundle_path.string(), entry.string()}, out, err);
        if (rc == 0)
        {
            fail("expected run to fail when bundle capability is not granted (long cap)");
        }
        if (err.find("missing capability required by bundle: " + cap) == std::string::npos)
        {
            fail("expected stderr to mention missing capability required by bundle");
        }
        if (err.find("bundle manifest requires capability") == std::string::npos)
        {
            fail("expected stderr to mention bundle manifest requires capability");
        }
        if (err.find("grant it with:") == std::string::npos)
        {
            fail("expected stderr to include grant-it-with hint");
        }

        fs::remove_all(dir);
    }

    fs::remove(ok_path);
    fs::remove(ok2_path);
    fs::remove(bad_path);
    fs::remove(bad_entry_path);
    fs::remove(missing_path);

    std::cout << "OK\n";
    return 0;
}
