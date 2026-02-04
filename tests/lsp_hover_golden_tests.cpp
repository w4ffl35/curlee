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

static std::size_t count_substring(std::string_view haystack, std::string_view needle)
{
    if (needle.empty())
    {
        return 0;
    }
    std::size_t count = 0;
    std::size_t pos = 0;
    while (true)
    {
        pos = haystack.find(needle, pos);
        if (pos == std::string_view::npos)
        {
            break;
        }
        ++count;
        pos += needle.size();
    }
    return count;
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

static bool run_hover_expr_case(const fs::path& data_dir, const std::string& lsp_exe)
{
    const fs::path fixture_path = fs::path("tests/fixtures/lsp_hover_expr.curlee");
    const std::string text = slurp(fixture_path);

    const std::string expected = slurp(data_dir / "hover_expr.expected");

    const std::string uri = "file://" + (fs::current_path() / fixture_path).string();

    const std::size_t ret_pos = text.find("return y;");
    if (ret_pos == std::string::npos)
    {
        fail("failed to find return statement in fixture");
    }

    std::size_t line = 0;
    std::size_t col = 0;
    compute_line_col(text, ret_pos + std::string("return ").size(), line, col); // hover on the 'y'

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
    for (const auto& p : payloads)
    {
        if (p.find("\"id\":2") == std::string::npos)
        {
            continue;
        }
        got = extract_json_string_value(p, "value");
        break;
    }

    if (!got.has_value())
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("did not find hover response (id=2) with a contents.value");
    }

    if (*got != expected)
    {
        std::cerr << "GOLDEN MISMATCH: hover_expr.expected\n";
        std::cerr << "--- expected ---\n" << expected << "\n";
        std::cerr << "--- got ---\n" << *got << "\n";
        return false;
    }

    return true;
}

static bool run_hover_member_expr_type_case(const std::string& lsp_exe)
{
    const fs::path fixture_path = fs::path("tests/fixtures/lsp_ast_traversal.curlee");
    const std::string text = slurp(fixture_path);
    const std::string uri = "file://" + (fs::current_path() / fixture_path).string();

    const std::size_t expr_pos = text.find("p.x;");
    if (expr_pos == std::string::npos)
    {
        fail("failed to find member expression in fixture");
    }

    std::size_t line = 0;
    std::size_t col = 0;
    compute_line_col(text, expr_pos + 2, line, col); // hover inside the 'x'

    const std::string init =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";

    const std::string did_open = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                 "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                 json_escape(uri) +
                                 "\",\"languageId\":\"curlee\",\"version\":1,\"text\":\"" +
                                 json_escape(text) + "\"}}}";

    std::ostringstream hover;
    hover << "{\"jsonrpc\":\"2.0\",\"id\":20,\"method\":\"textDocument/"
             "hover\",\"params\":{\"textDocument\":{\"uri\":\""
          << json_escape(uri) << "\"},\"position\":{\"line\":" << line << ",\"character\":" << col
          << "}}}";

    const std::string shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":21,\"method\":\"shutdown\",\"params\":{}}";
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
    for (const auto& p : payloads)
    {
        if (p.find("\"id\":20") == std::string::npos)
        {
            continue;
        }
        got = extract_json_string_value(p, "value");
        break;
    }

    if (!got.has_value())
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("did not find hover response (id=20) with a contents.value");
    }
    if (*got != "Int")
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("expected hover type to be Int, got: " + *got);
    }

    return true;
}

static bool run_publish_diagnostics_nonempty_case(const std::string& lsp_exe)
{
    const std::string bad_text = "fn main() -> Int { let x: Int = ; return 0; }";
    const std::string uri =
        "file://" + (fs::current_path() / "tests/fixtures" / "hello.curlee").string();

    const std::string init =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";

    const std::string did_open = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                 "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                 json_escape(uri) +
                                 "\",\"languageId\":\"curlee\",\"version\":1,\"text\":\"" +
                                 json_escape(bad_text) + "\"}}}";

    const std::string shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":{}}";
    const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

    std::string stdin_data;
    stdin_data += lsp_frame(init);
    stdin_data += lsp_frame(did_open);
    stdin_data += lsp_frame(shutdown);
    stdin_data += lsp_frame(exit);

    const ProcResult result = run_proc(lsp_exe, stdin_data);
    if (result.exit_code != 0)
    {
        std::cerr << "curlee_lsp stderr:\n" << result.err << "\n";
        fail("curlee_lsp exited non-zero: " + std::to_string(result.exit_code));
    }

    const auto payloads = split_lsp_frames(result.out);
    bool saw_diag = false;
    for (const auto& p : payloads)
    {
        if (p.find("\"method\":\"textDocument/publishDiagnostics\"") == std::string::npos)
        {
            continue;
        }
        // Assert at least one diagnostic object is present.
        if (p.find("\"diagnostics\":[{") != std::string::npos)
        {
            saw_diag = true;
        }
    }

    if (!saw_diag)
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("expected a non-empty publishDiagnostics diagnostics array for invalid source");
    }

    return true;
}

