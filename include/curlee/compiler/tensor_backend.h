#pragma once

#include <cstdint>
#include <curlee/compiler/tensor_ir.h>
#include <string>
#include <variant>
#include <vector>

namespace curlee::compiler::tensor_ir
{

struct ExecError
{
    std::string message;
};

template <typename T> using Result = std::variant<T, ExecError>;

struct Tensor
{
    DType dtype;
    Shape shape;
    std::vector<std::int32_t> i32;
};

class Backend
{
  public:
    virtual ~Backend() = default;

    virtual Result<Tensor> zeros(const Shape& shape, DType dtype) = 0;
    virtual Result<Tensor> add(const Tensor& lhs, const Tensor& rhs) = 0;
};

Result<Tensor> execute(const Program& program, ValueId output, Backend& backend);

class CpuBackend final : public Backend
{
  public:
    Result<Tensor> zeros(const Shape& shape, DType dtype) override;
    Result<Tensor> add(const Tensor& lhs, const Tensor& rhs) override;
};

} // namespace curlee::compiler::tensor_ir
