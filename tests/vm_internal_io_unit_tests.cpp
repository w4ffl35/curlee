#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

// White-box include to reach internal helpers in src/vm/vm.cpp.
#include "../src/vm/vm.cpp"

static void set_nonblocking_should_ignore_invalid_fd()
{
    // Should not crash.
    set_nonblocking(-1);
}

static void read_into_should_report_limit_when_already_full()
{
    int fds[2] = {-1, -1};
    if (pipe(fds) != 0)
    {
        fail(std::string("pipe failed: ") + std::strerror(errno));
    }

    const char data[] = "hello";
    const auto ignored = write(fds[1], data, sizeof(data));
    (void)ignored;
    close(fds[1]);

    std::string out;
    bool eof = false;
    bool limit_hit = false;
    std::size_t total = 10;
    read_into(fds[0], out, eof, total, 10, limit_hit);
    close(fds[0]);

    if (!limit_hit || !eof)
    {
        fail("expected limit_hit + eof when total_bytes >= max_total_bytes");
    }
}

static void read_into_should_partial_append_and_hit_limit()
{
    int fds[2] = {-1, -1};
    if (pipe(fds) != 0)
    {
        fail(std::string("pipe failed: ") + std::strerror(errno));
    }

    const char data[] = "abcdef";
    if (write(fds[1], data, sizeof(data) - 1) < 0)
    {
        fail(std::string("write failed: ") + std::strerror(errno));
    }
    close(fds[1]);

    std::string out;
    bool eof = false;
    bool limit_hit = false;
    std::size_t total = 8;
    read_into(fds[0], out, eof, total, 10, limit_hit);
    close(fds[0]);

    if (!limit_hit || !eof)
    {
        fail("expected limit_hit + eof on partial append");
    }
    if (out.size() != 2)
    {
        fail("expected exactly remaining bytes to append");
    }
}

static void read_into_should_return_eagain_without_eof()
{
    int fds[2] = {-1, -1};
    if (pipe(fds) != 0)
    {
        fail(std::string("pipe failed: ") + std::strerror(errno));
    }

    // Keep writer open so the read end sees EAGAIN instead of EOF.
    set_nonblocking(fds[0]);

    std::string out;
    bool eof = false;
    bool limit_hit = false;
    std::size_t total = 0;
    read_into(fds[0], out, eof, total, 10, limit_hit);

    close(fds[1]);
    close(fds[0]);

    if (eof)
    {
        fail("expected no eof on EAGAIN");
    }
    if (!out.empty())
    {
        fail("expected no output on EAGAIN");
    }
    if (limit_hit)
    {
        fail("did not expect limit_hit on EAGAIN");
    }
}

static void read_into_should_set_eof_on_bad_fd()
{
    std::string out;
    bool eof = false;
    bool limit_hit = false;
    std::size_t total = 0;

    errno = 0;
    read_into(-1, out, eof, total, 10, limit_hit);

    if (!eof)
    {
        fail("expected eof on bad fd");
    }
}

static void vm_internal_helpers_should_cover_path_and_escape_branches()
{
    // Path helper success/false branches.
    if (!is_valid_fs_path("ok/file.txt"))
    {
        fail("expected simple relative fs path to be valid");
    }
    if (is_valid_fs_path(".."))
    {
        fail("expected parent path to be invalid");
    }
    if (is_valid_fs_path("a/./b"))
    {
        fail("expected dot path component to be invalid");
    }

    // Status error-code branches should conservatively allow (helper returns true).
    const std::filesystem::path too_long(std::string(8192, 'a'));
    if (!has_owner_read_permission(too_long))
    {
        fail("expected read permission helper to allow on status error");
    }
    if (!has_owner_write_permission(too_long))
    {
        fail("expected write permission helper to allow on status error");
    }

    // Escape helper should quote backslashes and quotes only.
    const std::string escaped = escape_tty_text("a\\\"b");
    if (escaped != "a\\\\\\\"b")
    {
        fail("expected deterministic tty text escaping");
    }
}

static void vm_seeded_run_wrapper_should_fill_profile_fields()
{
    using namespace curlee::vm;

    // Success path: profile uses the provided fuel as remaining when no out-of-fuel error.
    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1));
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk, 7, VM::Capabilities{}, std::uint64_t{123});
        if (!res.ok)
        {
            fail("expected seeded wrapper run to succeed");
        }
        if (res.profile.fuel_limit != 7 || res.profile.fuel_used != 0 ||
            res.profile.fuel_remaining != 7 || !res.profile.rng_seed.has_value() ||
            *res.profile.rng_seed != std::uint64_t{123})
        {
            fail("expected seeded wrapper profile fields on success");
        }
    }

    // Out-of-fuel path: profile should report all fuel consumed.
    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1));
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk, 0, VM::Capabilities{}, std::uint64_t{9});
        if (res.ok || res.error != "out of fuel")
        {
            fail("expected seeded wrapper out-of-fuel error");
        }
        if (res.profile.fuel_limit != 0 || res.profile.fuel_used != 0 ||
            res.profile.fuel_remaining != 0 || !res.profile.rng_seed.has_value() ||
            *res.profile.rng_seed != std::uint64_t{9})
        {
            fail("expected seeded wrapper profile fields on out-of-fuel");
        }
    }
}

static void vm_value_inline_helpers_should_cover_remaining_branches()
{
    using namespace curlee::vm;

    Value a = Value::struct_v("S", {{"x", Value::int_v(1)}});
    Value b = Value::struct_v("S", {{"x", Value::int_v(1)}});
    a.struct_fields[0].second.reset();
    if (a == b)
    {
        fail("expected struct null/non-null field values to compare unequal");
    }

    Value c = Value::struct_v("S", {{"x", Value::int_v(1)}});
    Value d = Value::struct_v("S", {{"x", Value::int_v(1)}});
    c.struct_fields[0].second.reset();
    d.struct_fields[0].second.reset();
    if (!(c == d))
    {
        fail("expected struct null/null field values to compare equal");
    }

    const std::string vec_bool = to_string(Value::vec_v(2, VecElementKind::Bool));
    if (vec_bool.find("Vec<Bool>") == std::string::npos)
    {
        fail("expected Vec<Bool> formatting in Value::to_string");
    }

    const std::string vec_int = to_string(Value::vec_v(2, VecElementKind::Int));
    if (vec_int.find("Vec<Int>") == std::string::npos)
    {
        fail("expected Vec<Int> formatting in Value::to_string");
    }
}

static void vm_should_fail_out_of_fuel()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit(OpCode::Return);
    VM vm;
    const auto res = vm.run(chunk, 0);
    if (res.ok || res.error != "out of fuel")
    {
        fail("expected out of fuel");
    }
}

static void vm_should_fail_truncated_constant_without_span()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.code.push_back(static_cast<std::uint8_t>(OpCode::Constant));
    // Intentionally omit spans.
    VM vm;
    const auto res = vm.run(chunk);
    if (res.ok || res.error != "truncated constant" || res.error_span.has_value())
    {
        fail("expected truncated constant with no span");
    }
}

static void vm_should_fail_constant_index_out_of_range()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit(OpCode::Constant);
    chunk.emit_u16(99);
    chunk.emit(OpCode::Return);
    VM vm;
    const auto res = vm.run(chunk);
    if (res.ok || res.error != "constant index out of range")
    {
        fail("expected constant index out of range");
    }
}