static bool run_publish_diagnostics_escape_uri_and_multi_diags_case(const std::string& lsp_exe)
{
    // Intentionally produce multiple parse diagnostics so the server must serialize
    // multiple entries (commas in the diagnostics array), and use a URI containing
    // characters that must be escaped in JSON output.
    const std::string bad_text = "fn a() -> Int { let x: Int = ; return 0; }\n"
                                 "fn b() -> Int { let y: Int = ; return 0; }\n";

    const std::string uri = std::string("file:///tmp/a\tb\nc\rd\"\\\\e.curlee");

    const std::string init =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";

    const std::string did_open = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                 "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                 json_escape(uri) +
                                 "\",\"languageId\":\"curlee\",\"version\":1,\"text\":\"" +
                                 json_escape(bad_text) + "\"}}}";

    const std::string shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":{}}";
    const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

    std::string stdin_data;
    stdin_data += lsp_frame(init);
    stdin_data += lsp_frame(did_open);
    stdin_data += lsp_frame(shutdown);
    stdin_data += lsp_frame(exit);

    const ProcResult result = run_proc(lsp_exe, stdin_data);
    if (result.exit_code != 0)
    {
        std::cerr << "curlee_lsp stderr:\n" << result.err << "\n";
        fail("curlee_lsp exited non-zero: " + std::to_string(result.exit_code));
    }

    const auto payloads = split_lsp_frames(result.out);
    bool saw_diag_multi = false;
    bool saw_uri_escapes = false;
    for (const auto& p : payloads)
    {
        if (p.find("\"method\":\"textDocument/publishDiagnostics\"") == std::string::npos)
        {
            continue;
        }
        if (p.find("\"diagnostics\":[{") != std::string::npos && p.find("},{") != std::string::npos)
        {
            saw_diag_multi = true;
        }
        if (p.find("\\t") != std::string::npos && p.find("\\n") != std::string::npos &&
            p.find("\\r") != std::string::npos && p.find("\\\"") != std::string::npos &&
            p.find("\\\\") != std::string::npos)
        {
            saw_uri_escapes = true;
        }
    }

    if (!saw_diag_multi)
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("expected publishDiagnostics to include multiple diagnostics");
    }
    if (!saw_uri_escapes)
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("expected publishDiagnostics to include escaped URI characters");
    }

    return true;
}

static bool run_hover_call_pred_bool_unary_and_missing_refinement_case(const std::string& lsp_exe)
{
    const std::string text =
        "fn g(x: Int where true, y: Int) -> Int [ requires !false; ] { return 0; }\n"
        "fn main() -> Int { return g(0, 0); }\n";

    const std::string uri =
        "file://" + (fs::current_path() / "tests" / "fixtures" / "pred_bool_unary.curlee").string();

    const std::size_t call_pos = text.find("g(0, 0)");
    if (call_pos == std::string::npos)
    {
        fail("failed to find call site in inline text");
    }

    std::size_t line = 0;
    std::size_t col = 0;
    compute_line_col(text, call_pos + 1, line, col);

    const std::string init =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";

    const std::string did_open = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                 "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                 json_escape(uri) +
                                 "\",\"languageId\":\"curlee\",\"version\":1,\"text\":\"" +
                                 json_escape(text) + "\"}}}";

    std::ostringstream hover;
    hover << "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/hover\",\"params\":{"
             "\"textDocument\":{\"uri\":\""
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
    for (const auto& p : payloads)
    {
        if (p.find("\"id\":2") == std::string::npos)
        {
            continue;
        }
        got = extract_json_string_value(p, "value");
        break;
    }

    if (!got.has_value())
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("did not find hover response (id=2) with a contents.value");
    }

    if (got->find("fn g") == std::string::npos || got->find("obligations:") == std::string::npos)
    {
        std::cerr << "LSP hover value:\n" << *got << "\n";
        fail("expected hover to include function signature and obligations");
    }
    if (got->find("true") == std::string::npos || got->find("!false") == std::string::npos)
    {
        std::cerr << "LSP hover value:\n" << *got << "\n";
        fail("expected hover obligations to include true and !false");
    }
    if (got->find("x: Int") == std::string::npos || got->find("y: Int") == std::string::npos)
    {
        std::cerr << "LSP hover value:\n" << *got << "\n";
        fail("expected hover signature to include both parameters");
    }

    return true;
}

static bool run_hover_without_open_case(const std::string& lsp_exe)
{
    const std::string uri =
        "file://" + (fs::current_path() / "tests/fixtures" / "hello.curlee").string();

    const std::string init =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";

    std::ostringstream hover;
    hover << "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/"
             "hover\",\"params\":{\"textDocument\":{\"uri\":\""
          << json_escape(uri) << "\"},\"position\":{\"line\":0,\"character\":0}}}";

    const std::string shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"shutdown\",\"params\":{}}";
    const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

    std::string stdin_data;
    stdin_data += lsp_frame(init);
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
    for (const auto& p : payloads)
    {
        if (p.find("\"id\":2") != std::string::npos)
        {
            std::cerr << "LSP stdout:\n" << result.out << "\n";
            fail("unexpected hover response for unopened document (id=2)");
        }
    }

    return true;
}

