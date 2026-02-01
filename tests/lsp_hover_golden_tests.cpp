#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static std::string slurp(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        fail("failed to open file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static std::string json_escape(const std::string& input)
{
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

static std::string lsp_frame(const std::string& payload)
{
    std::ostringstream oss;
    oss << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
    return oss.str();
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

static ProcResult run_proc(const std::string& exe_path, const std::string& stdin_data)
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

        const int rc = poll(fds, 2, 2000);
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
    else if (WIFSIGNALED(status))
    {
        result.exit_code = 128 + WTERMSIG(status);
    }

    return result;
}

static std::optional<std::string> extract_json_string_value(const std::string& payload,
                                                            const std::string& key)
{
    const std::string marker = "\"" + key + "\":";
    const std::size_t start = payload.find(marker);
    if (start == std::string::npos)
    {
        return std::nullopt;
    }
    std::size_t i = start + marker.size();
    if (i >= payload.size() || payload[i] != '"')
    {
        return std::nullopt;
    }
    ++i;

    std::string out;
    while (i < payload.size())
    {
        const char c = payload[i++];
        if (c == '"')
        {
            return out;
        }
        if (c != '\\')
        {
            out.push_back(c);
            continue;
        }
        if (i >= payload.size())
        {
            return std::nullopt;
        }
        const char esc = payload[i++];
        switch (esc)
        {
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        case '\\':
            out.push_back('\\');
            break;
        case '"':
            out.push_back('"');
            break;
        default:
            // Minimal unescape; keep unknown escapes as-is.
            out.push_back(esc);
            break;
        }
    }

    return std::nullopt;
}

struct HoverRange
{
    std::size_t start_line = 0;
    std::size_t start_character = 0;
    std::size_t end_line = 0;
    std::size_t end_character = 0;
};

static bool read_number_after(const std::string& payload, std::size_t from,
                              const std::string& marker, double& out, std::size_t& next)
{
    const std::size_t pos = payload.find(marker, from);
    if (pos == std::string::npos)
    {
        return false;
    }
    const std::size_t start = pos + marker.size();
    const char* begin = payload.c_str() + start;
    char* end = nullptr;
    out = std::strtod(begin, &end);
    if (end == begin)
    {
        return false;
    }
    next = static_cast<std::size_t>(end - payload.c_str());
    return true;
}

static std::optional<HoverRange> extract_hover_range(const std::string& payload)
{
    const std::size_t range_pos = payload.find("\"range\"");
    if (range_pos == std::string::npos)
    {
        return std::nullopt;
    }

    auto extract_line_character =
        [&](std::size_t key_pos) -> std::optional<std::pair<double, double>>
    {
        const std::size_t brace_open = payload.find('{', key_pos);
        if (brace_open == std::string::npos)
        {
            return std::nullopt;
        }
        const std::size_t brace_close = payload.find('}', brace_open);
        if (brace_close == std::string::npos)
        {
            return std::nullopt;
        }

        const std::string obj = payload.substr(brace_open, (brace_close - brace_open) + 1);
        double line = 0;
        double character = 0;
        std::size_t tmp = 0;
        if (!read_number_after(obj, 0, "\"line\":", line, tmp))
        {
            return std::nullopt;
        }
        if (!read_number_after(obj, 0, "\"character\":", character, tmp))
        {
            return std::nullopt;
        }
        return std::pair<double, double>{line, character};
    };

    const std::size_t start_pos = payload.find("\"start\"", range_pos);
    const std::size_t end_pos = payload.find("\"end\"", range_pos);
    if (start_pos == std::string::npos || end_pos == std::string::npos)
    {
        return std::nullopt;
    }

    const auto start = extract_line_character(start_pos);
    const auto end = extract_line_character(end_pos);
    if (!start.has_value() || !end.has_value())
    {
        return std::nullopt;
    }

    HoverRange r;
    r.start_line = static_cast<std::size_t>(start->first);
    r.start_character = static_cast<std::size_t>(start->second);
    r.end_line = static_cast<std::size_t>(end->first);
    r.end_character = static_cast<std::size_t>(end->second);
    return r;
}

static std::vector<std::string> split_lsp_frames(const std::string& out)
{
    std::vector<std::string> payloads;

    std::size_t pos = 0;
    while (true)
    {
        const std::size_t header = out.find("Content-Length:", pos);
        if (header == std::string::npos)
        {
            break;
        }
        const std::size_t line_end = out.find("\r\n", header);
        if (line_end == std::string::npos)
        {
            break;
        }
        const std::string len_str =
            out.substr(header + std::string("Content-Length:").size(),
                       line_end - (header + std::string("Content-Length:").size()));
        std::size_t content_length = 0;
        try
        {
            content_length = static_cast<std::size_t>(std::stoul(len_str));
        }
        catch (...)
        {
            break;
        }
        const std::size_t header_end = out.find("\r\n\r\n", line_end);
        if (header_end == std::string::npos)
        {
            break;
        }
        const std::size_t payload_start = header_end + 4;
        if (payload_start + content_length > out.size())
        {
            break;
        }
        payloads.push_back(out.substr(payload_start, content_length));
        pos = payload_start + content_length;
    }

    return payloads;
}

static void compute_line_col(const std::string& text, std::size_t offset, std::size_t& line,
                             std::size_t& col)
{
    line = 0;
    col = 0;
    for (std::size_t i = 0; i < offset && i < text.size(); ++i)
    {
        if (text[i] == '\n')
        {
            ++line;
            col = 0;
        }
        else
        {
            ++col;
        }
    }
}

static bool run_hover_call_case(const fs::path& data_dir, const std::string& lsp_exe)
{
    const fs::path fixture_path = fs::path("tests/fixtures/lsp_hover_call.curlee");
    const std::string text = slurp(fixture_path);

    const std::string expected = slurp(data_dir / "hover_call.expected");

    const std::string uri = "file://" + (fs::current_path() / fixture_path).string();

    const std::size_t call_pos = text.find("take_nonzero(y)");
    if (call_pos == std::string::npos)
    {
        fail("failed to find call site in fixture");
    }

    const std::size_t callee_start = call_pos;
    const std::size_t callee_end = call_pos + std::string("take_nonzero").size();
    std::size_t expected_start_line = 0;
    std::size_t expected_start_col = 0;
    std::size_t expected_end_line = 0;
    std::size_t expected_end_col = 0;
    compute_line_col(text, callee_start, expected_start_line, expected_start_col);
    compute_line_col(text, callee_end, expected_end_line, expected_end_col);

    std::size_t line = 0;
    std::size_t col = 0;
    compute_line_col(text, call_pos + 2, line, col); // hover inside identifier

    const std::string init =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";

    const std::string did_open = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                 "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                 json_escape(uri) +
                                 "\",\"languageId\":\"curlee\",\"version\":1,\"text\":\"" +
                                 json_escape(text) + "\"}}}";

    std::ostringstream hover;
    hover << "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/"
             "hover\",\"params\":{\"textDocument\":{\"uri\":\""
          << json_escape(uri) << "\"},\"position\":{\"line\":" << line << ",\"character\":" << col
          << "}}}";

    const std::string shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"shutdown\",\"params\":{}}";
    const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

    std::string stdin_data;
    stdin_data += lsp_frame(init);
    stdin_data += lsp_frame(did_open);
    stdin_data += lsp_frame(hover.str());
    stdin_data += lsp_frame(shutdown);
    stdin_data += lsp_frame(exit);

    const ProcResult result = run_proc(lsp_exe, stdin_data);

    if (result.exit_code != 0)
    {
        std::cerr << "curlee_lsp stderr:\n" << result.err << "\n";
        fail("curlee_lsp exited non-zero: " + std::to_string(result.exit_code));
    }

    const auto payloads = split_lsp_frames(result.out);
    std::optional<std::string> got;
    std::optional<HoverRange> got_range;
    for (const auto& p : payloads)
    {
        if (p.find("\"id\":2") == std::string::npos)
        {
            continue;
        }
        got = extract_json_string_value(p, "value");
        got_range = extract_hover_range(p);
        break;
    }

    if (!got.has_value())
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("did not find hover response (id=2) with a contents.value");
    }

    if (!got_range.has_value())
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("did not find hover response (id=2) with a range");
    }

    if (got_range->start_line != expected_start_line ||
        got_range->start_character != expected_start_col ||
        got_range->end_line != expected_end_line || got_range->end_character != expected_end_col)
    {
        std::cerr << "RANGE MISMATCH for hover call\n";
        std::cerr << "expected start(" << expected_start_line << "," << expected_start_col
                  << ") end(" << expected_end_line << "," << expected_end_col << ")\n";
        std::cerr << "got start(" << got_range->start_line << "," << got_range->start_character
                  << ") end(" << got_range->end_line << "," << got_range->end_character << ")\n";
        return false;
    }

    if (*got != expected)
    {
        std::cerr << "GOLDEN MISMATCH: hover_call.expected\n";
        std::cerr << "--- expected ---\n" << expected << "\n";
        std::cerr << "--- got ---\n" << *got << "\n";
        return false;
    }

    return true;
}

