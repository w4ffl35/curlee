// SPDX-License-Identifier: MIT
#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/lexer/token.h>
#include <curlee/parser/ast.h>
#include <span>
#include <string>
#include <variant>
#include <vector>

/**
 * @file parser.h
 * @brief Public parser API returning parse results or diagnostics.
 */

namespace curlee::parser
{

/**
 * @brief A build-time constant injected via `curlee build --define NAME=VALUE`
 * (or `curlee check --define NAME=VALUE`), issue #296.
 *
 * Build defines are per-build-target static array sizing: the SAME source
 * declares a static array whose size is a constant expression over defines
 * (e.g. `static buf: [U8; BUF_SIZE] = [0; BUF_SIZE];`), and two `curlee build`
 * invocations with different define values emit different-sized arrays. The
 * parser resolves define names in two positions:
 *   - array length / repeat count constant expressions (`[T; W * H]`,
 *     `[v; W * H]`), folded to the decimal value at parse time; and
 *   - primary-expression positions, where an identifier matching a define
 *     name lowers to an `IntExpr` holding the constant's decimal value.
 * A define name that is not provided is a plain identifier (an unresolved
 * name error downstream); there is no implicit default. This is NOT a textual
 * preprocessor — no `#ifdef`, no macro expansion, no text substitution —
 * only typed integer constants resolved by the parser.
 */
struct BuildDefine
{
    std::string name;   // [A-Za-z_][A-Za-z0-9_]*
    std::uint64_t value;
};

/** @brief Result of parsing: either a Program or diagnostics. */
using ParseResult = std::variant<Program, std::vector<curlee::diag::Diagnostic>>;

/**
 * @brief Parse a sequence of tokens into a Program or diagnostics.
 *
 * @param tokens  The lexed token stream.
 * @param defines Build-time constants (`--define NAME=VALUE`) resolved in
 *                array-length / repeat-count constant expressions and in
 *                primary-expression positions (issue #296). Empty by default.
 */
[[nodiscard]] ParseResult parse(std::span<const curlee::lexer::Token> tokens,
                                std::span<const BuildDefine> defines = {});
/** @brief Dump a Program to a human-readable string (for debugging/tests). */
[[nodiscard]] std::string dump(const Program& program);

/**
 * @brief Reassign expression ids so they are unique across the program.
 *
 * Useful after transformations that merge or reorder expressions.
 */
void reassign_expr_ids(Program& program);

} // namespace curlee::parser
