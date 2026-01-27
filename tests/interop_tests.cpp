#include <cstdlib>
#include <curlee/interop/python_ffi.h>
#include <iostream>
#include <string>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

int main()
{
    using namespace curlee::interop;

    {
        curlee::runtime::Capabilities caps;
        caps.insert("io:stdout");

        const auto res = call_python(caps, "math", "sqrt", {"4"});
        if (!std::holds_alternative<PythonFfiError>(res))
        {
            fail("expected missing capability error");
        }
        const auto& err = std::get<PythonFfiError>(res);
        if (err.message != "python capability required")
        {
            fail("expected python capability required error");
        }
    }

    {
        curlee::runtime::Capabilities caps;
        caps.insert("python:ffi");

        const auto res = call_python(caps, "math", "sqrt", {"4"});
        if (!std::holds_alternative<PythonFfiError>(res))
        {
            fail("expected stub to return error until implemented");
        }
        const auto& err = std::get<PythonFfiError>(res);
        if (err.message != "python interop not implemented")
        {
            fail("expected not implemented error when capability present");
        }
    }

    std::cout << "OK\n";
    return 0;
}
