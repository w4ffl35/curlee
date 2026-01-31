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
};

} // namespace curlee::diag
