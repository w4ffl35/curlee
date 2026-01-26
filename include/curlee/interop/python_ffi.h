#pragma once

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

[[nodiscard]] PythonFfiResult call_python(const std::vector<std::string>& capabilities,
                                          std::string_view module, std::string_view function,
                                          const std::vector<std::string>& args);

} // namespace curlee::interop
