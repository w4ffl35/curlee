#include <iostream>
#include <string>

int main()
{
    // Minimal deterministic fake runner for tests.
    // Reads a line (ignored) and returns a structured error response with
    // common escape sequences in the message.
    std::string line;
    (void)std::getline(std::cin, line);

    std::cout
        << "{\"error\":{\"kind\":\"runner_crash\",\"message\":\"forced "
           "\\\"runner\\nerror\\twith\\rcarriage\\\\slash and "
           "esc\\qape\",\"retryable\":false},\"id\":\"vm\",\"ok\":false,\"protocol_version\":1}\n";
    return 2;
}
