#include <iostream>
#include <string>

int main()
{
    // Reads a line (ignored) and prints a "message" field that ends with a single
    // trailing backslash and never terminates (no closing quote).
    // This drives extract_error_message() through the (c=='\\' && i<json.size()) false path.
    std::string line;
    (void)std::getline(std::cin, line);

    std::cout << "{\"error\":{\"kind\":\"runner_crash\",\"message\":\"trail\\";
    return 2;
}
