#pragma once

#include <optional>
#include <string>
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
    Struct,
    Enum,
};

struct Type
{
    TypeKind kind;
    // For nominal types (Struct/Enum), this is the declared type name.
    // Empty for core scalar types.
    std::string_view name = {};
};

[[nodiscard]] constexpr bool operator==(Type a, Type b)
{
    if (a.kind != b.kind)
    {
        return false;
    }
    if (a.kind == TypeKind::Struct || a.kind == TypeKind::Enum)
    {
        return a.name == b.name;
    }
    return true;
}

struct FunctionType
{
    std::vector<Type> params;
    Type result;
};

[[nodiscard]] inline bool operator==(const FunctionType& a, const FunctionType& b)
{
    return a.params == b.params && a.result == b.result;
}

struct CapabilityType
{
    // Opaque capability-bearing values: equality is name-based.
    std::string name;
};

[[nodiscard]] inline bool operator==(const CapabilityType& a, const CapabilityType& b)
{
    return a.name == b.name;
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
    case TypeKind::Struct:
        return "Struct";
    case TypeKind::Enum:
        return "Enum";
    }
    return "<unknown>";
}

[[nodiscard]] constexpr std::string_view to_string(Type t)
{
    if (t.kind == TypeKind::Struct || t.kind == TypeKind::Enum)
    {
        return t.name;
    }
    return to_string(t.kind);
}

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
