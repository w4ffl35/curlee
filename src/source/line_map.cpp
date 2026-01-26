#include <algorithm>
#include <curlee/source/line_map.h>

namespace curlee::source
{

LineMap::LineMap(std::string_view text) : text_size_(text.size())
{
    line_starts_.push_back(0);
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\n')
        {
            const std::size_t next = i + 1;
            if (next <= text.size())
            {
                line_starts_.push_back(next);
            }
        }
    }

    // Ensure at least one line.
    if (line_starts_.empty())
    {
        line_starts_.push_back(0);
    }
}

LineCol LineMap::offset_to_line_col(std::size_t offset) const
{
    if (line_starts_.empty())
    {
        return LineCol{.line = 1, .col = 1};
    }

    // Clamp to end-of-text.
    if (offset > text_size_)
    {
        offset = text_size_;
    }

    // Find the last line start <= offset.
    auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
    if (it == line_starts_.begin())
    {
        return LineCol{.line = 1, .col = 1 + offset};
    }

    const std::size_t index = static_cast<std::size_t>(std::distance(line_starts_.begin(), it) - 1);
    const std::size_t start = line_starts_[index];

    return LineCol{.line = index + 1, .col = 1 + (offset - start)};
}

std::size_t LineMap::line_start_offset(std::size_t line) const
{
    if (line == 0)
    {
        return 0;
    }

    if (line_starts_.empty())
    {
        return 0;
    }

    const std::size_t index = line - 1;
    if (index >= line_starts_.size())
    {
        return text_size_;
    }

    return line_starts_[index];
}

std::size_t LineMap::line_count() const
{
    return line_starts_.empty() ? 1 : line_starts_.size();
}

} // namespace curlee::source
