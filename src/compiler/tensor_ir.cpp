#include <curlee/compiler/tensor_ir.h>
#include <sstream>

namespace curlee::compiler::tensor_ir
{

static const char* dtype_to_string(DType dtype)
{
    switch (dtype)
    {
    case DType::I32:
        return "i32";
    }
    return "<unknown>";
}

static void append_shape(std::ostringstream& out, const Shape& shape)
{
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
}

ValueId Program::zeros(Shape shape, DType dtype)
{
    const ValueId id{static_cast<std::uint32_t>(ops_.size())};
    ops_.push_back(Op{"zeros", dtype, std::move(shape), {}});
    return id;
}

ValueId Program::add(ValueId lhs, ValueId rhs)
{
    const ValueId id{static_cast<std::uint32_t>(ops_.size())};
    // Minimal: inherit dtype/shape from lhs for now.
    const auto& lhs_op = ops_.at(lhs.id);
    ops_.push_back(Op{"add", lhs_op.dtype, lhs_op.shape, {lhs, rhs}});
    return id;
}

std::string Program::dump() const
{
    std::ostringstream out;
    for (std::size_t i = 0; i < ops_.size(); ++i)
    {
        const auto& op = ops_[i];
        out << '%' << i << " = " << op.name;

        if (!op.inputs.empty())
        {
            out << ' ';
            for (std::size_t j = 0; j < op.inputs.size(); ++j)
            {
                if (j != 0)
                {
                    out << ' ';
                }
                out << '%' << op.inputs[j].id;
            }
            out << " : " << dtype_to_string(op.dtype);
            append_shape(out, op.shape);
        }
        else
        {
            out << ' ' << dtype_to_string(op.dtype);
            append_shape(out, op.shape);
        }

        out << '\n';
    }
    return out.str();
}

} // namespace curlee::compiler::tensor_ir