static bool run_definition_without_open_case(const std::string& lsp_exe)
{
    const std::string uri =
        "file://" + (fs::current_path() / "tests/fixtures" / "hello.curlee").string();

    const std::string init =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";

    std::ostringstream def;
    def << "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/"
           "definition\",\"params\":{\"textDocument\":{\"uri\":\""
        << json_escape(uri) << "\"},\"position\":{\"line\":0,\"character\":0}}}";

    const std::string shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"shutdown\",\"params\":{}}";
    const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

    std::string stdin_data;
    stdin_data += lsp_frame(init);
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
    for (const auto& p : payloads)
    {
        if (p.find("\"id\":2") != std::string::npos)
        {
            std::cerr << "LSP stdout:\n" << result.out << "\n";
            fail("unexpected definition response for unopened document (id=2)");
        }
    }

    return true;
}

static bool run_hover_negative_line_case(const std::string& lsp_exe)
{
    const fs::path fixture_path = fs::path("tests/fixtures/lsp_hover_call.curlee");
    const std::string text = slurp(fixture_path);
    const std::string uri = "file://" + (fs::current_path() / fixture_path).string();

    const std::string init =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";

    const std::string did_open = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                 "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                 json_escape(uri) +
                                 "\",\"languageId\":\"curlee\",\"version\":1,\"text\":\"" +
                                 json_escape(text) + "\"}}}";

    // Negative line number should not produce any response.
    const std::string hover_neg = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/"
                                  "hover\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                  json_escape(uri) +
                                  "\"},\"position\":{\"line\":-1,\"character\":0}}}";

    const std::string shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"shutdown\",\"params\":{}}";
    const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

    std::string stdin_data;
    stdin_data += lsp_frame(init);
    stdin_data += lsp_frame(did_open);
    stdin_data += lsp_frame(hover_neg);
    stdin_data += lsp_frame(shutdown);
    stdin_data += lsp_frame(exit);

    const ProcResult result = run_proc(lsp_exe, stdin_data);
    if (result.exit_code != 0)
    {
        std::cerr << "curlee_lsp stderr:\n" << result.err << "\n";
        fail("curlee_lsp exited non-zero: " + std::to_string(result.exit_code));
    }

    const auto payloads = split_lsp_frames(result.out);
    for (const auto& p : payloads)
    {
        if (p.find("\"id\":2") != std::string::npos)
        {
            std::cerr << "LSP stdout:\n" << result.out << "\n";
            fail("unexpected hover response for negative line (id=2)");
        }
    }

    return true;
}

static bool run_hover_out_of_range_line_no_response_case(const std::string& lsp_exe)
{
    const fs::path fixture_path = fs::path("tests/fixtures/lsp_hover_call.curlee");
    const std::string text = slurp(fixture_path);
    const std::string uri = "file://" + (fs::current_path() / fixture_path).string();

    const std::string init =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";

    const std::string did_open = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                 "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                 json_escape(uri) +
                                 "\",\"languageId\":\"curlee\",\"version\":1,\"text\":\"" +
                                 json_escape(text) + "\"}}}";

    // Out-of-range line number should not produce any response.
    const std::string hover_oob = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/"
                                  "hover\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                  json_escape(uri) +
                                  "\"},\"position\":{\"line\":999999,\"character\":0}}}";

    const std::string shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"shutdown\",\"params\":{}}";
    const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

    std::string stdin_data;
    stdin_data += lsp_frame(init);
    stdin_data += lsp_frame(did_open);
    stdin_data += lsp_frame(hover_oob);
    stdin_data += lsp_frame(shutdown);
    stdin_data += lsp_frame(exit);

    const ProcResult result = run_proc(lsp_exe, stdin_data);
    if (result.exit_code != 0)
    {
        std::cerr << "curlee_lsp stderr:\n" << result.err << "\n";
        fail("curlee_lsp exited non-zero: " + std::to_string(result.exit_code));
    }

    const auto payloads = split_lsp_frames(result.out);
    for (const auto& p : payloads)
    {
        if (p.find("\"id\":2") != std::string::npos)
        {
            std::cerr << "LSP stdout:\n" << result.out << "\n";
            fail("unexpected hover response for out-of-range line (id=2)");
        }
    }

    return true;
}

