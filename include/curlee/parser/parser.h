#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/lexer/token.h>
#include <curlee/parser/ast.h>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace curlee::parser
{

using ParseResult = std::variant<Program, std::vector<curlee::diag::Diagnostic>>;

[[nodiscard]] ParseResult parse(std::span<const curlee::lexer::Token> tokens);
[[nodiscard]] std::string dump(const Program& program);

// Recomputes expression IDs so they are unique across the full program.
// Useful after program transformations (e.g., merging imported modules).
void reassign_expr_ids(Program& program);

} // namespace curlee::parser
