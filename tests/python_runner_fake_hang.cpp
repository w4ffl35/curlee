#include <iostream>
#include <string>
#include <unistd.h>

int main()
{
    // Minimal deterministic fake runner for tests.
    // Reads a line (ignored) and then blocks long enough to trigger the VM timeout.
    std::string line;
    (void)std::getline(std::cin, line);

    ::sleep(10);

    // If the VM didn't kill us, emit a valid response.
    std::cout << "{\"protocol_version\":1,\"id\":\"vm\",\"ok\":true}\n";
    return 0;
}
