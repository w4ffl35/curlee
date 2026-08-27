// SPDX-License-Identifier: MIT
#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/parser/ast.h>
#include <curlee/parser/parser.h>
#include <curlee/resolver/symbol.h>
#include <curlee/source/source_file.h>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

/**
 * @file resolver.h
 * @brief Name resolution API and result types.
 */

namespace curlee::resolver
{

/** @brief A resolved symbol with its id, name and declaration span. */
struct Symbol
{
    SymbolId id;
    std::string_view name;
    curlee::source::Span span;
};

/** @brief A use of a name referring to a target symbol, with its span. */
struct NameUse
{
    SymbolId target;
    curlee::source::Span span;
};

/** @brief Resolution result containing symbol table and uses mapping. */
struct Resolution
{
    std::vector<Symbol> symbols;
    std::vector<NameUse> uses;
};

using ResolveResult = std::variant<Resolution, std::vector<curlee::diag::Diagnostic>>;

// Re-exported so callers (the CLI) can pass `--define` build constants
// (issue #296) through name resolution to the parser's imported-module
// parsing. A define name resolves in array-length / repeat-count constant
// expressions and primary-expression positions (see parser.h).
using BuildDefine = curlee::parser::BuildDefine;

/**
 * @brief True if `name` is a compiler/runtime builtin call name (print, the
 * __* intrinsics, variant_is/variant_unwrap).
 *
 * This is the single source of truth for the builtin call surface. Consumers
 * that must reject or special-case builtins (e.g. the freestanding codegen
 * backend) reuse this instead of maintaining a second list.
 */
[[nodiscard]] bool is_builtin_call_name(std::string_view name);

/** @brief Resolve names in `program`. */
[[nodiscard]] ResolveResult resolve(const curlee::parser::Program& program);
/** @brief Resolve names with an associated source file (for precise spans). */
[[nodiscard]] ResolveResult resolve(const curlee::parser::Program& program,
                                    const curlee::source::SourceFile& source);
/**
 * @brief Resolve with explicit stdlib roots.
 *
 * @param defines Build-time constants (`--define NAME=VALUE`, issue #296)
 *                forwarded to the parser's imported-module parsing (a module
 *                that sizes static arrays from defines parses identically to
 *                the entry file). Empty by default.
 */
[[nodiscard]] ResolveResult resolve(const curlee::parser::Program& program,
                                    const curlee::source::SourceFile& source,
                                    std::optional<std::filesystem::path> entry_dir,
                                    std::vector<std::filesystem::path> stdlib_roots = {},
                                    std::span<const BuildDefine> defines = {});

} // namespace curlee::resolver
