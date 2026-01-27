#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <curlee/vm/vm.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include <poll.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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

void set_nonblocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        return;
    }
    (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

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
        if (errno == EAGAIN || errno == EWOULDBLOCK)
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
    int in_pipe[2] = {-1, -1};
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    ProcResult result;

    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0 || pipe(err_pipe) != 0)
    {
        result.exit_code = 127;
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0)
    {
        result.exit_code = 127;
        return result;
    }

    if (pid == 0)
    {
        (void)dup2(in_pipe[0], STDIN_FILENO);
        (void)dup2(out_pipe[1], STDOUT_FILENO);
        (void)dup2(err_pipe[1], STDERR_FILENO);

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

    // Write input then close.
    {
        const char* data = stdin_data.data();
        std::size_t remaining = stdin_data.size();
        while (remaining > 0)
        {
            const ssize_t n = write(in_pipe[1], data, remaining);
            if (n < 0)
            {
                break;
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
    if (waitpid(pid, &status, 0) < 0)
    {
        result.exit_code = 127;
        return result;
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

std::string find_python_runner_path()
{
    if (const char* env = std::getenv("CURLEE_PYTHON_RUNNER"); env != nullptr && *env != '\0')
    {
        return std::string(env);
    }

    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0)
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

const curlee::vm::VM::Capabilities& empty_caps()
{
    static const curlee::vm::VM::Capabilities caps;
    return caps;
}

} // namespace

namespace curlee::vm
{

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
    return run(chunk, std::numeric_limits<std::size_t>::max(), empty_caps());
}

VmResult VM::run(const Chunk& chunk, std::size_t fuel)
{
    return run(chunk, fuel, empty_caps());
}

VmResult VM::run(const Chunk& chunk, const Capabilities& capabilities)
{
    return run(chunk, std::numeric_limits<std::size_t>::max(), capabilities);
}

VmResult VM::run(const Chunk& chunk, std::size_t fuel, const Capabilities& capabilities)
{
    stack_.clear();
    std::vector<Value> locals(chunk.max_locals, Value::unit_v());
    std::vector<std::size_t> call_stack;

    std::size_t ip = 0;
    while (ip < chunk.code.size())
    {
        if (fuel == 0)
        {
            return VmResult{.ok = false,
                            .value = Value::unit_v(),
                            .error = "out of fuel",
                            .error_span = std::nullopt};
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
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "truncated constant",
                                .error_span = span};
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t idx = static_cast<std::uint16_t>(lo | (hi << 8));
            if (idx >= chunk.constants.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "constant index out of range",
                                .error_span = span};
            }
            push(chunk.constants[idx]);
            break;
        }
        case OpCode::LoadLocal:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "truncated local index",
                                .error_span = span};
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t idx = static_cast<std::uint16_t>(lo | (hi << 8));
            if (idx >= locals.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "local index out of range",
                                .error_span = span};
            }
            push(locals[idx]);
            break;
        }
        case OpCode::StoreLocal:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "truncated local index",
                                .error_span = span};
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t idx = static_cast<std::uint16_t>(lo | (hi << 8));
            auto value = pop();
            if (!value.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (idx >= locals.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "local index out of range",
                                .error_span = span};
            }
            locals[idx] = *value;
            break;
        }
        case OpCode::Add:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
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
            return VmResult{.ok = false,
                            .value = Value::unit_v(),
                            .error = "add expects Int or String",
                            .error_span = span};
            break;
        }
        case OpCode::Sub:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "sub expects Int",
                                .error_span = span};
            }
            push(Value::int_v(lhs->int_value - rhs->int_value));
            break;
        }
        case OpCode::Mul:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "mul expects Int",
                                .error_span = span};
            }
            push(Value::int_v(lhs->int_value * rhs->int_value));
            break;
        }
        case OpCode::Div:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "div expects Int",
                                .error_span = span};
            }
            if (rhs->int_value == 0)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "divide by zero",
                                .error_span = span};
            }
            push(Value::int_v(lhs->int_value / rhs->int_value));
            break;
        }
        case OpCode::Neg:
        {
            auto value = pop();
            if (!value.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (value->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "neg expects Int",
                                .error_span = span};
            }
            push(Value::int_v(-value->int_value));
            break;
        }
        case OpCode::Not:
        {
            auto value = pop();
            if (!value.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (value->kind != ValueKind::Bool)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "not expects Bool",
                                .error_span = span};
            }
            push(Value::bool_v(!value->bool_value));
            break;
        }
        case OpCode::Equal:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            push(Value::bool_v(*lhs == *rhs));
            break;
        }
        case OpCode::NotEqual:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            push(Value::bool_v(!(*lhs == *rhs)));
            break;
        }
        case OpCode::Less:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "lt expects Int",
                                .error_span = span};
            }
            push(Value::bool_v(lhs->int_value < rhs->int_value));
            break;
        }
        case OpCode::LessEqual:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "le expects Int",
                                .error_span = span};
            }
            push(Value::bool_v(lhs->int_value <= rhs->int_value));
            break;
        }
        case OpCode::Greater:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "gt expects Int",
                                .error_span = span};
            }
            push(Value::bool_v(lhs->int_value > rhs->int_value));
            break;
        }
        case OpCode::GreaterEqual:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!lhs.has_value() || !rhs.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "ge expects Int",
                                .error_span = span};
            }
            push(Value::bool_v(lhs->int_value >= rhs->int_value));
            break;
        }
        case OpCode::Pop:
        {
            if (!pop().has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            break;
        }
        case OpCode::Return:
        {
            auto result = pop();
            if (!result.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "missing return",
                                .error_span = span};
            }
            return VmResult{.ok = true, .value = *result, .error = {}, .error_span = std::nullopt};
        }
        case OpCode::Jump:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "truncated jump target",
                                .error_span = span};
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t target = static_cast<std::uint16_t>(lo | (hi << 8));
            if (static_cast<std::size_t>(target) >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "jump target out of range",
                                .error_span = span};
            }
            ip = static_cast<std::size_t>(target);
            break;
        }
        case OpCode::JumpIfFalse:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "truncated jump target",
                                .error_span = span};
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t target = static_cast<std::uint16_t>(lo | (hi << 8));

            auto cond = pop();
            if (!cond.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            if (cond->kind != ValueKind::Bool)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "jump-if-false expects Bool",
                                .error_span = span};
            }
            if (!cond->bool_value)
            {
                if (static_cast<std::size_t>(target) >= chunk.code.size())
                {
                    return VmResult{.ok = false,
                                    .value = Value::unit_v(),
                                    .error = "jump target out of range",
                                    .error_span = span};
                }
                ip = static_cast<std::size_t>(target);
            }
            break;
        }
        case OpCode::Call:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "truncated call target",
                                .error_span = span};
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t target = static_cast<std::uint16_t>(lo | (hi << 8));
            if (static_cast<std::size_t>(target) >= chunk.code.size())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "call target out of range",
                                .error_span = span};
            }

            call_stack.push_back(ip);
            ip = static_cast<std::size_t>(target);
            break;
        }
        case OpCode::Ret:
        {
            if (call_stack.empty())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "return with empty call stack",
                                .error_span = span};
            }
            ip = call_stack.back();
            call_stack.pop_back();
            break;
        }
        case OpCode::Print:
        {
            if (!capabilities.contains("io:stdout"))
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "missing capability io:stdout",
                                .error_span = span};
            }
            auto value = pop();
            if (!value.has_value())
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "stack underflow",
                                .error_span = span};
            }
            // MVP: stub effect. No ambient IO; host can later wire an output sink.
            push(Value::unit_v());
            break;
        }
        case OpCode::PythonCall:
        {
            if (!capabilities.contains("python:ffi"))
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "python capability required",
                                .error_span = span};
            }

            const std::string runner = find_python_runner_path();
            const std::string request =
                "{\"protocol_version\":1,\"id\":\"vm\",\"op\":\"handshake\"}\n";
            const auto proc =
                run_process(runner, request, kPythonRunnerTimeoutMs, kPythonRunnerMaxOutputBytes);

            if (proc.timed_out)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "python runner timed out",
                                .error_span = span};
            }
            if (proc.output_limit_exceeded)
            {
                return VmResult{.ok = false,
                                .value = Value::unit_v(),
                                .error = "python runner output too large",
                                .error_span = span};
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
                    msg = "python runner exec failed";
                }
                return VmResult{
                    .ok = false, .value = Value::unit_v(), .error = msg, .error_span = span};
            }

            push(Value::unit_v());
            break;
        }
        }
    }

    return VmResult{
        .ok = false, .value = Value::unit_v(), .error = "no return", .error_span = std::nullopt};
}

} // namespace curlee::vm
