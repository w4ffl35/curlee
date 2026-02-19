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
constexpr std::size_t kMaxStdinLineBytes = 4096;
constexpr std::size_t kMaxFsPathBytes = 512;
constexpr std::size_t kMaxFsTextBytes = 1 * 1024 * 1024;
constexpr std::size_t kMaxVecCapacity = 65536;

struct StdinReadResult
{
    bool ok = false;
    bool too_long = false;
    std::string line;
};

[[nodiscard]] StdinReadResult read_stdin_line_bounded(std::size_t max_bytes)
{
    StdinReadResult out;
    out.ok = true;

    while (true)
    {
        const int c = std::cin.get();
        if (c == std::char_traits<char>::eof())
        {
            break;
        }

        if (c == '\n')
        {
            break;
        }

        if (out.line.size() >= max_bytes)
        {
            out.ok = false;
            out.too_long = true;
            return out;
        }

        out.line.push_back(static_cast<char>(c));
    }

    return out;
} // GCOVR_EXCL_LINE

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

std::string tty_write_trace(long long row, long long col, std::string_view text)
{
    return "[tty.write_at row=" + std::to_string(row) + " col=" + std::to_string(col) +
           " text=\"" + std::string(text) + "\"]\n";
}

[[nodiscard]] bool is_fs_access_denied(int err)
{
    return err == EACCES || err == EPERM; // GCOVR_EXCL_LINE
}

[[nodiscard]] std::optional<std::string> normalize_fs_path(std::string_view raw)
{
    if (raw.empty())
    {
        return std::nullopt;
    }

    if (raw.size() > kMaxFsPathBytes)
    {
        return std::nullopt;
    }

    if (raw.find('\0') != std::string_view::npos)
    {
        return std::nullopt;
    }

    const std::filesystem::path path(raw);
    if (path.is_absolute())
    {
        return std::nullopt;
    }

    for (const auto& part : path)
    {
        const auto component = part.string();
        if (component == "..")
        {
            return std::nullopt;
        }
    }

    const std::string normalized = path.lexically_normal().generic_string();
    if (normalized.empty() || normalized == ".") // GCOVR_EXCL_LINE
    {
        return std::nullopt;
    }
    if (!normalized.empty() && normalized.front() == '/') // GCOVR_EXCL_LINE
    {
        return std::nullopt; // GCOVR_EXCL_LINE
    }

    return normalized;
}

[[nodiscard]] std::variant<std::string, std::string_view> read_text_file_bounded(std::string_view path,
                                                                                  std::size_t max_bytes)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path file_path(path);
    const bool exists = fs::exists(file_path, ec);
    if (ec) // GCOVR_EXCL_LINE
    {
        if (ec == std::errc::permission_denied) // GCOVR_EXCL_LINE
        {
            return std::string_view{"fs access denied"}; // GCOVR_EXCL_LINE
        }
        return std::string_view{"fs read failed"}; // GCOVR_EXCL_LINE
    }

    if (!exists)
    {
        return std::string_view{"fs file not found"};
    }

    if (!fs::is_regular_file(file_path, ec)) // GCOVR_EXCL_LINE
    {
        if (ec == std::errc::permission_denied) // GCOVR_EXCL_LINE
        {
            return std::string_view{"fs access denied"}; // GCOVR_EXCL_LINE
        }
        return std::string_view{"fs read failed"};
    }

    const auto file_size = fs::file_size(file_path, ec);
    if (ec) // GCOVR_EXCL_LINE
    {
        if (ec == std::errc::permission_denied) // GCOVR_EXCL_LINE
        {
            return std::string_view{"fs access denied"}; // GCOVR_EXCL_LINE
        }
        return std::string_view{"fs read failed"}; // GCOVR_EXCL_LINE
    }

    if (file_size > max_bytes)
    {
        return std::string_view{"fs file too large"};
    }

    errno = 0;
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) // GCOVR_EXCL_LINE
    {
        if (is_fs_access_denied(errno)) // GCOVR_EXCL_LINE
        {
            return std::string_view{"fs access denied"};
        }
        return std::string_view{"fs read failed"}; // GCOVR_EXCL_LINE
    }

    std::string out;
    out.resize(static_cast<std::size_t>(file_size));
    if (file_size > 0) // GCOVR_EXCL_LINE
    {
        in.read(out.data(), static_cast<std::streamsize>(file_size));
        if (!in.good() && !in.eof()) // GCOVR_EXCL_LINE
        {
            return std::string_view{"fs read failed"}; // GCOVR_EXCL_LINE
        }
    }
    return out;
}