static void vm_should_fail_truncated_local_index()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit(OpCode::LoadLocal);
    VM vm;
    const auto res = vm.run(chunk);
    if (res.ok || res.error != "truncated local index")
    {
        fail("expected truncated local index");
    }
}

static void vm_should_fail_local_index_out_of_range()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.max_locals = 1;
    chunk.emit(OpCode::LoadLocal);
    chunk.emit_u16(2);
    chunk.emit(OpCode::Return);
    VM vm;
    const auto res = vm.run(chunk);
    if (res.ok || res.error != "local index out of range")
    {
        fail("expected local index out of range");
    }
}

static void vm_should_fail_add_type_error()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::bool_v(true));
    chunk.emit_constant(Value::int_v(1));
    chunk.emit(OpCode::Add);
    chunk.emit(OpCode::Return);
    VM vm;
    const auto res = vm.run(chunk);
    if (res.ok || res.error != "add expects Int or String")
    {
        fail("expected add type error");
    }
}

static void vm_should_fail_print_missing_capability()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit(OpCode::Print);
    chunk.emit(OpCode::Return);
    VM vm;
    const auto res = vm.run(chunk);
    if (res.ok || res.error != "missing capability io.stdout")
    {
        fail("expected missing capability io.stdout");
    }
}

static void vm_should_fail_read_line_missing_capability()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::ReadLine);
    chunk.emit(OpCode::Return);
    VM vm;
    const auto res = vm.run(chunk);
    if (res.ok || res.error != "missing capability io.stdin")
    {
        fail("expected missing capability io.stdin");
    }
}

static void vm_read_line_should_return_one_line_without_newline()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::ReadLine);
    chunk.emit(OpCode::Return);

    std::istringstream in("hello world\nnext line\n");
    std::streambuf* old_in = std::cin.rdbuf(in.rdbuf());

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.stdin");
    const auto res = vm.run(chunk, caps);

    std::cin.rdbuf(old_in);

    if (!res.ok || !(res.value == Value::string_v("hello world")))
    {
        fail("expected ReadLine to return one line without newline");
    }
}

static void vm_read_line_should_fail_when_line_exceeds_limit()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::ReadLine);
    chunk.emit(OpCode::Return);

    std::string long_line(4097, 'a');
    long_line.push_back('\n');
    std::istringstream in(long_line);
    std::streambuf* old_in = std::cin.rdbuf(in.rdbuf());

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.stdin");
    const auto res = vm.run(chunk, caps);

    std::cin.rdbuf(old_in);

    if (res.ok || res.error != "stdin line too long")
    {
        fail("expected stdin line too long when ReadLine exceeds limit");
    }
}

static void vm_read_line_should_return_empty_string_on_eof()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::ReadLine);
    chunk.emit(OpCode::Return);

    std::istringstream in("");
    std::streambuf* old_in = std::cin.rdbuf(in.rdbuf());
    std::cin.clear();

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.stdin");
    const auto res = vm.run(chunk, caps);

    std::cin.rdbuf(old_in);

    if (!res.ok || !(res.value == Value::string_v("")))
    {
        fail("expected ReadLine to return empty string on EOF");
    }
}

static void vm_read_line_should_fail_on_stack_underflow()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit(OpCode::ReadLine);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.stdin");
    const auto res = vm.run(chunk, caps);

    if (res.ok || res.error != "stack underflow")
    {
        fail("expected stack underflow when ReadLine has no capability argument value");
    }
}

static void vm_should_fail_fs_read_missing_capability()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::string_v("missing.txt"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsReadText);
    chunk.emit(OpCode::Return);

    VM vm;
    const auto res = vm.run(chunk);
    if (res.ok || res.error != "missing capability fs.read")
    {
        fail("expected missing capability fs.read");
    }
}

static void vm_should_fail_fs_write_missing_capability()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::string_v("out.txt"));
    chunk.emit_constant(Value::string_v("hello"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsWriteText);
    chunk.emit(OpCode::Return);

    VM vm;
    const auto res = vm.run(chunk);
    if (res.ok || res.error != "missing capability fs.write")
    {
        fail("expected missing capability fs.write");
    }
}

static void vm_fs_should_reject_invalid_path()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::string_v("../secret.txt"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsReadText);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("fs.read");
    const auto res = vm.run(chunk, caps);
    if (res.ok || res.error != "invalid fs path")
    {
        fail("expected invalid fs path for traversal");
    }
}

static void vm_fs_should_reject_empty_and_absolute_paths()
{
    using namespace curlee::vm;

    {
        Chunk chunk;
        chunk.emit_constant(Value::string_v(""));
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::FsReadText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.read");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "invalid fs path")
        {
            fail("expected invalid fs path for empty path");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::string_v("/tmp/abs.txt"));
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::FsReadText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.read");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "invalid fs path")
        {
            fail("expected invalid fs path for absolute path");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::string_v("."));
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::FsReadText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.read");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "invalid fs path")
        {
            fail("expected invalid fs path for dot path");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::string_v(std::string(513, 'a')));
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::FsReadText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.read");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "invalid fs path")
        {
            fail("expected invalid fs path for oversized path");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::string_v(std::string("a\0b", 3)));
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::FsReadText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.read");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "invalid fs path")
        {
            fail("expected invalid fs path for embedded NUL");
        }
    }
}

static void vm_fs_read_should_fail_on_stack_underflow_and_non_string_path()
{
    using namespace curlee::vm;

    {
        Chunk chunk;
        chunk.emit(OpCode::FsReadText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.read");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow when FsReadText has no operands");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::FsReadText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.read");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow when FsReadText is missing path value");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1));
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::FsReadText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.read");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "fs path must be String")
        {
            fail("expected fs path must be String for FsReadText");
        }
    }
}

static void vm_fs_read_should_fail_missing_file()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::string_v("does-not-exist.txt"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsReadText);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("fs.read");
    const auto res = vm.run(chunk, caps);
    if (res.ok || res.error != "fs file not found")
    {
        fail("expected fs file not found");
    }
}

static void vm_fs_write_should_fail_when_content_exceeds_limit()
{
    using namespace curlee::vm;
    Chunk chunk;
    std::string content((1 * 1024 * 1024) + 1, 'x');
    chunk.emit_constant(Value::string_v("large.txt"));
    chunk.emit_constant(Value::string_v(content));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsWriteText);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("fs.write");
    const auto res = vm.run(chunk, caps);
    if (res.ok || res.error != "fs content too large")
    {
        fail("expected fs content too large");
    }
}

