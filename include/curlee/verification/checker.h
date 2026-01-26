#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/parser/ast.h>
#include <curlee/types/type_check.h>
#include <variant>
#include <vector>

namespace curlee::verification
{

struct Verified
{
};

using VerificationResult = std::variant<Verified, std::vector<curlee::diag::Diagnostic>>;

[[nodiscard]] VerificationResult verify(const curlee::parser::Program& program,
                                        const curlee::types::TypeInfo& type_info);

} // namespace curlee::verification