[[nodiscard]] std::optional<std::string_view> write_text_file_bounded(std::string_view path,
                                                                       std::string_view content,
                                                                       std::size_t max_bytes)
{
    if (content.size() > max_bytes)
    {
        return std::string_view{"fs content too large"};
    }

    const std::filesystem::path file_path(path);
    errno = 0;
    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) // GCOVR_EXCL_LINE
    {
        if (is_fs_access_denied(errno)) // GCOVR_EXCL_LINE
        {
            return std::string_view{"fs access denied"};
        }
        return std::string_view{"fs write failed"}; // GCOVR_EXCL_LINE
    }

    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out.good()) // GCOVR_EXCL_LINE
    {
        return std::string_view{"fs write failed"}; // GCOVR_EXCL_LINE
    }

    return std::nullopt;
}

std::uint64_t next_rng_word(std::uint64_t& state)
{
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31U);
}

const curlee::vm::VM::Capabilities kEmptyCaps;

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
    return run(chunk, std::numeric_limits<std::size_t>::max(), empty_caps(), std::nullopt);
}

VmResult VM::run(const Chunk& chunk, std::size_t fuel)
{
    return run(chunk, fuel, empty_caps(), std::nullopt);
}

VmResult VM::run(const Chunk& chunk, const Capabilities& capabilities)
{
    return run(chunk, std::numeric_limits<std::size_t>::max(), capabilities, std::nullopt);
}

VmResult VM::run(const Chunk& chunk, std::size_t fuel, const Capabilities& capabilities)
{
    return run(chunk, fuel, capabilities, std::nullopt);
}

