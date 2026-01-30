#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace curlee::compiler::tensor_ir
{

enum class DType
{
    I32,
};

struct Shape
{
    std::vector<std::int64_t> dims;
};

struct ValueId
{
    std::uint32_t id;
};

class Program
{
  public:
    ValueId zeros(Shape shape, DType dtype);
    ValueId add(ValueId lhs, ValueId rhs);

    std::string dump() const;

  private:
    struct Op
    {
        std::string name;
        DType dtype;
        Shape shape;
        std::vector<ValueId> inputs;
    };

    std::vector<Op> ops_;
};

} // namespace curlee::compiler::tensor_ir
