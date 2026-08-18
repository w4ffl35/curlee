// SPDX-License-Identifier: MIT
#pragma once

#include <curlee/base/expected.h>
#include <curlee/diag/diagnostic.h>
#include <curlee/source/source_file.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace curlee::resolver
{

/**
 * @brief Configuration for module resolution search paths.
 */
struct ModuleSearchOptions
{
    /** @brief The directory of the importing file (for relative imports). */
    std::filesystem::path importing_file_dir;

    /** @brief The entry file's directory (for project-root imports). */
    std::filesystem::path entry_dir;

    /** @brief Explicit stdlib roots (e.g. from --stdlib-root or env var). */
    std::vector<std::filesystem::path> stdlib_roots;

    /** @brief Whether to allow standard library relative imports (e.g. 'std.io'). */
    bool allow_stdlib = true;

    /** @brief Whether to print debug traces to stderr. */
    bool debug_trace = false;
};

/**
 * @brief Represents the result of a successful module resolution.
 */
struct ResolvedModule
{
    /** @brief The absolute path to the module file (.curlee). */
    std::filesystem::path path;

    /** @brief The canonical import key (for deduplication/cycle checking). */
    std::string canonical_path;
};

/**
 * @brief Resolve a module import path to a concrete file on disk.
 *
 * Implements the v1 import resolution algorithm:
 * 1. Search in `importing_file_dir` (relative import).
 * 2. Search in `entry_dir` (project root), if different.
 * 3. Search in `stdlib_roots` (in order), if allowed.
 *
 * @param import_path The dotted import path components (e.g. ["std", "io"]).
 * @param options The search configuration.
 * @return The resolved module path, or a diagnostic if not found.
 */
[[nodiscard]] curlee::SingleResult<ResolvedModule>
resolve_module_path(const std::vector<std::string_view>& import_path,
                    const ModuleSearchOptions& options);

} // namespace curlee::resolver
