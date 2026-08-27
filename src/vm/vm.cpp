// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <curlee/vm/vm.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace
{

struct ProcResult
{
    int exit_code = -1;
    std::string out;
    std::string err;
    bool timed_out = false;
    bool output_limit_exceeded = false;
};

constexpr int kPythonRunnerTimeoutMs = 500;
constexpr std::size_t kPythonRunnerMaxOutputBytes = 1 * 1024 * 1024;

ProcResult run_process_argv(const std::vector<const char*>& argv, const std::string& exe_path,
                            const std::string& stdin_data, int timeout_ms,
                            std::size_t max_output_bytes);

void set_nonblocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        return;
    }
    (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
} // GCOVR_EXCL_LINE

void read_into(int fd, std::string& out, bool& eof, std::size_t& total_bytes,
               std::size_t max_total_bytes, bool& limit_hit)
{
    char buf[4096];
    while (true)
    {
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0)
        {
            const std::size_t count = static_cast<std::size_t>(n);
            if (total_bytes >= max_total_bytes)
            {
                limit_hit = true;
                eof = true;
                return;
            }

            const std::size_t remaining = max_total_bytes - total_bytes;
            const std::size_t to_append = (count <= remaining) ? count : remaining;
            out.append(buf, to_append);
            total_bytes += to_append;
            if (to_append < count)
            {
                limit_hit = true;
                eof = true;
                return;
            }

            continue;
        }
        if (n == 0)
        {
            eof = true;
            return;
        }
        if (errno == EAGAIN
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
            || errno == EWOULDBLOCK
#endif
        )
        {
            return;
        }
        eof = true;
        return;
    }
}

ProcResult run_process(const std::string& exe_path, const std::string& stdin_data, int timeout_ms,
                       std::size_t max_output_bytes)
{
    std::vector<std::string> argv_storage;
    argv_storage.push_back(exe_path);

    std::vector<const char*> argv;
    argv.reserve(argv_storage.size() + 1);
    for (const auto& s : argv_storage)
    {
        argv.push_back(s.c_str());
    }
    argv.push_back(nullptr);

    return run_process_argv(argv, exe_path, stdin_data, timeout_ms, max_output_bytes);
}

