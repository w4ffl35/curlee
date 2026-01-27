#pragma once

#include <cstdint>
#include <string>

namespace curlee::vm
{

enum class ValueKind
{
    Int,
    Bool,
    String,
    Unit,
};

struct Value
{
    ValueKind kind = ValueKind::Unit;
    std::int64_t int_value = 0;
    bool bool_value = false;
    std::string string_value;

    static Value int_v(std::int64_t v)
    {
        return Value{.kind = ValueKind::Int, .int_value = v, .bool_value = false, .string_value = {}};
    }

    static Value bool_v(bool v)
    {
        return Value{.kind = ValueKind::Bool, .int_value = 0, .bool_value = v, .string_value = {}};
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
};

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
    }
    return false;
}

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
    }
    return "<unknown>";
}

} // namespace curlee::vm
