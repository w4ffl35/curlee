#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * @file tensor_ir.h
 * @brief Minimal IR for tensor computation used by compiler tests.
 */

namespace curlee::compiler::tensor_ir
{

/** @brief Element dtype for tensors (MVP: only I32 supported). */
enum class DType
{
    I32,
};

/** @brief Shape descriptor for tensors. */
struct Shape
{
    std::vector<std::int64_t> dims;
};

/** @brief Opaque handle to a value produced within a Program. */
struct ValueId
{
    std::uint32_t id;
};

/** @brief A simple tensor program builder. */
class Program
{
  public:
    ValueId zeros(Shape shape, DType dtype);
    ValueId add(ValueId lhs, ValueId rhs);

    struct Op
    {
        std::string name;
        DType dtype;
        Shape shape;
        std::vector<ValueId> inputs;
    };

    const std::vector<Op>& ops() const { return ops_; }

    std::string dump() const;

  private:
    std::vector<Op> ops_;
};

} // namespace curlee::compiler::tensor_ir
