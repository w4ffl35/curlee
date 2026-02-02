#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
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

struct ProcResult
{
    int exit_code = -1;
    std::string out;
    std::string err;
};

static void set_nonblocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        fail(std::string("fcntl(F_GETFL) failed: ") + std::strerror(errno));
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        fail(std::string("fcntl(F_SETFL) failed: ") + std::strerror(errno));
    }
}

static void read_into(int fd, std::string& out, bool& eof)
{
    char buf[4096];
    while (true)
    {
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0)
        {
            out.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0)
        {
            eof = true;
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return;
        }
        fail(std::string("read failed: ") + std::strerror(errno));
    }
}

static ProcResult run_runner(const std::string& exe_path, const std::string& stdin_data)
{
    int in_pipe[2] = {-1, -1};
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0 || pipe(err_pipe) != 0)
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
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);

        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(exe_path.c_str()));
        argv.push_back(nullptr);

        execv(exe_path.c_str(), argv.data());
        std::cerr << "execv failed: " << std::strerror(errno) << "\n";
        std::exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);

    // Send input then close stdin.
    {
        const char* data = stdin_data.data();
        std::size_t remaining = stdin_data.size();
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

    set_nonblocking(out_pipe[0]);
    set_nonblocking(err_pipe[0]);

    ProcResult result;
    bool out_eof = false;
    bool err_eof = false;

    while (!out_eof || !err_eof)
    {
        struct pollfd fds[2];
        fds[0].fd = out_pipe[0];
        fds[0].events = POLLIN;
        fds[1].fd = err_pipe[0];
        fds[1].events = POLLIN;

        const int rc = poll(fds, 2, 1000);
        if (rc < 0)
        {
            fail(std::string("poll failed: ") + std::strerror(errno));
        }

        if (!out_eof)
        {
            read_into(out_pipe[0], result.out, out_eof);
        }
        if (!err_eof)
        {
            read_into(err_pipe[0], result.err, err_eof);
        }
    }

    close(out_pipe[0]);
    close(err_pipe[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
    {
        fail(std::string("waitpid failed: ") + std::strerror(errno));
    }
    if (WIFEXITED(status))
    {
        result.exit_code = WEXITSTATUS(status);
    }
    else
    {
        result.exit_code = 128;
    }

    return result;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fail("usage: curlee_python_runner_tests <path-to-curlee_python_runner>");
    }
    const std::string runner = argv[1];

    // empty input
    {
        const auto res = run_runner(runner, "");
        if (res.exit_code != 2)
        {
            fail("expected empty input to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"empty "
            "input\",\"retryable\":false},\"id\":\"\",\"ok\":false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("empty input stdout mismatch");
        }
    }

    {
        const auto res =
            run_runner(runner, "{\"protocol_version\":1,\"id\":\"t1\",\"op\":\"handshake\"}\n");
        if (res.exit_code != 0)
        {
            fail("expected handshake to exit 0");
        }
        const std::string expected = "{\"id\":\"t1\",\"ok\":true,\"protocol_version\":1,\"result\":"
                                     "{\"type\":\"string\",\"value\":\"ok\"}}\n";
        if (res.out != expected)
        {
            fail("handshake stdout mismatch");
        }
        if (!res.err.empty())
        {
            fail("handshake expected empty stderr");
        }
    }

    {
        const auto res = run_runner(
            runner,
            "{\"protocol_version\":1,\"id\":\"t2\",\"op\":\"echo\",\"echo\":{\"value\":\"hi\"}}\n");
        if (res.exit_code != 0)
        {
            fail("expected echo to exit 0");
        }
        const std::string expected = "{\"id\":\"t2\",\"ok\":true,\"protocol_version\":1,\"result\":"
                                     "{\"type\":\"string\",\"value\":\"hi\"}}\n";
        if (res.out != expected)
        {
            fail("echo stdout mismatch");
        }
        if (!res.err.empty())
        {
            fail("echo expected empty stderr");
        }
    }

    // protocol_version: exponent form should parse as an integral 1.
    {
        const auto res = run_runner(
            runner, "{\"protocol_version\":1e0,\"id\":\"t_exp\",\"op\":\"handshake\"}\n");
        if (res.exit_code != 0)
        {
            fail("expected exponent protocol_version to exit 0");
        }
        const std::string expected =
            "{\"id\":\"t_exp\",\"ok\":true,\"protocol_version\":1,\"result\":"
            "{\"type\":\"string\",\"value\":\"ok\"}}\n";
        if (res.out != expected)
        {
            fail("exponent protocol_version stdout mismatch");
        }
        if (!res.err.empty())
        {
            fail("exponent protocol_version expected empty stderr");
        }
    }

    // protocol_version: exponent form with explicit negative sign should parse as integral 1.
    {
        const auto res = run_runner(
            runner, "{\"protocol_version\":1e-0,\"id\":\"t_exp_neg\",\"op\":\"handshake\"}\n");
        if (res.exit_code != 0)
        {
            fail("expected exponent-with-sign protocol_version to exit 0");
        }
        const std::string expected =
            "{\"id\":\"t_exp_neg\",\"ok\":true,\"protocol_version\":1,\"result\":"
            "{\"type\":\"string\",\"value\":\"ok\"}}\n";
        if (res.out != expected)
        {
            fail("exponent-with-sign protocol_version stdout mismatch");
        }
        if (!res.err.empty())
        {
            fail("exponent-with-sign protocol_version expected empty stderr");
        }
    }

    // Extra fields: exercise array/object parsing plus null/true/false and more number forms.
    {
        const auto res =
            run_runner(runner, "{\"protocol_version\":1E+0,\"id\":\"t_extra\",\"op\":\"handshake\","
                               "\"arr\":[null,true,false,1,-2.5e1],\"obj\":{\"k\":\"v\"}}\n");
        if (res.exit_code != 0)
        {
            fail("expected extra-fields handshake to exit 0");
        }
        const std::string expected =
            "{\"id\":\"t_extra\",\"ok\":true,\"protocol_version\":1,\"result\":"
            "{\"type\":\"string\",\"value\":\"ok\"}}\n";
        if (res.out != expected)
        {
            fail("extra-fields handshake stdout mismatch");
        }
        if (!res.err.empty())
        {
            fail("extra-fields handshake expected empty stderr");
        }
    }

    // More string escapes (/, \b, \f, \r, \t) should be accepted.
    {
        const auto res =
            run_runner(runner, "{\"protocol_version\":1,\"id\":\"t_esc2\",\"op\":\"handshake\","
                               "\"x\":\"\\/\\b\\f\\r\\t\"}\n");
        if (res.exit_code != 0)
        {
            fail("expected additional-escapes handshake to exit 0");
        }
        const std::string expected =
            "{\"id\":\"t_esc2\",\"ok\":true,\"protocol_version\":1,\"result\":"
            "{\"type\":\"string\",\"value\":\"ok\"}}\n";
        if (res.out != expected)
        {
            fail("additional-escapes handshake stdout mismatch");
        }
    }

    {
        const auto res =
            run_runner(runner, "{\"protocol_version\":2,\"id\":\"t3\",\"op\":\"handshake\"}\n");
        if (res.exit_code != 2)
        {
            fail("expected unsupported protocol to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"protocol_version_unsupported\",\"message\":\"unsupported "
            "protocol "
            "version\",\"retryable\":false},\"id\":\"t3\",\"ok\":false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("unsupported protocol stdout mismatch");
        }
    }

    {
        const auto res = run_runner(runner, "not json\n");
        if (res.exit_code != 2)
        {
            fail("expected malformed json to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"malformed "
            "json\",\"retryable\":false},\"id\":\"\",\"ok\":false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("malformed json stdout mismatch");
        }
    }

    // Malformed object: non-string key.
    {
        const auto res = run_runner(runner, "{1:2}\n");
        if (res.exit_code != 2)
        {
            fail("expected non-string object key to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"malformed "
            "json\",\"retryable\":false},\"id\":\"\",\"ok\":false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("non-string object key stdout mismatch");
        }
    }

    // Malformed number: '-' should fail number parsing.
    {
        const auto res = run_runner(
            runner, "{\"protocol_version\":-,\"id\":\"t_badnum\",\"op\":\"handshake\"}\n");
        if (res.exit_code != 2)
        {
            fail("expected malformed number to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"malformed "
            "json\",\"retryable\":false},\"id\":\"\",\"ok\":false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("malformed number stdout mismatch");
        }
    }

    // Malformed array: missing comma.
    {
        const auto res = run_runner(
            runner,
            "{\"protocol_version\":1,\"id\":\"t_badarr\",\"op\":\"handshake\",\"arr\":[1 2]}\n");
        if (res.exit_code != 2)
        {
            fail("expected malformed array to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"malformed "
            "json\",\"retryable\":false},\"id\":\"\",\"ok\":false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("malformed array stdout mismatch");
        }
    }

    // Malformed object: missing ':' after key.
    {
        const auto res = run_runner(runner, "{\"protocol_version\":1,\"id\":\"t_badobj\",\"op\":"
                                            "\"handshake\",\"obj\":{\"k\" \"v\"}}\n");
        if (res.exit_code != 2)
        {
            fail("expected malformed object to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"malformed "
            "json\",\"retryable\":false},\"id\":\"\",\"ok\":false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("malformed object stdout mismatch");
        }
    }

    // Malformed string: trailing backslash before end-of-input.
    {
        const auto res = run_runner(
            runner,
            "{\"protocol_version\":1,\"id\":\"t_badesc\",\"op\":\"handshake\",\"x\":\"abc\\\\\n");
        if (res.exit_code != 2)
        {
            fail("expected malformed string to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"malformed "
            "json\",\"retryable\":false},\"id\":\"\",\"ok\":false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("malformed string stdout mismatch");
        }
    }

    // JSON parses but is not an object.
    {
        const auto res = run_runner(runner, "[]\n");
        if (res.exit_code != 2)
        {
            fail("expected non-object json to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"malformed "
            "json\",\"retryable\":false},\"id\":\"\",\"ok\":false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("non-object json stdout mismatch");
        }
    }

    // Trailing garbage should fail parse_json() (not just parse_value()).
    {
        const auto res = run_runner(
            runner, "{\"protocol_version\":1,\"id\":\"t_tail\",\"op\":\"handshake\"} trailing\n");
        if (res.exit_code != 2)
        {
            fail("expected trailing garbage to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"malformed "
            "json\",\"retryable\":false},\"id\":\"\",\"ok\":false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("trailing garbage stdout mismatch");
        }
    }

    // Invalid string escape should be rejected as malformed json.
    {
        const auto res = run_runner(
            runner,
            "{\"protocol_version\":1,\"id\":\"t_esc\",\"op\":\"handshake\",\"x\":\"bad\\q\"}\n");
        if (res.exit_code != 2)
        {
            fail("expected invalid escape to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"malformed "
            "json\",\"retryable\":false},\"id\":\"\",\"ok\":false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("invalid escape stdout mismatch");
        }
    }

    // Missing protocol_version should be reported using the parsed request id.
    {
        const auto res = run_runner(runner, "{\"id\":\"t_nover\",\"op\":\"handshake\"}\n");
        if (res.exit_code != 2)
        {
            fail("expected missing protocol to exit 2");
        }
        const std::string expected = "{\"error\":{\"kind\":\"protocol_version_unsupported\","
                                     "\"message\":\"unsupported protocol "
                                     "version\",\"retryable\":false},\"id\":\"t_nover\",\"ok\":"
                                     "false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("missing protocol stdout mismatch");
        }
    }

    // Non-integral protocol_version should be rejected.
    {
        const auto res = run_runner(
            runner, "{\"protocol_version\":1.1,\"id\":\"t_nonint\",\"op\":\"handshake\"}\n");
        if (res.exit_code != 2)
        {
            fail("expected non-integral protocol to exit 2");
        }
        const std::string expected = "{\"error\":{\"kind\":\"protocol_version_unsupported\","
                                     "\"message\":\"unsupported protocol "
                                     "version\",\"retryable\":false},\"id\":\"t_nonint\",\"ok\":"
                                     "false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("non-integral protocol stdout mismatch");
        }
    }

    // Negative protocol_version should be rejected (covers '-' number parsing with digits).
    {
        const auto res =
            run_runner(runner, "{\"protocol_version\":-1,\"id\":\"t_neg\",\"op\":\"handshake\"}\n");
        if (res.exit_code != 2)
        {
            fail("expected negative protocol_version to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"protocol_version_unsupported\",\"message\":\"unsupported "
            "protocol "
            "version\",\"retryable\":false},\"id\":\"t_neg\",\"ok\":false,\"protocol_version\":1}"
            "\n";
        if (res.out != expected)
        {
            fail("negative protocol_version stdout mismatch");
        }
    }

    // Missing op should be rejected.
    {
        const auto res = run_runner(runner, "{\"protocol_version\":1,\"id\":\"t_noop\"}\n");
        if (res.exit_code != 2)
        {
            fail("expected missing op to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"missing "
            "op\",\"retryable\":false},\"id\":\"t_noop\",\"ok\":false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("missing op stdout mismatch");
        }
    }

    // Handshake without an id should succeed with id:"".
    {
        const auto res = run_runner(runner, "{\"protocol_version\":1,\"op\":\"handshake\"}\n");
        if (res.exit_code != 0)
        {
            fail("expected handshake without id to exit 0");
        }
        const std::string expected = "{\"id\":\"\",\"ok\":true,\"protocol_version\":1,\"result\":"
                                     "{\"type\":\"string\",\"value\":\"ok\"}}\n";
        if (res.out != expected)
        {
            fail("handshake without id stdout mismatch");
        }
    }

    // Unknown op should be rejected.
    {
        const auto res =
            run_runner(runner, "{\"protocol_version\":1,\"id\":\"t_unknown\",\"op\":\"nope\"}\n");
        if (res.exit_code != 2)
        {
            fail("expected unknown op to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"unknown "
            "op\",\"retryable\":false},\"id\":\"t_unknown\",\"ok\":false,\"protocol_version\":1}\n";
        if (res.out != expected)
        {
            fail("unknown op stdout mismatch");
        }
    }

    // Echo: missing echo payload.
    {
        const auto res =
            run_runner(runner, "{\"protocol_version\":1,\"id\":\"t_echo0\",\"op\":\"echo\"}\n");
        if (res.exit_code != 2)
        {
            fail("expected missing echo payload to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"missing echo "
            "payload\",\"retryable\":false},\"id\":\"t_echo0\",\"ok\":false,\"protocol_version\":1}"
            "\n";
        if (res.out != expected)
        {
            fail("missing echo payload stdout mismatch");
        }
    }

    // Echo: wrong type for echo payload.
    {
        const auto res = run_runner(
            runner,
            "{\"protocol_version\":1,\"id\":\"t_echo1\",\"op\":\"echo\",\"echo\":\"hi\"}\n");
        if (res.exit_code != 2)
        {
            fail("expected wrong echo payload type to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"missing echo "
            "payload\",\"retryable\":false},\"id\":\"t_echo1\",\"ok\":false,\"protocol_version\":1}"
            "\n";
        if (res.out != expected)
        {
            fail("wrong echo payload type stdout mismatch");
        }
    }

    // Echo: echo.value must be a string.
    {
        const auto res = run_runner(
            runner,
            "{\"protocol_version\":1,\"id\":\"t_echo2\",\"op\":\"echo\",\"echo\":{\"value\":1}}\n");
        if (res.exit_code != 2)
        {
            fail("expected non-string echo.value to exit 2");
        }
        const std::string expected =
            "{\"error\":{\"kind\":\"invalid_request\",\"message\":\"echo.value must be "
            "string\",\"retryable\":false},\"id\":\"t_echo2\",\"ok\":false,\"protocol_version\":1}"
            "\n";
        if (res.out != expected)
        {
            fail("non-string echo.value stdout mismatch");
        }
    }

    // Echo: string escaping/roundtrip.
    {
        const auto res =
            run_runner(runner, "{\"protocol_version\":1,\"id\":\"t_echo3\",\"op\":\"echo\","
                               "\"echo\":{\"value\":\"hi\\n\\\"there\\\"\\\\end\"}}\n");
        if (res.exit_code != 0)
        {
            fail("expected escape-heavy echo to exit 0");
        }
        const std::string expected =
            "{\"id\":\"t_echo3\",\"ok\":true,\"protocol_version\":1,\"result\":{\"type\":"
            "\"string\",\"value\":\"hi\\n\\\"there\\\"\\\\end\"}}\n";
        if (res.out != expected)
        {
            fail("escape-heavy echo stdout mismatch");
        }
        if (!res.err.empty())
        {
            fail("escape-heavy echo expected empty stderr");
        }
    }

    std::cout << "OK\n";
    return 0;
}
