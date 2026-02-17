#pragma once

#include <curlee/diag/diagnostic.h>
#include <expected>
#include <vector>

namespace curlee
{

/**
 * @brief A result type that returns T on success, or a list of diagnostics on failure.
 *
 * Use this for high-level compiler stages (parser, resolver, etc.) that can aggregate errors.
 */
template <typename T> using Result = std::expected<T, std::vector<curlee::diag::Diagnostic>>;

/**
 * @brief A result type that returns T on success, or a single diagnostic on failure.
 *
 * Use this for lower-level or leaf operations that fail with a specific reason.
 */
template <typename T> using SingleResult = std::expected<T, curlee::diag::Diagnostic>;

} // namespace curlee
