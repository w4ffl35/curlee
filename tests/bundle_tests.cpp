#include <curlee/bundle/bundle.h>
#include <filesystem>
#include <fstream>
#include <iostream>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static std::filesystem::path temp_path(const std::string& name)
{
    return std::filesystem::temp_directory_path() / name;
}

int main()
{
    using namespace curlee::bundle;

    {
        const std::vector<std::uint8_t> bytes{'a', 'b', 'c'};
        const auto hash = hash_bytes(bytes);
        if (hash != "e71fa2190541574b")
        {
            fail("expected stable hash for abc");
        }
    }

    {
        const auto path = temp_path("curlee_bundle_roundtrip.bundle");
        std::filesystem::remove(path);

        Bundle bundle;
        bundle.manifest.format_version = kBundleFormatVersion;
        bundle.manifest.capabilities = {"io:stdout", "net:none"};
        bundle.manifest.imports = {ImportPin{.path = "stdlib.math", .hash = "deadbeef"}};
        bundle.manifest.proof = "proof-v1";
        bundle.bytecode = {0x01, 0x02, 0x03, 0x04};

        const auto write_err = write_bundle(path.string(), bundle);
        if (!write_err.message.empty())
        {
            fail("expected bundle to write successfully");
        }

        const auto read_res = read_bundle(path.string());
        if (auto err = std::get_if<BundleError>(&read_res))
        {
            fail(std::string("unexpected read error: ") + err->message);
        }

        const auto& roundtrip = std::get<Bundle>(read_res);
        if (roundtrip.manifest.format_version != kBundleFormatVersion)
        {
            fail("expected roundtrip version to match");
        }
        if (roundtrip.manifest.capabilities != bundle.manifest.capabilities)
        {
            fail("expected capabilities to roundtrip");
        }
        if (roundtrip.manifest.imports.size() != 1 ||
            roundtrip.manifest.imports[0].path != "stdlib.math" ||
            roundtrip.manifest.imports[0].hash != "deadbeef")
        {
            fail("expected imports to roundtrip");
        }
        if (!roundtrip.manifest.proof.has_value() ||
            roundtrip.manifest.proof != bundle.manifest.proof)
        {
            fail("expected proof to roundtrip");
        }
        if (roundtrip.bytecode != bundle.bytecode)
        {
            fail("expected bytecode to roundtrip");
        }

        std::filesystem::remove(path);
    }

    {
        const auto path = temp_path("curlee_bundle_invalid_hash.bundle");
        std::filesystem::remove(path);

        std::ofstream out(path);
        out << "CURLEE_BUNDLE_V1\n";
        out << "version=" << kBundleFormatVersion << "\n";
        out << "bytecode_hash=deadbeef\n";
        out << "capabilities=io:stdout\n";
        out << "imports=stdlib.math:bead\n";
        out << "proof=\n";
        out << "bytecode=AQIDBA==\n";
        out.close();

        const auto read_res = read_bundle(path.string());
        if (auto err = std::get_if<BundleError>(&read_res))
        {
            if (err->message != "bytecode hash mismatch")
            {
                fail("expected bytecode hash mismatch error");
            }
        }
        else
        {
            fail("expected invalid bundle to fail verification");
        }

        std::filesystem::remove(path);
    }

    std::cout << "OK\n";
    return 0;
}
