#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/parser/ast.h>
#include <curlee/vm/bytecode.h>
#include <variant>
#include <vector>

/**
 * @file emitter.h
 * @brief Bytecode emission API from the compiler's AST.
 */

namespace curlee::compiler
{

/** @brief Result of emitting bytecode: a Chunk or diagnostics. */
using EmitResult = std::variant<curlee::vm::Chunk, std::vector<curlee::diag::Diagnostic>>;

/** @brief Emit VM bytecode for the provided Program or return diagnostics. */
[[nodiscard]] EmitResult emit_bytecode(const curlee::parser::Program& program);

} // namespace curlee::compiler
