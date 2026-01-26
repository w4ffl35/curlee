#pragma once

#include <cstdint>
#include <string>

namespace curlee::vm
{

enum class ValueKind
{
    Int,
    Bool,
    Unit,
};

struct Value
{
    ValueKind kind = ValueKind::Unit;
    std::int64_t int_value = 0;
    bool bool_value = false;

    static Value int_v(std::int64_t v)
    {
        return Value{.kind = ValueKind::Int, .int_value = v, .bool_value = false};
    }

    static Value bool_v(bool v)
    {
        return Value{.kind = ValueKind::Bool, .int_value = 0, .bool_value = v};
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
    case ValueKind::Unit:
        return "()";
    }
    return "<unknown>";
}

} // namespace curlee::vm
