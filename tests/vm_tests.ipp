#include <cerrno>
#include <cstdlib>
#include <curlee/source/span.h>
#include <curlee/vm/bytecode.h>
#include <curlee/vm/vm.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <sys/resource.h>
#include <unistd.h>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static std::string sibling_exe(const char* argv0, const char* name)
{
    const std::filesystem::path bin = std::filesystem::path(argv0);
    return (bin.parent_path() / name).string();
}

int main(int argc, char** argv)
{
    using namespace curlee::vm;

    struct EnvVarGuard
    {
        std::string key;
        bool had_old = false;
        std::string old;

        explicit EnvVarGuard(const char* k) : key(k)
        {
            if (const char* v = std::getenv(k); v != nullptr)
            {
                had_old = true;
                old = v;
            }
        }

        void set(const char* v) const { (void)setenv(key.c_str(), v, 1); }
        void unset() const { (void)unsetenv(key.c_str()); }

        ~EnvVarGuard()
        {
            if (had_old)
            {
                (void)setenv(key.c_str(), old.c_str(), 1);
            }
            else
            {
                (void)unsetenv(key.c_str());
            }
        }
    };

    // Cover Value equality and stringification for each kind.
    {
        const Value i1 = Value::int_v(7);
        const Value i2 = Value::int_v(7);
        const Value i3 = Value::int_v(8);
        if (!(i1 == i2) || (i1 == i3))
        {
            fail("unexpected Int value equality behavior");
        }

        const Value b1 = Value::bool_v(true);
        const Value b2 = Value::bool_v(true);
        const Value b3 = Value::bool_v(false);
        if (!(b1 == b2) || (b1 == b3))
        {
            fail("unexpected Bool value equality behavior");
        }

        const Value s1 = Value::string_v("hi");
        const Value s2 = Value::string_v("hi");
        const Value s3 = Value::string_v("bye");
        if (!(s1 == s2) || (s1 == s3))
        {
            fail("unexpected String value equality behavior");
        }

        const Value u1 = Value::unit_v();
        const Value u2 = Value::unit_v();
        if (!(u1 == u2))
        {
            fail("unexpected Unit value equality behavior");
        }

        if (i1 == b1 || i1 == s1 || i1 == u1)
        {
            fail("expected different Value kinds to compare unequal");
        }

        if (to_string(Value::int_v(42)) != "42")
        {
            fail("unexpected Int to_string");
        }
        if (to_string(Value::bool_v(true)) != "true" || to_string(Value::bool_v(false)) != "false")
        {
            fail("unexpected Bool to_string");
        }
        if (to_string(Value::string_v("abc")) != "abc")
        {
            fail("unexpected String to_string");
        }
        if (to_string(Value::unit_v()) != "()")
        {
            fail("unexpected Unit to_string");
        }

        Value unknown;
        unknown.kind = static_cast<ValueKind>(999);
        if (to_string(unknown) != "<unknown>")
        {
            fail("expected unknown ValueKind to stringify to '<unknown>'");
        }
        if (unknown == unknown)
        {
            fail("expected unknown ValueKind to compare unequal (defensive default)");
        }
    }

    auto run_twice_deterministic = [](const Chunk& chunk, Value expected)
    {
        VM vm;
        const auto res1 = vm.run(chunk);
        if (!res1.ok)
        {
            fail("expected VM to run successfully");
        }
        if (!(res1.value == expected))
        {
            fail("unexpected VM result");
        }

        const auto res2 = vm.run(chunk);
        if (!res2.ok || !(res2.value == expected))
        {
            fail("expected deterministic VM result across runs");
        }
    };

    auto expect_underflow = [](OpCode op, const char* what)
    {
        const curlee::source::Span span{.start = 900, .end = 901};
        Chunk chunk;
        chunk.emit(op, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail(std::string("expected ") + what + " stack underflow error");
        }
        if (!res.error_span.has_value() || res.error_span->start != span.start ||
            res.error_span->end != span.end)
        {
            fail(std::string("expected ") + what + " underflow span mapping");
        }
    };

    auto expect_underflow_one_operand = [](OpCode op, const char* what)
    {
        const curlee::source::Span span{.start = 902, .end = 903};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(op, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail(std::string("expected ") + what + " one-operand stack underflow error");
        }
        if (!res.error_span.has_value() || res.error_span->start != span.start ||
            res.error_span->end != span.end)
        {
            fail(std::string("expected ") + what + " one-operand underflow span mapping");
        }
    };

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1));
        chunk.emit_constant(Value::int_v(2));
        chunk.emit(OpCode::Add);
        chunk.emit(OpCode::Return);

        run_twice_deterministic(chunk, Value::int_v(3));
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::string_v("a"));
        chunk.emit_constant(Value::string_v("b"));
        chunk.emit(OpCode::Add);
        chunk.emit(OpCode::Return);

        run_twice_deterministic(chunk, Value::string_v("ab"));
    }

    // Stack underflow paths for common ops.
    expect_underflow(OpCode::Add, "add");
    expect_underflow(OpCode::Sub, "sub");
    expect_underflow(OpCode::Mul, "mul");
    expect_underflow(OpCode::Div, "div");
    expect_underflow(OpCode::Neg, "neg");
    expect_underflow(OpCode::Not, "not");
    expect_underflow(OpCode::Equal, "equal");
    expect_underflow(OpCode::NotEqual, "not-equal");
    expect_underflow(OpCode::Less, "less");
    expect_underflow(OpCode::LessEqual, "less-equal");
    expect_underflow(OpCode::Greater, "greater");
    expect_underflow(OpCode::GreaterEqual, "greater-equal");

    // One-operand underflow paths for binary ops (distinct from empty-stack underflow).
    expect_underflow_one_operand(OpCode::Add, "add");
    expect_underflow_one_operand(OpCode::Sub, "sub");
    expect_underflow_one_operand(OpCode::Mul, "mul");
    expect_underflow_one_operand(OpCode::Div, "div");
    expect_underflow_one_operand(OpCode::Equal, "equal");
    expect_underflow_one_operand(OpCode::NotEqual, "not-equal");
    expect_underflow_one_operand(OpCode::Less, "less");
    expect_underflow_one_operand(OpCode::LessEqual, "less-equal");
    expect_underflow_one_operand(OpCode::Greater, "greater");
    expect_underflow_one_operand(OpCode::GreaterEqual, "greater-equal");

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(42));
        chunk.emit(OpCode::Return);

        run_twice_deterministic(chunk, Value::int_v(42));
    }

    // Conditional branch via JumpIfFalse.
    {
        Chunk chunk;
        chunk.emit_constant(Value::bool_v(false));
        chunk.emit(OpCode::JumpIfFalse);
        chunk.emit_u16(12);
        chunk.emit_constant(Value::int_v(111));
        chunk.emit(OpCode::Jump);
        chunk.emit_u16(15);
        chunk.emit_constant(Value::int_v(222));
        chunk.emit(OpCode::Return);

        run_twice_deterministic(chunk, Value::int_v(222));
    }

    // While loop via back-edge Jump and JumpIfFalse exit.
    {
        Chunk chunk;
        // local0 = true
        chunk.emit_constant(Value::bool_v(true));
        chunk.emit_local(OpCode::StoreLocal, 0);

        // loop_start @ ip=6
        chunk.emit_local(OpCode::LoadLocal, 0);
        chunk.emit(OpCode::JumpIfFalse);
        chunk.emit_u16(24);

        // body: local0 = false; push 42
        chunk.emit_constant(Value::bool_v(false));
        chunk.emit_local(OpCode::StoreLocal, 0);
        chunk.emit_constant(Value::int_v(42));

        // back-edge
        chunk.emit(OpCode::Jump);
        chunk.emit_u16(6);

        // exit
        chunk.emit(OpCode::Return);

        run_twice_deterministic(chunk, Value::int_v(42));
    }

    // Call/return path using explicit call frames.
    {
        Chunk chunk;

        // Call f() at ip=8; f returns 7. Then add 1 and return 8.
        chunk.emit(OpCode::Call);
        chunk.emit_u16(8);
        chunk.emit_constant(Value::int_v(1));
        chunk.emit(OpCode::Add);
        chunk.emit(OpCode::Return);

        // f @ ip=8
        chunk.emit_constant(Value::int_v(7));
        chunk.emit(OpCode::Ret);

        run_twice_deterministic(chunk, Value::int_v(8));
    }

    // Cover success paths for comparisons and JumpIfFalse (cond=true => no jump).
    {
        Chunk chunk;

        // if (1 < 2) { ... } else { return 0 }
        chunk.emit_constant(Value::int_v(1));
        chunk.emit_constant(Value::int_v(2));
        chunk.emit(OpCode::Less);
        chunk.emit(OpCode::JumpIfFalse);
        chunk.emit_u16(57);

        // if (2 <= 2) { ... } else { return 0 }
        chunk.emit_constant(Value::int_v(2));
        chunk.emit_constant(Value::int_v(2));
        chunk.emit(OpCode::LessEqual);
        chunk.emit(OpCode::JumpIfFalse);
        chunk.emit_u16(57);

        // if (3 > 2) { ... } else { return 0 }
        chunk.emit_constant(Value::int_v(3));
        chunk.emit_constant(Value::int_v(2));
        chunk.emit(OpCode::Greater);
        chunk.emit(OpCode::JumpIfFalse);
        chunk.emit_u16(57);

        // if (3 >= 3) { ... } else { return 0 }
        chunk.emit_constant(Value::int_v(3));
        chunk.emit_constant(Value::int_v(3));
        chunk.emit(OpCode::GreaterEqual);
        chunk.emit(OpCode::JumpIfFalse);
        chunk.emit_u16(57);

        // if (1 != 2) { ... } else { return 0 }
        chunk.emit_constant(Value::int_v(1));
        chunk.emit_constant(Value::int_v(2));
        chunk.emit(OpCode::NotEqual);
        chunk.emit(OpCode::JumpIfFalse);
        chunk.emit_u16(57);

        // if (2 == 2) { return (6 / 2) } else { return 0 }
        chunk.emit_constant(Value::int_v(2));
        chunk.emit_constant(Value::int_v(2));
        chunk.emit(OpCode::Equal);
        chunk.emit(OpCode::JumpIfFalse);
        chunk.emit_u16(57);

        chunk.emit_constant(Value::int_v(6));
        chunk.emit_constant(Value::int_v(2));
        chunk.emit(OpCode::Div);
        chunk.emit(OpCode::Return);

        // fail_block @ ip=57
        chunk.emit_constant(Value::int_v(0));
        chunk.emit(OpCode::Return);

        run_twice_deterministic(chunk, Value::int_v(3));
    }

    // Call-related error should be span-mapped.
    {
        Chunk chunk;
        const curlee::source::Span span{.start = 30, .end = 40};
        chunk.emit(OpCode::Ret, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "return with empty call stack")
        {
            fail("expected return-with-empty-call-stack error");
        }
        if (!res.error_span.has_value() || res.error_span->start != span.start ||
            res.error_span->end != span.end)
        {
            fail("expected call-related runtime error span to map to Ret opcode span");
        }
    }

    // Span mapping for new control-flow ops.
    {
        Chunk chunk;
        const curlee::source::Span span{.start = 10, .end = 20};
        chunk.emit(OpCode::Jump, span);
        chunk.emit_u16(999, span);
        chunk.emit_constant(Value::int_v(1));
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "jump target out of range")
        {
            fail("expected jump target out of range error");
        }
        if (!res.error_span.has_value() || res.error_span->start != span.start ||
            res.error_span->end != span.end)
        {
            fail("expected runtime error span to map to jump opcode span");
        }
    }

    // Capability-gated builtin (stdout print).
    {
        Chunk chunk;
        const curlee::source::Span span{.start = 1, .end = 2};
        chunk.emit_constant(Value::int_v(42), span);
        chunk.emit(OpCode::Print, span);
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::Return, span);

        VM vm;

        // Denied by default (no capabilities).
        const auto denied = vm.run(chunk);
        if (denied.ok || denied.error != "missing capability io.stdout")
        {
            fail("expected print to be denied without io.stdout capability");
        }
        if (!denied.error_span.has_value() || denied.error_span->start != span.start ||
            denied.error_span->end != span.end)
        {
            fail("expected denied print to map span to print opcode");
        }

        // Allowed when capability present.
        VM::Capabilities caps;
        caps.insert("io.stdout");
        const auto allowed = vm.run(chunk, caps);
        if (!allowed.ok || !(allowed.value == Value::int_v(1)))
        {
            fail("expected print to succeed with io.stdout capability");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1));
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk, 1);
        if (res.ok || res.error != "out of fuel")
        {
            fail("expected fuel exhaustion to halt deterministically");
        }
    }

    {
        Chunk chunk;
        const curlee::source::Span span{.start = 4, .end = 6};
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::Add, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "add expects Int or String")
        {
            fail("expected add type error");
        }
        if (!res.error_span.has_value() || res.error_span->start != span.start ||
            res.error_span->end != span.end)
        {
            fail("expected runtime error span to map to add opcode span");
        }
    }

    // Add type error where lhs is Int but rhs is not (covers rhs-side of the Int&&Int check).
    {
        Chunk chunk;
        const curlee::source::Span span{.start = 6, .end = 8};
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit(OpCode::Add, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "add expects Int or String")
        {
            fail("expected add type error (int + bool)");
        }
    }

    // Add type error where lhs is String but rhs is not (covers rhs-side of the String&&String
    // check).
    {
        Chunk chunk;
        const curlee::source::Span span{.start = 8, .end = 10};
        chunk.emit_constant(Value::string_v("s"), span);
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::Add, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "add expects Int or String")
        {
            fail("expected add type error (string + int)");
        }
    }

    // Truncated operands and bounds errors.
    {
        const curlee::source::Span span{.start = 1, .end = 2};
        Chunk chunk;
        chunk.emit(OpCode::Constant, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "truncated constant")
        {
            fail("expected truncated constant error");
        }
        if (!res.error_span.has_value() || res.error_span->start != span.start ||
            res.error_span->end != span.end)
        {
            fail("expected truncated constant error span mapping");
        }
    }

    {
        const curlee::source::Span span{.start = 3, .end = 4};
        Chunk chunk;
        chunk.emit(OpCode::Constant, span);
        chunk.emit_u16(0, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "constant index out of range")
        {
            fail("expected constant index out of range error");
        }
        if (!res.error_span.has_value() || res.error_span->start != span.start ||
            res.error_span->end != span.end)
        {
            fail("expected constant index error span mapping");
        }
    }

    {
        const curlee::source::Span span{.start = 5, .end = 6};
        Chunk chunk;
        chunk.max_locals = 1;
        chunk.emit(OpCode::LoadLocal, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "truncated local index")
        {
            fail("expected truncated local index error");
        }
        if (!res.error_span.has_value() || res.error_span->start != span.start ||
            res.error_span->end != span.end)
        {
            fail("expected truncated local index span mapping");
        }
    }

    {
        const curlee::source::Span span{.start = 7, .end = 8};
        Chunk chunk;
        chunk.max_locals = 1;
        chunk.emit(OpCode::LoadLocal, span);
        chunk.emit_u16(9, span);
        chunk.emit(OpCode::Return, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "local index out of range")
        {
            fail("expected local index out of range error");
        }
    }

    {
        const curlee::source::Span span{.start = 9, .end = 10};
        Chunk chunk;
        chunk.max_locals = 1;
        chunk.emit(OpCode::StoreLocal, span);
        chunk.emit_u16(0, span);
        chunk.emit(OpCode::Return, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected store-local stack underflow error");
        }
    }

    {
        const curlee::source::Span span{.start = 11, .end = 12};
        Chunk chunk;
        chunk.max_locals = 1;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::StoreLocal, span);
        chunk.emit_u16(9, span);
        chunk.emit(OpCode::Return, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "local index out of range")
        {
            fail("expected store-local local index out of range error");
        }
    }

    // Arithmetic and boolean op errors.
    {
        const curlee::source::Span span{.start = 20, .end = 21};
        Chunk chunk;
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::Sub, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "sub expects Int")
        {
            fail("expected sub type error");
        }
    }

    {
        const curlee::source::Span span{.start = 21, .end = 22};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit(OpCode::Sub, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "sub expects Int")
        {
            fail("expected sub type error (int - bool)");
        }
    }

    {
        const curlee::source::Span span{.start = 22, .end = 23};
        Chunk chunk;
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit_constant(Value::int_v(2), span);
        chunk.emit(OpCode::Mul, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "mul expects Int")
        {
            fail("expected mul type error");
        }
    }

    {
        const curlee::source::Span span{.start = 23, .end = 24};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(2), span);
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit(OpCode::Mul, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "mul expects Int")
        {
            fail("expected mul type error (int * bool)");
        }
    }

    {
        const curlee::source::Span span{.start = 24, .end = 25};
        Chunk chunk;
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit_constant(Value::int_v(2), span);
        chunk.emit(OpCode::Div, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "div expects Int")
        {
            fail("expected div type error");
        }
    }

    {
        const curlee::source::Span span{.start = 25, .end = 26};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(2), span);
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit(OpCode::Div, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "div expects Int")
        {
            fail("expected div type error (int / bool)");
        }
    }

    {
        const curlee::source::Span span{.start = 26, .end = 27};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit_constant(Value::int_v(0), span);
        chunk.emit(OpCode::Div, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "divide by zero")
        {
            fail("expected divide by zero error");
        }
    }

    {
        const curlee::source::Span span{.start = 28, .end = 29};
        Chunk chunk;
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit(OpCode::Neg, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "neg expects Int")
        {
            fail("expected neg type error");
        }
    }

    {
        const curlee::source::Span span{.start = 30, .end = 31};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::Not, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "not expects Bool")
        {
            fail("expected not type error");
        }
    }

    // Comparison ops and stack underflow.
    {
        const curlee::source::Span span{.start = 40, .end = 41};
        Chunk chunk;
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::Less, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "lt expects Int")
        {
            fail("expected lt type error");
        }
    }

    {
        const curlee::source::Span span{.start = 41, .end = 42};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit(OpCode::Less, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "lt expects Int")
        {
            fail("expected lt type error (int < bool)");
        }
    }

    {
        const curlee::source::Span span{.start = 42, .end = 43};
        Chunk chunk;
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::LessEqual, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "le expects Int")
        {
            fail("expected le type error");
        }
    }

    {
        const curlee::source::Span span{.start = 43, .end = 44};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit(OpCode::LessEqual, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "le expects Int")
        {
            fail("expected le type error (int <= bool)");
        }
    }

    {
        const curlee::source::Span span{.start = 44, .end = 45};
        Chunk chunk;
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::Greater, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "gt expects Int")
        {
            fail("expected gt type error");
        }
    }

    {
        const curlee::source::Span span{.start = 45, .end = 46};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit(OpCode::Greater, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "gt expects Int")
        {
            fail("expected gt type error (int > bool)");
        }
    }

    {
        const curlee::source::Span span{.start = 46, .end = 47};
        Chunk chunk;
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::GreaterEqual, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "ge expects Int")
        {
            fail("expected ge type error");
        }
    }

    {
        const curlee::source::Span span{.start = 47, .end = 48};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit(OpCode::GreaterEqual, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "ge expects Int")
        {
            fail("expected ge type error (int >= bool)");
        }
    }

    {
        const curlee::source::Span span{.start = 48, .end = 49};
        Chunk chunk;
        chunk.emit(OpCode::Pop, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected pop stack underflow error");
        }
    }

    {
        const curlee::source::Span span{.start = 50, .end = 51};
        Chunk chunk;
        chunk.emit(OpCode::Return, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "missing return")
        {
            fail("expected missing return error");
        }
    }

    auto run_spanless = [&](const std::vector<std::uint8_t>& code,
                            const std::vector<Value>& constants = {}, VM::Capabilities caps = {})
    {
        Chunk chunk;
        chunk.code = code;
        chunk.constants = constants;
        chunk.spans.clear();
        chunk.max_locals = 0;
        VM vm;
        return vm.run(chunk, caps);
    };

    // Span-less errors: cover `span = nullopt` path and optional assignment branches.
    {
        const auto res = run_spanless({static_cast<std::uint8_t>(OpCode::Constant),
                                       static_cast<std::uint8_t>(OpCode::Return)});
        if (res.ok || res.error != "truncated constant" || res.error_span.has_value())
        {
            fail("expected span-less truncated constant error with nullopt span");
        }
    }

    {
        // Constant index out of range (with no spans).
        const std::vector<std::uint8_t> code = {
            static_cast<std::uint8_t>(OpCode::Constant),
            0xFF,
            0xFF,
            static_cast<std::uint8_t>(OpCode::Return),
        };
        const auto res = run_spanless(code);
        if (res.ok || res.error != "constant index out of range" || res.error_span.has_value())
        {
            fail("expected span-less constant index out of range error");
        }
    }

    {
        const auto res = run_spanless({static_cast<std::uint8_t>(OpCode::LoadLocal),
                                       static_cast<std::uint8_t>(OpCode::Return)});
        if (res.ok || res.error != "truncated local index" || res.error_span.has_value())
        {
            fail("expected span-less LoadLocal truncated local index error");
        }
    }

    {
        const auto res = run_spanless({static_cast<std::uint8_t>(OpCode::StoreLocal),
                                       static_cast<std::uint8_t>(OpCode::Return)});
        if (res.ok || res.error != "truncated local index" || res.error_span.has_value())
        {
            fail("expected span-less StoreLocal truncated local index error");
        }
    }

    {
        const auto res = run_spanless({static_cast<std::uint8_t>(OpCode::Add)});
        if (res.ok || res.error != "stack underflow" || res.error_span.has_value())
        {
            fail("expected span-less Add stack underflow error");
        }
    }

    {
        const auto res = run_spanless({static_cast<std::uint8_t>(OpCode::JumpIfFalse), 0, 0});
        if (res.ok || res.error != "stack underflow" || res.error_span.has_value())
        {
            fail("expected span-less JumpIfFalse stack underflow error");
        }
    }

    // JumpIfFalse expects Bool.
    {
        const curlee::source::Span span{.start = 83, .end = 84};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::JumpIfFalse, span);
        chunk.emit_u16(0, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "jump-if-false expects Bool")
        {
            fail("expected jump-if-false expects Bool error");
        }
    }

    // `no return` at end of VM::run.
    {
        const auto res =
            run_spanless({static_cast<std::uint8_t>(OpCode::Constant), 0, 0}, {Value::int_v(1)});
        if (res.ok || res.error != "no return")
        {
            fail("expected no return error");
        }
    }

    // Unknown opcode value falls through switch without executing any case.
    {
        const curlee::source::Span span{.start = 84, .end = 85};
        Chunk chunk;
        chunk.code.push_back(0xFF);
        chunk.spans.push_back(span);
        chunk.emit_constant(Value::int_v(5), span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok || res.value.kind != ValueKind::Int || res.value.int_value != 5)
        {
            fail("expected unknown opcode to be skipped and return 5");
        }
    }

    // Out-of-fuel handling.
    {
        const curlee::source::Span span{.start = 52, .end = 53};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk, 0);
        if (res.ok || res.error != "out of fuel")
        {
            fail("expected out of fuel error");
        }
    }

    // Happy-path execution for a variety of opcodes (covers success branches).
    {
        const curlee::source::Span span{.start = 54, .end = 55};
        Chunk chunk;

        // 10 + 3 = 13
        chunk.emit_constant(Value::int_v(10), span);
        chunk.emit_constant(Value::int_v(3), span);
        chunk.emit(OpCode::Add, span);

        // 13 - 5 = 8
        chunk.emit_constant(Value::int_v(5), span);
        chunk.emit(OpCode::Sub, span);

        // 8 * 2 = 16
        chunk.emit_constant(Value::int_v(2), span);
        chunk.emit(OpCode::Mul, span);

        // 16 / 4 = 4
        chunk.emit_constant(Value::int_v(4), span);
        chunk.emit(OpCode::Div, span);

        // neg 4 -> -4
        chunk.emit(OpCode::Neg, span);

        // Store/Load local 0.
        chunk.emit_local(OpCode::StoreLocal, 0, span);
        chunk.emit_local(OpCode::LoadLocal, 0, span);

        // Equal and Not on bool.
        chunk.emit_constant(Value::int_v(-4), span);
        chunk.emit(OpCode::Equal, span);
        chunk.emit(OpCode::Not, span);
        chunk.emit(OpCode::Pop, span);

        // NotEqual.
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit_constant(Value::int_v(2), span);
        chunk.emit(OpCode::NotEqual, span);
        chunk.emit(OpCode::Pop, span);

        // Comparisons.
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit_constant(Value::int_v(2), span);
        chunk.emit(OpCode::Less, span);
        chunk.emit(OpCode::Pop, span);

        chunk.emit_constant(Value::int_v(2), span);
        chunk.emit_constant(Value::int_v(2), span);
        chunk.emit(OpCode::LessEqual, span);
        chunk.emit(OpCode::Pop, span);

        chunk.emit_constant(Value::int_v(3), span);
        chunk.emit_constant(Value::int_v(2), span);
        chunk.emit(OpCode::Greater, span);
        chunk.emit(OpCode::Pop, span);

        chunk.emit_constant(Value::int_v(3), span);
        chunk.emit_constant(Value::int_v(3), span);
        chunk.emit(OpCode::GreaterEqual, span);
        chunk.emit(OpCode::Pop, span);

        // Return success.
        chunk.emit_constant(Value::int_v(123), span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok || res.value.kind != ValueKind::Int || res.value.int_value != 123)
        {
            fail("expected happy-path chunk to return 123");
        }
    }

    // Print success path when capability is present and stack has a value.
    {
        const curlee::source::Span span{.start = 56, .end = 57};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::Print, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        VM::Capabilities caps;
        caps.insert("io.stdout");
        const auto res = vm.run(chunk, caps);
        if (!res.ok || res.value.kind != ValueKind::Unit)
        {
            fail("expected Print to succeed and return Unit");
        }
    }

    // Jump success path.
    {
        const curlee::source::Span span{.start = 58, .end = 59};
        Chunk chunk;
        chunk.emit(OpCode::Jump, span);
        const std::size_t patch_pos = chunk.code.size();
        chunk.emit_u16(0, span);

        // Skipped.
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::Return, span);

        const std::uint16_t target = static_cast<std::uint16_t>(chunk.code.size());
        chunk.code[patch_pos] = static_cast<std::uint8_t>(target & 0xFF);
        chunk.code[patch_pos + 1] = static_cast<std::uint8_t>((target >> 8) & 0xFF);

        chunk.emit_constant(Value::int_v(7), span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok || res.value.kind != ValueKind::Int || res.value.int_value != 7)
        {
            fail("expected Jump to land on return 7");
        }
    }

    // JumpIfFalse success paths (both taken and not-taken).
    {
        const curlee::source::Span span{.start = 60, .end = 61};
        Chunk chunk;
        chunk.emit_constant(Value::bool_v(false), span);
        chunk.emit(OpCode::JumpIfFalse, span);
        const std::size_t patch_pos = chunk.code.size();
        chunk.emit_u16(0, span);

        // Not taken (skipped because cond is false and we jump).
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::Return, span);

        const std::uint16_t target = static_cast<std::uint16_t>(chunk.code.size());
        chunk.code[patch_pos] = static_cast<std::uint8_t>(target & 0xFF);
        chunk.code[patch_pos + 1] = static_cast<std::uint8_t>((target >> 8) & 0xFF);

        chunk.emit_constant(Value::int_v(99), span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok || res.value.kind != ValueKind::Int || res.value.int_value != 99)
        {
            fail("expected JumpIfFalse (false) to jump to 99");
        }
    }

    {
        const curlee::source::Span span{.start = 62, .end = 63};
        Chunk chunk;
        chunk.emit_constant(Value::bool_v(true), span);
        chunk.emit(OpCode::JumpIfFalse, span);
        const std::size_t patch_pos = chunk.code.size();
        chunk.emit_u16(0, span);

        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::Return, span);

        const std::uint16_t target = static_cast<std::uint16_t>(chunk.code.size());
        chunk.code[patch_pos] = static_cast<std::uint8_t>(target & 0xFF);
        chunk.code[patch_pos + 1] = static_cast<std::uint8_t>((target >> 8) & 0xFF);

        // Unreachable label.
        chunk.emit_constant(Value::int_v(2), span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok || res.value.kind != ValueKind::Int || res.value.int_value != 1)
        {
            fail("expected JumpIfFalse (true) to not jump and return 1");
        }
    }

    // Call/Ret success path.
    {
        const curlee::source::Span span{.start = 64, .end = 65};
        Chunk chunk;
        chunk.emit(OpCode::Call, span);
        const std::size_t patch_pos = chunk.code.size();
        chunk.emit_u16(0, span);

        // After returning from call.
        chunk.emit_constant(Value::int_v(7), span);
        chunk.emit(OpCode::Return, span);

        const std::uint16_t target = static_cast<std::uint16_t>(chunk.code.size());
        chunk.code[patch_pos] = static_cast<std::uint8_t>(target & 0xFF);
        chunk.code[patch_pos + 1] = static_cast<std::uint8_t>((target >> 8) & 0xFF);

        // Callee body.
        chunk.emit_constant(Value::int_v(0), span);
        chunk.emit(OpCode::Pop, span);
        chunk.emit(OpCode::Ret, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok || res.value.kind != ValueKind::Int || res.value.int_value != 7)
        {
            fail("expected Call/Ret to return 7");
        }
    }

    // Additional deterministic error cases.
    {
        const curlee::source::Span span{.start = 66, .end = 67};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit_constant(Value::int_v(0), span);
        chunk.emit(OpCode::Div, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "divide by zero")
        {
            fail("expected divide by zero error");
        }
    }

    {
        const curlee::source::Span span{.start = 68, .end = 69};
        Chunk chunk;
        chunk.emit(OpCode::Constant, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "truncated constant")
        {
            fail("expected truncated constant error");
        }
    }

    {
        const curlee::source::Span span{.start = 70, .end = 71};
        Chunk chunk;
        chunk.emit(OpCode::Constant, span);
        chunk.emit_u16(999, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "constant index out of range")
        {
            fail("expected constant index out of range error");
        }
    }

    {
        const curlee::source::Span span{.start = 72, .end = 73};
        Chunk chunk;
        chunk.emit(OpCode::LoadLocal, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "truncated local index")
        {
            fail("expected truncated local index error (LoadLocal)");
        }
    }

    {
        const curlee::source::Span span{.start = 74, .end = 75};
        Chunk chunk;
        chunk.emit(OpCode::LoadLocal, span);
        chunk.emit_u16(0, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "local index out of range")
        {
            fail("expected local index out of range error (LoadLocal)");
        }
    }

    {
        const curlee::source::Span span{.start = 76, .end = 77};
        Chunk chunk;
        chunk.emit(OpCode::StoreLocal, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "truncated local index")
        {
            fail("expected truncated local index error (StoreLocal)");
        }
    }

    {
        const curlee::source::Span span{.start = 78, .end = 79};
        Chunk chunk;
        chunk.emit(OpCode::StoreLocal, span);
        chunk.emit_u16(0, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow error (StoreLocal)");
        }
    }

    {
        const curlee::source::Span span{.start = 82, .end = 83};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::StoreLocal, span);
        chunk.emit_u16(0, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "local index out of range")
        {
            fail("expected local index out of range error (StoreLocal)");
        }
    }

    // Missing Return opcode entirely.
    {
        Chunk chunk;
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "no return")
        {
            fail("expected no return error");
        }
        if (res.error_span.has_value())
        {
            fail("expected no return error_span to be nullopt");
        }
    }

    // Truncated and invalid jump/call targets.
    {
        const curlee::source::Span span{.start = 60, .end = 61};
        Chunk chunk;
        chunk.emit(OpCode::Jump, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "truncated jump target")
        {
            fail("expected truncated jump target error");
        }
    }

    {
        const curlee::source::Span span{.start = 62, .end = 63};
        Chunk chunk;
        chunk.emit(OpCode::JumpIfFalse, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "truncated jump target")
        {
            fail("expected truncated jump-if-false target error");
        }
    }

    {
        const curlee::source::Span span{.start = 64, .end = 65};
        Chunk chunk;
        chunk.emit(OpCode::JumpIfFalse, span);
        chunk.emit_u16(0, span);
        chunk.emit(OpCode::Return, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected jump-if-false stack underflow error");
        }
    }

    {
        const curlee::source::Span span{.start = 66, .end = 67};
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::JumpIfFalse, span);
        chunk.emit_u16(0, span);
        chunk.emit_constant(Value::int_v(2), span);
        chunk.emit(OpCode::Return, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "jump-if-false expects Bool")
        {
            fail("expected jump-if-false type error");
        }
    }

    {
        const curlee::source::Span span{.start = 68, .end = 69};
        Chunk chunk;
        chunk.emit_constant(Value::bool_v(false), span);
        chunk.emit(OpCode::JumpIfFalse, span);
        chunk.emit_u16(999, span);
        chunk.emit_constant(Value::int_v(1), span);
        chunk.emit(OpCode::Return, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "jump target out of range")
        {
            fail("expected jump-if-false out-of-range error");
        }
    }

    {
        const curlee::source::Span span{.start = 70, .end = 71};
        Chunk chunk;
        chunk.emit(OpCode::Call, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "truncated call target")
        {
            fail("expected truncated call target error");
        }
    }

    {
        const curlee::source::Span span{.start = 72, .end = 73};
        Chunk chunk;
        chunk.emit(OpCode::Call, span);
        chunk.emit_u16(999, span);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "call target out of range")
        {
            fail("expected call target out of range error");
        }
    }

    // Print underflow when capability is present.
    {
        const curlee::source::Span span{.start = 80, .end = 81};
        Chunk chunk;
        chunk.emit(OpCode::Print, span);

        VM vm;
        VM::Capabilities caps;
        caps.insert("io.stdout");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected print stack underflow error");
        }
    }

    // PythonCall capability checks and runner failures.
    {
        const curlee::source::Span span{.start = 90, .end = 91};
        Chunk chunk;
        chunk.emit(OpCode::PythonCall, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "python capability required")
        {
            fail("expected python capability required error");
        }
    }

    // Cover run_process_argv env var handling branches in vm.cpp.
    {
        EnvVarGuard path("PATH");
        EnvVarGuard ld_library_path("LD_LIBRARY_PATH");
        EnvVarGuard asan_options("ASAN_OPTIONS");
        EnvVarGuard ubsan_options("UBSAN_OPTIONS");
        EnvVarGuard lsan_options("LSAN_OPTIONS");
        EnvVarGuard runner("CURLEE_PYTHON_RUNNER");

        const curlee::source::Span span{.start = 94, .end = 95};
        Chunk chunk;
        chunk.emit(OpCode::PythonCall, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        VM::Capabilities caps;
        caps.insert("python.ffi");

        runner.set("/bin/true");

        // Env vars present.
        path.set("/usr/bin:/bin");
        ld_library_path.set("/tmp");
        asan_options.set("detect_leaks=0");
        ubsan_options.set("print_stacktrace=1");
        lsan_options.set("verbosity=1");
        {
            const auto res = vm.run(chunk, caps);
            if (res.ok || res.error != "python runner failed")
            {
                fail("expected /bin/true python runner to fail handshake");
            }
        }

        // Env vars absent.
        path.unset();
        ld_library_path.unset();
        asan_options.unset();
        ubsan_options.unset();
        lsan_options.unset();
        {
            const auto res = vm.run(chunk, caps);
            if (res.ok || res.error != "python runner failed")
            {
                fail("expected /bin/true python runner to fail handshake with env vars unset");
            }
        }
    }

    if (argc >= 1)
    {
        const std::string runner_error = sibling_exe(argv[0], "curlee_python_runner_fake_error");
        const std::string runner_error_escapes =
            sibling_exe(argv[0], "curlee_python_runner_fake_error_escapes");
        const std::string runner_error_unterminated =
            sibling_exe(argv[0], "curlee_python_runner_fake_error_unterminated");
        const std::string runner_hang = sibling_exe(argv[0], "curlee_python_runner_fake_hang");
        const std::string runner_close_stdout =
            sibling_exe(argv[0], "curlee_python_runner_fake_close_stdout");
        const std::string runner_close_stderr =
            sibling_exe(argv[0], "curlee_python_runner_fake_close_stderr");
        const std::string runner_close_stdin =
            sibling_exe(argv[0], "curlee_python_runner_fake_close_stdin");
        const std::string runner_error_trailing_backslash =
            sibling_exe(argv[0], "curlee_python_runner_fake_error_trailing_backslash");
        const std::string runner_real = sibling_exe(argv[0], "curlee_python_runner");
        const std::string runner_plain_fail =
            sibling_exe(argv[0], "curlee_python_runner_fake_plain_fail");
        const std::string runner_spam = sibling_exe(argv[0], "curlee_python_runner_fake_spam");
        const std::string runner_sandbox_required =
            sibling_exe(argv[0], "curlee_python_runner_fake_sandbox_required");
        const std::string bwrap_fake = sibling_exe(argv[0], "curlee_bwrap_fake");

        const curlee::source::Span span{.start = 92, .end = 93};
        Chunk chunk;
        chunk.emit(OpCode::PythonCall, span);
        chunk.emit(OpCode::Return, span);

        VM vm;
        VM::Capabilities caps;
        caps.insert("python.ffi");

        // Non-sandbox exec failure path.
        (void)setenv("CURLEE_PYTHON_RUNNER", "/no/such/curlee_python_runner_missing", 1);
        {
            const auto res = vm.run(chunk, caps);
            if (res.ok || res.error != "python runner exec failed")
            {
                fail("expected python runner exec failed error");
            }
        }

        // Sandbox path where CURLEE_BWRAP is not set: find_bwrap_path() falls back to "bwrap",
        // and execve fails deterministically (no PATH search).
        {
            VM::Capabilities sandbox_caps = caps;
            sandbox_caps.insert("python.sandbox");

            (void)unsetenv("CURLEE_BWRAP");
            (void)setenv("CURLEE_PYTHON_RUNNER", runner_plain_fail.c_str(), 1);

            const auto res = vm.run(chunk, sandbox_caps);
            if (res.ok || res.error != "python sandbox exec failed")
            {
                fail("expected sandbox exec failure when CURLEE_BWRAP is unset");
            }
        }

        // Sandbox path where CURLEE_BWRAP is set but empty: covers the second operand of
        // `env != nullptr && *env != '\0'`.
        {
            VM::Capabilities sandbox_caps = caps;
            sandbox_caps.insert("python.sandbox");

            (void)setenv("CURLEE_BWRAP", "", 1);
            (void)setenv("CURLEE_PYTHON_RUNNER", runner_plain_fail.c_str(), 1);

            const auto res = vm.run(chunk, sandbox_caps);
            if (res.ok || res.error != "python sandbox exec failed")
            {
                fail("expected sandbox exec failure when CURLEE_BWRAP is empty");
            }
        }

        // Sandbox path where CURLEE_PYTHON_RUNNER is not set: find_python_runner_path() takes
        // the fallback path, but bwrap exec still fails so the runner is never executed.
        {
            VM::Capabilities sandbox_caps = caps;
            sandbox_caps.insert("python.sandbox");

            (void)unsetenv("CURLEE_PYTHON_RUNNER");
            (void)unsetenv("CURLEE_BWRAP");

            const auto res = vm.run(chunk, sandbox_caps);
            if (res.ok || res.error != "python sandbox exec failed")
            {
                fail("expected sandbox exec failure with CURLEE_PYTHON_RUNNER unset");
            }
        }

        // Sandbox path where CURLEE_PYTHON_RUNNER is set but empty: covers the second operand of
        // `env != nullptr && *env != '\0'`.
        {
            VM::Capabilities sandbox_caps = caps;
            sandbox_caps.insert("python.sandbox");

            (void)setenv("CURLEE_PYTHON_RUNNER", "", 1);
            (void)unsetenv("CURLEE_BWRAP");

            const auto res = vm.run(chunk, sandbox_caps);
            if (res.ok || res.error != "python sandbox exec failed")
            {
                fail("expected sandbox exec failure with CURLEE_PYTHON_RUNNER empty");
            }
        }

        // Cover find_python_runner_path() fallback when the sibling curlee_python_runner binary is
        // missing. We rename it temporarily so filesystem::exists(candidate) is false.
        {
            struct RenameGuard
            {
                std::filesystem::path from;
                std::filesystem::path to;
                bool active = false;

                RenameGuard(const std::filesystem::path& f, const std::filesystem::path& t)
                    : from(f), to(t)
                {
                    std::error_code ec;
                    if (!std::filesystem::exists(from, ec))
                    {
                        return;
                    }
                    std::filesystem::rename(from, to, ec);
                    if (ec)
                    {
                        fail("failed to rename curlee_python_runner for test");
                    }
                    active = true;
                }

                ~RenameGuard()
                {
                    if (!active)
                    {
                        return;
                    }
                    std::error_code ec;
                    std::filesystem::rename(to, from, ec);
                    if (ec)
                    {
                        // Best-effort: the test has already passed/failed; keep deterministic.
                    }
                }
            };

            const std::filesystem::path from(runner_real);
            const std::filesystem::path to(runner_real + ".bak");
            RenameGuard guard(from, to);

            VM::Capabilities sandbox_caps = caps;
            sandbox_caps.insert("python.sandbox");
            (void)unsetenv("CURLEE_PYTHON_RUNNER");
            (void)unsetenv("CURLEE_BWRAP");

            const auto res = vm.run(chunk, sandbox_caps);
            if (res.ok || res.error != "python sandbox exec failed")
            {
                fail("expected sandbox exec failure with runner binary temporarily missing");
            }
        }

        // Exercise stdout/stderr EOF interleavings in run_process_argv.
        (void)setenv("CURLEE_PYTHON_RUNNER", runner_close_stdout.c_str(), 1);
        {
            const auto res = vm.run(chunk, caps);
            if (res.ok || res.error != "python runner failed")
            {
                fail("expected close-stdout python runner to fail handshake");
            }
        }

        (void)setenv("CURLEE_PYTHON_RUNNER", runner_close_stderr.c_str(), 1);
        {
            const auto res = vm.run(chunk, caps);
            if (res.ok || res.error != "python runner failed")
            {
                fail("expected close-stderr python runner to fail handshake");
            }
        }

        // Force a parent-side write() error (n < 0) by running a runner that closes stdin.
        (void)setenv("CURLEE_PYTHON_RUNNER", runner_close_stdin.c_str(), 1);
        {
            const auto res = vm.run(chunk, caps);
            if (res.ok || res.error != "python runner failed")
            {
                fail("expected close-stdin python runner to fail handshake");
            }
        }

        // Exercise extract_error_message() falling off the end of input with a trailing '\\'.
        (void)setenv("CURLEE_PYTHON_RUNNER", runner_error_unterminated.c_str(), 1);
        {
            const auto res = vm.run(chunk, caps);
            if (res.ok || res.error != "python runner failed")
            {
                fail("expected unterminated-message python runner to fail with default message");
            }
        }

        // Exercise extract_error_message() with a single trailing backslash (i == json.size()).
        (void)setenv("CURLEE_PYTHON_RUNNER", runner_error_trailing_backslash.c_str(), 1);
        {
            const auto res = vm.run(chunk, caps);
            if (res.ok || res.error != "python runner failed")
            {
                fail("expected trailing-backslash python runner to fail with default message");
            }
        }

        // Cover pipe()-failure branches in run_process_argv deterministically by exhausting
        // file descriptors so pipe creation fails at different points.
        {
            struct rlimit old_limit;
            if (getrlimit(RLIMIT_NOFILE, &old_limit) != 0)
            {
                fail("getrlimit(RLIMIT_NOFILE) failed");
            }

            auto expect_exec_failed_via_exhaustion = [&](int headroom)
            {
                // Pick a small fixed soft limit so we can exhaust it quickly.
                // Must be >= 3 (stdin/stdout/stderr).
                const rlim_t soft = 64;
                if (soft > old_limit.rlim_max)
                {
                    fail("unexpected RLIMIT_NOFILE max too small for test");
                }
                struct rlimit new_limit = old_limit;
                new_limit.rlim_cur = soft;
                if (setrlimit(RLIMIT_NOFILE, &new_limit) != 0)
                {
                    fail("setrlimit(RLIMIT_NOFILE) failed");
                }

                std::vector<int> fds;
                for (;;)
                {
                    errno = 0;
                    const int fd = ::open("/dev/null", O_RDONLY);
                    if (fd < 0)
                    {
                        if (errno != EMFILE)
                        {
                            // Restore limit before failing.
                            (void)setrlimit(RLIMIT_NOFILE, &old_limit);
                            fail("expected EMFILE while exhausting fds");
                        }
                        break;
                    }
                    fds.push_back(fd);
                }

                // Leave exactly `headroom` descriptors free.
                for (int i = 0; i < headroom; ++i)
                {
                    if (fds.empty())
                    {
                        (void)setrlimit(RLIMIT_NOFILE, &old_limit);
                        fail("fd exhaustion left no fds to free");
                    }
                    ::close(fds.back());
                    fds.pop_back();
                }

                (void)setenv("CURLEE_PYTHON_RUNNER", "/bin/true", 1);
                const auto res = vm.run(chunk, caps);

                for (const int fd : fds)
                {
                    ::close(fd);
                }
                (void)setrlimit(RLIMIT_NOFILE, &old_limit);

                if (res.ok || res.error != "python runner exec failed")
                {
                    fail("expected python runner exec failed error via pipe failure (got: " +
                         res.error + ")");
                }
            };

            // Leave 0 free fds: first pipe(in_pipe) fails.
            expect_exec_failed_via_exhaustion(0);

            // Leave 2 free fds: first pipe succeeds, second pipe(out_pipe) fails.
            expect_exec_failed_via_exhaustion(2);

            // Leave 4 free fds: first two pipes succeed, third pipe(err_pipe) fails.
            expect_exec_failed_via_exhaustion(4);
        }

        // Non-sandbox default failure (non-JSON, exit_code != 127).
        (void)setenv("CURLEE_PYTHON_RUNNER", runner_plain_fail.c_str(), 1);
        {
            const auto res = vm.run(chunk, caps);
            if (res.ok || res.error != "python runner failed")
            {
                fail("expected python runner failed default error");
            }
        }

        (void)setenv("CURLEE_PYTHON_RUNNER", runner_hang.c_str(), 1);
        {
            const auto res = vm.run(chunk, caps);
            if (res.ok || res.error != "python runner timed out")
            {
                fail("expected python runner timed out error");
            }
        }

        (void)setenv("CURLEE_PYTHON_RUNNER", runner_spam.c_str(), 1);
        {
            const auto res = vm.run(chunk, caps);
            if (res.ok || res.error != "python runner output too large")
            {
                fail("expected python runner output too large error");
            }
        }

        (void)setenv("CURLEE_PYTHON_RUNNER", runner_error.c_str(), 1);
        {
            const auto res = vm.run(chunk, caps);
            if (res.ok || res.error != "forced runner error")
            {
                fail("expected python runner structured error message");
            }
        }

        // Structured error with common escape sequences should be decoded.
        (void)setenv("CURLEE_PYTHON_RUNNER", runner_error_escapes.c_str(), 1);
        {
            const auto res = vm.run(chunk, caps);
            const std::string expected = "forced \"runner\nerror\twith\rcarriage\\slash and escape";
            if (res.ok || res.error != expected)
            {
                fail("expected python runner escape decoding in error message");
            }
        }

        // Sandbox success path (via bwrap_fake).
        (void)setenv("CURLEE_PYTHON_RUNNER", runner_sandbox_required.c_str(), 1);
        (void)setenv("CURLEE_BWRAP", bwrap_fake.c_str(), 1);
        {
            VM::Capabilities sandbox_caps;
            sandbox_caps.insert("python.ffi");
            sandbox_caps.insert("python.sandbox");
            const auto res = vm.run(chunk, sandbox_caps);
            if (!res.ok || !(res.value == Value::unit_v()))
            {
                fail("expected sandboxed python call to succeed");
            }
        }

        // Sandbox failure path (exit_code != 127, no structured message).
        (void)setenv("CURLEE_PYTHON_RUNNER", runner_plain_fail.c_str(), 1);
        (void)setenv("CURLEE_BWRAP", bwrap_fake.c_str(), 1);
        {
            VM::Capabilities sandbox_caps;
            sandbox_caps.insert("python.ffi");
            sandbox_caps.insert("python.sandbox");
            const auto res = vm.run(chunk, sandbox_caps);
            if (res.ok || res.error != "python sandbox failed")
            {
                fail("expected python sandbox failed error");
            }
        }

        // Sandbox exec failure path.
        (void)setenv("CURLEE_PYTHON_RUNNER", runner_sandbox_required.c_str(), 1);
        (void)setenv("CURLEE_BWRAP", "/no/such/bwrap", 1);
        {
            VM::Capabilities sandbox_caps;
            sandbox_caps.insert("python.ffi");
            sandbox_caps.insert("python.sandbox");
            const auto res = vm.run(chunk, sandbox_caps);
            if (res.ok || res.error != "python sandbox exec failed")
            {
                fail("expected python sandbox exec failed error");
            }
        }

        (void)unsetenv("CURLEE_PYTHON_RUNNER");
        (void)unsetenv("CURLEE_BWRAP");
    }

    std::cout << "OK\n";
    return 0;
}
