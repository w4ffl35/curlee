#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/parser/ast.h>
#include <curlee/vm/bytecode.h>
#include <variant>
#include <vector>

namespace curlee::compiler
{

using EmitResult = std::variant<curlee::vm::Chunk, std::vector<curlee::diag::Diagnostic>>;

[[nodiscard]] EmitResult emit_bytecode(const curlee::parser::Program& program);

} // namespace curlee::compiler
