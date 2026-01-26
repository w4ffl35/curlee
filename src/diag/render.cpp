#include <algorithm>
#include <cstddef>
#include <curlee/diag/render.h>
#include <curlee/source/line_map.h>
#include <sstream>
#include <string_view>

namespace curlee::diag
{
namespace
{

constexpr std::string_view severity_string(Severity s)
{
    switch (s)
    {
    case Severity::Error:
        return "error";
    case Severity::Warning:
        return "warning";
    case Severity::Note:
        return "note";
    }
    return "error";
}

struct LineSlice
{
    std::size_t start = 0;
    std::size_t end = 0; // exclusive
};

LineSlice slice_for_line(std::string_view text, std::size_t line_start)
{
    if (line_start > text.size())
    {
        line_start = text.size();
    }

    const std::size_t nl = text.find('\n', line_start);
    const std::size_t line_end = (nl == std::string_view::npos) ? text.size() : nl;
    return LineSlice{.start = line_start, .end = line_end};
}

std::size_t clamp(std::size_t value, std::size_t low, std::size_t high)
{
    return std::min(std::max(value, low), high);
}

} // namespace

std::string render(const Diagnostic& diagnostic, const curlee::source::SourceFile& file)
{
    std::ostringstream out;

    if (!diagnostic.span.has_value())
    {
        out << file.path << ": " << severity_string(diagnostic.severity) << ": "
            << diagnostic.message << "\n";
        for (const auto& note : diagnostic.notes)
        {
            out << "note: " << note.message << "\n";
        }
        return out.str();
    }

    const auto span = *diagnostic.span;
    const std::string_view text = file.contents;
    const curlee::source::LineMap map(text);

    const auto lc = map.offset_to_line_col(span.start);
    out << file.path << ":" << lc.line << ":" << lc.col << ": "
        << severity_string(diagnostic.severity) << ": " << diagnostic.message << "\n";

    const std::size_t line_start = map.line_start_offset(lc.line);
    const auto line_slice = slice_for_line(text, line_start);
    const std::string_view line_text =
        text.substr(line_slice.start, line_slice.end - line_slice.start);

    out << "  |\n";
    out << "  | " << line_text << "\n";

    const std::size_t line_len = line_text.size();

    // caret_start is 0-based within the line.
    const std::size_t caret_start = (lc.col == 0) ? 0 : (lc.col - 1);

    std::size_t span_len = 0;
    if (span.end > span.start)
    {
        span_len = span.end - span.start;
    }

    // If the span crosses lines, highlight only the first line.
    const std::size_t first_line_remaining =
        (caret_start <= line_len) ? (line_len - caret_start) : 0;
    const bool crosses_line = (span.start + span_len > line_slice.end);

    std::size_t caret_len = 1;
    if (!crosses_line && span_len > 0)
    {
        caret_len = span_len;
    }

    // Clamp caret within the line.
    const std::size_t safe_caret_start = clamp(caret_start, 0, line_len);
    const std::size_t safe_caret_len =
        clamp(caret_len, 1, std::max<std::size_t>(1, first_line_remaining));

    out << "  | " << std::string(safe_caret_start, ' ') << std::string(safe_caret_len, '^') << "\n";

    for (const auto& note : diagnostic.notes)
    {
        out << "note: " << note.message << "\n";
    }

    return out.str();
}

} // namespace curlee::diag
