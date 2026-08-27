// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

/**
 * @file value.h
 * @brief Runtime value representation used by the VM for evaluation and tests.
 */

namespace curlee::vm
{

struct Value;
struct VecValue;
struct SetValue;
struct ArrayValue;

enum class VecElementKind
{
    Int,
    Bool,
};

/** @brief Kind of a runtime value. */
enum class ValueKind
{
    Int,
    Bool,
    String,
    Unit,
    Struct,
    Enum,
    Vec,
    Set,
    Array,
};

/**
 * @brief A simple value container used by the VM.
 *
 * Uses explicit fields for each variant for simplicity and testing convenience.
 */
struct Value
{
    ValueKind kind = ValueKind::Unit;
    std::int64_t int_value = 0;
    bool bool_value = false;
    std::string string_value;
    std::string struct_name;
    std::vector<std::pair<std::string, std::shared_ptr<Value>>> struct_fields;
    std::string enum_name;
    std::string variant_name;
    std::shared_ptr<Value> payload;
    std::shared_ptr<VecValue> vec_value;
    std::shared_ptr<SetValue> set_value;
    std::shared_ptr<ArrayValue> array_value;

    static Value int_v(std::int64_t v)
    {
        return Value{
            .kind = ValueKind::Int,
            .int_value = v,
            .bool_value = false,
            .string_value = {},
            .struct_name = {},
            .struct_fields = {},
            .enum_name = {},
            .variant_name = {},
            .payload = nullptr,
            .vec_value = nullptr,
            .set_value = nullptr,
            .array_value = nullptr,
        };
    } // GCOVR_EXCL_LINE

    static Value bool_v(bool v)
    {
        return Value{
            .kind = ValueKind::Bool,
            .int_value = 0,
            .bool_value = v,
            .string_value = {},
            .struct_name = {},
            .struct_fields = {},
            .enum_name = {},
            .variant_name = {},
            .payload = nullptr,
            .vec_value = nullptr,
            .set_value = nullptr,
            .array_value = nullptr,
        };
    } // GCOVR_EXCL_LINE

    static Value string_v(std::string v)
    {
        Value out;
        out.kind = ValueKind::String;
        out.int_value = 0;
        out.bool_value = false;
        out.string_value = std::move(v);
        return out;
    } // GCOVR_EXCL_LINE

    static Value unit_v() { return Value{}; }

    static Value struct_v(std::string struct_name, std::vector<std::pair<std::string, Value>> fields)
    {
        Value out;
        out.kind = ValueKind::Struct;
        out.struct_name = std::move(struct_name);
        out.struct_fields.reserve(fields.size());
        for (auto& [name, value] : fields)
        {
            out.struct_fields.emplace_back(std::move(name),
                                           std::make_shared<Value>(std::move(value)));
        }
        return out;
    } // GCOVR_EXCL_LINE

    static Value vec_v(std::size_t max_len, VecElementKind element_kind = VecElementKind::Int);

    static Value set_v()
    {
        Value out;
        out.kind = ValueKind::Set;
        out.set_value = std::make_shared<SetValue>();
        return out;
    } // GCOVR_EXCL_LINE

    static Value enum_v(std::string enum_name, std::string variant_name,
                        std::optional<Value> payload = std::nullopt)
    {
        Value out;
        out.kind = ValueKind::Enum;
        out.enum_name = std::move(enum_name);
        out.variant_name = std::move(variant_name);
        if (payload.has_value())
        {
            out.payload = std::make_shared<Value>(std::move(*payload));
        }
        return out;
    } // GCOVR_EXCL_LINE

