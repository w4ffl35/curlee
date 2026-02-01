#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

struct RunOpts
{
    std::string stdin_data;
    bool redirect_stdout_to_devnull = false;
    bool redirect_stderr_to_devnull = false;
};

static int run_child(const std::string& exe, const std::vector<std::string>& args, const RunOpts& o)
{
    int in_pipe[2] = {-1, -1};
    if (pipe(in_pipe) != 0)
    {
        fail(std::string("pipe failed: ") + std::strerror(errno));
    }

    const pid_t pid = fork();
    if (pid < 0)
    {
        fail(std::string("fork failed: ") + std::strerror(errno));
    }

    if (pid == 0)
    {
        (void)dup2(in_pipe[0], STDIN_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);

        if (o.redirect_stdout_to_devnull)
        {
            const int fd = open("/dev/null", O_WRONLY);
            if (fd >= 0)
            {
                (void)dup2(fd, STDOUT_FILENO);
                close(fd);
            }
        }
        if (o.redirect_stderr_to_devnull)
        {
            const int fd = open("/dev/null", O_WRONLY);
            if (fd >= 0)
            {
                (void)dup2(fd, STDERR_FILENO);
                close(fd);
            }
        }

        std::vector<char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char*>(exe.c_str()));
        for (const auto& a : args)
        {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        execv(exe.c_str(), argv.data());
        // If execv fails.
        std::cerr << "execv failed: " << std::strerror(errno) << "\n";
        std::exit(127);
    }

    close(in_pipe[0]);

    // Send input then close stdin.
    {
        const char* data = o.stdin_data.data();
        std::size_t remaining = o.stdin_data.size();
        while (remaining > 0)
        {
            const ssize_t n = write(in_pipe[1], data, remaining);
            if (n < 0)
            {
                fail(std::string("write failed: ") + std::strerror(errno));
            }
            data += n;
            remaining -= static_cast<std::size_t>(n);
        }
        close(in_pipe[1]);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
    {
        fail(std::string("waitpid failed: ") + std::strerror(errno));
    }

    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status))
    {
        return 128 + WTERMSIG(status);
    }

    return 128;
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        fail("usage: curlee_fake_exes_tests <bwrap_fake> <runner_hang> <runner_spam>");
    }

    const std::string bwrap_fake = argv[1];
    const std::string runner_hang = argv[2];
    const std::string runner_spam = argv[3];

    // bwrap_fake: missing "--" should exit 2.
    {
        const int code = run_child(bwrap_fake, {}, RunOpts{});
        if (code != 2)
        {
            fail("expected bwrap_fake with no args to exit 2");
        }
    }

    // bwrap_fake: execv failure path should exit 127.
    {
        const int code = run_child(bwrap_fake, {"--", "/no/such/definitely_missing"}, RunOpts{});
        if (code != 127)
        {
            fail("expected bwrap_fake execv failure to exit 127");
        }
    }

    // python_runner_fake_hang: fast path should exit 0 quickly.
    {
        (void)setenv("CURLEE_TEST_HANG_MODE", "exit", 1);
        RunOpts o;
        o.stdin_data = "{}\n";
        const int code = run_child(runner_hang, {}, o);
        (void)unsetenv("CURLEE_TEST_HANG_MODE");
        if (code != 0)
        {
            fail("expected runner_hang fast path to exit 0");
        }
    }

    // python_runner_fake_spam: run to completion with smaller output.
    {
        (void)setenv("CURLEE_TEST_SPAM_BYTES", "1024", 1);
        RunOpts o;
        o.stdin_data = "{}\n";
        o.redirect_stdout_to_devnull = true;
        const int code = run_child(runner_spam, {}, o);
        (void)unsetenv("CURLEE_TEST_SPAM_BYTES");
        if (code != 0)
        {
            fail("expected runner_spam to exit 0");
        }
    }

    std::cout << "OK\n";
    return 0;
}
