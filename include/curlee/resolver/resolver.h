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

namespace curlee::resolver
{

struct Symbol
{
    SymbolId id;
    std::string_view name;
    curlee::source::Span span;
};

struct NameUse
{
    SymbolId target;
    curlee::source::Span span;
};

struct Resolution
{
    std::vector<Symbol> symbols;
    std::vector<NameUse> uses;
};

using ResolveResult = std::variant<Resolution, std::vector<curlee::diag::Diagnostic>>;

[[nodiscard]] ResolveResult resolve(const curlee::parser::Program& program);
[[nodiscard]] ResolveResult resolve(const curlee::parser::Program& program,
                                    const curlee::source::SourceFile& source);
[[nodiscard]] ResolveResult resolve(const curlee::parser::Program& program,
                                    const curlee::source::SourceFile& source,
                                    std::optional<std::filesystem::path> entry_dir);

} // namespace curlee::resolver
