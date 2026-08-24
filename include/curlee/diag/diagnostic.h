// SPDX-License-Identifier: MIT
#pragma once

#include <curlee/source/span.h>
#include <optional>
#include <string>
#include <vector>

/**
 * @file diagnostic.h
 * @brief Types for diagnostics (errors, warnings and notes) produced by the compiler.
 */

namespace curlee::diag
{

/** @brief Severity level for a diagnostic. */
enum class Severity
{
    Error,
    Warning,
    Note,
};

/** @brief Additional related message attached to a diagnostic, with optional span. */
struct Related
{
    std::string message;
    std::optional<curlee::source::Span> span;
};

/**
 * @brief A diagnostic message with optional source span and related notes.
 *
 * Diagnostics are used throughout the toolchain to report errors and warnings.
 */
struct Diagnostic
{
    Severity severity = Severity::Error;
    std::string message;
    std::optional<curlee::source::Span> span;
    std::vector<Related> notes;

    // Optional owning source file path. Populated by producers that can
    // attribute a span to a specific file (e.g. the CLI merge of imported
    // modules); consumers use it to render against the correct file when
    // spans are plain byte offsets that don't carry file identity.
    std::optional<std::string> file_path;

    // Optional name of the function whose body produced this diagnostic.
    // Useful when spans are per-file offsets that collide across imported
    // files; the consumer can resolve the file via a function->file map.
    std::string function_name;
};

} // namespace curlee::diag
