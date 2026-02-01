#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

#if defined(__GNUC__)
extern "C" void __gcov_flush(void) __attribute__((weak));
static void maybe_gcov_flush()
{
    if (__gcov_flush)
    {
        __gcov_flush();
    }
}
#else
static void maybe_gcov_flush() {}
#endif

int main()
{
    // Minimal deterministic fake runner for tests.
    // Reads a line (ignored) and then blocks long enough to trigger the VM timeout.
    std::string line;
    (void)std::getline(std::cin, line);

    // Flush early so coverage is still recorded if the VM kills us.
    maybe_gcov_flush();

    // Allow tests to run this helper to completion quickly.
    if (const char* mode = std::getenv("CURLEE_TEST_HANG_MODE");
        mode != nullptr && std::string(mode) == "exit")
    {
        std::cout << "{\"protocol_version\":1,\"id\":\"vm\",\"ok\":true}\n";
        return 0;
    }

    ::sleep(10);

    // If the VM didn't kill us, emit a valid response.
    std::cout << "{\"protocol_version\":1,\"id\":\"vm\",\"ok\":true}\n";
    return 0;
}
