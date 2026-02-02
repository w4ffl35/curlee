#include <cstdlib>
#include <curlee/source/span.h>
#include <curlee/vm/bytecode.h>
#include <curlee/vm/vm.h>
#include <filesystem>
#include <iostream>

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
        if (denied.ok || denied.error != "missing capability io:stdout")
        {
            fail("expected print to be denied without io:stdout capability");
        }
        if (!denied.error_span.has_value() || denied.error_span->start != span.start ||
            denied.error_span->end != span.end)
        {
            fail("expected denied print to map span to print opcode");
        }

        // Allowed when capability present.
        VM::Capabilities caps;
        caps.insert("io:stdout");
        const auto allowed = vm.run(chunk, caps);
        if (!allowed.ok || !(allowed.value == Value::int_v(1)))
        {
            fail("expected print to succeed with io:stdout capability");
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
        caps.insert("io:stdout");
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

    if (argc >= 1)
    {
        const std::string runner_error = sibling_exe(argv[0], "curlee_python_runner_fake_error");
        const std::string runner_error_escapes =
            sibling_exe(argv[0], "curlee_python_runner_fake_error_escapes");
        const std::string runner_hang = sibling_exe(argv[0], "curlee_python_runner_fake_hang");
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
        caps.insert("python:ffi");

        // Non-sandbox exec failure path.
        (void)setenv("CURLEE_PYTHON_RUNNER", "/no/such/curlee_python_runner_missing", 1);
        {
            const auto res = vm.run(chunk, caps);
            if (res.ok || res.error != "python runner exec failed")
            {
                fail("expected python runner exec failed error");
            }
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
            sandbox_caps.insert("python:ffi");
            sandbox_caps.insert("python:sandbox");
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
            sandbox_caps.insert("python:ffi");
            sandbox_caps.insert("python:sandbox");
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
            sandbox_caps.insert("python:ffi");
            sandbox_caps.insert("python:sandbox");
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
