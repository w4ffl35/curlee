#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace curlee::source
{

struct LineCol
{
    std::size_t line = 1; // 1-based
    std::size_t col = 1;  // 1-based, in bytes for MVP
};

class LineMap
{
  public:
    explicit LineMap(std::string_view text);

    [[nodiscard]] LineCol offset_to_line_col(std::size_t offset) const;
    [[nodiscard]] std::size_t line_start_offset(std::size_t line) const;
    [[nodiscard]] std::size_t line_count() const;

  private:
    std::size_t text_size_ = 0;
    std::vector<std::size_t> line_starts_; // byte offsets of each line start (1st line included)
};

} // namespace curlee::source