static bool run_hover_invalid_document_no_response_case(const std::string& lsp_exe)
{
    const std::string bad_text = "fn main() -> Int { let x: Int = ; return 0; }";
    const std::string uri =
        "file://" + (fs::current_path() / "tests/fixtures" / "hello.curlee").string();

    const std::string init =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";

    const std::string did_open = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                 "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                 json_escape(uri) +
                                 "\",\"languageId\":\"curlee\",\"version\":1,\"text\":\"" +
                                 json_escape(bad_text) + "\"}}}";

    const std::string hover = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/"
                              "hover\",\"params\":{\"textDocument\":{\"uri\":\"" +
                              json_escape(uri) + "\"},\"position\":{\"line\":0,\"character\":0}}}";

    const std::string shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"shutdown\",\"params\":{}}";
    const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

    std::string stdin_data;
    stdin_data += lsp_frame(init);
    stdin_data += lsp_frame(did_open);
    stdin_data += lsp_frame(hover);
    stdin_data += lsp_frame(shutdown);
    stdin_data += lsp_frame(exit);

    const ProcResult result = run_proc(lsp_exe, stdin_data);
    if (result.exit_code != 0)
    {
        std::cerr << "curlee_lsp stderr:\n" << result.err << "\n";
        fail("curlee_lsp exited non-zero: " + std::to_string(result.exit_code));
    }

    const auto payloads = split_lsp_frames(result.out);
    for (const auto& p : payloads)
    {
        if (p.find("\"id\":2") != std::string::npos)
        {
            std::cerr << "LSP stdout:\n" << result.out << "\n";
            fail("unexpected hover response for invalid document (id=2)");
        }
    }

    return true;
}

static bool run_hover_call_dedup_obligations_case(const std::string& lsp_exe)
{
    const fs::path fixture_path = fs::path("tests/fixtures/lsp_hover_dedup.curlee");
    const std::string text = slurp(fixture_path);
    const std::string uri = "file://" + (fs::current_path() / fixture_path).string();

    const std::size_t call_pos = text.find("dup(1)");
    if (call_pos == std::string::npos)
    {
        fail("failed to find call site in fixture");
    }

    const std::size_t callee_start = call_pos;
    const std::size_t callee_end = call_pos + std::string("dup").size();

    std::size_t expected_start_line = 0;
    std::size_t expected_start_col = 0;
    std::size_t expected_end_line = 0;
    std::size_t expected_end_col = 0;
    compute_line_col(text, callee_start, expected_start_line, expected_start_col);
    compute_line_col(text, callee_end, expected_end_line, expected_end_col);

    std::size_t line = 0;
    std::size_t col = 0;
    compute_line_col(text, call_pos + 1, line, col);

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
        std::cerr << "RANGE MISMATCH for hover call (dedup fixture)\n";
        std::cerr << "expected start(" << expected_start_line << "," << expected_start_col
                  << ") end(" << expected_end_line << "," << expected_end_col << ")\n";
        std::cerr << "got start(" << got_range->start_line << "," << got_range->start_character
                  << ") end(" << got_range->end_line << "," << got_range->end_character << ")\n";
        return false;
    }

    if (got->find("fn dup") == std::string::npos)
    {
        std::cerr << "LSP hover value:\n" << *got << "\n";
        fail("expected hover text to include function signature for dup");
    }

    const auto obligations_pos = got->find("\nobligations: ");
    if (obligations_pos == std::string::npos)
    {
        std::cerr << "LSP hover value:\n" << *got << "\n";
        fail("expected hover text to include obligations line");
    }

    const std::size_t line_start = obligations_pos + 1;
    const std::size_t line_end = got->find('\n', line_start);
    const std::size_t end = (line_end == std::string::npos) ? got->size() : line_end;

    const std::string_view obligations_line(*got);
    const std::string_view line_view = obligations_line.substr(line_start, end - line_start);

    // The function requires x > 0 and also refines x with x > 0. These are intentionally
    // duplicates; the hover's obligations list should deduplicate them.
    if (count_substring(line_view, "> 0") != 1)
    {
        std::cerr << "obligations line:\n" << std::string(line_view) << "\n";
        fail("expected obligations line to contain exactly one '> 0' occurrence");
    }

    return true;
}

static bool run_hover_call_operator_rendering_case(const std::string& lsp_exe)
{
    const fs::path fixture_path = fs::path("tests/fixtures/lsp_hover_ops.curlee");
    const std::string text = slurp(fixture_path);
    const std::string uri = "file://" + (fs::current_path() / fixture_path).string();

    const std::size_t call_pos = text.find("ops(0)");
    if (call_pos == std::string::npos)
    {
        fail("failed to find call site in fixture");
    }

    std::size_t line = 0;
    std::size_t col = 0;
    compute_line_col(text, call_pos + 1, line, col);

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
    for (const auto& p : payloads)
    {
        if (p.find("\"id\":2") == std::string::npos)
        {
            continue;
        }
        got = extract_json_string_value(p, "value");
        break;
    }

    if (!got.has_value())
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("did not find hover response (id=2) with a contents.value");
    }

    if (got->find("fn ops") == std::string::npos)
    {
        std::cerr << "LSP hover value:\n" << *got << "\n";
        fail("expected hover text to include function signature for ops");
    }
    if (got->find("==") == std::string::npos)
    {
        std::cerr << "LSP hover value:\n" << *got << "\n";
        fail("expected hover text to include '==' operator");
    }
    if (got->find("&&") == std::string::npos)
    {
        std::cerr << "LSP hover value:\n" << *got << "\n";
        fail("expected hover text to include '&&' operator");
    }
    if (got->find("||") == std::string::npos)
    {
        std::cerr << "LSP hover value:\n" << *got << "\n";
        fail("expected hover text to include '||' operator");
    }
    if (got->find("!(") == std::string::npos)
    {
        std::cerr << "LSP hover value:\n" << *got << "\n";
        fail("expected hover text to include unary '!' operator");
    }
    if (got->find("true") == std::string::npos || got->find("false") == std::string::npos)
    {
        std::cerr << "LSP hover value:\n" << *got << "\n";
        fail("expected hover text to include boolean literals");
    }

    return true;
}

