#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/lexer/token.h>
#include <string_view>
#include <variant>
#include <vector>

/**
 * @file lexer.h
 * @brief Public lexer API: tokenizes input into Token sequences or a diagnostic.
 */

namespace curlee::lexer
{

/** @brief Result of lexing: token vector on success, diagnostic on failure. */
using LexResult = std::variant<std::vector<Token>, curlee::diag::Diagnostic>;

/** @brief Lex the provided input into tokens. On success includes a terminal Eof token. */
[[nodiscard]] LexResult lex(std::string_view input);

} // namespace curlee::lexer
