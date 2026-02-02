#include <iostream>
#include <string>

int main()
{
    // Minimal deterministic fake runner for tests.
    // Reads a line (ignored) and then emits a lot of output to trigger the VM output limit.
    std::string line;
    (void)std::getline(std::cin, line);

    // Allow tests to request a small spam payload to avoid huge output during coverage runs.
    const char* small_spam = std::getenv("CURLEE_TEST_SMALL_SPAM");
    if (small_spam != nullptr && std::string(small_spam) == "1")
    {
        std::cout << "xxxxx\n";
    }
    else
    {
        const std::string spam(2 * 1024 * 1024, 'x');
        std::cout << spam;
        std::cout.flush();
    }

    return 0;
}
