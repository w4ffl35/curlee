#include <iostream>
#include <string>
#include <unistd.h>

int main()
{
    // Minimal deterministic fake runner for tests.
    // Reads a line (ignored) and then blocks long enough to trigger the VM timeout.
    std::string line;
    (void)std::getline(std::cin, line);

    // For tests we allow a short-circuit by setting CURLEE_TEST_SHORT_SLEEP=1
    // to avoid long sleeps in coverage runs while still exercising code paths.
    const char* short_sleep = std::getenv("CURLEE_TEST_SHORT_SLEEP");
    if (short_sleep == nullptr || std::string(short_sleep) != "1")
    {
        ::sleep(10);
    }

    // If the VM didn't kill us, emit a valid response.
    std::cout << "{\"protocol_version\":1,\"id\":\"vm\",\"ok\":true}\n";
    return 0;
}
