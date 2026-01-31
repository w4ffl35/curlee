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

/** @brief Result of parsing: either a Program or diagnostics. */
using ParseResult = std::variant<Program, std::vector<curlee::diag::Diagnostic>>;

/** @brief Parse a sequence of tokens into a Program or diagnostics. */
[[nodiscard]] ParseResult parse(std::span<const curlee::lexer::Token> tokens);
/** @brief Dump a Program to a human-readable string (for debugging/tests). */
[[nodiscard]] std::string dump(const Program& program);

/**
 * @brief Reassign expression ids so they are unique across the program.
 *
 * Useful after transformations that merge or reorder expressions.
 */
void reassign_expr_ids(Program& program);

} // namespace curlee::parser