    /**
     * @brief A fixed-size array value (`[T; N]`, issue #278) as interpreted by
     * the VM (issue #300): a normal in-memory buffer of element values. The
     * element type name ("Int"/"U8"/"U16"/"U32"/"U64") is carried for display
     * parity with the freestanding target; every element is stored as an Int
     * (the VM's single numeric representation).
     */
    static Value array_v(std::string element_name, std::vector<Value> items);
};

struct VecValue
{
    std::size_t max_len = 0;
    VecElementKind element_kind = VecElementKind::Int;
    std::vector<Value> items;
};

struct SetValue
{
    std::unordered_set<std::int64_t> items;
};

struct ArrayValue
{
    // Element type name as written in the source ("Int", "U8", "U16", "U32",
    // "U64"); all elements are stored as ValueKind::Int in the VM.
    std::string element_name;
    std::vector<Value> items;
};

inline Value Value::array_v(std::string element_name, std::vector<Value> items)
{
    Value out;
    out.kind = ValueKind::Array;
    out.array_value = std::make_shared<ArrayValue>();
    out.array_value->element_name = std::move(element_name);
    out.array_value->items = std::move(items);
    return out;
} // GCOVR_EXCL_LINE

inline Value Value::vec_v(std::size_t max_len, VecElementKind element_kind)
{
    Value out;
    out.kind = ValueKind::Vec;
    out.vec_value = std::make_shared<VecValue>();
    out.vec_value->max_len = max_len;
    out.vec_value->element_kind = element_kind;
    return out;
} // GCOVR_EXCL_LINE

/** @brief Equality comparison for values. */
inline bool operator==(const Value& a, const Value& b)
{
    if (a.kind != b.kind)
    {
        return false;
    }
    switch (a.kind)
    {
    case ValueKind::Int:
        return a.int_value == b.int_value;
    case ValueKind::Bool:
        return a.bool_value == b.bool_value;
    case ValueKind::String:
        return a.string_value == b.string_value;
    case ValueKind::Unit:
        return true;
    case ValueKind::Struct:
        if (a.struct_name != b.struct_name || a.struct_fields.size() != b.struct_fields.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.struct_fields.size(); ++i)
        {
            if (a.struct_fields[i].first != b.struct_fields[i].first)
            {
                return false;
            }
            const auto& av = a.struct_fields[i].second;
            const auto& bv = b.struct_fields[i].second;
            if (av == nullptr || bv == nullptr)
            {
                if (av != nullptr || bv != nullptr)
                {
                    return false;
                }
                continue;
            }
            if (!(*av == *bv))
            {
                return false;
            }
        }
        return true;
    case ValueKind::Enum:
        if (a.enum_name != b.enum_name || a.variant_name != b.variant_name) // GCOVR_EXCL_LINE
        {
            return false;
        }
        if (a.payload == nullptr || b.payload == nullptr) // GCOVR_EXCL_LINE
        {
            return a.payload == nullptr && b.payload == nullptr; // GCOVR_EXCL_LINE
        }
        return *a.payload == *b.payload;
    case ValueKind::Vec:
        if (a.vec_value == nullptr || b.vec_value == nullptr)
        {
            return a.vec_value == nullptr && b.vec_value == nullptr;
        }
        return a.vec_value->max_len == b.vec_value->max_len &&
               a.vec_value->element_kind == b.vec_value->element_kind &&
               a.vec_value->items == b.vec_value->items;
    case ValueKind::Set:
        if (a.set_value == nullptr || b.set_value == nullptr)
        {
            return a.set_value == nullptr && b.set_value == nullptr;
        }
        return a.set_value->items == b.set_value->items;
    case ValueKind::Array:
        if (a.array_value == nullptr || b.array_value == nullptr)
        {
            return a.array_value == nullptr && b.array_value == nullptr;
        }
        return a.array_value->element_name == b.array_value->element_name &&
               a.array_value->items == b.array_value->items;
    }
    return false;
}

/** @brief Convert a runtime Value to a human-readable string. */
inline std::string to_string(const Value& v)
{
    switch (v.kind)
    {
    case ValueKind::Int:
        return std::to_string(v.int_value);
    case ValueKind::Bool:
        return v.bool_value ? "true" : "false";
    case ValueKind::String:
        return v.string_value;
    case ValueKind::Unit:
        return "()";
    case ValueKind::Struct:
    {
        std::string out = v.struct_name + "{";
        for (std::size_t i = 0; i < v.struct_fields.size(); ++i)
        {
            if (i > 0)
            {
                out += ", ";
            }
            out += v.struct_fields[i].first + ": ";
            if (v.struct_fields[i].second == nullptr)
            {
                out += "<null>";
            }
            else
            {
                out += to_string(*v.struct_fields[i].second);
            }
        }
        out += "}";
        return out;
    }
    case ValueKind::Enum:
        if (v.payload == nullptr)
        {
            return v.enum_name + "::" + v.variant_name;
        }
        return v.enum_name + "::" + v.variant_name + "(" + to_string(*v.payload) + ")";
    case ValueKind::Vec:
        if (v.vec_value == nullptr)
        {
            return "Vec<?>(null)";
        }
        return std::string(v.vec_value->element_kind == VecElementKind::Bool ? "Vec<Bool>" :
                                                                             "Vec<Int>") +
               "(len=" + std::to_string(v.vec_value->items.size()) + "/" +
               std::to_string(v.vec_value->max_len) + ")";
    case ValueKind::Set:
        if (v.set_value == nullptr)
        {
            return "Set<Int>(null)";
        }
        return "Set<Int>(len=" + std::to_string(v.set_value->items.size()) + ")";
    case ValueKind::Array:
        if (v.array_value == nullptr)
        {
            return "Array<?>(null)";
        }
        return "[" + v.array_value->element_name + "; " +
               std::to_string(v.array_value->items.size()) + "]";
    }
    return "<unknown>";
}

} // namespace curlee::vm
