#pragma once

#include <cstdint>

namespace curlee::resolver
{

struct SymbolId
{
    std::uint32_t value = 0;

    friend constexpr bool operator==(SymbolId a, SymbolId b) { return a.value == b.value; }
    friend constexpr bool operator!=(SymbolId a, SymbolId b) { return !(a == b); }
};

} // namespace curlee::resolver
