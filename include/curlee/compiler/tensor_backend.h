#pragma once

#include <cstdint>
#include <curlee/compiler/tensor_ir.h>
#include <string>
#include <variant>
#include <vector>

/**
 * @file tensor_backend.h
 * @brief Execution backend API for the tensor IR used in tests.
 */

namespace curlee::compiler::tensor_ir
{

/** @brief Execution error returned by backends. */
struct ExecError
{
    std::string message;
};

template <typename T> using Result = std::variant<T, ExecError>;

/** @brief A concrete tensor instance produced by a backend. */
struct Tensor
{
    DType dtype;
    Shape shape;
    std::vector<std::int32_t> i32;
};

/** @brief Abstract execution backend interface. */
class Backend
{
  public:
    virtual ~Backend() = default;

    virtual Result<Tensor> zeros(const Shape& shape, DType dtype) = 0;
    virtual Result<Tensor> add(const Tensor& lhs, const Tensor& rhs) = 0;
};

/** @brief Execute the program and return the tensor value for `output` using `backend`. */
Result<Tensor> execute(const Program& program, ValueId output, Backend& backend);

/** @brief Reference CPU backend implementation for tests. */
class CpuBackend final : public Backend
{
  public:
    Result<Tensor> zeros(const Shape& shape, DType dtype) override;
    Result<Tensor> add(const Tensor& lhs, const Tensor& rhs) override;
};

} // namespace curlee::compiler::tensor_ir
