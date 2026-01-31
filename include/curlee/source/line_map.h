#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

/**
 * @file line_map.h
 * @brief Utilities to map byte offsets to line/column positions within source text.
 */

namespace curlee::source
{

/**
 * @brief A 1-based line/column pair.
 *
 * Both fields are 1-based; columns are counted in bytes for the MVP.
 */
struct LineCol
{
    std::size_t line = 1; // 1-based
    std::size_t col = 1;  // 1-based, in bytes for MVP
};

/**
 * @brief Precomputes line start offsets for fast offset-to-line/col queries.
 */
class LineMap
{
  public:
    explicit LineMap(std::string_view text);

    /** @brief Convert a byte offset into a LineCol (1-based). */
    [[nodiscard]] LineCol offset_to_line_col(std::size_t offset) const;
    /** @brief Return the start offset (byte index) of the given 1-based line. */
    [[nodiscard]] std::size_t line_start_offset(std::size_t line) const;
    /** @brief Return the total number of lines in the mapped text. */
    [[nodiscard]] std::size_t line_count() const;

  private:
    std::size_t text_size_ = 0;
    std::vector<std::size_t> line_starts_; // byte offsets of each line start (1st line included)
};

} // namespace curlee::source
