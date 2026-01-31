#pragma once

#include <curlee/runtime/capabilities.h>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/**
 * @file python_ffi.h
 * @brief Small host-bound Python FFI helper (test harness only).
 */

namespace curlee::interop
{

/** @brief Error returned when a Python call fails. */
struct PythonFfiError
{
    std::string message;
};

/** @brief Result of `call_python`: empty on success or an error. */
using PythonFfiResult = std::variant<std::monostate, PythonFfiError>;

/**
 * @brief Call a Python function in `module::function` with string args.
 *
 * Requires the provided capabilities to allow Python calls.
 */
[[nodiscard]] PythonFfiResult call_python(const curlee::runtime::Capabilities& capabilities,
                                          std::string_view module, std::string_view function,
                                          const std::vector<std::string>& args);

} // namespace curlee::interop
