#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/parser/ast.h>
#include <curlee/types/type_check.h>
#include <variant>
#include <vector>

/**
 * @file checker.h
 * @brief Public API for program verification.
 */

namespace curlee::verification
{

/** @brief Marker type indicating verification succeeded. */
struct Verified
{
};

/** @brief Result of verification: success marker or diagnostics. */
using VerificationResult = std::variant<Verified, std::vector<curlee::diag::Diagnostic>>;

/** @brief Verify the program using provided type information; returns diagnostics on failure. */
[[nodiscard]] VerificationResult verify(const curlee::parser::Program& program,
                                        const curlee::types::TypeInfo& type_info);

} // namespace curlee::verification
