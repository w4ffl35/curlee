#include <iostream>
#include <string>

int main()
{
    // Reads a line (ignored) and prints a "message" field that never terminates.
    // This drives extract_error_message() to run to end-of-string and return nullopt.
    std::string line;
    (void)std::getline(std::cin, line);

    // Emit a trailing backslash at end-of-string (no closing quote) to exercise parser edge cases.
    std::cout << "{\"error\":{\"kind\":\"runner_crash\",\"message\":\"unterminated\\\\";
    return 2;
}
