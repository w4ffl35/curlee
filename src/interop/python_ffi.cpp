#include <curlee/interop/python_ffi.h>

namespace curlee::interop
{

namespace
{

constexpr std::string_view kPythonCapability = "python.ffi";

} // namespace

PythonFfiResult call_python(const curlee::runtime::Capabilities& capabilities,
                            std::string_view /*module*/, std::string_view /*function*/,
                            const std::vector<std::string>& /*args*/)
{
    const bool has_capability = capabilities.contains(std::string(kPythonCapability));
    if (!has_capability)
    {
        return PythonFfiError{"python capability required"};
    }

    return PythonFfiError{"python interop not implemented"};
}

} // namespace curlee::interop
