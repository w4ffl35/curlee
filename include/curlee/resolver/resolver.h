#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/parser/ast.h>
#include <curlee/resolver/symbol.h>
#include <curlee/source/source_file.h>
#include <filesystem>
#include <optional>
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

/** @brief Resolve names in `program`. */
[[nodiscard]] ResolveResult resolve(const curlee::parser::Program& program);
/** @brief Resolve names with an associated source file (for precise spans). */
[[nodiscard]] ResolveResult resolve(const curlee::parser::Program& program,
                                    const curlee::source::SourceFile& source);
/** @brief Resolve with an optional entry directory to resolve import paths. */
[[nodiscard]] ResolveResult resolve(const curlee::parser::Program& program,
                                    const curlee::source::SourceFile& source,
                                    std::optional<std::filesystem::path> entry_dir);

} // namespace curlee::resolver