VmResult VM::run(const Chunk& chunk, std::size_t fuel, const Capabilities& capabilities,
                 std::optional<std::uint64_t> rng_seed)
{
    const std::size_t fuel_limit = fuel;
    std::size_t steps = 0;
    std::optional<std::uint64_t> rng_state = rng_seed;

    auto finalize_result = [&](VmResult result) -> VmResult
    {
        result.profile.steps = steps;
        result.profile.fuel_limit = fuel_limit;
        result.profile.fuel_remaining = fuel;
        result.profile.fuel_used = fuel_limit - fuel;
        result.profile.rng_seed = rng_seed;
        return result;
    };

    stack_.clear();
    std::vector<std::string> tty_pending_output;
    std::vector<Value> locals(chunk.max_locals, Value::unit_v());
    std::vector<std::size_t> call_stack;

    std::size_t ip = 0;
    while (ip < chunk.code.size())
    {
        if (fuel == 0)
        {
            return finalize_result(err_result("out of fuel", std::nullopt));
        }
        --fuel;
        ++steps;

        const std::size_t op_index = ip;
        const auto op = static_cast<OpCode>(chunk.code[ip++]);
        const auto span = (op_index < chunk.spans.size())
                              ? std::optional<curlee::source::Span>(chunk.spans[op_index])
                              : std::nullopt;

        auto read_u16_operand = [&]() -> std::optional<std::uint16_t>
        {
            if (ip + 1 >= chunk.code.size())
            {
                return std::nullopt;
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            return static_cast<std::uint16_t>(lo | (hi << 8));
        };

        auto read_string_constant = [&]() -> std::optional<std::string>
        {
            const auto idx = read_u16_operand();
            if (!idx.has_value())
            {
                return std::nullopt;
            }
            if (*idx >= chunk.constants.size())
            {
                return std::nullopt;
            }
            const auto& constant = chunk.constants[*idx];
            if (constant.kind != ValueKind::String)
            {
                return std::nullopt;
            }
            return constant.string_value;
        };

        switch (op)
        {
        case OpCode::Constant:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return finalize_result(err_result("truncated constant", span));
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t idx = static_cast<std::uint16_t>(lo | (hi << 8));
            if (idx >= chunk.constants.size())
            {
                return finalize_result(err_result("constant index out of range", span));
            }
            push(chunk.constants[idx]);
            break;
        }
        case OpCode::LoadLocal:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return finalize_result(err_result("truncated local index", span));
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t idx = static_cast<std::uint16_t>(lo | (hi << 8));
            if (idx >= locals.size())
            {
                return finalize_result(err_result("local index out of range", span));
            }
            push(locals[idx]);
            break;
        }
        case OpCode::StoreLocal:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return finalize_result(err_result("truncated local index", span));
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t idx = static_cast<std::uint16_t>(lo | (hi << 8));
            auto value = pop();
            if (!value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (idx >= locals.size())
            {
                return finalize_result(err_result("local index out of range", span));
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
                return finalize_result(err_result("stack underflow", span));
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
            return finalize_result(err_result("add expects Int or String", span));
        }
        case OpCode::Sub:
        {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs.has_value() || !lhs.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return finalize_result(err_result("sub expects Int", span));
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
                return finalize_result(err_result("stack underflow", span));
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return finalize_result(err_result("mul expects Int", span));
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
                return finalize_result(err_result("stack underflow", span));
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return finalize_result(err_result("div expects Int", span));
            }
            if (rhs->int_value == 0)
            {
                return finalize_result(err_result("divide by zero", span));
            }
            push(Value::int_v(lhs->int_value / rhs->int_value));
            break;
        }
        case OpCode::Neg:
        {
            auto value = pop();
            if (!value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (value->kind != ValueKind::Int)
            {
                return finalize_result(err_result("neg expects Int", span));
            }
            push(Value::int_v(-value->int_value));
            break;
        }
        case OpCode::Not:
        {
            auto value = pop();
            if (!value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (value->kind != ValueKind::Bool)
            {
                return finalize_result(err_result("not expects Bool", span));
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
                return finalize_result(err_result("stack underflow", span));
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
                return finalize_result(err_result("stack underflow", span));
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
                return finalize_result(err_result("stack underflow", span));
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return finalize_result(err_result("lt expects Int", span));
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
                return finalize_result(err_result("stack underflow", span));
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return finalize_result(err_result("le expects Int", span));
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
                return finalize_result(err_result("stack underflow", span));
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return finalize_result(err_result("gt expects Int", span));
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
                return finalize_result(err_result("stack underflow", span));
            }
            if (lhs->kind != ValueKind::Int || rhs->kind != ValueKind::Int)
            {
                return finalize_result(err_result("ge expects Int", span));
            }
            push(Value::bool_v(lhs->int_value >= rhs->int_value));
            break;
        }
        case OpCode::Pop:
        {
            if (!pop().has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            break;
        }
        case OpCode::Return:
        {
            auto result = pop();
            if (!result.has_value())
            {
                return finalize_result(err_result("missing return", span));
            }
            return finalize_result(ok_result(*result));
        }
        case OpCode::Jump:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return finalize_result(err_result("truncated jump target", span));
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t target = static_cast<std::uint16_t>(lo | (hi << 8));
            if (static_cast<std::size_t>(target) >= chunk.code.size())
            {
                return finalize_result(err_result("jump target out of range", span));
            }
            ip = static_cast<std::size_t>(target);
            break;
        }
        case OpCode::JumpIfFalse:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return finalize_result(err_result("truncated jump target", span));
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t target = static_cast<std::uint16_t>(lo | (hi << 8));

            auto cond = pop();
            if (!cond.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (cond->kind != ValueKind::Bool)
            {
                return finalize_result(err_result("jump-if-false expects Bool", span));
            }
            if (!cond->bool_value)
            {
                if (static_cast<std::size_t>(target) >= chunk.code.size())
                {
                    return finalize_result(err_result("jump target out of range", span));
                }
                ip = static_cast<std::size_t>(target);
            }
            break;
        }
        case OpCode::Call:
        {
            if (ip + 1 >= chunk.code.size())
            {
                return finalize_result(err_result("truncated call target", span));
            }
            const std::uint16_t lo = chunk.code[ip++];
            const std::uint16_t hi = chunk.code[ip++];
            const std::uint16_t target = static_cast<std::uint16_t>(lo | (hi << 8));
            if (static_cast<std::size_t>(target) >= chunk.code.size())
            {
                return finalize_result(err_result("call target out of range", span));
            }

            call_stack.push_back(ip);
            ip = static_cast<std::size_t>(target);
            break;
        }
        case OpCode::Ret:
        {
            if (call_stack.empty())
            {
                return finalize_result(err_result("return with empty call stack", span));
            }
            ip = call_stack.back();
            call_stack.pop_back();
            break;
        }
        case OpCode::Print:
        {
            if (!capabilities.contains("io.stdout"))
            {
                return finalize_result(err_result("missing capability io.stdout", span));
            }
            auto value = pop();
            if (!value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            // MVP: stub effect. No ambient IO; host can later wire an output sink.
            push(Value::unit_v());
            break;
        }
        case OpCode::ReadLine:
        {
            if (!capabilities.contains("io.stdin"))
            {
                return finalize_result(err_result("missing capability io.stdin", span));
            }

            auto cap_value = pop();
            if (!cap_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }

            const auto line = read_stdin_line_bounded(kMaxStdinLineBytes);
            if (!line.ok)
            {
                return finalize_result(err_result("stdin line too long", span));
            }

            push(Value::string_v(line.line));
            break;
        }
        case OpCode::FsReadText:
        {
            if (!capabilities.contains("fs.read"))
            {
                return finalize_result(err_result("missing capability fs.read", span));
            }

            auto cap_value = pop();
            auto path_value = pop();
            if (!cap_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (!path_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }

            if (path_value->kind != ValueKind::String)
            {
                return finalize_result(err_result("fs path must be String", span));
            }

            const auto path = normalize_fs_path(path_value->string_value);
            if (!path.has_value())
            {
                return finalize_result(err_result("invalid fs path", span));
            }

            auto read_result = read_text_file_bounded(*path, kMaxFsTextBytes);
            if (const auto* err = std::get_if<std::string_view>(&read_result))
            {
                return finalize_result(err_result(*err, span));
            }

            push(Value::string_v(std::get<std::string>(std::move(read_result))));
            break;
        }
        case OpCode::FsWriteText:
        {
            if (!capabilities.contains("fs.write"))
            {
                return finalize_result(err_result("missing capability fs.write", span));
            }

            auto cap_value = pop();
            auto content_value = pop();
            auto path_value = pop();
            if (!cap_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (!content_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (!path_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }

            if (path_value->kind != ValueKind::String)
            {
                return finalize_result(err_result("fs path must be String", span));
            }
            if (content_value->kind != ValueKind::String)
            {
                return finalize_result(err_result("fs content must be String", span));
            }

            const auto path = normalize_fs_path(path_value->string_value);
            if (!path.has_value())
            {
                return finalize_result(err_result("invalid fs path", span));
            }

            const auto write_error =
                write_text_file_bounded(*path, content_value->string_value, kMaxFsTextBytes);
            if (write_error.has_value())
            {
                return finalize_result(err_result(*write_error, span));
            }

            push(Value::unit_v());
            break;
        }
        case OpCode::TtyClear:
        {
            if (!capabilities.contains("io.tty"))
            {
                return finalize_result(err_result("missing capability io.tty", span));
            }

            auto tty_value = pop();
            if (!tty_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }

            tty_pending_output.push_back("[tty.clear]\n");
            push(Value::unit_v());
            break;
        }
        case OpCode::TtyWriteAt:
        {
            if (!capabilities.contains("io.tty"))
            {
                return finalize_result(err_result("missing capability io.tty", span));
            }

            auto tty_value = pop();
            auto text_value = pop();
            auto col_value = pop();
            auto row_value = pop();
            if (!tty_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (!text_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (!col_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (!row_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }

            if (row_value->kind != ValueKind::Int)
            {
                return finalize_result(err_result("tty coordinates must be Int", span));
            }
            if (col_value->kind != ValueKind::Int)
            {
                return finalize_result(err_result("tty coordinates must be Int", span));
            }
            if (text_value->kind != ValueKind::String)
            {
                return finalize_result(err_result("tty text must be String", span));
            }

            const long long row = row_value->int_value;
            const long long col = col_value->int_value;
            if (row < 0)
            {
                return finalize_result(err_result("tty coordinates must be >= 0", span));
            }
            if (col < 0)
            {
                return finalize_result(err_result("tty coordinates must be >= 0", span));
            }
            if (row > 999)
            {
                return finalize_result(err_result("tty coordinates out of bounds (max 999)", span));
            }
            if (col > 999)
            {
                return finalize_result(err_result("tty coordinates out of bounds (max 999)", span));
            }

            tty_pending_output.push_back(tty_write_trace(row, col, text_value->string_value));
            push(Value::unit_v());
            break;
        }
        case OpCode::TtyFlush:
        {
            if (!capabilities.contains("io.tty"))
            {
                return finalize_result(err_result("missing capability io.tty", span));
            }

            auto tty_value = pop();
            if (!tty_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }

            for (const auto& segment : tty_pending_output)
            {
                std::cout << segment;
            }
            std::cout.flush();
            tty_pending_output.clear();
            push(Value::unit_v());
            break;
        }
        case OpCode::RngNextInt:
        {
            if (!capabilities.contains("rng.seeded"))
            {
                return finalize_result(err_result("missing capability rng.seeded", span));
            }

            auto rng_cap_value = pop();
            auto max_exclusive_value = pop();
            if (!rng_cap_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            (void)rng_cap_value;
            if (!max_exclusive_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }

            if (max_exclusive_value->kind != ValueKind::Int)
            {
                return finalize_result(err_result("rng max_exclusive must be Int", span));
            }

            const long long max_exclusive = max_exclusive_value->int_value;
            if (max_exclusive <= 0)
            {
                return finalize_result(err_result("rng max_exclusive must be > 0", span));
            }

            if (!rng_state.has_value())
            {
                return finalize_result(err_result("missing RNG seed; pass --seed <n>", span));
            }

            const std::uint64_t word = next_rng_word(*rng_state);
            const auto bound = static_cast<std::uint64_t>(max_exclusive);
            const long long sampled = static_cast<long long>(word % bound);
            push(Value::int_v(sampled));
            break;
        }
        case OpCode::VecNew:
        {
            auto max_len_value = pop();
            if (!max_len_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (max_len_value->kind != ValueKind::Int)
            {
                return finalize_result(err_result("vec max_len must be Int", span));
            }

            const auto max_len = max_len_value->int_value;
            if (max_len < 0)
            {
                return finalize_result(err_result("vec max_len must be >= 0", span));
            }
            if (static_cast<std::size_t>(max_len) > kMaxVecCapacity)
            {
                return finalize_result(err_result("vec max_len too large", span));
            }

            push(Value::vec_v(static_cast<std::size_t>(max_len), VecElementKind::Int));
            break;
        }
        case OpCode::VecLen:
        {
            auto vec_value = pop();
            if (!vec_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (vec_value->kind != ValueKind::Vec || vec_value->vec_value == nullptr)
            {
                return finalize_result(err_result("vec value must be Vec", span));
            }
            if (vec_value->vec_value->element_kind != VecElementKind::Int)
            {
                return finalize_result(err_result("vec value must be Vec<Int>", span));
            }

            push(Value::int_v(static_cast<long long>(vec_value->vec_value->items.size())));
            break;
        }
        case OpCode::VecPush:
        {
            auto value = pop();
            auto vec_value = pop();
            if (!value.has_value() || !vec_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (vec_value->kind != ValueKind::Vec || vec_value->vec_value == nullptr)
            {
                return finalize_result(err_result("vec value must be Vec", span));
            }
            if (vec_value->vec_value->element_kind != VecElementKind::Int)
            {
                return finalize_result(err_result("vec value must be Vec<Int>", span));
            }
            if (value->kind != ValueKind::Int)
            {
                return finalize_result(err_result("vec element must be Int", span));
            }

            auto& items = vec_value->vec_value->items;
            if (items.size() >= vec_value->vec_value->max_len)
            {
                return finalize_result(err_result("vec capacity exceeded", span));
            }

            items.push_back(*value);
            push(Value::unit_v());
            break;
        }
        case OpCode::VecGet:
        {
            auto index = pop();
            auto vec_value = pop();
            if (!index.has_value() || !vec_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (vec_value->kind != ValueKind::Vec || vec_value->vec_value == nullptr)
            {
                return finalize_result(err_result("vec value must be Vec", span));
            }
            if (vec_value->vec_value->element_kind != VecElementKind::Int)
            {
                return finalize_result(err_result("vec value must be Vec<Int>", span));
            }
            if (index->kind != ValueKind::Int)
            {
                return finalize_result(err_result("vec index must be Int", span));
            }

            const auto i = index->int_value;
            if (i < 0 || static_cast<std::size_t>(i) >= vec_value->vec_value->items.size())
            {
                return finalize_result(err_result("vec index out of bounds", span));
            }

            push(vec_value->vec_value->items[static_cast<std::size_t>(i)]);
            break;
        }
        case OpCode::VecSet:
        {
            auto value = pop();
            auto index = pop();
            auto vec_value = pop();
            if (!value.has_value() || !index.has_value() || !vec_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (vec_value->kind != ValueKind::Vec || vec_value->vec_value == nullptr)
            {
                return finalize_result(err_result("vec value must be Vec", span));
            }
            if (vec_value->vec_value->element_kind != VecElementKind::Int)
            {
                return finalize_result(err_result("vec value must be Vec<Int>", span));
            }
            if (index->kind != ValueKind::Int)
            {
                return finalize_result(err_result("vec index must be Int", span));
            }
            if (value->kind != ValueKind::Int)
            {
                return finalize_result(err_result("vec element must be Int", span));
            }

            const auto i = index->int_value;
            if (i < 0 || static_cast<std::size_t>(i) >= vec_value->vec_value->items.size())
            {
                return finalize_result(err_result("vec index out of bounds", span));
            }

            vec_value->vec_value->items[static_cast<std::size_t>(i)] = *value;
            push(Value::unit_v());
            break;
        }
        case OpCode::VecNewBool:
        {
            auto max_len_value = pop();
            if (!max_len_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (max_len_value->kind != ValueKind::Int)
            {
                return finalize_result(err_result("vec max_len must be Int", span));
            }

            const auto max_len = max_len_value->int_value;
            if (max_len < 0)
            {
                return finalize_result(err_result("vec max_len must be >= 0", span));
            }
            if (static_cast<std::size_t>(max_len) > kMaxVecCapacity)
            {
                return finalize_result(err_result("vec max_len too large", span));
            }

            push(Value::vec_v(static_cast<std::size_t>(max_len), VecElementKind::Bool));
            break;
        }
        case OpCode::VecLenBool:
        {
            auto vec_value = pop();
            if (!vec_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (vec_value->kind != ValueKind::Vec || vec_value->vec_value == nullptr)
            {
                return finalize_result(err_result("vec value must be Vec", span));
            }
            if (vec_value->vec_value->element_kind != VecElementKind::Bool)
            {
                return finalize_result(err_result("vec value must be Vec<Bool>", span));
            }

            push(Value::int_v(static_cast<long long>(vec_value->vec_value->items.size())));
            break;
        }
        case OpCode::VecPushBool:
        {
            auto value = pop();
            auto vec_value = pop();
            if (!value.has_value() || !vec_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (vec_value->kind != ValueKind::Vec || vec_value->vec_value == nullptr)
            {
                return finalize_result(err_result("vec value must be Vec", span));
            }
            if (vec_value->vec_value->element_kind != VecElementKind::Bool)
            {
                return finalize_result(err_result("vec value must be Vec<Bool>", span));
            }
            if (value->kind != ValueKind::Bool)
            {
                return finalize_result(err_result("vec element must be Bool", span));
            }

            auto& items = vec_value->vec_value->items;
            if (items.size() >= vec_value->vec_value->max_len)
            {
                return finalize_result(err_result("vec capacity exceeded", span));
            }

            items.push_back(*value);
            push(Value::unit_v());
            break;
        }
        case OpCode::VecGetBool:
        {
            auto index = pop();
            auto vec_value = pop();
            if (!index.has_value() || !vec_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (vec_value->kind != ValueKind::Vec || vec_value->vec_value == nullptr)
            {
                return finalize_result(err_result("vec value must be Vec", span));
            }
            if (vec_value->vec_value->element_kind != VecElementKind::Bool)
            {
                return finalize_result(err_result("vec value must be Vec<Bool>", span));
            }
            if (index->kind != ValueKind::Int)
            {
                return finalize_result(err_result("vec index must be Int", span));
            }

            const auto i = index->int_value;
            if (i < 0 || static_cast<std::size_t>(i) >= vec_value->vec_value->items.size())
            {
                return finalize_result(err_result("vec index out of bounds", span));
            }

            push(vec_value->vec_value->items[static_cast<std::size_t>(i)]);
            break;
        }
        case OpCode::VecSetBool:
        {
            auto value = pop();
            auto index = pop();
            auto vec_value = pop();
            if (!value.has_value() || !index.has_value() || !vec_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (vec_value->kind != ValueKind::Vec || vec_value->vec_value == nullptr)
            {
                return finalize_result(err_result("vec value must be Vec", span));
            }
            if (vec_value->vec_value->element_kind != VecElementKind::Bool)
            {
                return finalize_result(err_result("vec value must be Vec<Bool>", span));
            }
            if (index->kind != ValueKind::Int)
            {
                return finalize_result(err_result("vec index must be Int", span));
            }
            if (value->kind != ValueKind::Bool)
            {
                return finalize_result(err_result("vec element must be Bool", span));
            }

            const auto i = index->int_value;
            if (i < 0 || static_cast<std::size_t>(i) >= vec_value->vec_value->items.size())
            {
                return finalize_result(err_result("vec index out of bounds", span));
            }

            vec_value->vec_value->items[static_cast<std::size_t>(i)] = *value;
            push(Value::unit_v());
            break;
        }
        case OpCode::SetNewInt:
            push(Value::set_v());
            break;
        case OpCode::SetHasInt:
        {
            auto value = pop();
            auto set_value = pop();
            if (!value.has_value() || !set_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (set_value->kind != ValueKind::Set || set_value->set_value == nullptr)
            {
                return finalize_result(err_result("set value must be Set", span));
            }
            if (value->kind != ValueKind::Int)
            {
                return finalize_result(err_result("set value must be Int", span));
            }

            push(Value::bool_v(set_value->set_value->items.contains(value->int_value)));
            break;
        }
        case OpCode::SetInsertInt:
        {
            auto value = pop();
            auto set_value = pop();
            if (!value.has_value() || !set_value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            if (set_value->kind != ValueKind::Set || set_value->set_value == nullptr)
            {
                return finalize_result(err_result("set value must be Set", span));
            }
            if (value->kind != ValueKind::Int)
            {
                return finalize_result(err_result("set value must be Int", span));
            }

            set_value->set_value->items.insert(value->int_value);
            push(Value::unit_v());
            break;
        }
        case OpCode::PythonCall:
        {
            if (!capabilities.contains("python.ffi"))
            {
                return finalize_result(err_result("python capability required", span));
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
                return finalize_result(err_result("python runner timed out", span));
            }
            if (proc.output_limit_exceeded)
            {
                return finalize_result(err_result("python runner output too large", span));
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
                return finalize_result(err_result(msg, span));
            }

            push(Value::unit_v());
            break;
        }
        case OpCode::MakeEnum:
        {
            const auto enum_name = read_string_constant();
            if (!enum_name.has_value())
            {
                return finalize_result(err_result("invalid enum constructor enum name", span));
            }

            const auto variant_name = read_string_constant();
            if (!variant_name.has_value())
            {
                return finalize_result(err_result("invalid enum constructor variant name", span));
            }

            if (ip >= chunk.code.size())
            {
                return finalize_result(err_result("truncated enum constructor", span));
            }

            const bool has_payload = chunk.code[ip++] != 0;
            if (!has_payload)
            {
                push(Value::enum_v(*enum_name, *variant_name));
                break;
            }

            auto payload = pop();
            if (!payload.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }
            push(Value::enum_v(*enum_name, *variant_name, std::move(*payload)));
            break;
        }
        case OpCode::EnumIs:
        {
            const auto enum_name = read_string_constant();
            if (!enum_name.has_value())
            {
                return finalize_result(err_result("invalid enum-is enum name", span));
            }

            const auto variant_name = read_string_constant();
            if (!variant_name.has_value())
            {
                return finalize_result(err_result("invalid enum-is variant name", span));
            }

            auto value = pop();
            if (!value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }

            const bool matches = value->kind == ValueKind::Enum && value->enum_name == *enum_name &&
                                 value->variant_name == *variant_name;
            push(Value::bool_v(matches));
            break;
        }
        case OpCode::EnumUnwrap:
        {
            const auto enum_name = read_string_constant();
            if (!enum_name.has_value())
            {
                return finalize_result(err_result("invalid enum-unwrap enum name", span));
            }

            const auto variant_name = read_string_constant();
            if (!variant_name.has_value())
            {
                return finalize_result(err_result("invalid enum-unwrap variant name", span));
            }

            auto value = pop();
            if (!value.has_value())
            {
                return finalize_result(err_result("stack underflow", span));
            }

            if (value->kind != ValueKind::Enum || value->enum_name != *enum_name || // GCOVR_EXCL_LINE
                value->variant_name != *variant_name)
            {
                return finalize_result(err_result("enum unwrap variant mismatch", span));
            }
            if (value->payload == nullptr)
            {
                return finalize_result(err_result("enum unwrap missing payload", span));
            }
            push(*value->payload);
            break;
        }
        }
    }

    return finalize_result(err_result("no return", std::nullopt));
}

} // namespace curlee::vm
