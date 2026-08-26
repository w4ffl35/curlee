// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
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
    Capability,
    Vec,
    Set,
    Struct,
    Enum,

    // Unsigned fixed-width integer types (Phys element kinds).
    U8,
    U16,
    U32,
    U64,

    // Physical memory pointer type (element kind stored in element_kind/element_name).
    Phys,

    // Fixed-size array type `[T; N]` (element kind stored in element_kind/
    // element_name, length in array_len).
    Array,
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
    // For Vec<T>, this stores the element type kind/name.
    std::optional<TypeKind> element_kind = std::nullopt;
    std::string_view element_name = {};
    // For the fixed-size array type `[T; N]`, the compile-time length.
    // Unset for all other kinds.
    std::optional<std::uint64_t> array_len = std::nullopt;
};

[[nodiscard]] constexpr bool operator==(Type a, Type b)
{
    if (a.kind != b.kind)
    {
        return false;
    }
    if (a.kind == TypeKind::Capability || a.kind == TypeKind::Struct || a.kind == TypeKind::Enum)
    {
        return a.name == b.name;
    }
    if (a.kind == TypeKind::Vec)
    {
        return a.element_kind == b.element_kind && a.element_name == b.element_name;
    }
    if (a.kind == TypeKind::Set)
    {
        return a.element_kind == b.element_kind && a.element_name == b.element_name;
    }
    if (a.kind == TypeKind::Phys)
    {
        return a.element_kind == b.element_kind && a.element_name == b.element_name;
    }
    if (a.kind == TypeKind::Array)
    {
        return a.element_kind == b.element_kind && a.element_name == b.element_name &&
               a.array_len == b.array_len;
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
    case TypeKind::Capability:
        return "cap";
    case TypeKind::Vec:
        return "Vec";
    case TypeKind::Set:
        return "Set";
    case TypeKind::Struct:
        return "Struct";
    case TypeKind::Enum:
        return "Enum";
    case TypeKind::U8:
        return "U8";
    case TypeKind::U16:
        return "U16";
    case TypeKind::U32:
        return "U32";
    case TypeKind::U64:
        return "U64";
    case TypeKind::Phys:
        return "Phys";
    case TypeKind::Array:
        return "Array";
    }
    return "<unknown>";
}

[[nodiscard]] constexpr std::string_view to_string(Type t)
{
    if (t.kind == TypeKind::Capability || t.kind == TypeKind::Struct || t.kind == TypeKind::Enum)
    {
        return t.name;
    }
    if (t.kind == TypeKind::Vec)
    {
        return "Vec";
    }
    if (t.kind == TypeKind::Set)
    {
        return "Set";
    }
    if (t.kind == TypeKind::Phys)
    {
        // Phys<U32> style display.
        return "Phys";
    }
    return to_string(t.kind);
}

/**
 * @brief Human-readable display string for a Type, including array lengths
 * (`[Int; 8]`). Unlike the constexpr `to_string`, this can format the numeric
 * array length, so diagnostics use it when an array type may be involved.
 */
[[nodiscard]] inline std::string to_display_string(Type t)
{
    if (t.kind == TypeKind::Array)
    {
        const std::string elem = t.element_name.empty()
                                     ? std::string(to_string(t.element_kind.value_or(TypeKind::Int)))
                                     : std::string(t.element_name);
        const std::string len =
            t.array_len.has_value() ? std::to_string(*t.array_len) : std::string("<unknown>");
        return "[" + elem + "; " + len + "]";
    }
    return std::string(to_string(t));
}

/**
 * @brief True if the kind is a supported fixed-size array element kind:
 * Int, U8, U16, U32 or U64 (the freestanding storable core types; issue #278).
 * Bool arrays are out of the MVP scope.
 */
[[nodiscard]] constexpr bool is_array_element_kind(TypeKind kind)
{
    return kind == TypeKind::Int || kind == TypeKind::U8 || kind == TypeKind::U16 ||
           kind == TypeKind::U32 || kind == TypeKind::U64;
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
    if (name == "U8")
    {
        return Type{.kind = TypeKind::U8};
    }
    if (name == "U16")
    {
        return Type{.kind = TypeKind::U16};
    }
    if (name == "U32")
    {
        return Type{.kind = TypeKind::U32};
    }
    if (name == "U64")
    {
        return Type{.kind = TypeKind::U64};
    }
    return std::nullopt;
}

/**
 * @brief Resolve a Phys element-kind name ("U8", "U16", "U32", "U64") to a TypeKind.
 *
 * Returns std::nullopt for any other name. Single source of truth for the set of
 * element kinds supported by `Phys<T>`.
 */
[[nodiscard]] inline std::optional<TypeKind> phys_element_kind_from_name(std::string_view name)
{
    if (name == "U8")
    {
        return TypeKind::U8;
    }
    if (name == "U16")
    {
        return TypeKind::U16;
    }
    if (name == "U32")
    {
        return TypeKind::U32;
    }
    if (name == "U64")
    {
        return TypeKind::U64;
    }
    return std::nullopt;
}

/** @brief True if the kind is one of the supported Phys element kinds (U8/U16/U32/U64). */
[[nodiscard]] constexpr bool is_phys_element_kind(TypeKind kind)
{
    return kind == TypeKind::U8 || kind == TypeKind::U16 || kind == TypeKind::U32 ||
           kind == TypeKind::U64;
}

} // namespace curlee::types