static void vm_fs_read_should_fail_when_file_exceeds_limit()
{
    using namespace curlee::vm;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() /
                         ("curlee_vm_fs_large_" + std::to_string(static_cast<long long>(::getpid())));
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (!fs::create_directories(dir, ec) || ec)
    {
        fail("failed to create temp directory for fs large-file test");
    }

    const fs::path old_cwd = fs::current_path(ec);
    if (ec)
    {
        fail("failed to read current working directory");
    }

    fs::current_path(dir, ec);
    if (ec)
    {
        fail("failed to set working directory for fs large-file test");
    }

    {
        std::ofstream out("too-large.txt", std::ios::binary | std::ios::trunc);
        out << std::string((1 * 1024 * 1024) + 1, 'a');
    }

    Chunk chunk;
    chunk.emit_constant(Value::string_v("too-large.txt"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsReadText);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("fs.read");
    const auto res = vm.run(chunk, caps);

    fs::current_path(old_cwd, ec);
    fs::remove_all(dir, ec);

    if (res.ok || res.error != "fs file too large")
    {
        fail("expected fs file too large");
    }
}

static void vm_fs_read_should_fail_for_directory_path()
{
    using namespace curlee::vm;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() /
                         ("curlee_vm_fs_dir_" + std::to_string(static_cast<long long>(::getpid())));
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (!fs::create_directories(dir, ec) || ec)
    {
        fail("failed to create temp directory for fs directory-read test");
    }

    const fs::path old_cwd = fs::current_path(ec);
    if (ec)
    {
        fail("failed to read current working directory");
    }

    fs::current_path(dir, ec);
    if (ec)
    {
        fail("failed to set working directory for fs directory-read test");
    }

    if (!fs::create_directories("just_a_dir", ec) || ec)
    {
        fs::current_path(old_cwd, ec);
        fs::remove_all(dir, ec);
        fail("failed to create child directory for fs directory-read test");
    }

    Chunk chunk;
    chunk.emit_constant(Value::string_v("just_a_dir"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsReadText);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("fs.read");
    const auto res = vm.run(chunk, caps);

    fs::current_path(old_cwd, ec);
    fs::remove_all(dir, ec);

    if (res.ok || res.error != "fs read failed")
    {
        fail("expected fs read failed when FsReadText targets a directory");
    }
}

static void vm_fs_roundtrip_should_succeed()
{
    using namespace curlee::vm;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() /
                         ("curlee_vm_fs_roundtrip_" + std::to_string(static_cast<long long>(::getpid())));
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (!fs::create_directories(dir, ec) || ec)
    {
        fail("failed to create temp directory for fs roundtrip test");
    }

    const fs::path old_cwd = fs::current_path(ec);
    if (ec)
    {
        fail("failed to read current working directory");
    }

    fs::current_path(dir, ec);
    if (ec)
    {
        fail("failed to set working directory for fs roundtrip test");
    }

    Chunk write_chunk;
    write_chunk.emit_constant(Value::string_v("roundtrip.txt"));
    write_chunk.emit_constant(Value::string_v("hello fs"));
    write_chunk.emit_constant(Value::unit_v());
    write_chunk.emit(OpCode::FsWriteText);
    write_chunk.emit(OpCode::Return);

    VM writer;
    VM::Capabilities write_caps;
    write_caps.insert("fs.write");
    const auto write_res = writer.run(write_chunk, write_caps);
    if (!write_res.ok || !(write_res.value == Value::unit_v()))
    {
        fs::current_path(old_cwd, ec);
        fs::remove_all(dir, ec);
        fail("expected fs write roundtrip step to succeed");
    }

    Chunk read_chunk;
    read_chunk.emit_constant(Value::string_v("roundtrip.txt"));
    read_chunk.emit_constant(Value::unit_v());
    read_chunk.emit(OpCode::FsReadText);
    read_chunk.emit(OpCode::Return);

    VM reader;
    VM::Capabilities read_caps;
    read_caps.insert("fs.read");
    const auto read_res = reader.run(read_chunk, read_caps);

    fs::current_path(old_cwd, ec);
    fs::remove_all(dir, ec);

    if (!read_res.ok || !(read_res.value == Value::string_v("hello fs")))
    {
        fail("expected fs roundtrip read to return written content");
    }
}

static void vm_fs_read_empty_file_should_succeed()
{
    using namespace curlee::vm;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() /
                         ("curlee_vm_fs_empty_" + std::to_string(static_cast<long long>(::getpid())));
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (!fs::create_directories(dir, ec) || ec)
    {
        fail("failed to create temp directory for fs empty-file test");
    }

    const fs::path old_cwd = fs::current_path(ec);
    if (ec)
    {
        fail("failed to read current working directory");
    }

    fs::current_path(dir, ec);
    if (ec)
    {
        fail("failed to set working directory for fs empty-file test");
    }

    {
        std::ofstream out("empty.txt", std::ios::binary | std::ios::trunc);
    }

    Chunk chunk;
    chunk.emit_constant(Value::string_v("empty.txt"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsReadText);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("fs.read");
    const auto res = vm.run(chunk, caps);

    fs::current_path(old_cwd, ec);
    fs::remove_all(dir, ec);

    if (!res.ok || !(res.value == Value::string_v("")))
    {
        fail("expected empty string when FsReadText reads empty file");
    }
}

static void vm_fs_read_should_fail_when_ifstream_open_is_denied()
{
    using namespace curlee::vm;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() /
                         ("curlee_vm_fs_open_denied_" +
                          std::to_string(static_cast<long long>(::getpid())));
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (!fs::create_directories(dir, ec) || ec)
    {
        fail("failed to create temp directory for fs open-denied test");
    }

    const fs::path old_cwd = fs::current_path(ec);
    if (ec)
    {
        fail("failed to read current working directory");
    }

    fs::current_path(dir, ec);
    if (ec)
    {
        fail("failed to set working directory for fs open-denied test");
    }

    fs::create_symlink("/proc/kmsg", "kmsg.txt", ec);
    if (ec)
    {
        fs::current_path(old_cwd, ec);
        fs::remove_all(dir, ec);
        fail("failed to create /proc/kmsg symlink for fs open-denied test");
    }

    Chunk chunk;
    chunk.emit_constant(Value::string_v("kmsg.txt"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsReadText);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("fs.read");
    const auto res = vm.run(chunk, caps);

    fs::current_path(old_cwd, ec);
    fs::remove_all(dir, ec);

    if (res.ok || res.error != "fs access denied")
    {
        fail("expected fs access denied when ifstream open fails for FsReadText");
    }
}

static void vm_fs_write_should_fail_when_stream_write_reports_error()
{
    using namespace curlee::vm;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() /
                         ("curlee_vm_fs_write_stream_err_" +
                          std::to_string(static_cast<long long>(::getpid())));
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (!fs::create_directories(dir, ec) || ec)
    {
        fail("failed to create temp directory for fs stream-write-error test");
    }

    const fs::path old_cwd = fs::current_path(ec);
    if (ec)
    {
        fail("failed to read current working directory");
    }

    fs::current_path(dir, ec);
    if (ec)
    {
        fail("failed to set working directory for fs stream-write-error test");
    }

    fs::create_symlink("/dev/full", "full.txt", ec);
    if (ec)
    {
        fs::current_path(old_cwd, ec);
        fs::remove_all(dir, ec);
        fail("failed to create /dev/full symlink for fs stream-write-error test");
    }

    Chunk chunk;
    chunk.emit_constant(Value::string_v("full.txt"));
    chunk.emit_constant(Value::string_v(std::string(1 * 1024 * 1024, 'x')));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsWriteText);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("fs.write");
    const auto res = vm.run(chunk, caps);

    fs::current_path(old_cwd, ec);
    fs::remove_all(dir, ec);

    if (res.ok || res.error != "fs write failed")
    {
        fail("expected fs write failed when stream write reports error");
    }
}

static void vm_fs_write_should_succeed_with_existing_writable_parent()
{
    using namespace curlee::vm;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() /
                         ("curlee_vm_fs_parent_ok_" +
                          std::to_string(static_cast<long long>(::getpid())));
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (!fs::create_directories(dir / "sub", ec) || ec)
    {
        fail("failed to create temp directory for writable-parent test");
    }

    const fs::path old_cwd = fs::current_path(ec);
    if (ec)
    {
        fail("failed to read current working directory");
    }

    fs::current_path(dir, ec);
    if (ec)
    {
        fail("failed to set working directory for writable-parent test");
    }

    Chunk chunk;
    chunk.emit_constant(Value::string_v("sub/out.txt"));
    chunk.emit_constant(Value::string_v("ok"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsWriteText);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("fs.write");
    const auto res = vm.run(chunk, caps);

    fs::current_path(old_cwd, ec);
    fs::remove_all(dir, ec);

    if (!res.ok || !(res.value == Value::unit_v()))
    {
        fail("expected FsWriteText success with existing writable parent");
    }
}

static void vm_fs_read_should_fail_for_fifo_file_size_error()
{
    using namespace curlee::vm;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() /
                         ("curlee_vm_fs_fifo_" +
                          std::to_string(static_cast<long long>(::getpid())));
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (!fs::create_directories(dir, ec) || ec)
    {
        fail("failed to create temp directory for fifo fs-read test");
    }

    const fs::path old_cwd = fs::current_path(ec);
    if (ec)
    {
        fail("failed to read current working directory");
    }

    fs::current_path(dir, ec);
    if (ec)
    {
        fail("failed to set working directory for fifo fs-read test");
    }

    if (::mkfifo("fifo.txt", 0644) != 0)
    {
        fs::current_path(old_cwd, ec);
        fs::remove_all(dir, ec);
        fail("failed to create fifo for fs-read test");
    }

    Chunk chunk;
    chunk.emit_constant(Value::string_v("fifo.txt"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsReadText);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("fs.read");
    const auto res = vm.run(chunk, caps);

    fs::current_path(old_cwd, ec);
    fs::remove_all(dir, ec);

    if (res.ok || res.error != "fs read failed")
    {
        fail("expected fs read failed when file_size() errors on FIFO");
    }
}

static void vm_fs_write_should_fail_with_access_denied()
{
    using namespace curlee::vm;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() /
                         ("curlee_vm_fs_denied_" + std::to_string(static_cast<long long>(::getpid())));
    const fs::path locked = dir / "locked";
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (!fs::create_directories(locked, ec) || ec)
    {
        fail("failed to create temp directory for fs denied test");
    }

    const fs::path old_cwd = fs::current_path(ec);
    if (ec)
    {
        fail("failed to read current working directory");
    }

    fs::current_path(dir, ec);
    if (ec)
    {
        fail("failed to set working directory for fs denied test");
    }

    if (::chmod("locked", 0555) != 0)
    {
        fs::current_path(old_cwd, ec);
        fs::remove_all(dir, ec);
        fail("failed to chmod locked directory");
    }

    Chunk chunk;
    chunk.emit_constant(Value::string_v("locked/out.txt"));
    chunk.emit_constant(Value::string_v("x"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsWriteText);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("fs.write");
    const auto res = vm.run(chunk, caps);

    (void)::chmod("locked", 0755);
    fs::current_path(old_cwd, ec);
    fs::remove_all(dir, ec);

    if (res.ok || res.error != "fs access denied")
    {
        fail("expected fs access denied for write into non-writable directory");
    }
}

static void vm_fs_read_should_fail_with_access_denied()
{
    using namespace curlee::vm;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() /
                         ("curlee_vm_fs_read_denied_" + std::to_string(static_cast<long long>(::getpid())));
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (!fs::create_directories(dir, ec) || ec)
    {
        fail("failed to create temp directory for fs read denied test");
    }

    const fs::path old_cwd = fs::current_path(ec);
    if (ec)
    {
        fail("failed to read current working directory");
    }

    fs::current_path(dir, ec);
    if (ec)
    {
        fail("failed to set working directory for fs read denied test");
    }

    {
        std::ofstream out("locked.txt", std::ios::binary | std::ios::trunc);
        out << "x";
    }
    if (::chmod("locked.txt", 0000) != 0)
    {
        fs::current_path(old_cwd, ec);
        fs::remove_all(dir, ec);
        fail("failed to chmod file for fs read denied test");
    }

    Chunk chunk;
    chunk.emit_constant(Value::string_v("locked.txt"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsReadText);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("fs.read");
    const auto res = vm.run(chunk, caps);

    (void)::chmod("locked.txt", 0644);
    fs::current_path(old_cwd, ec);
    fs::remove_all(dir, ec);

    if (res.ok || res.error != "fs access denied")
    {
        fail("expected fs access denied for reading non-readable file");
    }
}

static void vm_fs_read_should_fail_when_exists_sets_error_code()
{
    using namespace curlee::vm;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() /
                         ("curlee_vm_fs_exists_ec_" +
                          std::to_string(static_cast<long long>(::getpid())));
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (!fs::create_directories(dir, ec) || ec)
    {
        fail("failed to create temp directory for fs exists-ec test");
    }

    const fs::path old_cwd = fs::current_path(ec);
    if (ec)
    {
        fail("failed to read current working directory");
    }

    fs::current_path(dir, ec);
    if (ec)
    {
        fail("failed to set working directory for fs exists-ec test");
    }

    if (!fs::create_directories("locked", ec) || ec)
    {
        fs::current_path(old_cwd, ec);
        fs::remove_all(dir, ec);
        fail("failed to create locked directory for fs exists-ec test");
    }

    if (::chmod("locked", 0000) != 0)
    {
        fs::current_path(old_cwd, ec);
        fs::remove_all(dir, ec);
        fail("failed to chmod locked directory for fs exists-ec test");
    }

    Chunk chunk;
    chunk.emit_constant(Value::string_v("locked/nope.txt"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::FsReadText);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("fs.read");
    const auto res = vm.run(chunk, caps);

    (void)::chmod("locked", 0755);
    fs::current_path(old_cwd, ec);
    fs::remove_all(dir, ec);

    if (res.ok || res.error != "fs read failed")
    {
        fail("expected fs read failed when exists() reports error");
    }
}

static void vm_fs_write_should_fail_on_stack_underflow_and_non_string_values()
{
    using namespace curlee::vm;

    {
        Chunk chunk;
        chunk.emit(OpCode::FsWriteText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.write");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow when FsWriteText has no operands");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::FsWriteText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.write");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow when FsWriteText is missing content/path");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::string_v("out.txt"));
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::FsWriteText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.write");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow when FsWriteText is missing path");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1));
        chunk.emit_constant(Value::string_v("x"));
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::FsWriteText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.write");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "fs path must be String")
        {
            fail("expected fs path must be String for FsWriteText");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::string_v("out.txt"));
        chunk.emit_constant(Value::int_v(7));
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::FsWriteText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.write");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "fs content must be String")
        {
            fail("expected fs content must be String for FsWriteText");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::string_v(".."));
        chunk.emit_constant(Value::string_v("x"));
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::FsWriteText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.write");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "invalid fs path")
        {
            fail("expected invalid fs path for FsWriteText");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::string_v("missing_parent/out.txt"));
        chunk.emit_constant(Value::string_v("x"));
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::FsWriteText);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("fs.write");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "fs access denied")
        {
            fail("expected fs access denied for FsWriteText when parent directory is missing");
        }
    }
}

static void vm_should_fail_tty_missing_capability()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyClear);
    chunk.emit(OpCode::Return);
    VM vm;
    const auto res = vm.run(chunk);
    if (res.ok || res.error != "missing capability io.tty")
    {
        fail("expected missing capability io.tty");
    }
}