static bool run_definition_call_case(const fs::path& data_dir, const std::string& lsp_exe)
{
    (void)data_dir;

    const fs::path fixture_path = fs::path("tests/fixtures/lsp_hover_call.curlee");
    const std::string text = slurp(fixture_path);

    const std::string uri = "file://" + (fs::current_path() / fixture_path).string();

    const std::size_t call_pos = text.find("take_nonzero(y)");
    if (call_pos == std::string::npos)
    {
        fail("failed to find call site in fixture");
    }

    // Expected definition span: the identifier in the function definition.
    const std::size_t def_pos = text.find("fn take_nonzero");
    if (def_pos == std::string::npos)
    {
        fail("failed to find function definition in fixture");
    }
    const std::size_t name_start = text.find("take_nonzero", def_pos);
    if (name_start == std::string::npos)
    {
        fail("failed to find function name in fixture");
    }
    const std::size_t name_end = name_start + std::string("take_nonzero").size();

    std::size_t expected_start_line = 0;
    std::size_t expected_start_col = 0;
    std::size_t expected_end_line = 0;
    std::size_t expected_end_col = 0;
    compute_line_col(text, name_start, expected_start_line, expected_start_col);
    compute_line_col(text, name_end, expected_end_line, expected_end_col);

    std::size_t line = 0;
    std::size_t col = 0;
    compute_line_col(text, call_pos + 2, line, col); // position inside identifier

    const std::string init =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";

    const std::string did_open = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                 "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                 json_escape(uri) +
                                 "\",\"languageId\":\"curlee\",\"version\":1,\"text\":\"" +
                                 json_escape(text) + "\"}}}";

    // Exercise didChange + array parsing path (contentChanges is an array).
    const std::string did_change = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                   "didChange\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                   json_escape(uri) +
                                   "\",\"version\":2},\"contentChanges\":[{\"text\":\"" +
                                   json_escape(text) + "\"}]}}";

    std::ostringstream def;
    def << "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"textDocument/"
           "definition\",\"params\":{\"textDocument\":{\"uri\":\""
        << json_escape(uri) << "\"},\"position\":{\"line\":" << line << ",\"character\":" << col
        << "}}}";

    const std::string shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"shutdown\",\"params\":{}}";
    const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

    std::string stdin_data;
    stdin_data += lsp_frame(init);
    stdin_data += lsp_frame(did_open);
    stdin_data += lsp_frame(did_change);
    stdin_data += lsp_frame(def.str());
    stdin_data += lsp_frame(shutdown);
    stdin_data += lsp_frame(exit);

    const ProcResult result = run_proc(lsp_exe, stdin_data);
    if (result.exit_code != 0)
    {
        std::cerr << "curlee_lsp stderr:\n" << result.err << "\n";
        fail("curlee_lsp exited non-zero: " + std::to_string(result.exit_code));
    }

    const auto payloads = split_lsp_frames(result.out);
    std::optional<std::string> got_uri;
    std::optional<HoverRange> got_range;
    for (const auto& p : payloads)
    {
        if (p.find("\"id\":4") == std::string::npos)
        {
            continue;
        }
        got_uri = extract_json_string_value(p, "uri");
        got_range = extract_hover_range(p);
        break;
    }

    if (!got_uri.has_value() || !got_range.has_value())
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("did not find definition response (id=4) with uri and range");
    }

    if (*got_uri != uri)
    {
        std::cerr << "URI MISMATCH for definition\n";
        std::cerr << "expected: " << uri << "\n";
        std::cerr << "got:      " << *got_uri << "\n";
        return false;
    }

    if (got_range->start_line != expected_start_line ||
        got_range->start_character != expected_start_col ||
        got_range->end_line != expected_end_line || got_range->end_character != expected_end_col)
    {
        std::cerr << "RANGE MISMATCH for definition\n";
        std::cerr << "expected start(" << expected_start_line << "," << expected_start_col
                  << ") end(" << expected_end_line << "," << expected_end_col << ")\n";
        std::cerr << "got start(" << got_range->start_line << "," << got_range->start_character
                  << ") end(" << got_range->end_line << "," << got_range->end_character << ")\n";
        return false;
    }

    return true;
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: curlee_lsp_hover_golden_tests <data_dir> <curlee_lsp_path>\n";
        return 2;
    }

    const fs::path data_dir = fs::path(argv[1]);
    const std::string lsp_exe = argv[2];

    if (!run_hover_call_case(data_dir, lsp_exe))
    {
        return 1;
    }

    if (!run_definition_call_case(data_dir, lsp_exe))
    {
        return 1;
    }

    return 0;
}
