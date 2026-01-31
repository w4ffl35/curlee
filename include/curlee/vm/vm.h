#pragma once

#include <cstddef>
#include <curlee/runtime/capabilities.h>
#include <curlee/source/span.h>
#include <curlee/vm/bytecode.h>
#include <optional>
#include <string>

/**
 * @file vm.h
 * @brief VM entrypoints and runtime execution helpers.
 */

namespace curlee::vm
{

/** @brief Result of executing a chunk in the VM. */
struct VmResult
{
    bool ok = true;
    Value value = Value::unit_v();
    std::string error;
    std::optional<curlee::source::Span> error_span;
};

/**
 * @brief Simple deterministic virtual machine used by the test harness and runtime.
 */
class VM
{
  public:
    using Capabilities = curlee::runtime::Capabilities;

    /** @brief Run a chunk to completion using default fuel and capabilities. */
    [[nodiscard]] VmResult run(const Chunk& chunk);
    /** @brief Run a chunk with a fuel limit (to bound execution). */
    [[nodiscard]] VmResult run(const Chunk& chunk, std::size_t fuel);

    /** @brief Run with explicit capabilities. */
    [[nodiscard]] VmResult run(const Chunk& chunk, const Capabilities& capabilities);
    /** @brief Run with fuel and explicit capabilities. */
    [[nodiscard]] VmResult run(const Chunk& chunk, std::size_t fuel,
                               const Capabilities& capabilities);

  private:
    std::vector<Value> stack_;

    bool push(Value value);
    std::optional<Value> pop();
};

} // namespace curlee::vm