static void vm_tty_should_enforce_coordinate_bounds()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::int_v(-1));
    chunk.emit_constant(Value::int_v(0));
    chunk.emit_constant(Value::string_v("x"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyWriteAt);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.tty");
    const auto res = vm.run(chunk, caps);
    if (res.ok || res.error != "tty coordinates must be >= 0")
    {
        fail("expected deterministic tty bounds error");
    }
}

static void vm_tty_clear_should_fail_on_stack_underflow()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit(OpCode::TtyClear);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.tty");
    const auto res = vm.run(chunk, caps);

    if (res.ok || res.error != "stack underflow")
    {
        fail("expected stack underflow for TtyClear without argument value");
    }
}

static void vm_tty_write_at_should_fail_missing_capability()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::int_v(0));
    chunk.emit_constant(Value::int_v(0));
    chunk.emit_constant(Value::string_v("x"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyWriteAt);
    chunk.emit(OpCode::Return);

    VM vm;
    const auto res = vm.run(chunk);
    if (res.ok || res.error != "missing capability io.tty")
    {
        fail("expected missing capability io.tty for TtyWriteAt");
    }
}

static void vm_tty_write_at_should_fail_on_stack_underflow()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit(OpCode::TtyWriteAt);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.tty");
    const auto res = vm.run(chunk, caps);

    if (res.ok || res.error != "stack underflow")
    {
        fail("expected stack underflow for TtyWriteAt without arguments");
    }
}