static bool run_hover_call_more_operator_rendering_case(const std::string& lsp_exe)
{
    const fs::path fixture_path = fs::path("tests/fixtures/lsp_hover_ops2.curlee");
    const std::string text = slurp(fixture_path);
    const std::string uri = "file://" + (fs::current_path() / fixture_path).string();

    const std::size_t call_pos = text.find("ops2(0)");
    if (call_pos == std::string::npos)
    {
        fail("failed to find call site in fixture");
    }

    std::size_t line = 0;
    std::size_t col = 0;
    compute_line_col(text, call_pos + 1, line, col);

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
    for (const auto& p : payloads)
    {
        if (p.find("\"id\":2") == std::string::npos)
        {
            continue;
        }
        got = extract_json_string_value(p, "value");
        break;
    }

    if (!got.has_value())
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("did not find hover response (id=2) with a contents.value");
    }

    if (got->find("fn ops2") == std::string::npos)
    {
        std::cerr << "LSP hover value:\n" << *got << "\n";
        fail("expected hover text to include function signature for ops2");
    }

    // Predicate operator rendering coverage.
    const std::vector<std::string_view> expected_tokens = {
        "!=", "<", "<=", ">", ">=", "+", "(-0 == 0)", "*", "/", "=="};
    for (const auto tok : expected_tokens)
    {
        if (got->find(std::string(tok)) == std::string::npos)
        {
            std::cerr << "LSP hover value:\n" << *got << "\n";
            fail("expected hover text to include token: " + std::string(tok));
        }
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

static bool run_initialize_without_id_case(const std::string& lsp_exe)
{
    const std::string init =
        "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";
    const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

    std::string stdin_data;
    stdin_data += lsp_frame(init);
    stdin_data += lsp_frame(exit);

    const ProcResult result = run_proc(lsp_exe, stdin_data);
    if (result.exit_code != 0)
    {
        std::cerr << "curlee_lsp stderr:\n" << result.err << "\n";
        fail("curlee_lsp exited non-zero: " + std::to_string(result.exit_code));
    }

    const auto payloads = split_lsp_frames(result.out);
    bool saw_initialize_response = false;
    for (const auto& p : payloads)
    {
        if (p.find("\"result\"") == std::string::npos)
        {
            continue;
        }
        if (p.find("\"capabilities\"") == std::string::npos)
        {
            continue;
        }

        saw_initialize_response = true;
        if (p.find("\"id\"") != std::string::npos)
        {
            std::cerr << "LSP stdout:\n" << result.out << "\n";
            fail("expected initialize response to omit id when request omits id");
        }
        break;
    }
    if (!saw_initialize_response)
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("expected initialize response even without id");
    }

    return true;
}

static bool run_shutdown_without_id_case(const std::string& lsp_exe)
{
    const std::string shutdown = "{\"jsonrpc\":\"2.0\",\"method\":\"shutdown\",\"params\":{}}";
    const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

    std::string stdin_data;
    stdin_data += lsp_frame(shutdown);
    stdin_data += lsp_frame(exit);

    const ProcResult result = run_proc(lsp_exe, stdin_data);
    if (result.exit_code != 0)
    {
        std::cerr << "curlee_lsp stderr:\n" << result.err << "\n";
        fail("curlee_lsp exited non-zero: " + std::to_string(result.exit_code));
    }

    const auto payloads = split_lsp_frames(result.out);
    bool saw_shutdown_response = false;
    for (const auto& p : payloads)
    {
        if (p.find("\"result\":null") == std::string::npos)
        {
            continue;
        }
        saw_shutdown_response = true;
        if (p.find("\"id\"") != std::string::npos)
        {
            std::cerr << "LSP stdout:\n" << result.out << "\n";
            fail("expected shutdown response to omit id when request omits id");
        }
        break;
    }
    if (!saw_shutdown_response)
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("expected shutdown response even without id");
    }

    return true;
}

static bool run_lsp_missing_content_length_case(const std::string& lsp_exe)
{
    // No Content-Length header: read_lsp_message() should return false and the server should exit
    // cleanly without emitting any responses.
    const ProcResult result = run_proc(lsp_exe, "{}");
    if (result.exit_code != 0)
    {
        std::cerr << "curlee_lsp stderr:\n" << result.err << "\n";
        fail("curlee_lsp exited non-zero: " + std::to_string(result.exit_code));
    }
    if (!result.out.empty())
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("expected no output when Content-Length header is missing");
    }
    return true;
}

