#pragma once

#include <cstddef>
#include <curlee/source/span.h>
#include <curlee/vm/bytecode.h>
#include <optional>
#include <string>

namespace curlee::vm
{

struct VmResult
{
    bool ok = true;
    Value value = Value::unit_v();
    std::string error;
    std::optional<curlee::source::Span> error_span;
};

class VM
{
  public:
    [[nodiscard]] VmResult run(const Chunk& chunk);
    [[nodiscard]] VmResult run(const Chunk& chunk, std::size_t fuel);

  private:
    std::vector<Value> stack_;

    bool push(Value value);
    std::optional<Value> pop();
};

} // namespace curlee::vm
