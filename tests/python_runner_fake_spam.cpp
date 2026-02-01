#include <cstdlib>
#include <iostream>
#include <string>

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
    // Reads a line (ignored) and then emits a lot of output to trigger the VM output limit.
    std::string line;
    (void)std::getline(std::cin, line);

    // Flush early so coverage is still recorded if the VM kills us mid-stream.
    maybe_gcov_flush();

    std::size_t spam_bytes = 2 * 1024 * 1024;
    if (const char* env = std::getenv("CURLEE_TEST_SPAM_BYTES"); env != nullptr && *env != '\0')
    {
        const long v = std::strtol(env, nullptr, 10);
        if (v > 0)
        {
            spam_bytes = static_cast<std::size_t>(v);
        }
    }

    const std::string spam(spam_bytes, 'x');
    std::cout << spam;
    std::cout.flush();

    return 0;
}
