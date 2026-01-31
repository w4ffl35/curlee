#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
std::string make_error_response(const std::string& id, const std::string& kind,
                                const std::string& message)
{
    return "{\"id\":\"" + id + "\",\"ok\":false,\"protocol_version\":1,\"error\":{\"kind\":\"" +
           kind + "\",\"message\":\"" + message + "\",\"retryable\":false}}\n";
}

std::string make_ok_response(const std::string& id)
{
    return "{\"id\":\"" + id +
           "\",\"ok\":true,\"protocol_version\":1,\"result\":{\"type\":\"string\",\"value\":\"ok\"}"
           "}\n";
}

} // namespace

int main()
{
    std::string line;
    if (!std::getline(std::cin, line))
    {
        std::cout << make_error_response("", "invalid_request", "empty input");
        return 2;
    }

    const char* sandbox = std::getenv("CURLEE_PYTHON_SANDBOX");
    if (sandbox == nullptr || std::string(sandbox) != "1")
    {
        std::cout << make_error_response("vm", "sandbox_required", "sandbox required");
        return 2;
    }

    std::cout << make_ok_response("vm");
    return 0;
}
