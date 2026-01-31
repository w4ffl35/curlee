#pragma once

#include <istream>
#include <string>
#include <variant>

/**
 * @file source_file.h
 * @brief Loading utilities for source files and streams.
 */

namespace curlee::source
{

/** @brief Represents the contents of a source file on disk. */
struct SourceFile
{
    std::string path;     /**< File path (as provided to the loader). */
    std::string contents; /**< Raw file contents. */
};

/** @brief Error returned when a source cannot be loaded. */
struct LoadError
{
    std::string message;
};

using LoadResult = std::variant<SourceFile, LoadError>;

/** @brief Load the file at `path` into a SourceFile or return a LoadError. */
LoadResult load_source_file(const std::string& path);

/** @brief Load source from the provided input stream; `path` is used for diagnostics. */
LoadResult load_source_stream(std::istream& in, const std::string& path);

} // namespace curlee::source
