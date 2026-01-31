#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file type.h
 * @brief Basic type representations used by the resolver and verifier.
 */

namespace curlee::types
{

/** @brief Kind of a type (primitive or nominal). */
enum class TypeKind
{
    Int,
    Bool,
    String,
    Unit,
    Struct,
    Enum,
};

/**
 * @brief A lightweight type descriptor.
 *
 * For nominal types (Struct/Enum) the `name` field stores the declared type name.
 */
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

/** @brief Function type with parameter types and a result type. */
struct FunctionType
{
    std::vector<Type> params;
    Type result;
};

[[nodiscard]] inline bool operator==(const FunctionType& a, const FunctionType& b)
{
    return a.params == b.params && a.result == b.result;
}

/** @brief Opaque capability type (identified by name). */
struct CapabilityType
{
    // Opaque capability-bearing values: equality is name-based.
    std::string name;
};

[[nodiscard]] inline bool operator==(const CapabilityType& a, const CapabilityType& b)
{
    return a.name == b.name;
}

/** @brief Stringify a TypeKind for diagnostics and tests. */
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

/** @brief Resolve a core type name ("Int", "Bool", "String", "Unit") to a Type. */
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
