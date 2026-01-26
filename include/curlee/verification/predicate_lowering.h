#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/parser/ast.h>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
#include <z3++.h>

namespace curlee::verification
{

struct LoweringContext
{
    explicit LoweringContext(z3::context& context) : ctx(context) {}

    z3::context& ctx;
    std::optional<z3::expr> result_int;
    std::optional<z3::expr> result_bool;
    std::unordered_map<std::string_view, z3::expr> int_vars;
    std::unordered_map<std::string_view, z3::expr> bool_vars;
};

using LoweringResult = std::variant<z3::expr, curlee::diag::Diagnostic>;

[[nodiscard]] LoweringResult lower_predicate(const curlee::parser::Pred& pred,
                                             const LoweringContext& ctx);

} // namespace curlee::verification
