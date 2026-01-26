#include <algorithm>
#include <curlee/interop/python_ffi.h>

namespace curlee::interop
{

namespace
{

constexpr std::string_view kPythonCapability = "python:ffi";

bool has_capability(const std::vector<std::string>& capabilities, std::string_view needle)
{
    return std::any_of(capabilities.begin(), capabilities.end(),
                       [&](const std::string& cap) { return cap == needle; });
}

} // namespace

PythonFfiResult call_python(const std::vector<std::string>& capabilities,
                            std::string_view /*module*/, std::string_view /*function*/,
                            const std::vector<std::string>& /*args*/)
{
    if (!has_capability(capabilities, kPythonCapability))
    {
        return PythonFfiError{"python capability required"};
    }

    return PythonFfiError{"python interop not implemented"};
}

} // namespace curlee::interop