static void vm_tty_write_at_should_fail_when_text_missing()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyWriteAt);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.tty");
    const auto res = vm.run(chunk, caps);

    if (res.ok || res.error != "stack underflow")
    {
        fail("expected stack underflow for TtyWriteAt when text is missing");
    }
}

static void vm_tty_write_at_should_fail_when_col_missing()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::string_v("x"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyWriteAt);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.tty");
    const auto res = vm.run(chunk, caps);

    if (res.ok || res.error != "stack underflow")
    {
        fail("expected stack underflow for TtyWriteAt when column is missing");
    }
}

static void vm_tty_write_at_should_fail_when_row_missing()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::int_v(0));
    chunk.emit_constant(Value::string_v("x"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyWriteAt);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.tty");
    const auto res = vm.run(chunk, caps);

    if (res.ok || res.error != "stack underflow")
    {
        fail("expected stack underflow for TtyWriteAt when row is missing");
    }
}

static void vm_tty_write_at_should_fail_non_int_coordinates()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::bool_v(true));
    chunk.emit_constant(Value::int_v(0));
    chunk.emit_constant(Value::string_v("x"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyWriteAt);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.tty");
    const auto res = vm.run(chunk, caps);

    if (res.ok || res.error != "tty coordinates must be Int")
    {
        fail("expected tty coordinates Int-type error");
    }
}

static void vm_tty_write_at_should_fail_non_int_column()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::int_v(0));
    chunk.emit_constant(Value::bool_v(true));
    chunk.emit_constant(Value::string_v("x"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyWriteAt);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.tty");
    const auto res = vm.run(chunk, caps);

    if (res.ok || res.error != "tty coordinates must be Int")
    {
        fail("expected tty column Int-type error");
    }
}

static void vm_tty_write_at_should_fail_non_string_text()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::int_v(0));
    chunk.emit_constant(Value::int_v(0));
    chunk.emit_constant(Value::int_v(1));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyWriteAt);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.tty");
    const auto res = vm.run(chunk, caps);

    if (res.ok || res.error != "tty text must be String")
    {
        fail("expected tty text String-type error");
    }
}

static void vm_tty_write_at_should_fail_upper_bound()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::int_v(1000));
    chunk.emit_constant(Value::int_v(0));
    chunk.emit_constant(Value::string_v("x"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyWriteAt);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.tty");
    const auto res = vm.run(chunk, caps);

    if (res.ok || res.error != "tty coordinates out of bounds (max 999)")
    {
        fail("expected deterministic tty upper-bounds error");
    }
}

static void vm_tty_write_at_should_fail_negative_column()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::int_v(0));
    chunk.emit_constant(Value::int_v(-1));
    chunk.emit_constant(Value::string_v("x"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyWriteAt);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.tty");
    const auto res = vm.run(chunk, caps);

    if (res.ok || res.error != "tty coordinates must be >= 0")
    {
        fail("expected deterministic tty negative-column error");
    }
}

static void vm_tty_write_at_should_fail_column_upper_bound()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::int_v(0));
    chunk.emit_constant(Value::int_v(1000));
    chunk.emit_constant(Value::string_v("x"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyWriteAt);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.tty");
    const auto res = vm.run(chunk, caps);

    if (res.ok || res.error != "tty coordinates out of bounds (max 999)")
    {
        fail("expected deterministic tty column upper-bounds error");
    }
}

static void vm_tty_flush_should_fail_missing_capability()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyFlush);
    chunk.emit(OpCode::Return);

    VM vm;
    const auto res = vm.run(chunk);
    if (res.ok || res.error != "missing capability io.tty")
    {
        fail("expected missing capability io.tty for TtyFlush");
    }
}

static void vm_tty_flush_should_fail_on_stack_underflow()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit(OpCode::TtyFlush);
    chunk.emit(OpCode::Return);

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.tty");
    const auto res = vm.run(chunk, caps);

    if (res.ok || res.error != "stack underflow")
    {
        fail("expected stack underflow for TtyFlush without argument value");
    }
}

static void vm_tty_should_flush_in_deterministic_order()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyClear);
    chunk.emit(OpCode::Pop);

    chunk.emit_constant(Value::int_v(0));
    chunk.emit_constant(Value::int_v(0));
    chunk.emit_constant(Value::string_v("A"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyWriteAt);
    chunk.emit(OpCode::Pop);

    chunk.emit_constant(Value::int_v(0));
    chunk.emit_constant(Value::int_v(1));
    chunk.emit_constant(Value::string_v("B"));
    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyWriteAt);
    chunk.emit(OpCode::Pop);

    chunk.emit_constant(Value::unit_v());
    chunk.emit(OpCode::TtyFlush);
    chunk.emit(OpCode::Pop);
    chunk.emit_constant(Value::int_v(0));
    chunk.emit(OpCode::Return);

    std::ostringstream captured_out;
    std::streambuf* old_cout = std::cout.rdbuf(captured_out.rdbuf());

    VM vm;
    VM::Capabilities caps;
    caps.insert("io.tty");
    const auto res = vm.run(chunk, caps);

    std::cout.rdbuf(old_cout);

    if (!res.ok || !(res.value == Value::int_v(0)))
    {
        fail("expected tty flush program to succeed");
    }

    const std::string expected = "[tty.clear]\n"
                                 "[tty.write_at row=0 col=0 text=\"A\"]\n"
                                 "[tty.write_at row=0 col=1 text=\"B\"]\n";
    if (captured_out.str() != expected)
    {
        fail("expected deterministic tty output ordering");
    }
}

