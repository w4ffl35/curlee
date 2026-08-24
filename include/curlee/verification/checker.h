// SPDX-License-Identifier: MIT
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

/**
 * @brief Verify the program using provided type information; returns diagnostics on failure.
 *
 * Note-severity diagnostics (e.g. the "extern boundary: contract assumed, not verified"
 * notice for extern declarations) are non-fatal: when the only diagnostics produced are
 * Notes, verification succeeds and the Notes are appended to `out_notes` (when non-null).
 * Error/Warning diagnostics still fail verification as before.
 */
[[nodiscard]] VerificationResult verify(const curlee::parser::Program& program,
                                        const curlee::types::TypeInfo& type_info,
                                        std::vector<curlee::diag::Diagnostic>* out_notes = nullptr);

} // namespace curlee::verification
