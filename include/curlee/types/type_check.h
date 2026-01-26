#pragma once

#include <cstddef>
#include <curlee/diag/diagnostic.h>
#include <curlee/types/type.h>
#include <unordered_map>
#include <variant>
#include <vector>

namespace curlee::parser
{
struct Expr;
struct Program;
} // namespace curlee::parser

namespace curlee::types
{

struct TypeInfo
{
    std::unordered_map<std::size_t, Type> expr_types;
};

using TypeCheckResult = std::variant<TypeInfo, std::vector<curlee::diag::Diagnostic>>;

[[nodiscard]] TypeCheckResult type_check(const curlee::parser::Program& program);

} // namespace curlee::types
