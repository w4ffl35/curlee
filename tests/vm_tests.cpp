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

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1));
        chunk.emit_constant(Value::int_v(2));
        chunk.emit(OpCode::Add);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok)
        {
            fail("expected VM to run successfully");
        }
        if (!(res.value == Value::int_v(3)))
        {
            fail("expected 1 + 2 to equal 3");
        }

        const auto res2 = vm.run(chunk);
        if (!res2.ok || !(res2.value == Value::int_v(3)))
        {
            fail("expected deterministic VM result across runs");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(42));
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok || !(res.value == Value::int_v(42)))
        {
            fail("expected constant return to work");
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
