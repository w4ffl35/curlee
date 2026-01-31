#pragma once

#include <cstddef>

/**
 * @file span.h
 * @brief Small utilities for byte-span locations within source text.
 */

namespace curlee::source
{

/**
 * @brief Represents a byte-range within a source file.
 *
 * Offsets are byte-based and [start, end) where start is inclusive and end is exclusive.
 */
struct Span
{
    std::size_t start = 0; // inclusive byte offset
    std::size_t end = 0;   // exclusive byte offset

    /** @brief Returns the length (in bytes) of the span. */
    [[nodiscard]] constexpr std::size_t length() const { return end - start; }
};

} // namespace curlee::source