static void vm_should_fail_no_return_at_end()
{
    using namespace curlee::vm;
    Chunk chunk;
    chunk.emit_constant(Value::int_v(1));
    VM vm;
    const auto res = vm.run(chunk);
    if (res.ok || res.error != "no return")
    {
        fail("expected no return");
    }
}

static void value_enum_equality_and_to_string()
{
    using namespace curlee::vm;

    const Value some1 = Value::enum_v("Maybe", "Some", Value::int_v(1));
    const Value some1_b = Value::enum_v("Maybe", "Some", Value::int_v(1));
    const Value some2 = Value::enum_v("Maybe", "Some", Value::int_v(2));
    const Value none = Value::enum_v("Maybe", "None");
    const Value other_enum = Value::enum_v("Other", "Some", Value::int_v(1));

    if (!(some1 == some1_b))
    {
        fail("expected equal enum values with equal payload");
    }
    if (some1 == some2)
    {
        fail("expected enum payload mismatch to compare unequal");
    }
    if (some1 == Value::enum_v("Maybe", "Some"))
    {
        fail("expected enum payload-presence mismatch to compare unequal");
    }
    if (some1 == other_enum)
    {
        fail("expected enum name mismatch to compare unequal");
    }
    if (curlee::vm::to_string(some1) != "Maybe::Some(1)")
    {
        fail("expected payload enum string form");
    }
    if (curlee::vm::to_string(none) != "Maybe::None")
    {
        fail("expected no-payload enum string form");
    }
}

static void vm_enum_ops_success_paths()
{
    using namespace curlee::vm;
    Chunk chunk;

    const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
    const auto some_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Some")));

    chunk.emit_constant(Value::int_v(7));
    chunk.emit(OpCode::MakeEnum);
    chunk.emit_u16(maybe_idx);
    chunk.emit_u16(some_idx);
    chunk.code.push_back(1);
    chunk.spans.push_back({});

    chunk.emit(OpCode::EnumIs);
    chunk.emit_u16(maybe_idx);
    chunk.emit_u16(some_idx);
    chunk.emit(OpCode::Return);

    VM vm;
    const auto is_res = vm.run(chunk);
    if (!is_res.ok || !(is_res.value == Value::bool_v(true)))
    {
        fail("expected enum-is true for matching variant");
    }

    Chunk unwrap_chunk;
    const auto maybe2_idx =
        static_cast<std::uint16_t>(unwrap_chunk.add_constant(Value::string_v("Maybe")));
    const auto some2_idx =
        static_cast<std::uint16_t>(unwrap_chunk.add_constant(Value::string_v("Some")));
    unwrap_chunk.emit_constant(Value::int_v(9));
    unwrap_chunk.emit(OpCode::MakeEnum);
    unwrap_chunk.emit_u16(maybe2_idx);
    unwrap_chunk.emit_u16(some2_idx);
    unwrap_chunk.code.push_back(1);
    unwrap_chunk.spans.push_back({});
    unwrap_chunk.emit(OpCode::EnumUnwrap);
    unwrap_chunk.emit_u16(maybe2_idx);
    unwrap_chunk.emit_u16(some2_idx);
    unwrap_chunk.emit(OpCode::Return);

    const auto unwrap_res = vm.run(unwrap_chunk);
    if (!unwrap_res.ok || !(unwrap_res.value == Value::int_v(9)))
    {
        fail("expected enum unwrap to return payload");
    }
}

