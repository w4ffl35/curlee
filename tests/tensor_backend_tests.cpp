#include <cstdlib>
#include <curlee/compiler/tensor_backend.h>
#include <iostream>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

int main()
{
    using namespace curlee::compiler::tensor_ir;

    {
        CpuBackend backend;
        Program p;
        const auto a = p.zeros(Shape{{2, 3}}, DType::I32);
        const auto b = p.zeros(Shape{{2, 3}}, DType::I32);
        const auto out_id = p.add(a, b);

        const auto res = execute(p, out_id, backend);
        if (const auto* err = std::get_if<ExecError>(&res))
        {
            fail("unexpected error: " + err->message);
        }

        const auto& t = std::get<Tensor>(res);
        if (t.dtype != DType::I32)
        {
            fail("unexpected dtype");
        }
        if (t.shape.dims != std::vector<std::int64_t>({2, 3}))
        {
            fail("unexpected shape");
        }
        if (t.i32.size() != 6)
        {
            fail("unexpected element count");
        }
        for (const auto v : t.i32)
        {
            if (v != 0)
            {
                fail("expected zeros output");
            }
        }
    }

    {
        CpuBackend backend;
        Program p;
        const auto a = p.zeros(Shape{{2, 3}}, DType::I32);
        const auto b = p.zeros(Shape{{3, 2}}, DType::I32);
        const auto out_id = p.add(a, b);

        const auto res = execute(p, out_id, backend);
        const auto* err = std::get_if<ExecError>(&res);
        if (err == nullptr)
        {
            fail("expected error but got success");
        }

        const std::string expected = "tensor backend: add shape mismatch: lhs [2,3] rhs [3,2]";
        if (err->message != expected)
        {
            fail("unexpected error message\n--- got ---\n" + err->message + "\n--- expected ---\n" +
                 expected);
        }
    }

    return 0;
}
