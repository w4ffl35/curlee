#include <curlee/vm/vm.h>
#include <limits>

namespace
{

const curlee::vm::VM::Capabilities& empty_caps()
{
    static const curlee::vm::VM::Capabilities caps;
    return caps;
}

} // namespace

namespace curlee::vm
{

bool VM::push(Value value)
{
    stack_.push_back(value);
    return true;
}

std::optional<Value> VM::pop()
{
    if (stack_.empty())
    {
        return std::nullopt;
    }
    Value value = stack_.back();
    stack_.pop_back();
    return value;
}

VmResult VM::run(const Chunk& chunk)
{
    return run(chunk, std::numeric_limits<std::size_t>::max(), empty_caps());
}

VmResult VM::run(const Chunk& chunk, std::size_t fuel)
{
    return run(chunk, fuel, empty_caps());
}

VmResult VM::run(const Chunk& chunk, const Capabilities& capabilities)
{
    return run(chunk, std::numeric_limits<std::size_t>::max(), capabilities);
}

VmResult VM::run(const Chunk& chunk, std::size_t fuel, const Capabilities& capabilities)
{
    stack_.clear();
    std::vector<Value> locals(chunk.max_locals, Value::unit_v());
    std::vector<std::size_t> call_stack;

    std::size_t ip = 0;
    while (ip < chunk.code.size())
    {
        if (fuel == 0)
        {
            return VmResult{.ok = false,
                            .value = Value::unit_v(),
                            .error = "out of fuel",
                            .error_span = std::nullopt};
        }
        --fuel;

        const std::size_t op_index = ip;
        const auto op = static_cast<OpCode>(chunk.code[ip++]);
        const auto span = (op_index < chunk.spans.size())
                              ? std::optional<curlee::source::Span>(chunk.spans[op_index])
                              : std::nullopt;
        switch (op)
        {
        case OpCode::Constant:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "truncated constant",
                                .error_span = span};
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t idx = static_cast<std::uint16_t>(lo | (hi << 8));
            if (idx >= chunk.constants.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "constant index out of range",
                                .error_span = span};
            }
            push(chunk.constants[idx]);
            break;
        }
        case OpCode::LoadLocal:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "truncated local index",
                                .error_span = span};
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t idx = static_cast<std::uint16_t>(lo | (hi << 8));
            if (idx >= locals.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "local index out of range",
                                .error_span = span};
            }
            push(locals[idx]);
            break;
        }
        case OpCode::StoreLocal:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "truncated local index",
                                .error_span = span};
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t idx = static_cast<std::uint16_t>(lo | (hi << 8));
            auto value = pop();
            if (!value.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (idx >= locals.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "local index out of range",
                                .error_span = span};
            }
            locals[idx] = *value;
            break;
        }
        case OpCode::Add:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind == ValueKind::Int && rhs->kind == ValueKind::Int)
            {
                push(Value::int_v(lhs->int_value + rhs->int_value));
                break;
            }
            if (lhs->kind == ValueKind::String && rhs->kind == ValueKind::String)
            {
                push(Value::string_v(lhs->string_value + rhs->string_value));
                break;
            }
            return VmResult{.ok = false,
                            .value = Value::unit_v(),
                            .error = "add expects Int or String",
                            .error_span = span};
            break;
        }
        case OpCode::Sub:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "sub expects Int",
                                .error_span = span};
            }
            push(Value::int_v(lhs->int_value - rhs->int_value));
            break;
        }
        case OpCode::Mul:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "mul expects Int",
                                .error_span = span};
            }
            push(Value::int_v(lhs->int_value * rhs->int_value));
            break;
        }
        case OpCode::Div:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "div expects Int",
                                .error_span = span};
            }
            if (rhs->int_value == 0)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "divide by zero",
                                .error_span = span};
            }
            push(Value::int_v(lhs->int_value / rhs->int_value));
            break;
        }
        case OpCode::Neg:
        {
            auto value = pop();
            if (!value.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (value->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "neg expects Int",
                                .error_span = span};
            }
            push(Value::int_v(-value->int_value));
            break;
        }
        case OpCode::Not:
        {
            auto value = pop();
            if (!value.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (value->kind != ValueKind::Bool)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "not expects Bool",
                                .error_span = span};
            }
            push(Value::bool_v(!value->bool_value));
            break;
        }
        case OpCode::Equal:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            push(Value::bool_v(*lhs == *rhs));
            break;
        }
        case OpCode::NotEqual:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            push(Value::bool_v(!(*lhs == *rhs)));
            break;
        }
        case OpCode::Less:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "lt expects Int",
                                .error_span = span};
            }
            push(Value::bool_v(lhs->int_value < rhs->int_value));
            break;
        }
        case OpCode::LessEqual:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "le expects Int",
                                .error_span = span};
            }
            push(Value::bool_v(lhs->int_value <= rhs->int_value));
            break;
        }
        case OpCode::Greater:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "gt expects Int",
                                .error_span = span};
            }
            push(Value::bool_v(lhs->int_value > rhs->int_value));
            break;
        }
        case OpCode::GreaterEqual:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "ge expects Int",
                                .error_span = span};
            }
            push(Value::bool_v(lhs->int_value >= rhs->int_value));
            break;
        }
        case OpCode::Pop:
        {
            if (!pop().has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            break;
        }
        case OpCode::Return:
        {
            auto result = pop();
            if (!result.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "missing return",
                                .error_span = span};
            }
            return VmResult{.ok = true, .value = *result, .error = {}, .error_span = std::nullopt};
        }
        case OpCode::Jump:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "truncated jump target",
                                .error_span = span};
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t target = static_cast<std::uint16_t>(lo | (hi << 8));
            if (static_cast<std::size_t>(target) >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "jump target out of range",
                                .error_span = span};
            }
            ip = static_cast<std::size_t>(target);
            break;
        }
        case OpCode::JumpIfFalse:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "truncated jump target",
                                .error_span = span};
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t target = static_cast<std::uint16_t>(lo | (hi << 8));

            auto cond = pop();
            if (!cond.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (cond->kind != ValueKind::Bool)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "jump-if-false expects Bool",
                                .error_span = span};
            }
            if (!cond->bool_value)
            {
                if (static_cast<std::size_t>(target) >= chunk.code.size())
                {
                    return VmResult{.ok = false,
                                    .value = Value::unit_v(),
                                    .error = "jump target out of range",
                                    .error_span = span};
                }
                ip = static_cast<std::size_t>(target);
            }
            break;
        }
        case OpCode::Call:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "truncated call target",
                                .error_span = span};
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t target = static_cast<std::uint16_t>(lo | (hi << 8));
            if (static_cast<std::size_t>(target) >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "call target out of range",
                                .error_span = span};
            }

            call_stack.push_back(ip);
            ip = static_cast<std::size_t>(target);
            break;
        }
        case OpCode::Ret:
        {
            if (call_stack.empty())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "return with empty call stack",
                                .error_span = span};
            }
            ip = call_stack.back();
            call_stack.pop_back();
            break;
        }
        case OpCode::Print:
        {
            if (!capabilities.contains("io:stdout"))
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "missing capability io:stdout",
                                .error_span = span};
            }
            auto value = pop();
            if (!value.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            // MVP: stub effect. No ambient IO; host can later wire an output sink.
            push(Value::unit_v());
            break;
        }
        case OpCode::PythonCall:
        {
            if (!capabilities.contains("python:ffi"))
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "python capability required",
                                .error_span = span};
            }

            return VmResult{.ok = false,
                            .value = Value::unit_v(),
                            .error = "python interop not implemented",
                            .error_span = span};
        }
        }
    }

    return VmResult{
        .ok = false, .value = Value::unit_v(), .error = "no return", .error_span = std::nullopt};
}

} // namespace curlee::vm
