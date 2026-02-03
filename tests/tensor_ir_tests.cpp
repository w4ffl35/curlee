#include <cstdlib>
#include <curlee/compiler/tensor_ir.h>
#include <iostream>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

int main()
{
    using namespace curlee::compiler::tensor_ir;

    Program p;
    const auto a = p.zeros(Shape{{2, 3}}, DType::I32);
    const auto b = p.zeros(Shape{{2, 3}}, DType::I32);
    (void)p.add(a, b);

    // Also validate ops() shape to ensure the builder actually stored ops.
    {
        const auto& ops = p.ops();
        if (ops.size() != 3)
        {
            fail("expected 3 ops");
        }
        if (ops[0].name != "zeros" || ops[1].name != "zeros" || ops[2].name != "add")
        {
            fail("unexpected op names");
        }
        if (ops[2].inputs.size() != 2 || ops[2].inputs[0].id != 0 || ops[2].inputs[1].id != 1)
        {
            fail("unexpected add inputs");
        }
    }

    const std::string expected = "%0 = zeros i32[2,3]\n"
                                 "%1 = zeros i32[2,3]\n"
                                 "%2 = add %0 %1 : i32[2,3]\n";

    const auto got = p.dump();
    if (got != expected)
    {
        fail("unexpected dump output\n--- got ---\n" + got + "--- expected ---\n" + expected);
    }

    // Edge cases: unknown dtype formatting + empty shape.
    {
        Program p2;
        (void)p2.zeros(Shape{{}}, static_cast<DType>(123));

        // ops() should contain the pushed zero op, even with unknown dtype.
        const auto& ops2 = p2.ops();
        if (ops2.size() != 1 || ops2[0].name != "zeros")
        {
            fail("expected one zeros op in p2");
        }

        const auto got2 = p2.dump();
        if (got2 != "%0 = zeros <unknown>[]\n")
        {
            fail("unexpected dump output (edge cases)\n--- got ---\n" + got2 +
                 "--- expected ---\n%0 = zeros <unknown>[]\n");
        }
    }

    return 0;
}
