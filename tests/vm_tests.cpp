#include <cstdlib>
#include <curlee/source/span.h>
#include <curlee/vm/bytecode.h>
#include <curlee/vm/vm.h>
#include <iostream>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

int main()
{
    using namespace curlee::vm;

    auto run_twice_deterministic = [](const Chunk& chunk, Value expected) {
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
        if (res.ok || res.error != "add expects Int")
        {
            fail("expected add type error");
        }
        if (!res.error_span.has_value() || res.error_span->start != span.start ||
            res.error_span->end != span.end)
        {
            fail("expected runtime error span to map to add opcode span");
        }
    }

    std::cout << "OK\n";
    return 0;
}
