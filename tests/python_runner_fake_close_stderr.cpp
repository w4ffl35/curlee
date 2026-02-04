#include <chrono>
#include <cstdlib>
#include <sys/select.h>
#include <thread>
#include <unistd.h>

namespace
{
void maybe_read_stdin_line()
{
    // Avoid blocking if stdin has no data (CTest leaves stdin open).
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    timeval tv{0, 0};
    const int rv = select(1, &rfds, nullptr, nullptr, &tv);
    if (rv > 0)
    {
        char buf[256];
        (void)read(0, buf, sizeof(buf));
    }
}
} // namespace

int main()
{
    maybe_read_stdin_line();

    // Close stderr so the parent VM sees err_eof become true while stdout stays open.
    (void)close(STDERR_FILENO);

    static constexpr char kOut[] = "not-json\n";
    (void)write(STDOUT_FILENO, kOut, sizeof(kOut) - 1);

    // Sleep long enough for the parent to spin at least one more poll iteration.
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    return 0;
}
