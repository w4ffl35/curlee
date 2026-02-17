#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
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
}

int main()
{
    set_nonblocking_should_ignore_invalid_fd();
    read_into_should_report_limit_when_already_full();
    read_into_should_partial_append_and_hit_limit();
    read_into_should_return_eagain_without_eof();
    read_into_should_set_eof_on_bad_fd();

    vm_should_fail_out_of_fuel();
    vm_should_fail_truncated_constant_without_span();
    vm_should_fail_constant_index_out_of_range();
    vm_should_fail_truncated_local_index();
    vm_should_fail_local_index_out_of_range();
    vm_should_fail_add_type_error();
    vm_should_fail_print_missing_capability();
    vm_should_fail_no_return_at_end();
    value_enum_equality_and_to_string();
    vm_enum_ops_success_paths();
    vm_enum_ops_error_paths();

    std::cout << "OK\n";
    return 0;
}
