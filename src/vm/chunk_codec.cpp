#include <cstddef>
#include <cstdint>
#include <curlee/source/span.h>
#include <curlee/vm/bytecode.h>
#include <curlee/vm/chunk_codec.h>
#include <curlee/vm/value.h>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace curlee::vm
{

namespace
{

void append_u8(std::vector<std::uint8_t>& out, std::uint8_t v)
{
    out.push_back(v);
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i)
    {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

struct Reader
{
    const std::vector<std::uint8_t>& in;
    std::size_t pos = 0;

    [[nodiscard]] bool has(std::size_t n) const { return pos + n <= in.size(); }

    [[nodiscard]] std::optional<std::uint8_t> read_u8()
    {
        if (!has(1))
        {
            return std::nullopt;
        }
        return in[pos++];
    }

    [[nodiscard]] std::optional<std::uint32_t> read_u32()
    {
        if (!has(4))
        {
            return std::nullopt;
        }
        const std::uint32_t b0 = in[pos + 0];
        const std::uint32_t b1 = in[pos + 1];
        const std::uint32_t b2 = in[pos + 2];
        const std::uint32_t b3 = in[pos + 3];
        pos += 4;
        return (b0) | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }

    [[nodiscard]] std::optional<std::uint64_t> read_u64()
    {
        if (!has(8))
        {
            return std::nullopt;
        }
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
        {
            v |= (static_cast<std::uint64_t>(in[pos + static_cast<std::size_t>(i)]) << (8 * i));
        }
        pos += 8;
        return v;
    }

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> read_bytes(std::size_t n)
    {
        if (!has(n))
        {
            return std::nullopt;
        }
        std::vector<std::uint8_t> out;
        out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(pos),
                   in.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
        return out;
    }

    [[nodiscard]] std::optional<std::string> read_string(std::size_t n)
    {
        if (!has(n))
        {
            return std::nullopt;
        }
        std::string out;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            out.push_back(static_cast<char>(in[pos + i]));
        }
        pos += n;
        return out;
    }
};

} // namespace

std::vector<std::uint8_t> encode_chunk(const Chunk& chunk)
{
    static constexpr char kMagic[] = "CURLEE_CHUNK";
    static constexpr std::uint32_t kChunkFormatVersion = 2;

    std::vector<std::uint8_t> out;
    out.reserve(64 + chunk.code.size());

    for (const char c : kMagic)
    {
        out.push_back(static_cast<std::uint8_t>(c));
    }

    append_u32(out, kChunkFormatVersion);

    append_u64(out, static_cast<std::uint64_t>(chunk.max_locals));

    append_u64(out, static_cast<std::uint64_t>(chunk.code.size()));
    out.insert(out.end(), chunk.code.begin(), chunk.code.end());

    append_u64(out, static_cast<std::uint64_t>(chunk.spans.size()));
    for (const auto& span : chunk.spans)
    {
        append_u64(out, static_cast<std::uint64_t>(span.start));
        append_u64(out, static_cast<std::uint64_t>(span.end));
    }

    append_u64(out, static_cast<std::uint64_t>(chunk.constants.size()));
    for (const auto& c : chunk.constants)
    {
        if (c.kind == ValueKind::Int)
        {
            append_u8(out, 0);
            append_u64(out, static_cast<std::uint64_t>(c.int_value));
            continue;
        }
        if (c.kind == ValueKind::Bool)
        {
            append_u8(out, 1);
            append_u8(out, c.bool_value ? 1 : 0);
            continue;
        }
        if (c.kind == ValueKind::String)
        {
            append_u8(out, 2);
            append_u64(out, static_cast<std::uint64_t>(c.string_value.size()));
            out.insert(out.end(), c.string_value.begin(), c.string_value.end());
            continue;
        }

        // ValueKind::Unit
        append_u8(out, 3);
    }

    return out;
} // GCOVR_EXCL_LINE

std::variant<Chunk, ChunkDecodeError> decode_chunk(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char kMagic[] = "CURLEE_CHUNK";
    static constexpr std::uint32_t kChunkFormatVersionV1 = 1;
    static constexpr std::uint32_t kChunkFormatVersionV2 = 2;

    Reader r{.in = bytes};

    for (const char c : kMagic)
    {
        const auto b = r.read_u8();
        if (!b.has_value() || *b != static_cast<std::uint8_t>(c))
        {
            return ChunkDecodeError{"invalid chunk header"};
        }
    }

    const auto ver = r.read_u32();
    if (!ver.has_value())
    {
        return ChunkDecodeError{"truncated chunk version"};
    }
    if (*ver != kChunkFormatVersionV1 && *ver != kChunkFormatVersionV2)
    {
        return ChunkDecodeError{"unsupported chunk format version"};
    }

    auto read_u64_size = [&](const char* truncated,
                             const char* overflow) -> std::variant<std::size_t, ChunkDecodeError>
    {
        const auto v = r.read_u64();
        if (!v.has_value())
        {
            return ChunkDecodeError{truncated};
        }
        if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t))
        {
            const auto kMaxSizeT =
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
            if (*v > kMaxSizeT)
            {
                return ChunkDecodeError{overflow};
            }
        }
        return static_cast<std::size_t>(*v);
    };

    auto read_u64_span_offset =
        [&](const char* truncated,
            const char* overflow) -> std::variant<std::size_t, ChunkDecodeError>
    {
        const auto v = r.read_u64();
        if (!v.has_value())
        {
            return ChunkDecodeError{truncated};
        }
        if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t))
        {
            const auto kMaxSizeT =
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
            if (*v > kMaxSizeT)
            {
                return ChunkDecodeError{overflow};
            }
        }
        return static_cast<std::size_t>(*v);
    };

    const bool v1 = (*ver == kChunkFormatVersionV1);

    std::size_t max_locals = 0;
    if (v1)
    {
        const auto ml = r.read_u32();
        if (!ml.has_value())
        {
            return ChunkDecodeError{"truncated max_locals"};
        }
        max_locals = static_cast<std::size_t>(*ml);
    }
    else
    {
        const auto ml = read_u64_size("truncated max_locals", "max_locals too large");
        if (const auto* err = std::get_if<ChunkDecodeError>(&ml))
        {
            return *err;
        }
        max_locals = std::get<std::size_t>(ml);
    }

    std::size_t code_len = 0;
    if (v1)
    {
        const auto cl = r.read_u32();
        if (!cl.has_value())
        {
            return ChunkDecodeError{"truncated code length"};
        }
        code_len = static_cast<std::size_t>(*cl);
    }
    else
    {
        const auto cl = read_u64_size("truncated code length", "code length too large");
        if (const auto* err = std::get_if<ChunkDecodeError>(&cl))
        {
            return *err;
        }
        code_len = std::get<std::size_t>(cl);
    }

    const auto code_bytes = r.read_bytes(code_len);
    if (!code_bytes.has_value())
    {
        return ChunkDecodeError{"truncated code bytes"};
    }

    std::size_t spans_len = 0;
    if (v1)
    {
        const auto sl = r.read_u32();
        if (!sl.has_value())
        {
            return ChunkDecodeError{"truncated spans length"};
        }
        spans_len = static_cast<std::size_t>(*sl);
    }
    else
    {
        const auto sl = read_u64_size("truncated spans length", "spans length too large");
        if (const auto* err = std::get_if<ChunkDecodeError>(&sl))
        {
            return *err;
        }
        spans_len = std::get<std::size_t>(sl);
    }

    std::vector<curlee::source::Span> spans;
    spans.reserve(spans_len);
    for (std::size_t i = 0; i < spans_len; ++i)
    {
        if (v1)
        {
            const auto start = r.read_u32();
            const auto end = r.read_u32();
            if (!start.has_value() || !end.has_value())
            {
                return ChunkDecodeError{"truncated span"};
            }
            spans.push_back(curlee::source::Span{.start = static_cast<std::size_t>(*start),
                                                 .end = static_cast<std::size_t>(*end)});
        }
        else
        {
            const auto start = read_u64_span_offset("truncated span", "span offset too large");
            if (const auto* err = std::get_if<ChunkDecodeError>(&start))
            {
                return *err;
            }
            const auto end = read_u64_span_offset("truncated span", "span offset too large");
            if (const auto* err = std::get_if<ChunkDecodeError>(&end))
            {
                return *err;
            }
            spans.push_back(curlee::source::Span{.start = std::get<std::size_t>(start),
                                                 .end = std::get<std::size_t>(end)});
        }
    }

    std::size_t const_len = 0;
    if (v1)
    {
        const auto cl = r.read_u32();
        if (!cl.has_value())
        {
            return ChunkDecodeError{"truncated constants length"};
        }
        const_len = static_cast<std::size_t>(*cl);
    }
    else
    {
        const auto cl = read_u64_size("truncated constants length", "constants length too large");
        if (const auto* err = std::get_if<ChunkDecodeError>(&cl))
        {
            return *err;
        }
        const_len = std::get<std::size_t>(cl);
    }

    std::vector<Value> constants;
    constants.reserve(const_len);

    for (std::size_t i = 0; i < const_len; ++i)
    {
        const auto kind = r.read_u8();
        if (!kind.has_value())
        {
            return ChunkDecodeError{"truncated constant kind"};
        }

        if (*kind == 0)
        {
            const auto raw = r.read_u64();
            if (!raw.has_value())
            {
                return ChunkDecodeError{"truncated int constant"};
            }
            constants.push_back(Value::int_v(static_cast<std::int64_t>(*raw)));
            continue;
        }

        if (*kind == 1)
        {
            const auto b = r.read_u8();
            if (!b.has_value())
            {
                return ChunkDecodeError{"truncated bool constant"};
            }
            if (*b != 0 && *b != 1)
            {
                return ChunkDecodeError{"invalid bool constant"};
            }
            constants.push_back(Value::bool_v(*b == 1));
            continue;
        }

        if (*kind == 2)
        {
            std::size_t n = 0;
            if (v1)
            {
                const auto n32 = r.read_u32();
                if (!n32.has_value())
                {
                    return ChunkDecodeError{"truncated string constant length"};
                }
                n = static_cast<std::size_t>(*n32);
            }
            else
            {
                const auto nsz =
                    read_u64_size("truncated string constant length", "string constant too large");
                if (const auto* err = std::get_if<ChunkDecodeError>(&nsz))
                {
                    return *err;
                }
                n = std::get<std::size_t>(nsz);
            }

            const auto s = r.read_string(n);
            if (!s.has_value())
            {
                return ChunkDecodeError{"truncated string constant"};
            }
            constants.push_back(Value::string_v(*s));
            continue;
        }

        if (*kind == 3)
        {
            constants.push_back(Value::unit_v());
            continue;
        }

        return ChunkDecodeError{"unknown constant kind"};
    }

    Chunk out;
    out.max_locals = max_locals;
    out.code = std::move(*code_bytes);
    out.spans = std::move(spans);
    out.constants = std::move(constants);

    if (out.spans.size() != out.code.size())
    {
        return ChunkDecodeError{"span map length mismatch"};
    }

    if (r.pos != bytes.size())
    {
        return ChunkDecodeError{"unexpected trailing bytes"};
    }

    return out;
}

} // namespace curlee::vm
