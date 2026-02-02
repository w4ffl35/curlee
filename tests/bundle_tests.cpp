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

static void write_text_file(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream out(path);
    out << content;
    out.close();
}

static void expect_read_error(const std::filesystem::path& path, const std::string& expected)
{
    using namespace curlee::bundle;

    const auto read_res = read_bundle(path.string());
    if (auto err = std::get_if<BundleError>(&read_res))
    {
        if (err->message != expected)
        {
            fail("expected error: '" + expected + "' but got: '" + err->message + "'");
        }
        return;
    }

    fail("expected read_bundle to fail: " + path.string());
}

static void expect_read_ok(const std::filesystem::path& path)
{
    using namespace curlee::bundle;

    const auto read_res = read_bundle(path.string());
    if (auto err = std::get_if<BundleError>(&read_res))
    {
        fail(std::string("unexpected read error: ") + err->message);
    }
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
        const auto missing_path = temp_path("curlee_bundle_does_not_exist.bundle");
        std::filesystem::remove(missing_path);
        const auto read_res = read_bundle(missing_path.string());
        if (auto err = std::get_if<BundleError>(&read_res))
        {
            if (err->message != "failed to open bundle")
            {
                fail("expected failed to open bundle");
            }
        }
        else
        {
            fail("expected non-existent bundle to fail");
        }
    }

    {
        const auto empty = temp_path("curlee_bundle_empty.bundle");
        std::filesystem::remove(empty);
        write_text_file(empty, "");
        expect_read_error(empty, "empty bundle");
        std::filesystem::remove(empty);
    }

    {
        const auto invalid_header = temp_path("curlee_bundle_invalid_header.bundle");
        std::filesystem::remove(invalid_header);
        write_text_file(invalid_header, "NOT_A_BUNDLE\n");
        expect_read_error(invalid_header, "invalid bundle header");
        std::filesystem::remove(invalid_header);
    }

    {
        const auto legacy_no_version = temp_path("curlee_bundle_legacy_no_version.bundle");
        std::filesystem::remove(legacy_no_version);

        const std::vector<std::uint8_t> bytes{0x01};
        write_text_file(
            legacy_no_version,
            std::string("CURLEE_BUNDLE_V1\n") +
                "bytecode_hash=" + hash_bytes(bytes) + "\n" +
                "capabilities=\n" +
                "imports=\n" +
                "proof=\n" +
                "bytecode=AQ==\n");

        expect_read_ok(legacy_no_version);
        std::filesystem::remove(legacy_no_version);
    }

    {
        const auto missing_version = temp_path("curlee_bundle_missing_version.bundle");
        std::filesystem::remove(missing_version);
        write_text_file(
            missing_version,
            std::string("CURLEE_BUNDLE\n") +
                "bytecode_hash=deadbeef\n" +
                "bytecode=AQ==\n");
        expect_read_error(missing_version, "missing bundle format version");
        std::filesystem::remove(missing_version);
    }

    {
        const auto invalid_version = temp_path("curlee_bundle_invalid_version.bundle");
        std::filesystem::remove(invalid_version);
        write_text_file(
            invalid_version,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=not_an_int\n" +
                "bytecode_hash=deadbeef\n" +
                "bytecode=AQ==\n");
        expect_read_error(invalid_version, "invalid bundle format version");
        std::filesystem::remove(invalid_version);
    }

    {
        const auto unsupported_version = temp_path("curlee_bundle_unsupported_version.bundle");
        std::filesystem::remove(unsupported_version);
        write_text_file(
            unsupported_version,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=999\n" +
                "bytecode_hash=deadbeef\n" +
                "bytecode=AQ==\n");
        expect_read_error(
            unsupported_version,
            "unsupported bundle format version: 999 (supported: " + std::to_string(kBundleFormatVersion) + ")");
        std::filesystem::remove(unsupported_version);
    }

    {
        const auto missing_hash = temp_path("curlee_bundle_missing_hash.bundle");
        std::filesystem::remove(missing_hash);
        write_text_file(
            missing_hash,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=" + std::to_string(kBundleFormatVersion) + "\n" +
                "bytecode=AQ==\n");
        expect_read_error(missing_hash, "missing bytecode_hash");
        std::filesystem::remove(missing_hash);
    }

    {
        const auto missing_bytecode = temp_path("curlee_bundle_missing_bytecode.bundle");
        std::filesystem::remove(missing_bytecode);
        write_text_file(
            missing_bytecode,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=" + std::to_string(kBundleFormatVersion) + "\n" +
                "bytecode_hash=deadbeef\n");
        expect_read_error(missing_bytecode, "missing bytecode");
        std::filesystem::remove(missing_bytecode);
    }

    {
        const auto invalid_b64 = temp_path("curlee_bundle_invalid_b64.bundle");
        std::filesystem::remove(invalid_b64);
        write_text_file(
            invalid_b64,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=" + std::to_string(kBundleFormatVersion) + "\n" +
                "bytecode_hash=deadbeef\n" +
                "bytecode=@!\n");
        expect_read_error(invalid_b64, "invalid base64 bytecode");
        std::filesystem::remove(invalid_b64);
    }

    {
        const auto b64_short = temp_path("curlee_bundle_b64_short.bundle");
        std::filesystem::remove(b64_short);
        write_text_file(
            b64_short,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=" + std::to_string(kBundleFormatVersion) + "\n" +
                "bytecode_hash=deadbeef\n" +
                "bytecode=A\n");
        expect_read_error(b64_short, "invalid base64 bytecode");
        std::filesystem::remove(b64_short);
    }

    {
        const auto b64_bad_padding = temp_path("curlee_bundle_b64_bad_padding.bundle");
        std::filesystem::remove(b64_bad_padding);
        write_text_file(
            b64_bad_padding,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=" + std::to_string(kBundleFormatVersion) + "\n" +
                "bytecode_hash=deadbeef\n" +
                "bytecode=====\n");
        expect_read_error(b64_bad_padding, "invalid base64 bytecode");
        std::filesystem::remove(b64_bad_padding);
    }

    {
        const auto b64_starts_with_pad = temp_path("curlee_bundle_b64_starts_with_pad.bundle");
        std::filesystem::remove(b64_starts_with_pad);
        write_text_file(
            b64_starts_with_pad,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=" + std::to_string(kBundleFormatVersion) + "\n" +
                "bytecode_hash=deadbeef\n" +
                "bytecode==AA\n");
        expect_read_error(b64_starts_with_pad, "invalid base64 bytecode");
        std::filesystem::remove(b64_starts_with_pad);
    }

    {
        const auto b64_lowercase = temp_path("curlee_bundle_b64_lowercase.bundle");
        std::filesystem::remove(b64_lowercase);
        write_text_file(
            b64_lowercase,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=" + std::to_string(kBundleFormatVersion) + "\n" +
                "bytecode_hash=deadbeef\n" +
                "bytecode=aAAA\n");
        expect_read_error(b64_lowercase, "bytecode hash mismatch");
        std::filesystem::remove(b64_lowercase);
    }

    {
        const auto b64_plus = temp_path("curlee_bundle_b64_plus.bundle");
        std::filesystem::remove(b64_plus);
        write_text_file(
            b64_plus,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=" + std::to_string(kBundleFormatVersion) + "\n" +
                "bytecode_hash=deadbeef\n" +
                "bytecode=++++\n");
        expect_read_error(b64_plus, "bytecode hash mismatch");
        std::filesystem::remove(b64_plus);
    }

    {
        const auto b64_digit = temp_path("curlee_bundle_b64_digit.bundle");
        std::filesystem::remove(b64_digit);
        write_text_file(
            b64_digit,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=" + std::to_string(kBundleFormatVersion) + "\n" +
                "bytecode_hash=deadbeef\n" +
                "bytecode=0AAA\n");
        expect_read_error(b64_digit, "bytecode hash mismatch");
        std::filesystem::remove(b64_digit);
    }

    {
        const auto whitespace_b64 = temp_path("curlee_bundle_whitespace_b64.bundle");
        std::filesystem::remove(whitespace_b64);
        const std::vector<std::uint8_t> empty_bytes;
        write_text_file(
            whitespace_b64,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=" + std::to_string(kBundleFormatVersion) + "\n" +
                "bytecode_hash=" + hash_bytes(empty_bytes) + "\n" +
                "imports=stdlib.math:deadbeef\n" +
                "capabilities=,io:stdout,,net:none,\n" +
                "proof=hello\n" +
                "bytecode=   \t \n");
        expect_read_ok(whitespace_b64);
        std::filesystem::remove(whitespace_b64);
    }

    {
        const auto invalid_import_no_colon = temp_path("curlee_bundle_invalid_import_no_colon.bundle");
        std::filesystem::remove(invalid_import_no_colon);
        write_text_file(
            invalid_import_no_colon,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=" + std::to_string(kBundleFormatVersion) + "\n" +
                "bytecode_hash=deadbeef\n" +
                "imports=stdlib.math\n" +
                "bytecode=AQ==\n");
        expect_read_error(invalid_import_no_colon, "invalid import pin");
        std::filesystem::remove(invalid_import_no_colon);
    }

    {
        const auto invalid_import_empty_path = temp_path("curlee_bundle_invalid_import_empty_path.bundle");
        std::filesystem::remove(invalid_import_empty_path);
        write_text_file(
            invalid_import_empty_path,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=" + std::to_string(kBundleFormatVersion) + "\n" +
                "bytecode_hash=deadbeef\n" +
                "imports=:bead\n" +
                "bytecode=AQ==\n");
        expect_read_error(invalid_import_empty_path, "invalid import pin");
        std::filesystem::remove(invalid_import_empty_path);
    }

    {
        const auto invalid_import_empty_hash = temp_path("curlee_bundle_invalid_import_empty_hash.bundle");
        std::filesystem::remove(invalid_import_empty_hash);
        write_text_file(
            invalid_import_empty_hash,
            std::string("CURLEE_BUNDLE\n") +
                "format_version=" + std::to_string(kBundleFormatVersion) + "\n" +
                "bytecode_hash=deadbeef\n" +
                "imports=stdlib.math:\n" +
                "bytecode=AQ==\n");
        expect_read_error(invalid_import_empty_hash, "invalid import pin");
        std::filesystem::remove(invalid_import_empty_hash);
    }

    {
        const auto weird_lines = temp_path("curlee_bundle_weird_lines.bundle");
        std::filesystem::remove(weird_lines);
        write_text_file(
            weird_lines,
            std::string("CURLEE_BUNDLE\n") +
                "\n" +
                "this line has no equals sign\n" +
                "format_version=" + std::to_string(kBundleFormatVersion) + "\n" +
                "bytecode_hash=deadbeef\n" +
                "bytecode=AQ==\n");
        expect_read_error(weird_lines, "bytecode hash mismatch");
        std::filesystem::remove(weird_lines);
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

    {
        const auto bundle_dir = temp_path("curlee_bundle_write_to_dir");
        std::filesystem::remove_all(bundle_dir);
        std::filesystem::create_directory(bundle_dir);

        Bundle bundle;
        bundle.manifest.format_version = kBundleFormatVersion;
        bundle.bytecode = {0x01, 0x02};
        const auto write_err = write_bundle(bundle_dir.string(), bundle);
        if (write_err.message != "failed to open bundle for writing")
        {
            fail("expected failed to open bundle for writing");
        }

        std::filesystem::remove_all(bundle_dir);
    }

    {
        const auto path_1 = temp_path("curlee_bundle_roundtrip_1.byte");
        const auto path_2 = temp_path("curlee_bundle_roundtrip_2.byte");
        std::filesystem::remove(path_1);
        std::filesystem::remove(path_2);

        Bundle bundle_1;
        bundle_1.manifest.format_version = kBundleFormatVersion;
        bundle_1.manifest.capabilities = {"io:stdout"};
        bundle_1.manifest.imports = {ImportPin{.path = "a", .hash = "b"}, ImportPin{.path = "c", .hash = "d"}};
        bundle_1.bytecode = {0xFF};
        const auto write_err_1 = write_bundle(path_1.string(), bundle_1);
        if (!write_err_1.message.empty())
        {
            fail("expected 1-byte bundle to write");
        }
        expect_read_ok(path_1);

        Bundle bundle_2;
        bundle_2.manifest.format_version = kBundleFormatVersion;
        bundle_2.bytecode = {0x01, 0x02};
        const auto write_err_2 = write_bundle(path_2.string(), bundle_2);
        if (!write_err_2.message.empty())
        {
            fail("expected 2-byte bundle to write");
        }
        expect_read_ok(path_2);

        std::filesystem::remove(path_1);
        std::filesystem::remove(path_2);
    }

    std::cout << "OK\n";
    return 0;
}
