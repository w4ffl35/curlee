#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

/**
 * @file bundle.h
 * @brief Bundle format helpers for packaging bytecode, manifest and imports.
 */

namespace curlee::bundle
{

inline constexpr int kBundleFormatVersion = 1;

/** @brief A pinned import with its content hash. */
struct ImportPin
{
    std::string path;
    std::string hash;
};

/** @brief Bundle manifest containing metadata and pins. */
struct Manifest
{
    int format_version = kBundleFormatVersion;
    std::string bytecode_hash;
    std::vector<std::string> capabilities;
    std::vector<ImportPin> imports;
    std::optional<std::string> proof;
};

/** @brief Full bundle containing manifest and bytecode. */
struct Bundle
{
    Manifest manifest;
    std::vector<std::uint8_t> bytecode;
};

/** @brief Error returned when bundle IO/parsing fails. */
struct BundleError
{
    std::string message;
};

using BundleResult = std::variant<Bundle, BundleError>;

/** @brief Compute a stable hash of bytes for manifest fingerprinting. */
[[nodiscard]] std::string hash_bytes(const std::vector<std::uint8_t>& bytes);

[[nodiscard]] BundleResult read_bundle(const std::string& path);
[[nodiscard]] BundleError write_bundle(const std::string& path, const Bundle& bundle);

} // namespace curlee::bundle
