#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

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

    execv(child_argv[0], child_argv.data());
    std::cerr << "bwrap_fake: execv failed: " << std::strerror(errno) << "\n";
    return 127;
}
