#pragma once

#include <cstddef>
#include <curlee/diag/diagnostic.h>
#include <curlee/types/type.h>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace curlee::parser
{
struct Expr;
struct Program;
} // namespace curlee::parser

/**
 * @file type_check.h
 * @brief Type checking API and result types.
 */

namespace curlee::types
{

/** @brief Type information mapping expression ids to inferred types. */
struct TypeInfo
{
    std::unordered_map<std::size_t, Type> expr_types;

    struct RequiredCapability
    {
        std::string_view name;
        curlee::source::Span span;
    };

    std::vector<RequiredCapability> required_capabilities;

    [[nodiscard]] std::optional<Type> type_of(std::size_t expr_id) const
    {
        const auto it = expr_types.find(expr_id);
        if (it == expr_types.end())
        {
            return std::nullopt;
        }
        return it->second;
    }
};

/** @brief Result of running type checking: either TypeInfo or a list of diagnostics. */
using TypeCheckResult = std::variant<TypeInfo, std::vector<curlee::diag::Diagnostic>>;

/** @brief Run type checking on the program, returning types or diagnostics. */
[[nodiscard]] TypeCheckResult type_check(const curlee::parser::Program& program);

} // namespace curlee::types
