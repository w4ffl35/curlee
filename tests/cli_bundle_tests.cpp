#include <curlee/bundle/bundle.h>
#include <curlee/cli/cli.h>
#include <curlee/vm/chunk_codec.h>
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

static curlee::vm::Chunk make_return_int_chunk(std::int64_t v)
{
    using namespace curlee::vm;
    Chunk chunk;
    const curlee::source::Span span{.start = 0, .end = 0};
    chunk.emit_constant(Value::int_v(v), span);
    chunk.emit(OpCode::Return, span);
    return chunk;
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
    bundle.bytecode = curlee::vm::encode_chunk(make_return_int_chunk(0));

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
        bundle.bytecode = curlee::vm::encode_chunk(make_return_int_chunk(0));

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

    // bundle verify: bundles produced by current tooling include a manifest_hash; tampering
    // with manifest fields should fail verification.
    {
        std::string contents = slurp(ok_path);
        const std::string needle = "capabilities=io:stdout,net:none\n";
        const auto pos = contents.find(needle);
        if (pos == std::string::npos)
        {
            fail("expected bundle to contain capabilities line for tamper test");
        }

        contents.replace(pos, needle.size(), "capabilities=io:stdout,net:all\n");
        write_all(ok_path, contents);

        std::string out;
        std::string err;
        const int rc = run_cli({"curlee", "bundle", "verify", ok_path.string()}, out, err);
        if (rc == 0)
        {
            fail("expected bundle verify to fail for tampered manifest");
        }
        if (err.find("manifest hash mismatch") == std::string::npos)
        {
            fail("expected stderr to mention manifest hash mismatch");
        }
        if (!out.empty())
        {
            fail("expected stdout to be empty on verify failure");
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

    // Bundle mode: bundled bytecode is the execution source-of-truth.
    {
        const fs::path dir = temp_path("curlee_cli_bundle_source_of_truth");
        fs::remove_all(dir);
        fs::create_directories(dir);

        const fs::path entry = dir / "main.curlee";
        const fs::path bundle_path = dir / "payload.bundle";

        {
            std::ofstream out(entry);
            out << "fn main() -> Int { return 0; }\n";
        }

        Bundle bundle;
        bundle.manifest.capabilities = {};
        bundle.manifest.imports = {};
        bundle.bytecode = curlee::vm::encode_chunk(make_return_int_chunk(7));

        const auto write_err = write_bundle(bundle_path.string(), bundle);
        if (!write_err.message.empty())
        {
            fail("expected bundle write to succeed");
        }

        {
            std::string out;
            std::string err;
            const int rc = run_cli(
                {"curlee", "run", "--bundle", bundle_path.string(), entry.string()}, out, err);
            if (rc != 0)
            {
                fail("expected run to succeed for bundled payload; stderr: " + err);
            }
            if (out.find("curlee run: result 7") == std::string::npos)
            {
                fail("expected run stdout to include result 7");
            }
            if (!err.empty())
            {
                fail("expected run stderr to be empty on success");
            }
        }

        // Mutate source after bundle creation: must not affect bundle execution.
        {
            std::ofstream out(entry);
            out << "fn main() -> Int { return 999; }\n";
        }

        {
            std::string out;
            std::string err;
            const int rc = run_cli(
                {"curlee", "run", "--bundle", bundle_path.string(), entry.string()}, out, err);
            if (rc != 0)
            {
                fail("expected run to succeed after source mutation; stderr: " + err);
            }
            if (out.find("curlee run: result 7") == std::string::npos)
            {
                fail("expected bundle execution to remain result 7 after source mutation");
            }
            if (!err.empty())
            {
                fail("expected run stderr to be empty on success");
            }
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
            out << "fn main() -> Int { return 0; }\n";
        }

        Bundle bundle;
        bundle.manifest.capabilities = {"python:ffi"};
        bundle.manifest.imports = {};
        bundle.bytecode = curlee::vm::encode_chunk(make_return_int_chunk(0));

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

        // Allowed when capability is granted.
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
        bundle.bytecode = curlee::vm::encode_chunk(make_return_int_chunk(0));

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

    // Bundle mode: entry file load errors should be diagnosed.
    {
        const fs::path dir = temp_path("curlee_cli_bundle_missing_entry");
        fs::remove_all(dir);
        fs::create_directories(dir);

        const fs::path entry = dir / "missing.curlee";
        const fs::path bundle_path = dir / "ok.bundle";

        Bundle bundle;
        bundle.manifest.capabilities = {};
        bundle.manifest.imports = {};
        bundle.bytecode = curlee::vm::encode_chunk(make_return_int_chunk(0));

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
            fail("expected run to fail when entry file cannot be loaded");
        }
        if (err.find("failed to open file") == std::string::npos)
        {
            fail("expected stderr to mention failed to open file");
        }

        fs::remove_all(dir);
    }

    // Bundle mode: invalid bundle bytecode should be diagnosed.
    {
        const fs::path dir = temp_path("curlee_cli_bundle_bad_bytecode");
        fs::remove_all(dir);
        fs::create_directories(dir);

        const fs::path entry = dir / "main.curlee";
        const fs::path bundle_path = dir / "bad_bytecode.bundle";

        {
            std::ofstream out(entry);
            out << "fn main() -> Int { return 0; }\n";
        }

        Bundle bundle;
        bundle.manifest.capabilities = {};
        bundle.manifest.imports = {};
        bundle.bytecode = {0x00, 0x01, 0x02};

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
            fail("expected run to fail when bundle bytecode is invalid");
        }
        if (err.find("invalid bundle bytecode") == std::string::npos)
        {
            fail("expected stderr to mention invalid bundle bytecode");
        }
        if (err.find("invalid chunk header") == std::string::npos)
        {
            fail("expected stderr to include the decode error");
        }

        fs::remove_all(dir);
    }

    // Bundle mode: VM runtime errors should be diagnosed.
    {
        const fs::path dir = temp_path("curlee_cli_bundle_vm_error");
        fs::remove_all(dir);
        fs::create_directories(dir);

        const fs::path entry = dir / "main.curlee";
        const fs::path bundle_path = dir / "vm_error.bundle";

        {
            std::ofstream out(entry);
            out << "fn main() -> Int { return 0; }\n";
        }

        curlee::vm::Chunk bad;
        const curlee::source::Span span{.start = 0, .end = 0};
        bad.emit(curlee::vm::OpCode::Jump, span);

        Bundle bundle;
        bundle.manifest.capabilities = {};
        bundle.manifest.imports = {};
        bundle.bytecode = curlee::vm::encode_chunk(bad);

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
            fail("expected run to fail when bundled bytecode triggers VM error");
        }
        if (err.find("truncated jump target") == std::string::npos)
        {
            fail("expected stderr to mention truncated jump target");
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
