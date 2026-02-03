#include <iostream>
#include <string>

int main()
{
    // Minimal deterministic fake runner for tests.
    // Reads a line (ignored) and returns a non-JSON failure.
    std::string line;
    (void)std::getline(std::cin, line);

    std::cout << "not json\n";
    return 1;
}