static bool run_lsp_truncated_payload_case(const std::string& lsp_exe)
{
    // Content-Length exceeds available payload bytes: read_lsp_message() should fail and the
    // server should exit cleanly.
    const std::string stdin_data = "Content-Length: 10\r\n\r\n{}";
    const ProcResult result = run_proc(lsp_exe, stdin_data);
    if (result.exit_code != 0)
    {
        std::cerr << "curlee_lsp stderr:\n" << result.err << "\n";
        fail("curlee_lsp exited non-zero: " + std::to_string(result.exit_code));
    }
    if (!result.out.empty())
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("expected no output when payload is truncated");
    }
    return true;
}

static bool run_lsp_uri_percent_decoding_case(const std::string& lsp_exe)
{
    const std::string uri = "file:///tmp/curlee%20space.curlee";
    const std::string text = "fn main() -> Int { return 0; }\n";

    const std::string init =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";

    const std::string did_open = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                 "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                 json_escape(uri) +
                                 "\",\"languageId\":\"curlee\",\"version\":1,\"text\":\"" +
                                 json_escape(text) + "\"}}}";

    const std::string shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":{}}";
    const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

    std::string stdin_data;
    stdin_data += lsp_frame(init);
    stdin_data += lsp_frame(did_open);
    stdin_data += lsp_frame(shutdown);
    stdin_data += lsp_frame(exit);

    const ProcResult result = run_proc(lsp_exe, stdin_data);
    if (result.exit_code != 0)
    {
        std::cerr << "curlee_lsp stderr:\n" << result.err << "\n";
        fail("curlee_lsp exited non-zero: " + std::to_string(result.exit_code));
    }

    const auto payloads = split_lsp_frames(result.out);
    bool saw_diag_for_uri = false;
    for (const auto& p : payloads)
    {
        if (p.find("\"method\":\"textDocument/publishDiagnostics\"") == std::string::npos)
        {
            continue;
        }
        if (p.find(json_escape(uri)) != std::string::npos)
        {
            saw_diag_for_uri = true;
        }
    }

    if (!saw_diag_for_uri)
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("expected publishDiagnostics to include the percent-encoded uri");
    }

    return true;
}

static bool run_lsp_uri_no_prefix_case(const std::string& lsp_exe)
{
    const std::string uri = "untitled:curlee_lsp_uri_no_prefix.curlee";
    const std::string text = "fn main() -> Int { return 0; }\n";

    const std::string init =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";

    const std::string did_open = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                 "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                 json_escape(uri) +
                                 "\",\"languageId\":\"curlee\",\"version\":1,\"text\":\"" +
                                 json_escape(text) + "\"}}}";

    const std::string shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":{}}";
    const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

    std::string stdin_data;
    stdin_data += lsp_frame(init);
    stdin_data += lsp_frame(did_open);
    stdin_data += lsp_frame(shutdown);
    stdin_data += lsp_frame(exit);

    const ProcResult result = run_proc(lsp_exe, stdin_data);
    if (result.exit_code != 0)
    {
        std::cerr << "curlee_lsp stderr:\n" << result.err << "\n";
        fail("curlee_lsp exited non-zero: " + std::to_string(result.exit_code));
    }

    const auto payloads = split_lsp_frames(result.out);
    bool saw_diag_for_uri = false;
    for (const auto& p : payloads)
    {
        if (p.find("\"method\":\"textDocument/publishDiagnostics\"") == std::string::npos)
        {
            continue;
        }
        if (p.find(json_escape(uri)) != std::string::npos)
        {
            saw_diag_for_uri = true;
        }
    }

    if (!saw_diag_for_uri)
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("expected publishDiagnostics to include the untitled: uri");
    }

    return true;
}

