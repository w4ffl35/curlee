#include <unistd.h>

int main()
{
    // Close stdin immediately so the parent write() hits EPIPE (with SIGPIPE ignored).
    (void)::close(STDIN_FILENO);
    return 2;
}
