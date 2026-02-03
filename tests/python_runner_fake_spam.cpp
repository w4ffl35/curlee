#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/select.h>

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
    // Avoid blocking if stdin has no data (CTest leaves stdin open). Poll with zero timeout.
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    struct timeval tv = {0, 0};
    const int rv = select(1, &rfds, nullptr, nullptr, &tv);
    if (rv > 0)
    {
        (void)std::getline(std::cin, line);
    }

    // Flush early so coverage is still recorded if the VM kills us mid-stream.
    maybe_gcov_flush();

    // Allow tests to request a small spam payload to avoid huge output during coverage runs.
    const char* small_spam = std::getenv("CURLEE_TEST_SMALL_SPAM");
    if (small_spam != nullptr && std::string(small_spam) == "1")
    {
        std::cout << "xxxxx\n";
        return 0;
    }

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

    // Write in chunks so the VM exercises multiple read/limit branches deterministically.
    const std::size_t first_chunk = 1 * 1024 * 1024;
    if (spam.size() <= first_chunk)
    {
        std::cout << spam;
        std::cout.flush();
        return 0;
    }

    std::cout.write(spam.data(), static_cast<std::streamsize>(first_chunk));
    std::cout.flush();
    std::cout.write(spam.data() + first_chunk,
                    static_cast<std::streamsize>(spam.size() - first_chunk));
    std::cout.flush();

    return 0;
}
