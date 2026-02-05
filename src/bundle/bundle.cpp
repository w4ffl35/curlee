#include <algorithm>
#include <cctype>
#include <charconv>
#include <curlee/bundle/bundle.h>
#include <fstream>
#include <sstream>
#include <string_view>

namespace curlee::bundle
{
namespace
{

#if defined(__GNUC__) || defined(__clang__)
#define CURLEE_NOINLINE __attribute__((noinline))
#else
#define CURLEE_NOINLINE
#endif

constexpr std::string_view kHeader = "CURLEE_BUNDLE";
constexpr std::string_view kHeaderLegacyV1 = "CURLEE_BUNDLE_V1";

CURLEE_NOINLINE int decode_base64_char(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return static_cast<int>(c - 'A');
    }
    if (c >= 'a' && c <= 'z')
    {
        return static_cast<int>(c - 'a' + 26);
    }
    if (c >= '0' && c <= '9')
    {
        return static_cast<int>(c - '0' + 52);
    }
    if (c == '+')
    {
        return 62;
    }
    if (c == '/')
    {
        return 63;
    }
    if (c == '=')
    {
        return -2;
    }
    return -1;
}

CURLEE_NOINLINE std::optional<int> parse_int(std::string_view input)
{
    int value = 0;
    const auto* begin = input.data();
    const auto* end = input.data() + input.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end)
    {
        return std::nullopt;
    }
    return value;
}

std::string to_hex(std::uint64_t value)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i)
    {
        out[i] = kHex[value & 0xF];
        value >>= 4;
    }
    return out;
} // GCOVR_EXCL_LINE

std::vector<std::string> split(std::string_view input, char delim)
{
    std::vector<std::string> out;
    std::string current;
    for (char ch : input)
    {
        if (ch == delim)
        {
            if (!current.empty())
            {
                out.push_back(current);
            }
            current.clear();
        }
        else
        {
            current.push_back(ch);
        }
    }
    if (!current.empty())
    {
        out.push_back(current);
    }
    return out;
} // GCOVR_EXCL_LINE

std::string base64_encode(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    while (i + 2 < bytes.size())
    {
        const std::uint32_t triple = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
        out.push_back(kTable[(triple >> 18) & 0x3F]);
        out.push_back(kTable[(triple >> 12) & 0x3F]);
        out.push_back(kTable[(triple >> 6) & 0x3F]);
        out.push_back(kTable[triple & 0x3F]);
        i += 3;
    }

    const std::size_t rem = bytes.size() - i;
    if (rem == 1)
    {
        const std::uint32_t triple = (bytes[i] << 16);
        out.push_back(kTable[(triple >> 18) & 0x3F]);
        out.push_back(kTable[(triple >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    }

    if (rem == 2)
    {
        const std::uint32_t triple = (bytes[i] << 16) | (bytes[i + 1] << 8);
        out.push_back(kTable[(triple >> 18) & 0x3F]);
        out.push_back(kTable[(triple >> 12) & 0x3F]);
        out.push_back(kTable[(triple >> 6) & 0x3F]);
        out.push_back('=');
    }

    return out;
} // GCOVR_EXCL_LINE

std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view input)
{
    std::vector<std::uint8_t> out;
    int vals[4];
    std::size_t i = 0;
    while (i < input.size())
    {
        int count = 0;
        for (; count < 4 && i < input.size(); ++i)
        {
            if (std::isspace(static_cast<unsigned char>(input[i])))
            {
                continue;
            }
            const int v = decode_base64_char(static_cast<unsigned char>(input[i]));
            if (v == -1)
            {
                return std::nullopt;
            }
            vals[count++] = v;
        }
        if (count == 0)
        {
            break;
        }
        if (count != 4)
        {
            return std::nullopt;
        }

        if (vals[0] < 0 || vals[1] < 0)
        {
            return std::nullopt;
        }

        const std::uint32_t triple = (static_cast<std::uint32_t>(vals[0]) << 18) |
                                     (static_cast<std::uint32_t>(vals[1]) << 12) |
                                     ((vals[2] > 0 ? vals[2] : 0) << 6) |
                                     (vals[3] > 0 ? vals[3] : 0);

        out.push_back(static_cast<std::uint8_t>((triple >> 16) & 0xFF));
        if (vals[2] != -2)
        {
            out.push_back(static_cast<std::uint8_t>((triple >> 8) & 0xFF));
        }
        if (vals[3] != -2)
        {
            out.push_back(static_cast<std::uint8_t>(triple & 0xFF));
        }
    }

    return out;
}

std::string join_pairs(const std::vector<ImportPin>& imports)
{
    std::string out;
    for (std::size_t i = 0; i < imports.size(); ++i)
    {
        if (i > 0)
        {
            out.push_back(',');
        }
        out.append(imports[i].path);
        out.push_back(':');
        out.append(imports[i].hash);
    }
    return out;
} // GCOVR_EXCL_LINE

std::string join_csv(const std::vector<std::string>& xs)
{
    std::string out;
    for (std::size_t i = 0; i < xs.size(); ++i)
    {
        if (i > 0)
        {
            out.push_back(',');
        }
        out.append(xs[i]);
    }
    return out;
} // GCOVR_EXCL_LINE

std::string compute_manifest_hash(const Manifest& manifest)
{
    // Stable, ordered serialization of manifest fields (excluding the manifest hash itself).
    // This is integrity, not cryptographic signing.
    const std::string proof = manifest.proof.value_or("");
    const std::string material =
        std::string("format_version=") + std::to_string(manifest.format_version) + "\n" +
        "bytecode_hash=" + manifest.bytecode_hash + "\n" +
        "capabilities=" + join_csv(manifest.capabilities) + "\n" +
        "imports=" + join_pairs(manifest.imports) + "\n" + "proof=" + proof + "\n";

    std::vector<std::uint8_t> bytes;
    bytes.reserve(material.size());
    for (const char c : material)
    {
        bytes.push_back(static_cast<std::uint8_t>(c));
    }
    return hash_bytes(bytes);
}

} // namespace

std::string hash_bytes(const std::vector<std::uint8_t>& bytes)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (auto b : bytes)
    {
        hash ^= b;
        hash *= 1099511628211ULL;
    }
    return to_hex(hash);
}

