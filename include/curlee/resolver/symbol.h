#pragma once

#include <cstdint>

/**
 * @file symbol.h
 * @brief Symbol identifier used by the resolver.
 */

namespace curlee::resolver
{

/** @brief Opaque numeric id for a resolved symbol. */
struct SymbolId
{
    std::uint32_t value = 0;

    friend constexpr bool operator==(SymbolId a, SymbolId b) { return a.value == b.value; }
    friend constexpr bool operator!=(SymbolId a, SymbolId b) { return !(a == b); }
};

} // namespace curlee::resolver
