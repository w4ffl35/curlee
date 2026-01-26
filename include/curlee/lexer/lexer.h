#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/lexer/token.h>
#include <string_view>
#include <variant>
#include <vector>

namespace curlee::lexer
{

using LexResult = std::variant<std::vector<Token>, curlee::diag::Diagnostic>;

// Lexes the full input into tokens. On success includes a terminal Eof token.
[[nodiscard]] LexResult lex(std::string_view input);

} // namespace curlee::lexer
