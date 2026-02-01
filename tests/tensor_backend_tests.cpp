#include <cstdint>
#include <cstdlib>
#include <curlee/compiler/tensor_backend.h>
#include <iostream>
#include <limits>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

int main()
{
    using namespace curlee::compiler::tensor_ir;

    // execute(): invalid output id.
    {
        CpuBackend backend;
        Program p;
        const auto res = execute(p, ValueId{12345}, backend);
        const auto* err = std::get_if<ExecError>(&res);
        if (err == nullptr)
        {
            fail("expected error but got success");
        }
        if (err->message != "tensor backend: invalid output value")
        {
            fail("unexpected error: " + err->message);
        }
    }

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

    // execute(): zeros shape validation.
    {
        CpuBackend backend;
        Program p;
        const auto out_id = p.zeros(Shape{{-1, 2}}, DType::I32);
        const auto res = execute(p, out_id, backend);
        const auto* err = std::get_if<ExecError>(&res);
        if (err == nullptr)
        {
            fail("expected error but got success");
        }
        if (err->message != "tensor backend: negative dimension in shape [-1,2]")
        {
            fail("unexpected error: " + err->message);
        }
    }

    {
        CpuBackend backend;
        Program p;
        const auto out_id = p.zeros(Shape{{0, 5}}, DType::I32);
        const auto res = execute(p, out_id, backend);
        if (const auto* err = std::get_if<ExecError>(&res))
        {
            fail("unexpected error: " + err->message);
        }
        const auto& t = std::get<Tensor>(res);
        if (!t.i32.empty())
        {
            fail("expected zero-element tensor for shape with zero dim");
        }
    }

    {
        CpuBackend backend;
        Program p;
        const auto huge = std::numeric_limits<std::int64_t>::max();
        const auto out_id = p.zeros(Shape{{huge, 3}}, DType::I32);
        const auto res = execute(p, out_id, backend);
        const auto* err = std::get_if<ExecError>(&res);
        if (err == nullptr)
        {
            fail("expected error but got success");
        }
        if (err->message != "tensor backend: shape too large [9223372036854775807,3]")
        {
            fail("unexpected error: " + err->message);
        }
    }

    // CpuBackend::zeros(): unsupported dtype (simulate future/invalid dtype).
    {
        CpuBackend backend;
        const auto res = backend.zeros(Shape{{1}}, static_cast<DType>(123));
        const auto* err = std::get_if<ExecError>(&res);
        if (err == nullptr)
        {
            fail("expected error but got success");
        }
        if (err->message != "tensor backend: unsupported dtype")
        {
            fail("unexpected error: " + err->message);
        }
    }

    // CpuBackend::add(): dtype mismatch.
    {
        CpuBackend backend;

        Tensor a;
        a.dtype = static_cast<DType>(123);
        a.shape = Shape{{1}};
        a.i32 = {0};

        Tensor b;
        b.dtype = DType::I32;
        b.shape = Shape{{1}};
        b.i32 = {0};

        const auto res = backend.add(a, b);
        const auto* err = std::get_if<ExecError>(&res);
        if (err == nullptr)
        {
            fail("expected error but got success");
        }
        if (err->message != "tensor backend: add dtype mismatch")
        {
            fail("unexpected error: " + err->message);
        }
    }

    // CpuBackend::add(): unsupported dtype.
    {
        CpuBackend backend;

        Tensor a;
        a.dtype = static_cast<DType>(123);
        a.shape = Shape{{1}};
        a.i32 = {0};

        Tensor b = a;

        const auto res = backend.add(a, b);
        const auto* err = std::get_if<ExecError>(&res);
        if (err == nullptr)
        {
            fail("expected error but got success");
        }
        if (err->message != "tensor backend: unsupported dtype")
        {
            fail("unexpected error: " + err->message);
        }
    }

    // CpuBackend::add(): internal size mismatch.
    {
        CpuBackend backend;

        Tensor a;
        a.dtype = DType::I32;
        a.shape = Shape{{1}};
        a.i32 = {0};

        Tensor b;
        b.dtype = DType::I32;
        b.shape = Shape{{1}};
        b.i32 = {0, 0};

        const auto res = backend.add(a, b);
        const auto* err = std::get_if<ExecError>(&res);
        if (err == nullptr)
        {
            fail("expected error but got success");
        }
        if (err->message != "tensor backend: add internal size mismatch")
        {
            fail("unexpected error: " + err->message);
        }
    }

    // CpuBackend::add(): overflow.
    {
        CpuBackend backend;

        Tensor a;
        a.dtype = DType::I32;
        a.shape = Shape{{1}};
        a.i32 = {std::numeric_limits<std::int32_t>::max()};

        Tensor b;
        b.dtype = DType::I32;
        b.shape = Shape{{1}};
        b.i32 = {1};

        const auto res = backend.add(a, b);
        const auto* err = std::get_if<ExecError>(&res);
        if (err == nullptr)
        {
            fail("expected error but got success");
        }
        if (err->message != "tensor backend: add overflow")
        {
            fail("unexpected error: " + err->message);
        }
    }

    return 0;
}
