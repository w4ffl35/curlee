#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/**
 * @file value.h
 * @brief Runtime value representation used by the VM for evaluation and tests.
 */

namespace curlee::vm
{

struct Value;
struct VecValue;

/** @brief Kind of a runtime value. */
enum class ValueKind
{
    Int,
    Bool,
    String,
    Unit,
    Enum,
    Vec,
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
    std::string enum_name;
    std::string variant_name;
    std::shared_ptr<Value> payload;
    std::shared_ptr<VecValue> vec_value;

    static Value int_v(std::int64_t v)
    {
        return Value{
            .kind = ValueKind::Int,
            .int_value = v,
            .bool_value = false,
            .string_value = {},
            .enum_name = {},
            .variant_name = {},
            .payload = nullptr,
            .vec_value = nullptr,
        };
    } // GCOVR_EXCL_LINE

    static Value bool_v(bool v)
    {
        return Value{
            .kind = ValueKind::Bool,
            .int_value = 0,
            .bool_value = v,
            .string_value = {},
            .enum_name = {},
            .variant_name = {},
            .payload = nullptr,
            .vec_value = nullptr,
        };
    }

    static Value string_v(std::string v)
    {
        Value out;
        out.kind = ValueKind::String;
        out.int_value = 0;
        out.bool_value = false;
        out.string_value = std::move(v);
        return out;
    }

    static Value unit_v() { return Value{}; }

    static Value vec_v(std::size_t max_len);

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
};

struct VecValue
{
    std::size_t max_len = 0;
    std::vector<Value> items;
};

inline Value Value::vec_v(std::size_t max_len)
{
    Value out;
    out.kind = ValueKind::Vec;
    out.vec_value = std::make_shared<VecValue>();
    out.vec_value->max_len = max_len;
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
               a.vec_value->items == b.vec_value->items;
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
        return "Vec(len=" + std::to_string(v.vec_value->items.size()) + "/" +
               std::to_string(v.vec_value->max_len) + ")";
    }
    return "<unknown>";
}

} // namespace curlee::vm
