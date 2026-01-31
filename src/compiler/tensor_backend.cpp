#include <cstddef>
#include <curlee/compiler/tensor_backend.h>
#include <limits>
#include <sstream>

namespace curlee::compiler::tensor_ir
{

static std::string shape_to_string(const Shape& shape)
{
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < shape.dims.size(); ++i)
    {
        if (i != 0)
        {
            out << ',';
        }
        out << shape.dims[i];
    }
    out << ']';
    return out.str();
}

static Result<std::size_t> num_elements(const Shape& shape)
{
    std::size_t elems = 1;
    for (const auto dim64 : shape.dims)
    {
        if (dim64 < 0)
        {
            return ExecError{.message = "tensor backend: negative dimension in shape " +
                                        shape_to_string(shape)};
        }

        const auto dim = static_cast<std::size_t>(dim64);
        if (dim == 0)
        {
            elems = 0;
            continue;
        }

        if (elems > (std::numeric_limits<std::size_t>::max() / dim))
        {
            return ExecError{.message =
                                 "tensor backend: shape too large " + shape_to_string(shape)};
        }
        elems *= dim;
    }
    return elems;
}

Result<Tensor> CpuBackend::zeros(const Shape& shape, DType dtype)
{
    if (dtype != DType::I32)
    {
        return ExecError{.message = "tensor backend: unsupported dtype"};
    }

    const auto elems_or_err = num_elements(shape);
    if (const auto* err = std::get_if<ExecError>(&elems_or_err))
    {
        return *err;
    }

    const auto elems = std::get<std::size_t>(elems_or_err);
    Tensor t;
    t.dtype = dtype;
    t.shape = shape;
    t.i32.assign(elems, 0);
    return t;
}

static bool same_shape(const Shape& a, const Shape& b)
{
    return a.dims == b.dims;
}

Result<Tensor> CpuBackend::add(const Tensor& lhs, const Tensor& rhs)
{
    if (lhs.dtype != rhs.dtype)
    {
        return ExecError{.message = "tensor backend: add dtype mismatch"};
    }
    if (!same_shape(lhs.shape, rhs.shape))
    {
        return ExecError{.message = "tensor backend: add shape mismatch: lhs " +
                                    shape_to_string(lhs.shape) + " rhs " +
                                    shape_to_string(rhs.shape)};
    }
    if (lhs.dtype != DType::I32)
    {
        return ExecError{.message = "tensor backend: unsupported dtype"};
    }
    if (lhs.i32.size() != rhs.i32.size())
    {
        return ExecError{.message = "tensor backend: add internal size mismatch"};
    }

    Tensor out;
    out.dtype = lhs.dtype;
    out.shape = lhs.shape;
    out.i32.resize(lhs.i32.size());

    for (std::size_t i = 0; i < lhs.i32.size(); ++i)
    {
        const std::int64_t sum =
            static_cast<std::int64_t>(lhs.i32[i]) + static_cast<std::int64_t>(rhs.i32[i]);
        if (sum < std::numeric_limits<std::int32_t>::min() ||
            sum > std::numeric_limits<std::int32_t>::max())
        {
            return ExecError{.message = "tensor backend: add overflow"};
        }
        out.i32[i] = static_cast<std::int32_t>(sum);
    }

    return out;
}

Result<Tensor> execute(const Program& program, ValueId output, Backend& backend)
{
    const auto& ops = program.ops();
    if (output.id >= ops.size())
    {
        return ExecError{.message = "tensor backend: invalid output value"};
    }

    std::vector<Tensor> values;
    values.reserve(ops.size());

    for (std::size_t i = 0; i < ops.size(); ++i)
    {
        const auto& op = ops[i];

        Result<Tensor> produced = ExecError{.message = "tensor backend: internal error"};

        if (op.name == "zeros")
        {
            produced = backend.zeros(op.shape, op.dtype);
        }
        else if (op.name == "add")
        {
            if (op.inputs.size() != 2)
            {
                return ExecError{.message = "tensor backend: add expects 2 inputs"};
            }

            const auto lhs_id = op.inputs[0].id;
            const auto rhs_id = op.inputs[1].id;
            if (lhs_id >= values.size() || rhs_id >= values.size())
            {
                return ExecError{.message = "tensor backend: op uses forward reference"};
            }

            produced = backend.add(values[lhs_id], values[rhs_id]);
        }
        else
        {
            return ExecError{.message = "tensor backend: unknown op '" + op.name + "'"};
        }

        if (const auto* err = std::get_if<ExecError>(&produced))
        {
            return *err;
        }

        values.push_back(std::move(std::get<Tensor>(produced)));
    }

    return values.at(output.id);
}

} // namespace curlee::compiler::tensor_ir
