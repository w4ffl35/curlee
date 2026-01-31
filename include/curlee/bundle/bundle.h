#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace curlee::bundle
{

inline constexpr int kBundleFormatVersion = 1;

struct ImportPin
{
    std::string path;
    std::string hash;
};

struct Manifest
{
    int format_version = kBundleFormatVersion;
    std::string bytecode_hash;
    std::vector<std::string> capabilities;
    std::vector<ImportPin> imports;
    std::optional<std::string> proof;
};

struct Bundle
{
    Manifest manifest;
    std::vector<std::uint8_t> bytecode;
};

struct BundleError
{
    std::string message;
};

using BundleResult = std::variant<Bundle, BundleError>;

[[nodiscard]] std::string hash_bytes(const std::vector<std::uint8_t>& bytes);

[[nodiscard]] BundleResult read_bundle(const std::string& path);
[[nodiscard]] BundleError write_bundle(const std::string& path, const Bundle& bundle);

} // namespace curlee::bundle
