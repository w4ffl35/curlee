#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

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

int main(int argc, char** argv)
{
    int sep = -1;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--") == 0)
        {
            sep = i;
            break;
        }
    }

    if (sep < 0 || sep + 1 >= argc)
    {
        std::cerr << "bwrap_fake: missing -- COMMAND\n";
        maybe_gcov_flush();
        return 2;
    }

    (void)setenv("CURLEE_PYTHON_SANDBOX", "1", 1);

    std::vector<char*> child_argv;
    child_argv.reserve(static_cast<std::size_t>(argc - sep));
    for (int i = sep + 1; i < argc; ++i)
    {
        child_argv.push_back(argv[i]);
    }
    child_argv.push_back(nullptr);

    // `execv` replaces the process image, so flush coverage data before exec.
    maybe_gcov_flush();
    execv(child_argv[0], child_argv.data());
    std::cerr << "bwrap_fake: execv failed: " << std::strerror(errno) << "\n";
    maybe_gcov_flush();
    return 127;
}
