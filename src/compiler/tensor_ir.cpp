#include <curlee/compiler/tensor_ir.h>
#include <sstream>
#include <utility>

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
    Op op;
    op.name = "zeros";
    op.dtype = dtype;
    op.shape = std::move(shape);
    op.inputs = {};
    ops_.push_back(std::move(op));
    return id;
}

ValueId Program::add(ValueId lhs, ValueId rhs)
{
    const ValueId id{static_cast<std::uint32_t>(ops_.size())};
    // Minimal: inherit dtype/shape from lhs for now.
    const auto& lhs_op = ops_.at(lhs.id);
    Op op;
    op.name = "add";
    op.dtype = lhs_op.dtype;
    op.shape = lhs_op.shape;
    op.inputs = {lhs, rhs};
    ops_.push_back(std::move(op));
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
