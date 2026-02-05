#include <cstdint>
#include <cstdlib>
#include <curlee/source/span.h>
#include <curlee/vm/chunk_codec.h>
#include <curlee/vm/value.h>
#include <iostream>
#include <limits>
#include <string>
#include <variant>
#include <vector>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static void expect(bool cond, const std::string& msg)
{
    if (!cond)
    {
        fail(msg);
    }
}

static void expect_eq(std::size_t got, std::size_t expected, const std::string& what)
{
    if (got != expected)
    {
        fail(what + ": got=" + std::to_string(got) + " expected=" + std::to_string(expected));
    }
}

static void expect_eq(std::int64_t got, std::int64_t expected, const std::string& what)
{
    if (got != expected)
    {
        fail(what + ": got=" + std::to_string(got) + " expected=" + std::to_string(expected));
    }
}

static void expect_eq(const std::string& got, const std::string& expected, const std::string& what)
{
    if (got != expected)
    {
        fail(what + ": got='" + got + "' expected='" + expected + "'");
    }
}

static void append_u8(std::vector<std::uint8_t>& out, std::uint8_t v)
{
    out.push_back(v);
}

static void append_u32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

static void append_u64(std::vector<std::uint8_t>& out, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i)
    {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

static std::vector<std::uint8_t> append_magic_and_ver(std::uint32_t ver)
{
    static constexpr char kMagic[] = "CURLEE_CHUNK";

    std::vector<std::uint8_t> out;
    for (const char c : kMagic)
    {
        out.push_back(static_cast<std::uint8_t>(c));
    }
    append_u32(out, ver);
    return out;
}

static void expect_decode_err(const std::vector<std::uint8_t>& bytes,
                              const std::string& expected_message)
{
    const auto decoded = curlee::vm::decode_chunk(bytes);
    const auto* err = std::get_if<curlee::vm::ChunkDecodeError>(&decoded);
    if (err == nullptr)
    {
        fail("expected decode error '" + expected_message + "', but decode succeeded");
    }
    expect_eq(err->message, expected_message, "decode error message");
}

static void expect_roundtrip(const curlee::vm::Chunk& expected,
                             const std::vector<std::uint8_t>& bytes, const std::string& what)
{
    const auto decoded = curlee::vm::decode_chunk(bytes);
    const auto* err = std::get_if<curlee::vm::ChunkDecodeError>(&decoded);
    if (err != nullptr)
    {
        fail(what + ": decode failed: " + err->message);
    }

    const auto& got = std::get<curlee::vm::Chunk>(decoded);
    expect_eq(got.max_locals, expected.max_locals, what + ": max_locals");
    expect(got.code == expected.code, what + ": code bytes");
    expect_eq(got.spans.size(), expected.spans.size(), what + ": spans size");
    for (std::size_t i = 0; i < got.spans.size(); ++i)
    {
        expect_eq(got.spans[i].start, expected.spans[i].start, what + ": span start");
        expect_eq(got.spans[i].end, expected.spans[i].end, what + ": span end");
    }

    expect_eq(got.constants.size(), expected.constants.size(), what + ": constants size");
    for (std::size_t i = 0; i < got.constants.size(); ++i)
    {
        const auto& a = got.constants[i];
        const auto& b = expected.constants[i];
        expect(a.kind == b.kind, what + ": constant kind");
        switch (a.kind)
        {
        case curlee::vm::ValueKind::Int:
            expect_eq(a.int_value, b.int_value, what + ": int constant");
            break;
        case curlee::vm::ValueKind::Bool:
            expect(a.bool_value == b.bool_value, what + ": bool constant");
            break;
        case curlee::vm::ValueKind::String:
            expect_eq(a.string_value, b.string_value, what + ": string constant");
            break;
        case curlee::vm::ValueKind::Unit:
            break;
        }
    }
}

static std::vector<std::uint8_t> encode_chunk_v1(const curlee::vm::Chunk& chunk)
{
    std::vector<std::uint8_t> out = append_magic_and_ver(1);

    append_u32(out, static_cast<std::uint32_t>(chunk.max_locals));

    append_u32(out, static_cast<std::uint32_t>(chunk.code.size()));
    out.insert(out.end(), chunk.code.begin(), chunk.code.end());

    append_u32(out, static_cast<std::uint32_t>(chunk.spans.size()));
    for (const auto& s : chunk.spans)
    {
        append_u32(out, static_cast<std::uint32_t>(s.start));
        append_u32(out, static_cast<std::uint32_t>(s.end));
    }

    append_u32(out, static_cast<std::uint32_t>(chunk.constants.size()));
    for (const auto& c : chunk.constants)
    {
        switch (c.kind)
        {
        case curlee::vm::ValueKind::Int:
            append_u8(out, 0);
            append_u64(out, static_cast<std::uint64_t>(c.int_value));
            break;
        case curlee::vm::ValueKind::Bool:
            append_u8(out, 1);
            append_u8(out, c.bool_value ? 1 : 0);
            break;
        case curlee::vm::ValueKind::String:
            append_u8(out, 2);
            append_u32(out, static_cast<std::uint32_t>(c.string_value.size()));
            out.insert(out.end(), c.string_value.begin(), c.string_value.end());
            break;
        case curlee::vm::ValueKind::Unit:
            append_u8(out, 3);
            break;
        }
    }

    return out;
}

int main()
{
    using curlee::source::Span;
    using curlee::vm::Chunk;
    using curlee::vm::Value;

    // v2 roundtrip.
    Chunk chunk;
    chunk.max_locals = 2;
    chunk.code = {0x01, 0x02, 0x03};
    chunk.spans = {Span{.start = 0, .end = 1}, Span{.start = 1, .end = 2},
                   Span{.start = 2, .end = 3}};
    chunk.constants = {Value::int_v(-7), Value::bool_v(true), Value::bool_v(false),
                       Value::string_v("hi"), Value::unit_v()};

    {
        const auto bytes = curlee::vm::encode_chunk(chunk);
        expect_roundtrip(chunk, bytes, "v2 roundtrip");
    }

    // v1 decode compatibility.
    {
        Chunk c1 = chunk;
        const auto bytes = encode_chunk_v1(c1);
        expect_roundtrip(c1, bytes, "v1 decode");
    }

    // Header / version errors.
    {
        const std::vector<std::uint8_t> bytes;
        expect_decode_err(bytes, "invalid chunk header");
    }
    {
        auto bytes = append_magic_and_ver(2);
        bytes[0] = 'X';
        expect_decode_err(bytes, "invalid chunk header");
    }
    {
        static constexpr char kMagic[] = "CURLEE_CHUNK";
        std::vector<std::uint8_t> bytes;
        for (const char c : kMagic)
        {
            bytes.push_back(static_cast<std::uint8_t>(c));
        }
        expect_decode_err(bytes, "truncated chunk version");
    }
    {
        auto bytes = append_magic_and_ver(999);
        expect_decode_err(bytes, "unsupported chunk format version");
    }

    // v2 max_locals errors.
    {
        auto bytes = append_magic_and_ver(2);
        expect_decode_err(bytes, "truncated max_locals");
    }
    {
        auto bytes = append_magic_and_ver(2);
        if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t))
        {
            const std::uint64_t too_big =
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) + 1ULL;
            append_u64(bytes, too_big);
            expect_decode_err(bytes, "max_locals too large");
        }
    }

    // v2 code length errors.
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        expect_decode_err(bytes, "truncated code length");
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t))
        {
            const std::uint64_t too_big =
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) + 1ULL;
            append_u64(bytes, too_big);
            expect_decode_err(bytes, "code length too large");
        }
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 1);
        expect_decode_err(bytes, "truncated code bytes");
    }

    // v2 spans length / span errors.
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        expect_decode_err(bytes, "truncated spans length");
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t))
        {
            const std::uint64_t too_big =
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) + 1ULL;
            append_u64(bytes, too_big);
            expect_decode_err(bytes, "spans length too large");
        }
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 1);
        expect_decode_err(bytes, "truncated span");
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0); // max_locals
        append_u64(bytes, 0); // code_len
        append_u64(bytes, 1); // spans_len
        append_u64(bytes, 0); // span start
        expect_decode_err(bytes, "truncated span");
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 1);
        if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t))
        {
            const std::uint64_t too_big =
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) + 1ULL;
            append_u64(bytes, too_big);
            append_u64(bytes, 0);
            expect_decode_err(bytes, "span offset too large");
        }
    }

    // v2 constants length / constant decode errors.
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        expect_decode_err(bytes, "truncated constants length");
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t))
        {
            const std::uint64_t too_big =
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) + 1ULL;
            append_u64(bytes, too_big);
            expect_decode_err(bytes, "constants length too large");
        }
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 1);
        expect_decode_err(bytes, "truncated constant kind");
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 1);
        append_u8(bytes, 0);
        expect_decode_err(bytes, "truncated int constant");
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 1);
        append_u8(bytes, 1);
        expect_decode_err(bytes, "truncated bool constant");
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 1);
        append_u8(bytes, 1);
        append_u8(bytes, 2);
        expect_decode_err(bytes, "invalid bool constant");
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 1);
        append_u8(bytes, 2);
        expect_decode_err(bytes, "truncated string constant length");
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 1);
        append_u8(bytes, 2);
        if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t))
        {
            const std::uint64_t too_big =
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) + 1ULL;
            append_u64(bytes, too_big);
            expect_decode_err(bytes, "string constant too large");
        }
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 1);
        append_u8(bytes, 2);
        append_u64(bytes, 1);
        expect_decode_err(bytes, "truncated string constant");
    }
    {
        auto bytes = append_magic_and_ver(2);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 0);
        append_u64(bytes, 1);
        append_u8(bytes, 99);
        expect_decode_err(bytes, "unknown constant kind");
    }

    // Span map length mismatch and trailing bytes.
    {
        Chunk bad;
        bad.max_locals = 0;
        bad.code = {0x01, 0x02};
        bad.spans = {Span{.start = 0, .end = 0}};
        bad.constants = {};

        const auto bytes = curlee::vm::encode_chunk(bad);
        expect_decode_err(bytes, "span map length mismatch");
    }
    {
        auto bytes = curlee::vm::encode_chunk(chunk);
        bytes.push_back(0);
        expect_decode_err(bytes, "unexpected trailing bytes");
    }

    // v1 truncation in string length decode.
    {
        auto bytes = append_magic_and_ver(1);
        expect_decode_err(bytes, "truncated max_locals");
    }
    {
        auto bytes = append_magic_and_ver(1);
        append_u32(bytes, 0);
        expect_decode_err(bytes, "truncated code length");
    }
    {
        auto bytes = append_magic_and_ver(1);
        append_u32(bytes, 0);
        append_u32(bytes, 0);
        expect_decode_err(bytes, "truncated spans length");
    }
    {
        auto bytes = append_magic_and_ver(1);
        append_u32(bytes, 0); // max_locals
        append_u32(bytes, 0); // code_len
        append_u32(bytes, 1); // spans_len
        append_u32(bytes, 0); // span start
        expect_decode_err(bytes, "truncated span");
    }
    {
        auto bytes = append_magic_and_ver(1);
        append_u32(bytes, 0); // max_locals
        append_u32(bytes, 0); // code_len
        append_u32(bytes, 1); // spans_len
        expect_decode_err(bytes, "truncated span");
    }
    {
        auto bytes = append_magic_and_ver(1);
        append_u32(bytes, 0);
        append_u32(bytes, 0);
        append_u32(bytes, 0);
        expect_decode_err(bytes, "truncated constants length");
    }
    {
        auto bytes = append_magic_and_ver(1);
        append_u32(bytes, 0);
        append_u32(bytes, 1);
        append_u8(bytes, 0x01);
        append_u32(bytes, 1);
        append_u32(bytes, 0);
        append_u32(bytes, 0);
        append_u32(bytes, 1);
        append_u8(bytes, 2);
        bytes.push_back(0x01);
        bytes.push_back(0x00);
        bytes.push_back(0x00);
        expect_decode_err(bytes, "truncated string constant length");
    }
    {
        Chunk c;
        c.max_locals = 0;
        c.code = {0x01};
        c.spans = {Span{.start = 0, .end = 0}};
        c.constants = {Value::string_v("hi")};

        auto bytes = encode_chunk_v1(c);
        bytes.resize(bytes.size() - 1);
        expect_decode_err(bytes, "truncated string constant");
    }

    std::cout << "OK\n";
    return 0;
}
