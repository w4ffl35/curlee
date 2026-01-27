#include <iostream>
#include <string>

int main()
{
    // Minimal deterministic fake runner for tests.
    // Reads a line (ignored) and then emits a lot of output to trigger the VM output limit.
    std::string line;
    (void)std::getline(std::cin, line);

    const std::string spam(2 * 1024 * 1024, 'x');
    std::cout << spam;
    std::cout.flush();

    return 0;
}
