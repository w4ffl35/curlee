#pragma once

#include <cstddef>

namespace curlee::source
{

struct Span
{
    std::size_t start = 0; // inclusive byte offset
    std::size_t end = 0;   // exclusive byte offset

    [[nodiscard]] constexpr std::size_t length() const { return end - start; }
};

} // namespace curlee::source