ProcResult run_process_argv(const std::vector<const char*>& argv, const std::string& exe_path,
                            const std::string& stdin_data, int timeout_ms,
                            std::size_t max_output_bytes)
{
    int in_pipe[2] = {-1, -1};
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    ProcResult result;

    std::vector<char*> argv_mut;
    argv_mut.reserve(argv.size());
    for (const auto* a : argv)
    {
        argv_mut.push_back(const_cast<char*>(a));
    }

    const auto* parent_path = std::getenv("PATH");
    const auto* parent_ld_library_path = std::getenv("LD_LIBRARY_PATH");
    const auto* parent_asan_options = std::getenv("ASAN_OPTIONS");
    const auto* parent_ubsan_options = std::getenv("UBSAN_OPTIONS");
    const auto* parent_lsan_options = std::getenv("LSAN_OPTIONS");

    std::vector<std::string> env_storage;
    env_storage.emplace_back("LC_ALL=C");
    env_storage.emplace_back("LANG=C");
    env_storage.emplace_back("TZ=UTC");
    env_storage.emplace_back("PYTHONHASHSEED=0");
    if (parent_path != nullptr)
    {
        env_storage.emplace_back(std::string("PATH=") + parent_path);
    }
    if (parent_ld_library_path != nullptr)
    {
        env_storage.emplace_back(std::string("LD_LIBRARY_PATH=") + parent_ld_library_path);
    }
    if (parent_asan_options != nullptr)
    {
        env_storage.emplace_back(std::string("ASAN_OPTIONS=") + parent_asan_options);
    }
    if (parent_ubsan_options != nullptr)
    {
        env_storage.emplace_back(std::string("UBSAN_OPTIONS=") + parent_ubsan_options);
    }
    if (parent_lsan_options != nullptr)
    {
        env_storage.emplace_back(std::string("LSAN_OPTIONS=") + parent_lsan_options);
    }

    std::vector<char*> envp;
    envp.reserve(env_storage.size() + 1);
    for (auto& kv : env_storage)
    {
        envp.push_back(kv.data());
    }
    envp.push_back(nullptr);

    if (pipe(in_pipe) != 0)
    {
        result.exit_code = 127; // GCOVR_EXCL_LINE
        return result;          // GCOVR_EXCL_LINE
    } // GCOVR_EXCL_LINE
    if (pipe(out_pipe) != 0)
    {
        close(in_pipe[0]);
        close(in_pipe[1]);
        result.exit_code = 127;
        return result;
    }
    if (pipe(err_pipe) != 0)
    {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        result.exit_code = 127;
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) // GCOVR_EXCL_LINE
    {
        result.exit_code = 127; // GCOVR_EXCL_LINE
        return result;          // GCOVR_EXCL_LINE
    }

    if (pid == 0) // GCOVR_EXCL_LINE
    {
        (void)dup2(in_pipe[0], STDIN_FILENO);   // GCOVR_EXCL_LINE
        (void)dup2(out_pipe[1], STDOUT_FILENO); // GCOVR_EXCL_LINE
        (void)dup2(err_pipe[1], STDERR_FILENO); // GCOVR_EXCL_LINE

        close(in_pipe[0]);  // GCOVR_EXCL_LINE
        close(in_pipe[1]);  // GCOVR_EXCL_LINE
        close(out_pipe[0]); // GCOVR_EXCL_LINE
        close(out_pipe[1]); // GCOVR_EXCL_LINE
        close(err_pipe[0]); // GCOVR_EXCL_LINE
        close(err_pipe[1]); // GCOVR_EXCL_LINE

        execve(exe_path.c_str(), argv_mut.data(), envp.data());         // GCOVR_EXCL_LINE
        std::cerr << "execve failed: " << std::strerror(errno) << "\n"; // GCOVR_EXCL_LINE
        std::cerr.flush();                                              // GCOVR_EXCL_LINE
        _exit(127);                                                     // GCOVR_EXCL_LINE
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);

    // Write input then close.
    {
        struct SigPipeIgnoreGuard
        {
            using Handler = void (*)(int);
            Handler old;

            SigPipeIgnoreGuard() : old(std::signal(SIGPIPE, SIG_IGN)) {}

            ~SigPipeIgnoreGuard()
            {
                if (old != SIG_ERR) // GCOVR_EXCL_LINE
                {
                    (void)std::signal(SIGPIPE, old);
                }
            }
        } sigpipe_guard;

        const char* data = stdin_data.data();
        std::size_t remaining = stdin_data.size();
        while (remaining > 0)
        {
            const ssize_t n = write(in_pipe[1], data, remaining);
            if (n < 0) // GCOVR_EXCL_LINE
            {
                break; // GCOVR_EXCL_LINE
            }
            data += n;
            remaining -= static_cast<std::size_t>(n);
        }
        close(in_pipe[1]);
    }

    set_nonblocking(out_pipe[0]);
    set_nonblocking(err_pipe[0]);

    bool out_eof = false;
    bool err_eof = false;
    bool limit_hit = false;
    std::size_t total_bytes = 0;
    const auto start = std::chrono::steady_clock::now();
    while (!out_eof || !err_eof)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed_ms > timeout_ms)
        {
            result.timed_out = true;
            break;
        }

        struct pollfd fds[2];
        fds[0].fd = out_pipe[0];
        fds[0].events = POLLIN;
        fds[1].fd = err_pipe[0];
        fds[1].events = POLLIN;
        const int remaining_ms = timeout_ms - static_cast<int>(elapsed_ms);
        const int poll_ms = (remaining_ms < 50) ? remaining_ms : 50;
        (void)poll(fds, 2, poll_ms);

        if (!out_eof)
        {
            read_into(out_pipe[0], result.out, out_eof, total_bytes, max_output_bytes, limit_hit);
        }
        if (!err_eof)
        {
            read_into(err_pipe[0], result.err, err_eof, total_bytes, max_output_bytes, limit_hit);
        }

        if (limit_hit)
        {
            result.output_limit_exceeded = true;
            break;
        }
    }

    if (result.timed_out || result.output_limit_exceeded)
    {
        (void)kill(pid, SIGKILL);
    }

    close(out_pipe[0]);
    close(err_pipe[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) // GCOVR_EXCL_LINE
    {
        result.exit_code = 127; // GCOVR_EXCL_LINE
        return result;          // GCOVR_EXCL_LINE
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

std::string find_bwrap_path()
{
    if (const char* env = std::getenv("CURLEE_BWRAP"); env != nullptr && *env != '\0')
    {
        return std::string(env);
    }
    return "bwrap";
}

std::string find_python_runner_path()
{
    if (const char* env = std::getenv("CURLEE_PYTHON_RUNNER"); env != nullptr && *env != '\0')
    {
        return std::string(env);
    }

    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) // GCOVR_EXCL_LINE
    {
        buf[n] = '\0';
        std::error_code ec;
        const std::filesystem::path self(buf);
        const auto candidate = self.parent_path() / "curlee_python_runner";
        if (std::filesystem::exists(candidate, ec))
        {
            return candidate.string();
        }
    }

    return "curlee_python_runner";
}

[[nodiscard]] bool response_ok_true(std::string_view json)
{
    return json.find("\"ok\":true") != std::string_view::npos;
}

std::optional<std::string> extract_error_message(std::string_view json)
{
    const std::string_view needle = "\"message\":\"";
    const std::size_t start = json.find(needle);
    if (start == std::string_view::npos)
    {
        return std::nullopt;
    }
    std::size_t i = start + needle.size();
    std::string out;
    while (i < json.size())
    {
        const char c = json[i++];
        if (c == '"')
        {
            return out;
        }
        if (c == '\\' && i < json.size())
        {
            const char esc = json[i++];
            switch (esc)
            {
            case '"':
                out.push_back('"');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            default:
                // Keep deterministic: skip unknown escapes.
                break;
            }
            continue;
        }
        out.push_back(c);
    }
    return std::nullopt;
}

const curlee::vm::VM::Capabilities kEmptyCaps;

constexpr std::size_t kMaxFsBytes = 1 * 1024 * 1024;
constexpr std::size_t kMaxFsPathBytes = 512;

bool is_valid_fs_path(std::string_view path)
{
    namespace fs = std::filesystem;

    if (path.empty() || path.size() > kMaxFsPathBytes)
    {
        return false;
    }
    if (path.find('\0') != std::string_view::npos)
    {
        return false;
    }

    const fs::path p(path);
    if (p.is_absolute())
    {
        return false;
    }

    const std::string generic = p.generic_string();
    if (generic == "." || generic == "..")
    {
        return false;
    }

    for (const auto& part : p)
    {
        const std::string s = part.string();
        if (s == "." || s == "..")
        {
            return false;
        }
    }

    return true;
}

bool has_owner_read_permission(const std::filesystem::path& p)
{
    std::error_code ec;
    const auto st = std::filesystem::status(p, ec);
    if (ec)
    {
        return true;
    }
    return (st.permissions() & std::filesystem::perms::owner_read) !=
           std::filesystem::perms::none;
}

bool has_owner_write_permission(const std::filesystem::path& p)
{
    std::error_code ec;
    const auto st = std::filesystem::status(p, ec);
    if (ec)
    {
        return true;
    }
    return (st.permissions() & std::filesystem::perms::owner_write) !=
           std::filesystem::perms::none;
}

std::string escape_tty_text(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char ch : text)
    {
        if (ch == '\\' || ch == '"')
        {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
} // GCOVR_EXCL_LINE

const curlee::vm::VM::Capabilities& empty_caps()
{
    return kEmptyCaps;
}

} // namespace

namespace curlee::vm
{

namespace
{

[[nodiscard]] VmResult ok_result(Value value)
{
    VmResult result;
    result.ok = true;
    result.value = value;
    result.error.clear();
    result.error_span = std::nullopt;
    return result;
} // GCOVR_EXCL_LINE

[[nodiscard]] VmResult err_result(std::string_view message,
                                  std::optional<curlee::source::Span> span)
{
    VmResult result;
    result.ok = false;
    result.value = Value::unit_v();
    result.error.assign(message.data(), message.size());
    result.error_span = span;
    return result;
} // GCOVR_EXCL_LINE

} // namespace

bool VM::push(Value value)
{
    stack_.push_back(value);
    return true;
}

std::optional<Value> VM::pop()
{
    if (stack_.empty())
    {
        return std::nullopt;
    }
    Value value = stack_.back();
    stack_.pop_back();
    return value;
}

VmResult VM::run(const Chunk& chunk)
{
    return run(chunk, std::numeric_limits<std::size_t>::max(), empty_caps(), VmRunOptions{});
}

VmResult VM::run(const Chunk& chunk, std::size_t fuel)
{
    return run(chunk, fuel, empty_caps(), VmRunOptions{});
}

VmResult VM::run(const Chunk& chunk, const Capabilities& capabilities)
{
    return run(chunk, std::numeric_limits<std::size_t>::max(), capabilities, VmRunOptions{});
}

VmResult VM::run(const Chunk& chunk, std::size_t fuel, const Capabilities& capabilities)
{
    return run(chunk, fuel, capabilities, VmRunOptions{});
}

VmResult VM::run(const Chunk& chunk, const Capabilities& capabilities, const VmRunOptions& options)
{
    return run(chunk, std::numeric_limits<std::size_t>::max(), capabilities, options);
}

VmResult VM::run(const Chunk& chunk, std::size_t fuel, const Capabilities& capabilities,
                 const VmRunOptions& options)
{
    stack_.clear();
    std::vector<Value> locals(chunk.max_locals, Value::unit_v());
    std::vector<std::size_t> call_stack;
    std::vector<std::string> command_stream;
    std::vector<std::string> tty_buffer;
    std::uint64_t rng_state = 0x9e3779b97f4a7c15ULL;

    if (options.use_window_graphics_backend)
    {
        if (!capabilities.contains("gfx.window"))
        {
            return err_result("missing capability gfx.window", std::nullopt);
        }
        command_stream.push_back("gfx.window:init");
        if (const auto* fail_init = std::getenv("CURLEE_GFX_WINDOW_INIT_FAIL");
            fail_init != nullptr && std::string_view(fail_init) == "1")
        {
            VmResult init_fail = err_result("graphics backend init failed", std::nullopt);
            init_fail.command_stream = std::move(command_stream);
            return init_fail;
        }
    }

    std::size_t ip = 0;
    while (ip < chunk.code.size())
    {
        if (fuel == 0)
        {
            return err_result("out of fuel", std::nullopt);
        }
        --fuel;

        const std::size_t op_index = ip;
        const auto op = static_cast<OpCode>(chunk.code[ip++]);
        const auto span = (op_index < chunk.spans.size())
                              ? std::optional<curlee::source::Span>(chunk.spans[op_index])
                              : std::nullopt;
        switch (op)
        {
        case OpCode::Constant:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return err_result("truncated constant", span);
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t idx = static_cast<std::uint16_t>(lo | (hi << 8));
            if (idx >= chunk.constants.size())
            {
                return err_result("constant index out of range", span);
            }
            push(chunk.constants[idx]);
            break;
        }
        case OpCode::LoadLocal:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return err_result("truncated local index", span);
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t idx = static_cast<std::uint16_t>(lo | (hi << 8));
            if (idx >= locals.size())
            {
                return err_result("local index out of range", span);
            }
            push(locals[idx]);
            break;
        }
        case OpCode::StoreLocal:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return err_result("truncated local index", span);
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t idx = static_cast<std::uint16_t>(lo | (hi << 8));
            auto value = pop();
            if (!value.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (idx >= locals.size())
            {
                return err_result("local index out of range", span);
            }
            locals[idx] = *value;
            break;
        }
        case OpCode::Add:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (lhs->kind == ValueKind::Int && rhs->kind == ValueKind::Int)
            {
                push(Value::int_v(lhs->int_value + rhs->int_value));
                break;
            }
            if (lhs->kind == ValueKind::String && rhs->kind == ValueKind::String)
            {
                push(Value::string_v(lhs->string_value + rhs->string_value));
                break;
            }
            return err_result("add expects Int or String", span);
        }
        case OpCode::Sub:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return err_result("sub expects Int", span);
            }
            push(Value::int_v(lhs->int_value - rhs->int_value));
            break;
        }
        case OpCode::Mul:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return err_result("mul expects Int", span);
            }
            push(Value::int_v(lhs->int_value * rhs->int_value));
            break;
        }
        case OpCode::Div:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return err_result("div expects Int", span);
            }
            if (rhs->int_value == 0)
            {
                return err_result("divide by zero", span);
            }
            push(Value::int_v(lhs->int_value / rhs->int_value));
            break;
        }
        case OpCode::Neg:
        {
            auto value = pop();
            if (!value.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (value->kind != ValueKind::Int)
            {
                return err_result("neg expects Int", span);
            }
            push(Value::int_v(-value->int_value));
            break;
        }
        case OpCode::Not:
        {
            auto value = pop();
            if (!value.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (value->kind != ValueKind::Bool)
            {
                return err_result("not expects Bool", span);
            }
            push(Value::bool_v(!value->bool_value));
            break;
        }
        case OpCode::Equal:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            push(Value::bool_v(*lhs == *rhs));
            break;
        }
        case OpCode::NotEqual:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            push(Value::bool_v(!(*lhs == *rhs)));
            break;
        }
        case OpCode::Less:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return err_result("lt expects Int", span);
            }
            push(Value::bool_v(lhs->int_value < rhs->int_value));
            break;
        }
        case OpCode::LessEqual:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return err_result("le expects Int", span);
            }
            push(Value::bool_v(lhs->int_value <= rhs->int_value));
            break;
        }
        case OpCode::Greater:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return err_result("gt expects Int", span);
            }
            push(Value::bool_v(lhs->int_value > rhs->int_value));
            break;
        }
        case OpCode::GreaterEqual:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return err_result("ge expects Int", span);
            }
            push(Value::bool_v(lhs->int_value >= rhs->int_value));
            break;
        }
        case OpCode::BitAnd:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return err_result("bitand expects Int", span);
            }
            push(Value::int_v(lhs->int_value & rhs->int_value));
            break;
        }
        case OpCode::BitOr:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return err_result("bitor expects Int", span);
            }
            push(Value::int_v(lhs->int_value | rhs->int_value));
            break;
        }
        case OpCode::BitXor:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return err_result("bitxor expects Int", span);
            }
            push(Value::int_v(lhs->int_value ^ rhs->int_value));
            break;
        }
        case OpCode::BitNot:
        {
            auto value = pop();
            if (!value.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (value->kind != ValueKind::Int)
            {
                return err_result("bitnot expects Int", span);
            }
            push(Value::int_v(~value->int_value));
            break;
        }
        case OpCode::ShiftLeft:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return err_result("shl expects Int", span);
            }
            if (rhs->int_value < 0 || rhs->int_value >= 64)
            {
                return err_result("shift amount out of range", span);
            }
            // Wrap-around left shift (64-bit two's complement), matching the
            // freestanding C target's uint64_t semantics.
            push(Value::int_v(static_cast<std::int64_t>(static_cast<std::uint64_t>(
                                 lhs->int_value)
                                 << rhs->int_value)));
            break;
        }
        case OpCode::ShiftRight:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return err_result("shr expects Int", span);
            }
            if (rhs->int_value < 0 || rhs->int_value >= 64)
            {
                return err_result("shift amount out of range", span);
            }
            // Arithmetic right shift (documented Int semantics, issue #270).
            push(Value::int_v(lhs->int_value >> rhs->int_value));
            break;
        }
        case OpCode::Mod:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return err_result("mod expects Int", span);
            }
            if (rhs->int_value == 0)
            {
                return err_result("modulo by zero", span);
            }
            push(Value::int_v(lhs->int_value % rhs->int_value));
            break;
        }
        case OpCode::Pop:
        {
            if (!pop().has_value())
            {
                return err_result("stack underflow", span);
            }
            break;
        }
        case OpCode::Return:
        {
            auto result = pop();
            if (!result.has_value())
            {
                return err_result("missing return", span);
            }
            if (options.use_window_graphics_backend)
            {
                command_stream.push_back("gfx.window:present");
                if (const auto* fail_present = std::getenv("CURLEE_GFX_WINDOW_PRESENT_FAIL");
                    fail_present != nullptr && std::string_view(fail_present) == "1")
                {
                    VmResult present_fail =
                        err_result("graphics backend present failed", std::nullopt);
                    present_fail.command_stream = std::move(command_stream);
                    return present_fail;
                }
            }
            VmResult ok = ok_result(*result);
            ok.command_stream = std::move(command_stream);
            return ok;
        }
        case OpCode::Jump:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return err_result("truncated jump target", span);
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t target = static_cast<std::uint16_t>(lo | (hi << 8));
            if (static_cast<std::size_t>(target) >= chunk.code.size())
            {
                return err_result("jump target out of range", span);
            }
            ip = static_cast<std::size_t>(target);
            break;
        }
        case OpCode::JumpIfFalse:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return err_result("truncated jump target", span);
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t target = static_cast<std::uint16_t>(lo | (hi << 8));

            auto cond = pop();
            if (!cond.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (cond->kind != ValueKind::Bool)
            {
                return err_result("jump-if-false expects Bool", span);
            }
            if (!cond->bool_value)
            {
                if (static_cast<std::size_t>(target) >= chunk.code.size())
                {
                    return err_result("jump target out of range", span);
                }
                ip = static_cast<std::size_t>(target);
            }
            break;
        }
        case OpCode::Call:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return err_result("truncated call target", span);
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t target = static_cast<std::uint16_t>(lo | (hi << 8));
            if (static_cast<std::size_t>(target) >= chunk.code.size())
            {
                return err_result("call target out of range", span);
            }

            call_stack.push_back(ip);
            ip = static_cast<std::size_t>(target);
            break;
        }
        case OpCode::Ret:
        {
            if (call_stack.empty())
            {
                return err_result("return with empty call stack", span);
            }
            ip = call_stack.back();
            call_stack.pop_back();
            break;
        }
        case OpCode::Print:
        {
            if (!capabilities.contains("io.stdout"))
            {
                return err_result("missing capability io.stdout", span);
            }
            auto value = pop();
            if (!value.has_value())
            {
                return err_result("stack underflow", span);
            }
            // MVP: stub effect. No ambient IO; host can later wire an output sink.
            push(Value::unit_v());
            break;
        }
        case OpCode::ReadLine:
        {
            if (!capabilities.contains("io.stdin"))
            {
                return err_result("missing capability io.stdin", span);
            }

            auto capability_token = pop();
            if (!capability_token.has_value())
            {
                return err_result("stack underflow", span);
            }

            std::string line;
            if (!std::getline(std::cin, line))
            {
                std::cin.clear();
                push(Value::string_v(""));
                break;
            }

            if (line.size() > 4096)
            {
                return err_result("stdin line too long", span);
            }

            push(Value::string_v(std::move(line)));
            break;
        }
        case OpCode::FsReadText:
        {
            if (!capabilities.contains("fs.read"))
            {
                return err_result("missing capability fs.read", span);
            }

            auto capability_token = pop();
            if (!capability_token.has_value())
            {
                return err_result("stack underflow", span);
            }

            auto path_value = pop();
            if (!path_value.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (path_value->kind != ValueKind::String)
            {
                return err_result("fs path must be String", span);
            }
            if (!is_valid_fs_path(path_value->string_value))
            {
                return err_result("invalid fs path", span);
            }

            const std::filesystem::path path(path_value->string_value);
            std::error_code ec;
            const bool exists = std::filesystem::exists(path, ec);
            if (ec)
            {
                return err_result("fs read failed", span);
            }
            if (!exists)
            {
                return err_result("fs file not found", span);
            }
            if (std::filesystem::is_directory(path, ec))
            {
                return err_result("fs read failed", span);
            }
            if (!has_owner_read_permission(path))
            {
                return err_result("fs access denied", span);
            }

            const auto size = std::filesystem::file_size(path, ec);
            if (ec)
            {
                return err_result("fs read failed", span);
            }
            if (size > kMaxFsBytes)
            {
                return err_result("fs file too large", span);
            }

            std::ifstream in(path, std::ios::binary);
            if (!in.is_open())
            {
                return err_result("fs access denied", span);
            }

            std::string content;
            content.resize(static_cast<std::size_t>(size));
            if (size > 0)
            {
                in.read(content.data(), static_cast<std::streamsize>(size));
                // Platform-dependent low-level stream I/O failure path.
                // EOF short-reads are covered separately; this branch requires
                // non-EOF stream errors that are not deterministic across CI hosts.
                // GCOVR_EXCL_START
                if (!in.good() && !in.eof())
                {
                    return err_result("fs read failed", span);
                }
                // GCOVR_EXCL_STOP
            }

            push(Value::string_v(std::move(content)));
            break;
        }
        case OpCode::FsWriteText:
        {
            if (!capabilities.contains("fs.write"))
            {
                return err_result("missing capability fs.write", span);
            }

            auto capability_token = pop();
            if (!capability_token.has_value())
            {
                return err_result("stack underflow", span);
            }

            auto content_value = pop();
            if (!content_value.has_value())
            {
                return err_result("stack underflow", span);
            }
            auto path_value = pop();
            if (!path_value.has_value())
            {
                return err_result("stack underflow", span);
            }

            if (path_value->kind != ValueKind::String)
            {
                return err_result("fs path must be String", span);
            }
            if (content_value->kind != ValueKind::String)
            {
                return err_result("fs content must be String", span);
            }
            if (!is_valid_fs_path(path_value->string_value))
            {
                return err_result("invalid fs path", span);
            }
            if (content_value->string_value.size() > kMaxFsBytes)
            {
                return err_result("fs content too large", span);
            }

            const std::filesystem::path path(path_value->string_value);
            const auto parent = path.parent_path();
            if (!parent.empty() && std::filesystem::exists(parent) &&
                !has_owner_write_permission(parent))
            {
                return err_result("fs access denied", span);
            }

            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out.is_open())
            {
                return err_result("fs access denied", span);
            }

            out.write(content_value->string_value.data(),
                      static_cast<std::streamsize>(content_value->string_value.size()));
            if (!out.good())
            {
                return err_result("fs write failed", span);
            }

            push(Value::unit_v());
            break;
        }
        case OpCode::TtyClear:
        {
            if (!capabilities.contains("io.tty"))
            {
                return err_result("missing capability io.tty", span);
            }

            auto capability_token = pop();
            if (!capability_token.has_value())
            {
                return err_result("stack underflow", span);
            }

            tty_buffer.push_back("[tty.clear]");
            push(Value::unit_v());
            break;
        }
        case OpCode::TtyWriteAt:
        {
            if (!capabilities.contains("io.tty"))
            {
                return err_result("missing capability io.tty", span);
            }

            auto capability_token = pop();
            if (!capability_token.has_value())
            {
                return err_result("stack underflow", span);
            }
            auto text = pop();
            if (!text.has_value())
            {
                return err_result("stack underflow", span);
            }
            auto col = pop();
            if (!col.has_value())
            {
                return err_result("stack underflow", span);
            }
            auto row = pop();
            if (!row.has_value())
            {
                return err_result("stack underflow", span);
            }

            if (row->kind != ValueKind::Int || col->kind != ValueKind::Int)
            {
                return err_result("tty coordinates must be Int", span);
            }
            if (text->kind != ValueKind::String)
            {
                return err_result("tty text must be String", span);
            }
            if (row->int_value < 0 || col->int_value < 0)
            {
                return err_result("tty coordinates must be >= 0", span);
            }
            if (row->int_value > 999 || col->int_value > 999)
            {
                return err_result("tty coordinates out of bounds (max 999)", span);
            }

            tty_buffer.push_back("[tty.write_at row=" + std::to_string(row->int_value) +
                                 " col=" + std::to_string(col->int_value) + " text=\"" +
                                 escape_tty_text(text->string_value) + "\"]");
            push(Value::unit_v());
            break;
        }
        case OpCode::TtyFlush:
        {
            if (!capabilities.contains("io.tty"))
            {
                return err_result("missing capability io.tty", span);
            }

            auto capability_token = pop();
            if (!capability_token.has_value())
            {
                return err_result("stack underflow", span);
            }

            for (const auto& line : tty_buffer)
            {
                std::cout << line << "\n";
            }
            std::cout.flush();
            tty_buffer.clear();

            push(Value::unit_v());
            break;
        }
        case OpCode::RngNextInt:
        {
            if (!capabilities.contains("rng.seeded"))
            {
                return err_result("missing capability rng.seeded", span);
            }

            auto capability_token = pop();
            if (!capability_token.has_value())
            {
                return err_result("stack underflow", span);
            }
            auto max_value = pop();
            if (!max_value.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (max_value->kind != ValueKind::Int)
            {
                return err_result("rng max must be Int", span);
            }
            if (max_value->int_value <= 0)
            {
                return err_result("rng max must be > 0", span);
            }

            rng_state = (rng_state * 6364136223846793005ULL) + 1ULL;
            const auto max_exclusive = static_cast<std::uint64_t>(max_value->int_value);
            const auto out = static_cast<std::int64_t>(rng_state % max_exclusive);
            push(Value::int_v(out));
            break;
        }
        case OpCode::VecNew:
        {
            auto max_len = pop();
            if (!max_len.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (max_len->kind != ValueKind::Int)
            {
                return err_result("vec max length must be Int", span);
            }
            if (max_len->int_value < 0)
            {
                return err_result("vec max length must be >= 0", span);
            }

            push(Value::vec_v(static_cast<std::size_t>(max_len->int_value), VecElementKind::Int));
            break;
        }
        case OpCode::VecLen:
        {
            auto vec = pop();
            if (!vec.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (vec->kind != ValueKind::Vec || vec->vec_value == nullptr)
            {
                return err_result("vec value must be Vec", span);
            }
            if (vec->vec_value->element_kind != VecElementKind::Int)
            {
                return err_result("vec value must be Vec<Int>", span);
            }

            push(Value::int_v(static_cast<std::int64_t>(vec->vec_value->items.size())));
            break;
        }
        case OpCode::VecPush:
        {
            auto value = pop();
            auto vec = pop();
            if (!value.has_value() || !vec.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (vec->kind != ValueKind::Vec || vec->vec_value == nullptr)
            {
                return err_result("vec value must be Vec", span);
            }
            if (vec->vec_value->element_kind != VecElementKind::Int)
            {
                return err_result("vec value must be Vec<Int>", span);
            }
            if (value->kind != ValueKind::Int)
            {
                return err_result("vec element must be Int", span);
            }

            if (vec->vec_value->items.size() >= vec->vec_value->max_len)
            {
                return err_result("vec capacity exceeded", span);
            }
            vec->vec_value->items.push_back(*value);
            push(Value::unit_v());
            break;
        }
        case OpCode::VecGet:
        {
            auto index = pop();
            auto vec = pop();
            if (!index.has_value() || !vec.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (vec->kind != ValueKind::Vec || vec->vec_value == nullptr)
            {
                return err_result("vec value must be Vec", span);
            }
            if (vec->vec_value->element_kind != VecElementKind::Int)
            {
                return err_result("vec value must be Vec<Int>", span);
            }
            if (index->kind != ValueKind::Int || index->int_value < 0 ||
                static_cast<std::size_t>(index->int_value) >= vec->vec_value->items.size())
            {
                return err_result("vec index out of bounds", span);
            }

            push(vec->vec_value->items[static_cast<std::size_t>(index->int_value)]);
            break;
        }
        case OpCode::VecSet:
        {
            auto value = pop();
            auto index = pop();
            auto vec = pop();
            if (!value.has_value() || !index.has_value() || !vec.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (vec->kind != ValueKind::Vec || vec->vec_value == nullptr)
            {
                return err_result("vec value must be Vec", span);
            }
            if (vec->vec_value->element_kind != VecElementKind::Int)
            {
                return err_result("vec value must be Vec<Int>", span);
            }
            if (value->kind != ValueKind::Int)
            {
                return err_result("vec element must be Int", span);
            }
            if (index->kind != ValueKind::Int || index->int_value < 0 ||
                static_cast<std::size_t>(index->int_value) >= vec->vec_value->items.size())
            {
                return err_result("vec index out of bounds", span);
            }

            vec->vec_value->items[static_cast<std::size_t>(index->int_value)] = *value;
            push(Value::unit_v());
            break;
        }
        case OpCode::VecNewBool:
        {
            auto max_len = pop();
            if (!max_len.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (max_len->kind != ValueKind::Int)
            {
                return err_result("vec max length must be Int", span);
            }
            if (max_len->int_value < 0)
            {
                return err_result("vec max length must be >= 0", span);
            }

            push(Value::vec_v(static_cast<std::size_t>(max_len->int_value),
                              VecElementKind::Bool));
            break;
        }
        case OpCode::VecLenBool:
        {
            auto vec = pop();
            if (!vec.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (vec->kind != ValueKind::Vec || vec->vec_value == nullptr)
            {
                return err_result("vec value must be Vec", span);
            }
            if (vec->vec_value->element_kind != VecElementKind::Bool)
            {
                return err_result("vec value must be Vec<Bool>", span);
            }

            push(Value::int_v(static_cast<std::int64_t>(vec->vec_value->items.size())));
            break;
        }
        case OpCode::VecPushBool:
        {
            auto value = pop();
            auto vec = pop();
            if (!value.has_value() || !vec.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (vec->kind != ValueKind::Vec || vec->vec_value == nullptr)
            {
                return err_result("vec value must be Vec", span);
            }
            if (vec->vec_value->element_kind != VecElementKind::Bool)
            {
                return err_result("vec value must be Vec<Bool>", span);
            }
            if (value->kind != ValueKind::Bool)
            {
                return err_result("vec element must be Bool", span);
            }

            if (vec->vec_value->items.size() >= vec->vec_value->max_len)
            {
                return err_result("vec capacity exceeded", span);
            }
            vec->vec_value->items.push_back(*value);
            push(Value::unit_v());
            break;
        }
        case OpCode::VecGetBool:
        {
            auto index = pop();
            auto vec = pop();
            if (!index.has_value() || !vec.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (vec->kind != ValueKind::Vec || vec->vec_value == nullptr)
            {
                return err_result("vec value must be Vec", span);
            }
            if (vec->vec_value->element_kind != VecElementKind::Bool)
            {
                return err_result("vec value must be Vec<Bool>", span);
            }
            if (index->kind != ValueKind::Int || index->int_value < 0 ||
                static_cast<std::size_t>(index->int_value) >= vec->vec_value->items.size())
            {
                return err_result("vec index out of bounds", span);
            }

            push(vec->vec_value->items[static_cast<std::size_t>(index->int_value)]);
            break;
        }
        case OpCode::VecSetBool:
        {
            auto value = pop();
            auto index = pop();
            auto vec = pop();
            if (!value.has_value() || !index.has_value() || !vec.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (vec->kind != ValueKind::Vec || vec->vec_value == nullptr)
            {
                return err_result("vec value must be Vec", span);
            }
            if (vec->vec_value->element_kind != VecElementKind::Bool)
            {
                return err_result("vec value must be Vec<Bool>", span);
            }
            if (value->kind != ValueKind::Bool)
            {
                return err_result("vec element must be Bool", span);
            }
            if (index->kind != ValueKind::Int || index->int_value < 0 ||
                static_cast<std::size_t>(index->int_value) >= vec->vec_value->items.size())
            {
                return err_result("vec index out of bounds", span);
            }

            vec->vec_value->items[static_cast<std::size_t>(index->int_value)] = *value;
            push(Value::unit_v());
            break;
        }
        case OpCode::ArrayNew:
        {
            // Stack: [..., repeat_value, count]; a u16 operand names the
            // element type via a string constant. Builds a fixed-size array of
            // `count` copies of the repeat value (the runtime mirror of the
            // freestanding `T q[N] = {v, v, ...};` emission).
            if (ip + 1 >= chunk.code.size())
            {
                return err_result("truncated array element type index", span);
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t elem_idx = static_cast<std::uint16_t>(lo | (hi << 8));
            if (elem_idx >= chunk.constants.size() ||
                chunk.constants[elem_idx].kind != ValueKind::String)
            {
                return err_result("array element type index out of range", span);
            }
            const std::string elem_name = chunk.constants[elem_idx].string_value;

            auto count = pop();
            auto repeat = pop();
            if (!count.has_value() || !repeat.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (count->kind != ValueKind::Int)
            {
                return err_result("array length must be Int", span);
            }
            if (count->int_value < 0)
            {
                return err_result("array length must be >= 0", span);
            }
            if (repeat->kind != ValueKind::Int)
            {
                return err_result("array element must be Int", span);
            }

            std::vector<Value> items;
            items.assign(static_cast<std::size_t>(count->int_value), *repeat);
            push(Value::array_v(elem_name, std::move(items)));
            break;
        }
        case OpCode::ArrayGet:
        {
            auto index = pop();
            auto array = pop();
            if (!index.has_value() || !array.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (array->kind != ValueKind::Array || array->array_value == nullptr)
            {
                return err_result("array value must be Array", span);
            }
            if (index->kind != ValueKind::Int || index->int_value < 0 ||
                static_cast<std::size_t>(index->int_value) >= array->array_value->items.size())
            {
                return err_result("array index out of bounds", span);
            }

            push(array->array_value->items[static_cast<std::size_t>(index->int_value)]);
            break;
        }
        case OpCode::ArraySet:
        {
            auto value = pop();
            auto index = pop();
            auto array = pop();
            if (!value.has_value() || !index.has_value() || !array.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (array->kind != ValueKind::Array || array->array_value == nullptr)
            {
                return err_result("array value must be Array", span);
            }
            if (value->kind != ValueKind::Int)
            {
                return err_result("array element must be Int", span);
            }
            if (index->kind != ValueKind::Int || index->int_value < 0 ||
                static_cast<std::size_t>(index->int_value) >= array->array_value->items.size())
            {
                return err_result("array index out of bounds", span);
            }

            array->array_value->items[static_cast<std::size_t>(index->int_value)] = *value;
            push(Value::unit_v());
            break;
        }
        case OpCode::SetNewInt:
            push(Value::set_v());
            break;
        case OpCode::SetHasInt:
        {
            auto value = pop();
            auto set = pop();
            if (!value.has_value() || !set.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (set->kind != ValueKind::Set || set->set_value == nullptr)
            {
                return err_result("set value must be Set", span);
            }
            if (value->kind != ValueKind::Int)
            {
                return err_result("set value must be Int", span);
            }

            push(Value::bool_v(set->set_value->items.contains(value->int_value)));
            break;
        }
        case OpCode::SetInsertInt:
        {
            auto value = pop();
            auto set = pop();
            if (!value.has_value() || !set.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (set->kind != ValueKind::Set || set->set_value == nullptr)
            {
                return err_result("set value must be Set", span);
            }
            if (value->kind != ValueKind::Int)
            {
                return err_result("set value must be Int", span);
            }

            set->set_value->items.insert(value->int_value);
            push(Value::unit_v());
            break;
        }
        case OpCode::PythonCall:
        {
            if (!capabilities.contains("python.ffi"))
            {
                return err_result("python capability required", span);
            }

            const std::string runner = find_python_runner_path();
            const std::string request =
                "{\"protocol_version\":1,\"id\":\"vm\",\"op\":\"handshake\"}\n";

            const bool use_sandbox = capabilities.contains("python.sandbox");
            ProcResult proc;
            if (use_sandbox)
            {
                const std::string bwrap = find_bwrap_path();

                std::vector<std::string> argv_storage;
                argv_storage.push_back(bwrap);
                argv_storage.push_back("--die-with-parent");
                argv_storage.push_back("--unshare-net");
                argv_storage.push_back("--ro-bind");
                argv_storage.push_back("/");
                argv_storage.push_back("/");
                argv_storage.push_back("--proc");
                argv_storage.push_back("/proc");
                argv_storage.push_back("--dev");
                argv_storage.push_back("/dev");
                argv_storage.push_back("--tmpfs");
                argv_storage.push_back("/tmp");
                argv_storage.push_back("--");
                argv_storage.push_back(runner);

                std::vector<const char*> argv;
                argv.reserve(argv_storage.size() + 1);
                for (const auto& s : argv_storage)
                {
                    argv.push_back(s.c_str());
                }
                argv.push_back(nullptr);

                proc = run_process_argv(argv, bwrap, request, kPythonRunnerTimeoutMs,
                                        kPythonRunnerMaxOutputBytes);
            }
            else
            {
                proc = run_process(runner, request, kPythonRunnerTimeoutMs,
                                   kPythonRunnerMaxOutputBytes);
            }

            if (proc.timed_out)
            {
                return err_result("python runner timed out", span);
            }
            if (proc.output_limit_exceeded)
            {
                return err_result("python runner output too large", span);
            }

            if (!response_ok_true(proc.out))
            {
                std::string msg = "python runner failed";
                if (auto m = extract_error_message(proc.out); m.has_value())
                {
                    msg = *m;
                }
                else if (proc.exit_code == 127)
                {
                    msg = use_sandbox ? "python sandbox exec failed" : "python runner exec failed";
                }
                else if (use_sandbox)
                {
                    msg = "python sandbox failed";
                }
                return err_result(msg, span);
            }

            push(Value::unit_v());
            break;
        }
        case OpCode::MakeStruct:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return err_result("invalid struct constructor name", span);
            }
            const std::uint16_t struct_lo = chunk.code[ip++];
            const std::uint16_t struct_hi = chunk.code[ip++];
            const std::uint16_t struct_idx =
                static_cast<std::uint16_t>(struct_lo | (struct_hi << 8));
            if (struct_idx >= chunk.constants.size() ||
                chunk.constants[struct_idx].kind != ValueKind::String)
            {
                return err_result("invalid struct constructor name", span);
            }

            if (ip + 1 >= chunk.code.size())
            {
                return err_result("truncated struct constructor field count", span);
            }
            const std::uint16_t count_lo = chunk.code[ip++];
            const std::uint16_t count_hi = chunk.code[ip++];
            const std::uint16_t field_count = static_cast<std::uint16_t>(count_lo | (count_hi << 8));

            std::vector<std::string> field_names;
            field_names.reserve(field_count);
            for (std::size_t i = 0; i < field_count; ++i)
            {
                if (ip + 1 >= chunk.code.size())
                {
                    return err_result("truncated struct constructor field names", span);
                }
                const std::uint16_t field_lo = chunk.code[ip++];
                const std::uint16_t field_hi = chunk.code[ip++];
                const std::uint16_t field_idx = static_cast<std::uint16_t>(field_lo | (field_hi << 8));
                if (field_idx >= chunk.constants.size() ||
                    chunk.constants[field_idx].kind != ValueKind::String)
                {
                    return err_result("invalid struct constructor field name", span);
                }
                field_names.push_back(chunk.constants[field_idx].string_value);
            }

            std::vector<std::pair<std::string, Value>> fields;
            fields.reserve(field_count);
            for (std::size_t i = 0; i < field_count; ++i)
            {
                auto field_value = pop();
                if (!field_value.has_value())
                {
                    return err_result("stack underflow", span);
                }
                const std::size_t field_index = static_cast<std::size_t>(field_count) - i - 1;
                fields.emplace_back(field_names[field_index], std::move(*field_value));
            }

            std::reverse(fields.begin(), fields.end());
            push(Value::struct_v(chunk.constants[struct_idx].string_value, std::move(fields)));
            break;
        }
        case OpCode::GetField:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return err_result("invalid struct field name", span);
            }
            const std::uint16_t field_lo = chunk.code[ip++];
            const std::uint16_t field_hi = chunk.code[ip++];
            const std::uint16_t field_idx = static_cast<std::uint16_t>(field_lo | (field_hi << 8));
            if (field_idx >= chunk.constants.size() ||
                chunk.constants[field_idx].kind != ValueKind::String)
            {
                return err_result("invalid struct field name", span);
            }

            auto base = pop();
            if (!base.has_value())
            {
                return err_result("stack underflow", span);
            }
            if (base->kind != ValueKind::Struct)
            {
                return err_result("member access expects Struct", span);
            }

            bool found = false;
            const std::string& field_name = chunk.constants[field_idx].string_value;
            for (const auto& [name, value] : base->struct_fields)
            {
                if (name != field_name)
                {
                    continue;
                }
                if (value == nullptr)
                {
                    return err_result("invalid struct field value", span);
                }
                push(*value);
                found = true;
                break;
            }

            if (!found)
            {
                return err_result("unknown struct field", span);
            }
            break;
        }
        case OpCode::MakeEnum:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return err_result("invalid enum constructor enum name", span);
            }
            const std::uint16_t enum_lo = chunk.code[ip++];
            const std::uint16_t enum_hi = chunk.code[ip++];
            const std::uint16_t enum_idx = static_cast<std::uint16_t>(enum_lo | (enum_hi << 8));
            if (enum_idx >= chunk.constants.size() ||
                chunk.constants[enum_idx].kind != ValueKind::String)
            {
                return err_result("invalid enum constructor enum name", span);
            }

            if (ip + 1 >= chunk.code.size())
            {
                return err_result("invalid enum constructor variant name", span);
            }
            const std::uint16_t variant_lo = chunk.code[ip++];
            const std::uint16_t variant_hi = chunk.code[ip++];
            const std::uint16_t variant_idx =
                static_cast<std::uint16_t>(variant_lo | (variant_hi << 8));
            if (variant_idx >= chunk.constants.size() ||
                chunk.constants[variant_idx].kind != ValueKind::String)
            {
                return err_result("invalid enum constructor variant name", span);
            }

            if (ip >= chunk.code.size())
            {
                return err_result("truncated enum constructor", span);
            }
            const bool has_payload = chunk.code[ip++] != 0;

            std::optional<Value> payload;
            if (has_payload)
            {
                auto payload_value = pop();
                if (!payload_value.has_value())
                {
                    return err_result("stack underflow", span);
                }
                payload = std::move(*payload_value);
            }

            push(Value::enum_v(chunk.constants[enum_idx].string_value,
                               chunk.constants[variant_idx].string_value, std::move(payload)));
            break;
        }
        case OpCode::EnumIs:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return err_result("invalid enum-is enum name", span);
            }
            const std::uint16_t enum_lo = chunk.code[ip++];
            const std::uint16_t enum_hi = chunk.code[ip++];
            const std::uint16_t enum_idx = static_cast<std::uint16_t>(enum_lo | (enum_hi << 8));
            if (enum_idx >= chunk.constants.size() ||
                chunk.constants[enum_idx].kind != ValueKind::String)
            {
                return err_result("invalid enum-is enum name", span);
            }

            if (ip + 1 >= chunk.code.size())
            {
                return err_result("invalid enum-is variant name", span);
            }
            const std::uint16_t variant_lo = chunk.code[ip++];
            const std::uint16_t variant_hi = chunk.code[ip++];
            const std::uint16_t variant_idx =
                static_cast<std::uint16_t>(variant_lo | (variant_hi << 8));
            if (variant_idx >= chunk.constants.size() ||
                chunk.constants[variant_idx].kind != ValueKind::String)
            {
                return err_result("invalid enum-is variant name", span);
            }

            auto value = pop();
            if (!value.has_value())
            {
                return err_result("stack underflow", span);
            }

            if (value->kind != ValueKind::Enum)
            {
                push(Value::bool_v(false));
                break;
            }

            const bool matches =
                value->enum_name == chunk.constants[enum_idx].string_value &&
                value->variant_name == chunk.constants[variant_idx].string_value;
            push(Value::bool_v(matches));
            break;
        }
        case OpCode::EnumUnwrap:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return err_result("invalid enum-unwrap enum name", span);
            }
            const std::uint16_t enum_lo = chunk.code[ip++];
            const std::uint16_t enum_hi = chunk.code[ip++];
            const std::uint16_t enum_idx = static_cast<std::uint16_t>(enum_lo | (enum_hi << 8));
            if (enum_idx >= chunk.constants.size() ||
                chunk.constants[enum_idx].kind != ValueKind::String)
            {
                return err_result("invalid enum-unwrap enum name", span);
            }

            if (ip + 1 >= chunk.code.size())
            {
                return err_result("invalid enum-unwrap variant name", span);
            }
            const std::uint16_t variant_lo = chunk.code[ip++];
            const std::uint16_t variant_hi = chunk.code[ip++];
            const std::uint16_t variant_idx =
                static_cast<std::uint16_t>(variant_lo | (variant_hi << 8));
            if (variant_idx >= chunk.constants.size() ||
                chunk.constants[variant_idx].kind != ValueKind::String)
            {
                return err_result("invalid enum-unwrap variant name", span);
            }

            auto value = pop();
            if (!value.has_value())
            {
                return err_result("stack underflow", span);
            }

            const bool matches =
                value->kind == ValueKind::Enum &&
                value->enum_name == chunk.constants[enum_idx].string_value &&
                value->variant_name == chunk.constants[variant_idx].string_value;
            if (!matches)
            {
                return err_result("enum unwrap variant mismatch", span);
            }
            if (value->payload == nullptr)
            {
                return err_result("enum unwrap missing payload", span);
            }

            push(*value->payload);
            break;
        }
        }
    }

    return err_result("no return", std::nullopt);
}

VmResult VM::run(const Chunk& chunk, std::size_t fuel, const Capabilities& capabilities,
                 std::optional<std::uint64_t> rng_seed)
{
    return run(chunk, fuel, capabilities, rng_seed, VmRunOptions{});
}

VmResult VM::run(const Chunk& chunk, std::size_t fuel, const Capabilities& capabilities,
                 std::optional<std::uint64_t> rng_seed, const VmRunOptions& options)
{
    VmResult result = run(chunk, fuel, capabilities, options);
    result.profile.fuel_limit = fuel;
    if (!result.ok && result.error == "out of fuel")
    {
        result.profile.steps = fuel;
        result.profile.fuel_used = fuel;
        result.profile.fuel_remaining = 0;
    }
    else
    {
        result.profile.steps = 0;
        result.profile.fuel_used = 0;
        result.profile.fuel_remaining = fuel;
    }
    result.profile.rng_seed = rng_seed;
    return result;
} // GCOVR_EXCL_LINE

} // namespace curlee::vm