static void vm_enum_ops_error_paths()
{
    using namespace curlee::vm;

    {
        Chunk chunk;
        chunk.emit(OpCode::MakeEnum);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum constructor enum name")
        {
            fail("expected invalid enum constructor enum-name error");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        chunk.emit(OpCode::MakeEnum);
        chunk.emit_u16(maybe_idx);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum constructor variant name")
        {
            fail("expected invalid enum constructor variant-name error");
        }
    }

    {
        Chunk chunk;
        const auto non_string_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::int_v(1)));
        const auto some_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Some")));
        chunk.emit(OpCode::MakeEnum);
        chunk.emit_u16(non_string_idx);
        chunk.emit_u16(some_idx);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum constructor enum name")
        {
            fail("expected invalid enum constructor enum-name for non-string constant kind");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        const auto non_string_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::int_v(2)));
        chunk.emit(OpCode::MakeEnum);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(non_string_idx);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum constructor variant name")
        {
            fail("expected invalid enum constructor variant-name for non-string constant kind");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        const auto some_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Some")));
        chunk.emit(OpCode::MakeEnum);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(some_idx);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "truncated enum constructor")
        {
            fail("expected truncated enum constructor error");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        const auto some_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Some")));
        chunk.emit(OpCode::MakeEnum);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(some_idx);
        chunk.code.push_back(1);
        chunk.spans.push_back({});
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow for payload enum construction");
        }
    }

    {
        Chunk chunk;
        chunk.emit(OpCode::MakeEnum);
        chunk.emit_u16(99);
        chunk.emit_u16(0);
        chunk.code.push_back(0);
        chunk.spans.push_back({});
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum constructor enum name")
        {
            fail("expected invalid enum constructor enum-name for out-of-range constant index");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        chunk.emit(OpCode::MakeEnum);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(99);
        chunk.code.push_back(0);
        chunk.spans.push_back({});
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum constructor variant name")
        {
            fail("expected invalid enum constructor variant-name for out-of-range constant index");
        }
    }

    {
        Chunk chunk;
        chunk.emit(OpCode::EnumIs);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum-is enum name")
        {
            fail("expected invalid enum-is enum-name error");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        chunk.emit(OpCode::EnumIs);
        chunk.emit_u16(maybe_idx);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum-is variant name")
        {
            fail("expected invalid enum-is variant-name error");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        chunk.emit_constant(Value::int_v(1));
        chunk.emit(OpCode::EnumIs);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(99);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum-is variant name")
        {
            fail("expected invalid enum-is variant-name for out-of-range constant index");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        const auto non_string_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::int_v(3)));
        chunk.emit_constant(Value::int_v(1));
        chunk.emit(OpCode::EnumIs);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(non_string_idx);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum-is variant name")
        {
            fail("expected invalid enum-is variant-name for non-string constant kind");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        const auto some_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Some")));
        chunk.emit(OpCode::EnumIs);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(some_idx);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow for enum-is");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        const auto some_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Some")));
        chunk.emit_constant(Value::int_v(0));
        chunk.emit(OpCode::EnumIs);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(some_idx);
        chunk.emit(OpCode::Return);
        VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok || !(res.value == Value::bool_v(false)))
        {
            fail("expected enum-is to be false on non-enum value");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        const auto some_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Some")));
        const auto other_enum_idx =
            static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Other")));
        chunk.emit(OpCode::MakeEnum);
        chunk.emit_u16(other_enum_idx);
        chunk.emit_u16(some_idx);
        chunk.code.push_back(0);
        chunk.spans.push_back({});
        chunk.emit(OpCode::EnumIs);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(some_idx);
        chunk.emit(OpCode::Return);
        VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok || !(res.value == Value::bool_v(false)))
        {
            fail("expected enum-is to be false on enum-name mismatch");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        const auto some_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Some")));
        const auto none_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("None")));
        chunk.emit(OpCode::MakeEnum);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(none_idx);
        chunk.code.push_back(0);
        chunk.spans.push_back({});
        chunk.emit(OpCode::EnumIs);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(some_idx);
        chunk.emit(OpCode::Return);
        VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok || !(res.value == Value::bool_v(false)))
        {
            fail("expected enum-is to be false on variant mismatch");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1));
        chunk.emit(OpCode::EnumIs);
        chunk.emit_u16(99);
        chunk.emit_u16(99);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum-is enum name")
        {
            fail("expected invalid enum-is enum-name for out-of-range constant index");
        }
    }

    {
        Chunk chunk;
        const auto int_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::int_v(1)));
        const auto some_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Some")));
        chunk.emit_constant(Value::int_v(1));
        chunk.emit(OpCode::EnumIs);
        chunk.emit_u16(int_idx);
        chunk.emit_u16(some_idx);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum-is enum name")
        {
            fail("expected invalid enum-is enum-name for non-string constant kind");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        const auto some_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Some")));
        const auto none_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("None")));

        chunk.emit(OpCode::MakeEnum);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(none_idx);
        chunk.code.push_back(0);
        chunk.spans.push_back({});

        chunk.emit(OpCode::EnumUnwrap);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(some_idx);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "enum unwrap variant mismatch")
        {
            fail("expected enum unwrap variant mismatch");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        const auto none_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("None")));

        chunk.emit(OpCode::MakeEnum);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(none_idx);
        chunk.code.push_back(0);
        chunk.spans.push_back({});

        chunk.emit(OpCode::EnumUnwrap);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(none_idx);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "enum unwrap missing payload")
        {
            fail("expected enum unwrap missing payload");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        const auto some_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Some")));
        chunk.emit_constant(Value::int_v(1));
        chunk.emit(OpCode::EnumUnwrap);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(some_idx);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "enum unwrap variant mismatch")
        {
            fail("expected enum unwrap mismatch on non-enum value");
        }
    }

    {
        Chunk chunk;
        chunk.emit(OpCode::EnumUnwrap);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum-unwrap enum name")
        {
            fail("expected invalid enum-unwrap enum-name error");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        chunk.emit(OpCode::EnumUnwrap);
        chunk.emit_u16(maybe_idx);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum-unwrap variant name")
        {
            fail("expected invalid enum-unwrap variant-name error");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        const auto some_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Some")));
        chunk.emit(OpCode::EnumUnwrap);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(some_idx);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow for enum-unwrap");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::enum_v("Other", "Some", Value::int_v(1)));
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        const auto some_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Some")));
        chunk.emit(OpCode::EnumUnwrap);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(some_idx);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "enum unwrap variant mismatch")
        {
            fail("expected enum unwrap mismatch on enum-name mismatch constant value");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::enum_v("Maybe", "Some", Value::int_v(1)));
        chunk.emit(OpCode::EnumUnwrap);
        chunk.emit_u16(99);
        chunk.emit_u16(0);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum-unwrap enum name")
        {
            fail("expected invalid enum-unwrap enum-name for out-of-range constant index");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::enum_v("Maybe", "Some", Value::int_v(1)));
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        chunk.emit(OpCode::EnumUnwrap);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(99);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum-unwrap variant name")
        {
            fail("expected invalid enum-unwrap variant-name for out-of-range constant index");
        }
    }

    {
        Chunk chunk;
        const auto non_string_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::int_v(4)));
        const auto some_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Some")));
        chunk.emit_constant(Value::enum_v("Maybe", "Some", Value::int_v(1)));
        chunk.emit(OpCode::EnumUnwrap);
        chunk.emit_u16(non_string_idx);
        chunk.emit_u16(some_idx);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum-unwrap enum name")
        {
            fail("expected invalid enum-unwrap enum-name for non-string constant kind");
        }
    }

    {
        Chunk chunk;
        const auto maybe_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::string_v("Maybe")));
        const auto non_string_idx = static_cast<std::uint16_t>(chunk.add_constant(Value::int_v(5)));
        chunk.emit_constant(Value::enum_v("Maybe", "Some", Value::int_v(1)));
        chunk.emit(OpCode::EnumUnwrap);
        chunk.emit_u16(maybe_idx);
        chunk.emit_u16(non_string_idx);
        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "invalid enum-unwrap variant name")
        {
            fail("expected invalid enum-unwrap variant-name for non-string constant kind");
        }
    }

}

