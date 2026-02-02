#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/select.h>
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
    // Avoid blocking if stdin has no data (CTest leaves stdin open). Poll with zero timeout.
    std::string line;
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    struct timeval tv = {0, 0};
    const int rv = select(1, &rfds, nullptr, nullptr, &tv);
    if (rv > 0)
    {
        (void)std::getline(std::cin, line);
    }

    // Flush early so coverage is still recorded if the VM kills us.
    maybe_gcov_flush();

    // Allow tests to run this helper to completion quickly.
    if (const char* mode = std::getenv("CURLEE_TEST_HANG_MODE");
        mode != nullptr && std::string(mode) == "exit")
    {
        std::cout << "{\"protocol_version\":1,\"id\":\"vm\",\"ok\":true}\n";
        return 0;
    }

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
