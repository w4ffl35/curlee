#include <curlee/diag/diagnostic.h>
#include <curlee/diag/render.h>
#include <curlee/source/source_file.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

static std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

struct MarkedSpanResult
{
    std::string text;
    curlee::source::Span span;
};

static MarkedSpanResult extract_marked_span(std::string text)
{
    const std::string open = "[[";
    const std::string close = "]]";

    const std::size_t open_pos = text.find(open);
    const std::size_t close_pos = text.find(close);

    if (open_pos == std::string::npos || close_pos == std::string::npos ||
        close_pos < open_pos + open.size())
    {
        throw std::runtime_error("fixture must contain a single [[...]] marked span");
    }

    const std::size_t payload_begin = open_pos + open.size();
    const std::size_t payload_len = close_pos - payload_begin;

    curlee::source::Span span{.start = open_pos, .end = open_pos + payload_len};

    // Remove markers (erase later indices first).
    text.erase(close_pos, close.size());
    text.erase(open_pos, open.size());

    return MarkedSpanResult{.text = std::move(text), .span = span};
}

static bool run_case(const fs::path& fixture)
{
    const fs::path golden = fixture.parent_path() / (fixture.stem().string() + ".golden");

    const std::string raw = read_file(fixture);
    const auto marked = extract_marked_span(raw);

    curlee::source::SourceFile file{
        .path = fixture.filename().string(),
        .contents = marked.text,
    };

    curlee::diag::Diagnostic diag{
        .severity = curlee::diag::Severity::Error,
        .message = "test error",
        .span = marked.span,
        .notes = {},
    };

    const std::string got = curlee::diag::render(diag, file);
    const std::string expected = read_file(golden);

    if (got != expected)
    {
        std::cerr << "GOLDEN MISMATCH: " << fixture.filename().string() << "\n";
        std::cerr << "--- expected ---\n" << expected;
        std::cerr << "--- got ---\n" << got;
        return false;
    }

    return true;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: curlee_diagnostic_golden_tests <tests/diagnostics-dir>\n";
        return 2;
    }

    const fs::path dir = fs::path(argv[1]);
    if (!fs::exists(dir) || !fs::is_directory(dir))
    {
        std::cerr << "not a directory: " << dir.string() << "\n";
        return 2;
    }

    bool ok = true;
    for (const auto& entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        const auto path = entry.path();
        if (path.extension() != ".curlee")
        {
            continue;
        }

        ok = run_case(path) && ok;
    }

    if (!ok)
    {
        return 1;
    }

    std::cout << "OK\n";
    return 0;
}
