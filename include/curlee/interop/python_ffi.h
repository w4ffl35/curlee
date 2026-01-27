#pragma once

#include <curlee/runtime/capabilities.h>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace curlee::interop
{

struct PythonFfiError
{
    std::string message;
};

using PythonFfiResult = std::variant<std::monostate, PythonFfiError>;

[[nodiscard]] PythonFfiResult call_python(const curlee::runtime::Capabilities& capabilities,
                                          std::string_view module, std::string_view function,
                                          const std::vector<std::string>& args);

} // namespace curlee::interop