BundleError write_bundle(const std::string& path, const Bundle& bundle)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return BundleError{"failed to open bundle for writing"};
    }

    Bundle bundle_copy = bundle;
    bundle_copy.manifest.format_version = kBundleFormatVersion;
    bundle_copy.manifest.bytecode_hash = hash_bytes(bundle.bytecode);
    const std::string manifest_hash = compute_manifest_hash(bundle_copy.manifest);

    out << kHeader << "\n";
    out << "format_version=" << bundle_copy.manifest.format_version << "\n";
    out << "bytecode_hash=" << bundle_copy.manifest.bytecode_hash << "\n";
    out << "manifest_hash=" << manifest_hash << "\n";
    out << "capabilities=";
    for (std::size_t i = 0; i < bundle_copy.manifest.capabilities.size(); ++i)
    {
        if (i > 0)
        {
            out << ",";
        }
        out << bundle_copy.manifest.capabilities[i];
    }
    out << "\n";
    out << "imports=" << join_pairs(bundle_copy.manifest.imports) << "\n";
    const std::string proof = bundle_copy.manifest.proof.value_or("");
    out << "proof=" << proof << "\n";
    out << "bytecode=" << base64_encode(bundle_copy.bytecode) << "\n";

    return BundleError{""};
}

BundleResult read_bundle(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return BundleError{"failed to open bundle"};
    }

    std::string line;
    if (!std::getline(in, line))
    {
        return BundleError{"empty bundle"};
    }
    const bool legacy_v1_header = (line == kHeaderLegacyV1);
    if (line != kHeader && !legacy_v1_header)
    {
        return BundleError{"invalid bundle header"};
    }

    Manifest manifest;
    bool saw_format_version = false;
    std::string bytecode_b64;
    std::string manifest_hash;

    if (legacy_v1_header)
    {
        manifest.format_version = 1;
        saw_format_version = true;
    }

    while (std::getline(in, line))
    {
        if (line.empty())
        {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);

        if (key == "format_version" || key == "version")
        {
            const auto parsed = parse_int(value);
            if (!parsed.has_value())
            {
                return BundleError{"invalid bundle format version"};
            }
            manifest.format_version = *parsed;
            saw_format_version = true;
        }
        else if (key == "bytecode_hash")
        {
            manifest.bytecode_hash = value;
        }
        else if (key == "manifest_hash")
        {
            manifest_hash = value;
        }
        else if (key == "capabilities")
        {
            manifest.capabilities = split(value, ',');
        }
        else if (key == "imports")
        {
            const auto entries = split(value, ',');
            for (const auto& entry : entries)
            {
                const auto pos = entry.find(':');
                if (pos == std::string::npos)
                {
                    return BundleError{"invalid import pin"};
                }
                if (pos == 0 || pos + 1 >= entry.size())
                {
                    return BundleError{"invalid import pin"};
                }
                const auto path = entry.substr(0, pos);
                const auto hash = entry.substr(pos + 1);
                manifest.imports.push_back(
                    ImportPin{.path = path, .hash = hash}); // GCOVR_EXCL_LINE
            }
        }
        else if (key == "proof")
        {
            if (!value.empty())
            {
                manifest.proof = value;
            }
        }
        else if (key == "bytecode")
        {
            bytecode_b64 = value;
        }
    }

    if (!saw_format_version)
    {
        return BundleError{"missing bundle format version"};
    }
    if (manifest.format_version != kBundleFormatVersion)
    {
        return BundleError{
            "unsupported bundle format version: " + std::to_string(manifest.format_version) +
            " (supported: " + std::to_string(kBundleFormatVersion) + ")"};
    }
    if (manifest.bytecode_hash.empty())
    {
        return BundleError{"missing bytecode_hash"};
    }
    if (bytecode_b64.empty())
    {
        return BundleError{"missing bytecode"};
    }

    auto decoded = base64_decode(bytecode_b64);
    if (!decoded.has_value())
    {
        return BundleError{"invalid base64 bytecode"};
    }

    const auto actual_hash = hash_bytes(*decoded);
    if (actual_hash != manifest.bytecode_hash)
    {
        return BundleError{"bytecode hash mismatch"};
    }

    // Optional manifest integrity check (bundles produced by current tooling include it).
    if (!manifest_hash.empty())
    {
        const std::string expected = compute_manifest_hash(manifest);
        if (expected != manifest_hash)
        {
            return BundleError{"manifest hash mismatch"};
        }
    }

    return Bundle{.manifest = std::move(manifest), .bytecode = std::move(*decoded)};
}

} // namespace curlee::bundle
