#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/parser/ast.h>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
#include <z3++.h>

/**
 * @file predicate_lowering.h
 * @brief Lower parser predicates into Z3 expressions for verification.
 */

namespace curlee::verification
{

/**
 * @brief Context used when lowering predicates to Z3 expressions.
 *
 * Holds temporary results and symbol maps for int and bool variables.
 */
struct LoweringContext
{
    explicit LoweringContext(z3::context& context) : ctx(context) {}

    z3::context& ctx;
    std::optional<z3::expr> result_int;
    std::optional<z3::expr> result_bool;
    std::unordered_map<std::string_view, z3::expr> int_vars;
    std::unordered_map<std::string_view, z3::expr> bool_vars;
};

/** @brief Result of lowering: either a z3::expr or a diagnostic on error. */
using LoweringResult = std::variant<z3::expr, curlee::diag::Diagnostic>;

/** @brief Lower a parsed predicate into a Z3 expression using the given context. */
[[nodiscard]] LoweringResult lower_predicate(const curlee::parser::Pred& pred,
                                             const LoweringContext& ctx);

} // namespace curlee::verification
