#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace curlee::types
{

enum class TypeKind
{
    Int,
    Bool,
    String,
    Unit,
};

struct Type
{
    TypeKind kind;
};

[[nodiscard]] constexpr bool operator==(Type a, Type b) { return a.kind == b.kind; }

struct FunctionType
{
    std::vector<Type> params;
    Type result;
};

[[nodiscard]] inline bool operator==(const FunctionType& a, const FunctionType& b)
{
    return a.params == b.params && a.result == b.result;
}

[[nodiscard]] constexpr std::string_view to_string(TypeKind kind)
{
    switch (kind)
    {
    case TypeKind::Int:
        return "Int";
    case TypeKind::Bool:
        return "Bool";
    case TypeKind::String:
        return "String";
    case TypeKind::Unit:
        return "Unit";
    }
    return "<unknown>";
}

[[nodiscard]] constexpr std::string_view to_string(Type t) { return to_string(t.kind); }

[[nodiscard]] inline std::optional<Type> core_type_from_name(std::string_view name)
{
    if (name == "Int")
    {
        return Type{.kind = TypeKind::Int};
    }
    if (name == "Bool")
    {
        return Type{.kind = TypeKind::Bool};
    }
    if (name == "String")
    {
        return Type{.kind = TypeKind::String};
    }
    if (name == "Unit")
    {
        return Type{.kind = TypeKind::Unit};
    }
    return std::nullopt;
}

} // namespace curlee::types