static void vm_rng_and_vec_error_paths()
{
    using namespace curlee::vm;

    // RngNextInt: missing capability.
    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(10));
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::RngNextInt);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "missing capability rng.seeded")
        {
            fail("expected missing capability rng.seeded");
        }
    }

    // RngNextInt: stack underflow for missing capability token.
    {
        Chunk chunk;
        chunk.emit(OpCode::RngNextInt);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("rng.seeded");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow for RngNextInt missing capability token");
        }
    }

    // RngNextInt: stack underflow for missing max argument.
    {
        Chunk chunk;
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::RngNextInt);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("rng.seeded");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow for RngNextInt missing max");
        }
    }

    // RngNextInt: type and bounds checks.
    {
        Chunk chunk;
        chunk.emit_constant(Value::string_v("bad"));
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::RngNextInt);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("rng.seeded");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "rng max must be Int")
        {
            fail("expected rng max must be Int");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(0));
        chunk.emit_constant(Value::unit_v());
        chunk.emit(OpCode::RngNextInt);
        chunk.emit(OpCode::Return);

        VM vm;
        VM::Capabilities caps;
        caps.insert("rng.seeded");
        const auto res = vm.run(chunk, caps);
        if (res.ok || res.error != "rng max must be > 0")
        {
            fail("expected rng max must be > 0");
        }
    }

    // VecNew: underflow, type, and bound checks.
    {
        Chunk chunk;
        chunk.emit(OpCode::VecNew);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow for VecNew");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::bool_v(true));
        chunk.emit(OpCode::VecNew);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec max length must be Int")
        {
            fail("expected vec max length must be Int");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(-1));
        chunk.emit(OpCode::VecNew);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec max length must be >= 0")
        {
            fail("expected vec max length must be >= 0");
        }
    }

    // VecLen: underflow and type checks.
    {
        Chunk chunk;
        chunk.emit(OpCode::VecLen);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow for VecLen");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1));
        chunk.emit(OpCode::VecLen);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec value must be Vec")
        {
            fail("expected vec value must be Vec for VecLen");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::vec_v(2));
        chunk.emit(OpCode::VecLen);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok || !(res.value == Value::int_v(0)))
        {
            fail("expected VecLen success on empty vec");
        }
    }

    // VecPush: underflow, type checks, and capacity checks.
    {
        Chunk chunk;
        chunk.emit(OpCode::VecPush);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow for VecPush");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1));
        chunk.emit(OpCode::VecPush);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow for VecPush when vec operand is missing");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1));
        chunk.emit_constant(Value::int_v(2));
        chunk.emit(OpCode::VecPush);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec value must be Vec")
        {
            fail("expected vec value must be Vec for VecPush");
        }
    }

    {
        Chunk chunk;
        Value malformed_vec;
        malformed_vec.kind = ValueKind::Vec;
        malformed_vec.vec_value = nullptr;
        chunk.emit_constant(std::move(malformed_vec));
        chunk.emit_constant(Value::int_v(2));
        chunk.emit(OpCode::VecPush);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec value must be Vec")
        {
            fail("expected vec value must be Vec for VecPush null internal vector");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::vec_v(0));
        chunk.emit_constant(Value::int_v(1));
        chunk.emit(OpCode::VecPush);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec capacity exceeded")
        {
            fail("expected vec capacity exceeded");
        }
    }

    // VecGet: underflow, type checks, and bounds checks.
    {
        Chunk chunk;
        chunk.emit(OpCode::VecGet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow for VecGet");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(0));
        chunk.emit(OpCode::VecGet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow for VecGet when vec operand is missing");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1));
        chunk.emit_constant(Value::int_v(0));
        chunk.emit(OpCode::VecGet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec value must be Vec")
        {
            fail("expected vec value must be Vec for VecGet");
        }
    }

    {
        Chunk chunk;
        Value malformed_vec;
        malformed_vec.kind = ValueKind::Vec;
        malformed_vec.vec_value = nullptr;
        chunk.emit_constant(std::move(malformed_vec));
        chunk.emit_constant(Value::int_v(0));
        chunk.emit(OpCode::VecGet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec value must be Vec")
        {
            fail("expected vec value must be Vec for VecGet null internal vector");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::vec_v(1));
        chunk.emit_constant(Value::int_v(0));
        chunk.emit(OpCode::VecGet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec index out of bounds")
        {
            fail("expected vec index out of bounds for VecGet");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::vec_v(1));
        chunk.emit_constant(Value::bool_v(true));
        chunk.emit(OpCode::VecGet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec index out of bounds")
        {
            fail("expected vec index out of bounds for VecGet non-int index");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::vec_v(1));
        chunk.emit_constant(Value::int_v(-1));
        chunk.emit(OpCode::VecGet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec index out of bounds")
        {
            fail("expected vec index out of bounds for VecGet negative index");
        }
    }

    // VecSet: underflow, type checks, and bounds checks.
    {
        Chunk chunk;
        chunk.emit(OpCode::VecSet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow for VecSet");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(0));
        chunk.emit_constant(Value::int_v(9));
        chunk.emit(OpCode::VecSet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow for VecSet when vec operand is missing");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(9));
        chunk.emit(OpCode::VecSet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "stack underflow")
        {
            fail("expected stack underflow for VecSet when index operand is missing");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::int_v(1));
        chunk.emit_constant(Value::int_v(0));
        chunk.emit_constant(Value::int_v(9));
        chunk.emit(OpCode::VecSet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec value must be Vec")
        {
            fail("expected vec value must be Vec for VecSet");
        }
    }

    {
        Chunk chunk;
        Value malformed_vec;
        malformed_vec.kind = ValueKind::Vec;
        malformed_vec.vec_value = nullptr;
        chunk.emit_constant(std::move(malformed_vec));
        chunk.emit_constant(Value::int_v(0));
        chunk.emit_constant(Value::int_v(9));
        chunk.emit(OpCode::VecSet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec value must be Vec")
        {
            fail("expected vec value must be Vec for VecSet null internal vector");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::vec_v(1));
        chunk.emit_constant(Value::int_v(0));
        chunk.emit_constant(Value::int_v(9));
        chunk.emit(OpCode::VecSet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec index out of bounds")
        {
            fail("expected vec index out of bounds for VecSet");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::vec_v(1));
        chunk.emit_constant(Value::bool_v(true));
        chunk.emit_constant(Value::int_v(9));
        chunk.emit(OpCode::VecSet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec index out of bounds")
        {
            fail("expected vec index out of bounds for VecSet non-int index");
        }
    }

    {
        Chunk chunk;
        chunk.emit_constant(Value::vec_v(1));
        chunk.emit_constant(Value::int_v(-1));
        chunk.emit_constant(Value::int_v(9));
        chunk.emit(OpCode::VecSet);
        chunk.emit(OpCode::Return);

        VM vm;
        const auto res = vm.run(chunk);
        if (res.ok || res.error != "vec index out of bounds")
        {
            fail("expected vec index out of bounds for VecSet negative index");
        }
    }
}

int main()
{
    set_nonblocking_should_ignore_invalid_fd();
    read_into_should_report_limit_when_already_full();
    read_into_should_partial_append_and_hit_limit();
    read_into_should_return_eagain_without_eof();
    read_into_should_set_eof_on_bad_fd();
    vm_internal_helpers_should_cover_path_and_escape_branches();
    vm_seeded_run_wrapper_should_fill_profile_fields();
    vm_value_inline_helpers_should_cover_remaining_branches();

    vm_should_fail_out_of_fuel();
    vm_should_fail_truncated_constant_without_span();
    vm_should_fail_constant_index_out_of_range();
    vm_should_fail_truncated_local_index();
    vm_should_fail_local_index_out_of_range();
    vm_should_fail_add_type_error();
    vm_should_fail_print_missing_capability();
    vm_should_fail_read_line_missing_capability();
    vm_read_line_should_return_one_line_without_newline();
    vm_read_line_should_fail_when_line_exceeds_limit();
    vm_read_line_should_return_empty_string_on_eof();
    vm_read_line_should_fail_on_stack_underflow();
    vm_should_fail_fs_read_missing_capability();
    vm_should_fail_fs_write_missing_capability();
    vm_fs_should_reject_invalid_path();
    vm_fs_should_reject_empty_and_absolute_paths();
    vm_fs_read_should_fail_on_stack_underflow_and_non_string_path();
    vm_fs_read_should_fail_missing_file();
    vm_fs_write_should_fail_when_content_exceeds_limit();
    vm_fs_read_should_fail_when_file_exceeds_limit();
    vm_fs_read_should_fail_for_directory_path();
    vm_fs_roundtrip_should_succeed();
    vm_fs_read_empty_file_should_succeed();
    vm_fs_read_should_fail_when_ifstream_open_is_denied();
    vm_fs_write_should_fail_when_stream_write_reports_error();
    vm_fs_write_should_succeed_with_existing_writable_parent();
    vm_fs_read_should_fail_for_fifo_file_size_error();
    vm_fs_write_should_fail_with_access_denied();
    vm_fs_read_should_fail_with_access_denied();
    vm_fs_read_should_fail_when_exists_sets_error_code();
    vm_fs_write_should_fail_on_stack_underflow_and_non_string_values();
    vm_should_fail_tty_missing_capability();
    vm_tty_should_enforce_coordinate_bounds();
    vm_tty_clear_should_fail_on_stack_underflow();
    vm_tty_write_at_should_fail_missing_capability();
    vm_tty_write_at_should_fail_on_stack_underflow();
    vm_tty_write_at_should_fail_when_text_missing();
    vm_tty_write_at_should_fail_when_col_missing();
    vm_tty_write_at_should_fail_when_row_missing();
    vm_tty_write_at_should_fail_non_int_coordinates();
    vm_tty_write_at_should_fail_non_int_column();
    vm_tty_write_at_should_fail_non_string_text();
    vm_tty_write_at_should_fail_upper_bound();
    vm_tty_write_at_should_fail_negative_column();
    vm_tty_write_at_should_fail_column_upper_bound();
    vm_tty_flush_should_fail_missing_capability();
    vm_tty_flush_should_fail_on_stack_underflow();
    vm_tty_should_flush_in_deterministic_order();
    vm_should_fail_no_return_at_end();
    value_enum_equality_and_to_string();
    vm_enum_ops_success_paths();
    vm_enum_ops_error_paths();
    vm_rng_and_vec_error_paths();

    std::cout << "OK\n";
    return 0;
}
