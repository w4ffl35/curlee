#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

bool eq_env(const char* key, const char* expected)
{
    const char* v = std::getenv(key);
    if (v == nullptr)
    {
        return false;
    }
    return std::string(v) == expected;
}

} // namespace

int main()
{
    // Minimal deterministic fake runner for tests.
    // Validates env scrubbing + determinism knobs.
    if (!eq_env("PYTHONHASHSEED", "0") || !eq_env("LC_ALL", "C") || !eq_env("LANG", "C"))
    {
        std::cout << "{\"protocol_version\":1,\"id\":\"vm\",\"ok\":false,\"error\":{"
                     "\"kind\":\"invalid_request\",\"message\":\"env check "
                     "failed\",\"retryable\":false}}\n";
        return 2;
    }

    if (std::getenv("FOO") != nullptr)
    {
        std::cout << "{\"protocol_version\":1,\"id\":\"vm\",\"ok\":false,\"error\":{"
                     "\"kind\":\"invalid_request\",\"message\":\"env not "
                     "scrubbed\",\"retryable\":false}}\n";
        return 2;
    }

    std::cout << "{\"protocol_version\":1,\"id\":\"vm\",\"ok\":true}\n";
    return 0;
}
