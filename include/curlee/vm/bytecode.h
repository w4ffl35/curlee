#pragma once

#include <cstddef>
#include <cstdint>
#include <curlee/source/span.h>
#include <curlee/vm/value.h>
#include <vector>

namespace curlee::vm
{

enum class OpCode : std::uint8_t
{
    Constant,
    LoadLocal,
    StoreLocal,
    Add,
    Sub,
    Mul,
    Div,
    Neg,
    Not,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Pop,
    Return,
    Jump,
    JumpIfFalse,
    Call,
    Ret,
    Print,
    PythonCall,
};

struct Chunk
{
    std::vector<std::uint8_t> code;
    std::vector<Value> constants;
    std::vector<curlee::source::Span> spans;
    std::size_t max_locals = 0;

    std::size_t add_constant(Value value)
    {
        constants.push_back(value);
        return constants.size() - 1;
    }

    void emit(OpCode op, curlee::source::Span span = {})
    {
        code.push_back(static_cast<std::uint8_t>(op));
        spans.push_back(span);
    }

    void emit_u16(std::uint16_t value, curlee::source::Span span = {})
    {
        code.push_back(static_cast<std::uint8_t>(value & 0xFF));
        code.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        spans.push_back(span);
        spans.push_back(span);
    }

    void emit_constant(Value value, curlee::source::Span span = {})
    {
        const auto idx = add_constant(value);
        emit(OpCode::Constant, span);
        emit_u16(static_cast<std::uint16_t>(idx), span);
    }

    void emit_local(OpCode op, std::uint16_t slot, curlee::source::Span span = {})
    {
        emit(op, span);
        emit_u16(slot, span);
        if (op == OpCode::StoreLocal && static_cast<std::size_t>(slot) + 1 > max_locals)
        {
            max_locals = static_cast<std::size_t>(slot) + 1;
        }
    }
};

} // namespace curlee::vm