static bool run_lsp_robustness_cases(const std::string& lsp_exe)
{
    const fs::path fixture_path = fs::path("tests/fixtures/lsp_hover_call.curlee");
    const std::string text = slurp(fixture_path);
    const std::string uri = "file://" + (fs::current_path() / fixture_path).string();

    // Intentionally malformed JSON: parser should reject and the server should continue.
    const std::string bad_json = "{";

    // Invalid string escape in JSON: parser should reject.
    const std::string bad_escape = "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"params\":{"
                                   "\"capabilities\":{\"x\":\"\\q\"}}}";

    // Missing method.
    const std::string missing_method = "{\"jsonrpc\":\"2.0\",\"id\":1}";

    // Method is not a string.
    const std::string non_string_method = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":123}";

    // Unknown method should be ignored.
    const std::string unknown_method =
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"unknown/method\",\"params\":{}}";

    // Exercise JSON parser value kinds at the top-level (non-object payloads).
    const std::string json_null = "null";
    const std::string json_true = "true";
    const std::string json_false = "false";
    // Near-misses should fail (parse_value should return nullopt).
    const std::string json_nul = "nul";
    const std::string json_tru = "tru";
    const std::string json_fals = "fals";
    const std::string json_number_exp = "1e+2";
    const std::string json_number_decimal = "1.25";
    const std::string json_empty_array = "[]";
    const std::string json_empty_object = "{}";

    // Exercise string escapes handled by JsonParser (\b, \f, \/).
    const std::string escape_variants = "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"unknown/"
                                        "method\",\"params\":{\"x\":\"\\b\\f\\/\"}}";

    // Force skip_ws() to consume whitespace and still parse a valid message.
    const std::string ws_initialize =
        " \n\t{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"initialize\","
        "\"params\":{\"capabilities\":{\"arr\":[null,true,false,1e0],"
        "\"obj\":{\"k\":\"v\"}}}}";

    // Trailing garbage after a valid JSON value => parse_json should fail.
    const std::string trailing_garbage = "{\"jsonrpc\":\"2.0\",\"method\":\"unknown/method\"}x";

    // Malformed string ending with a trailing backslash => parse_string should fail.
    const std::string string_trailing_backslash = "\"abc\\";

    // Malformed number => parse_number should fail.
    const std::string number_minus_only = "-";

    // Malformed array/object => parse_array/parse_object should fail.
    const std::string bad_array_missing_comma = "[1 2]";
    const std::string bad_array_trailing_comma = "[1,]";
    const std::string bad_object_missing_colon = "{\"a\" 1}";
    const std::string bad_object_missing_comma = "{\"a\":1 \"b\":2}";

    // Exercise common string escapes in JsonParser (\" \\ \r \t \n).
    const std::string string_escape_variants =
        "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"unknown/method\","
        "\"params\":{\"s\":\"\\\"\\\\\\r\\t\\n\"}}";

    // didOpen missing text.
    const std::string did_open_missing_text = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                              "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                              json_escape(uri) +
                                              "\",\"languageId\":\"curlee\",\"version\":1}}}";

    // didChange empty contentChanges.
    const std::string did_change_empty = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                         "didChange\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                         json_escape(uri) +
                                         "\",\"version\":2},\"contentChanges\":[]}}";

    // didChange contentChanges[0] is not an object.
    const std::string did_change_non_object =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
        "didChange\",\"params\":{\"textDocument\":{\"uri\":\"" +
        json_escape(uri) + "\",\"version\":3},\"contentChanges\":[1]}}";

    // didChange contentChanges is not an array.
    // didChange contentChanges[0] is an object but missing the required text field.
    const std::string did_change_missing_text =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
        "didChange\",\"params\":{\"textDocument\":{\"uri\":\"" +
        json_escape(uri) + "\",\"version\":5},\"contentChanges\":[{\"range\":null}]}}";

    const std::string did_change_not_array =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
        "didChange\",\"params\":{\"textDocument\":{\"uri\":\"" +
        json_escape(uri) + "\",\"version\":4},\"contentChanges\":{}}}";

    // hover with missing params.
    const std::string hover_missing_params =
        "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"textDocument/hover\"}";

    // hover with out-of-range position should be ignored.
    const std::string hover_oob = "{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"textDocument/"
                                  "hover\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                  json_escape(uri) +
                                  "\"},\"position\":{\"line\":9999,\"character\":0}}}";

    // hover with position types that do not parse as numbers should be ignored.
    const std::string hover_bad_position_types =
        "{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"textDocument/"
        "hover\",\"params\":{\"textDocument\":{\"uri\":\"" +
        json_escape(uri) + "\"},\"position\":{\"line\":\"0\",\"character\":0}}}";

    // Now open the document successfully.
    const std::string did_open = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                 "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                 json_escape(uri) +
                                 "\",\"languageId\":\"curlee\",\"version\":1,\"text\":\"" +
                                 json_escape(text) + "\"}}}";

    // definition at a position that should not resolve to any symbol => result:null.
    const std::string definition_no_target =
        "{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"textDocument/"
        "definition\",\"params\":{\"textDocument\":{\"uri\":\"" +
        json_escape(uri) + "\"},\"position\":{\"line\":0,\"character\":0}}}";

    // hover at a position that should not map to an expression => result:null.
    const std::string hover_no_expr = "{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"textDocument/"
                                      "hover\",\"params\":{\"textDocument\":{\"uri\":\"" +
                                      json_escape(uri) +
                                      "\"},\"position\":{\"line\":0,\"character\":0}}}";

    // Sanity: send a valid initialize at the end and confirm the server still responds.
    const std::string init_ok = "{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"initialize\","
                                "\"params\":{\"capabilities\":{}}}";
    const std::string shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":100,\"method\":\"shutdown\",\"params\":{}}";
    const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

    std::string stdin_data;
    stdin_data += lsp_frame(bad_json);
    stdin_data += lsp_frame(bad_escape);
    stdin_data += lsp_frame(missing_method);
    stdin_data += lsp_frame(non_string_method);
    stdin_data += lsp_frame(unknown_method);
    stdin_data += lsp_frame(json_null);
    stdin_data += lsp_frame(json_true);
    stdin_data += lsp_frame(json_false);
    stdin_data += lsp_frame(json_nul);
    stdin_data += lsp_frame(json_tru);
    stdin_data += lsp_frame(json_fals);
    stdin_data += lsp_frame(json_number_exp);
    stdin_data += lsp_frame(json_number_decimal);
    stdin_data += lsp_frame(json_empty_array);
    stdin_data += lsp_frame(json_empty_object);
    stdin_data += lsp_frame(escape_variants);
    stdin_data += lsp_frame(string_escape_variants);
    stdin_data += lsp_frame(ws_initialize);
    stdin_data += lsp_frame(trailing_garbage);
    stdin_data += lsp_frame(string_trailing_backslash);
    stdin_data += lsp_frame(number_minus_only);
    stdin_data += lsp_frame(bad_array_missing_comma);
    stdin_data += lsp_frame(bad_array_trailing_comma);
    stdin_data += lsp_frame(bad_object_missing_colon);
    stdin_data += lsp_frame(bad_object_missing_comma);
    stdin_data += lsp_frame(did_open_missing_text);
    stdin_data += lsp_frame(did_change_empty);
    stdin_data += lsp_frame(did_change_non_object);
    stdin_data += lsp_frame(did_change_not_array);
    stdin_data += lsp_frame(did_change_missing_text);
    stdin_data += lsp_frame(hover_missing_params);
    stdin_data += lsp_frame(hover_oob);
    stdin_data += lsp_frame(did_open);
    stdin_data += lsp_frame(definition_no_target);
    stdin_data += lsp_frame(hover_no_expr);
    stdin_data += lsp_frame(hover_bad_position_types);
    stdin_data += lsp_frame(init_ok);
    stdin_data += lsp_frame(shutdown);
    stdin_data += lsp_frame(exit);

    const ProcResult result = run_proc(lsp_exe, stdin_data);
    if (result.exit_code != 0)
    {
        std::cerr << "curlee_lsp stderr:\n" << result.err << "\n";
        fail("curlee_lsp exited non-zero: " + std::to_string(result.exit_code));
    }

    const auto payloads = split_lsp_frames(result.out);
    bool saw_init_99 = false;
    bool saw_init_5 = false;
    bool saw_def_null = false;
    bool saw_hover_null = false;
    bool saw_hover_14 = false;
    for (const auto& p : payloads)
    {
        if (p.find("\"id\":99") != std::string::npos)
        {
            saw_init_99 = true;
        }
        if (p.find("\"id\":5") != std::string::npos)
        {
            saw_init_5 = true;
        }
        if (p.find("\"id\":12") != std::string::npos &&
            p.find("\"result\":null") != std::string::npos)
        {
            saw_def_null = true;
        }
        if (p.find("\"id\":13") != std::string::npos &&
            p.find("\"result\":null") != std::string::npos)
        {
            saw_hover_null = true;
        }
        if (p.find("\"id\":14") != std::string::npos)
        {
            saw_hover_14 = true;
        }
    }

    if (!saw_def_null)
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("expected definition no-target response with result:null");
    }
    if (!saw_hover_null)
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("expected hover no-expr response with result:null");
    }
    if (!saw_init_99)
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("expected initialize response after malformed messages");
    }
    if (!saw_init_5)
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("expected initialize response for whitespace-prefixed request (id=5)");
    }
    if (saw_hover_14)
    {
        std::cerr << "LSP stdout:\n" << result.out << "\n";
        fail("unexpected hover response for bad position types (id=14)");
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

    if (!run_hover_expr_case(data_dir, lsp_exe))
    {
        return 1;
    }

    if (!run_hover_member_expr_type_case(lsp_exe))
    {
        return 1;
    }

    if (!run_publish_diagnostics_nonempty_case(lsp_exe))
    {
        return 1;
    }

    if (!run_publish_diagnostics_escape_uri_and_multi_diags_case(lsp_exe))
    {
        return 1;
    }

    if (!run_hover_without_open_case(lsp_exe))
    {
        return 1;
    }

    if (!run_definition_without_open_case(lsp_exe))
    {
        return 1;
    }

    if (!run_hover_negative_line_case(lsp_exe))
    {
        return 1;
    }

    if (!run_hover_out_of_range_line_no_response_case(lsp_exe))
    {
        return 1;
    }

    if (!run_hover_invalid_document_no_response_case(lsp_exe))
    {
        return 1;
    }

    if (!run_hover_call_dedup_obligations_case(lsp_exe))
    {
        return 1;
    }

    if (!run_hover_call_pred_bool_unary_and_missing_refinement_case(lsp_exe))
    {
        return 1;
    }

    if (!run_hover_call_operator_rendering_case(lsp_exe))
    {
        return 1;
    }

    if (!run_hover_call_more_operator_rendering_case(lsp_exe))
    {
        return 1;
    }

    if (!run_initialize_without_id_case(lsp_exe))
    {
        return 1;
    }

    if (!run_shutdown_without_id_case(lsp_exe))
    {
        return 1;
    }

    if (!run_lsp_missing_content_length_case(lsp_exe))
    {
        return 1;
    }

    if (!run_lsp_truncated_payload_case(lsp_exe))
    {
        return 1;
    }

    if (!run_lsp_uri_percent_decoding_case(lsp_exe))
    {
        return 1;
    }

    if (!run_lsp_uri_no_prefix_case(lsp_exe))
    {
        return 1;
    }

    if (!run_lsp_robustness_cases(lsp_exe))
    {
        return 1;
    }

    return 0;
}
